#include "TvmLowering.hpp"
#include "TreeMap.hpp"

#include "Tvm/FunctionalBuilder.hpp"
#include "Tvm/Function.hpp"
#include "Tvm/Recursive.hpp"

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

namespace Psi {
namespace Compiler {
namespace {
  std::vector<std::string> string_split(const std::string& s, char c) {
    std::vector<std::string> parts;
    std::string::size_type pos = 0;
    while (true) {
      std::string::size_type next = s.find(c, pos);
      if (next == std::string::npos) {
        parts.push_back(s.substr(pos));
        break;
      } else {
        parts.push_back(s.substr(pos, next-pos));
        pos = next + 1;
      }
    }
    return parts;
  }
}

struct TvmFunctionalLowererMap {
  static TvmResult build_array_type(TvmFunctionalBuilder& builder, const TreePtr<ArrayType>& array_ty) {
    TvmResult element = builder.build(array_ty->element_type);
    TvmResult length = builder.build(array_ty->length);
    return TvmResult(TvmScope::join(element.scope, length.scope),
                     Tvm::FunctionalBuilder::array_type(element.value, length.value, array_ty->location()));
  }
  
  static TvmResult build_struct_type(TvmFunctionalBuilder& builder, const TreePtr<StructType>& struct_ty) {
    std::vector<Tvm::ValuePtr<> > members;
    TvmScope *scope = NULL;
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = struct_ty->members.begin(), ie = struct_ty->members.end(); ii != ie; ++ii) {
      TvmResult f = builder.build(*ii);
      members.push_back(f.value);
      scope = TvmScope::join(scope, f.scope);
    }
    return TvmResult(scope, Tvm::FunctionalBuilder::struct_type(builder.tvm_context(), members, struct_ty->location()));
  }

  static TvmResult build_union_type(TvmFunctionalBuilder& builder, const TreePtr<UnionType>& union_ty) {
    std::vector<Tvm::ValuePtr<> > members;
    TvmScope *scope = NULL;
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = union_ty->members.begin(), ie = union_ty->members.end(); ii != ie; ++ii) {
      TvmResult f = builder.build(*ii);
      members.push_back(f.value);
      scope = TvmScope::join(scope, f.scope);
    }
    return TvmResult(scope, Tvm::FunctionalBuilder::struct_type(builder.tvm_context(), members, union_ty->location()));
  }
  
  static TvmResult build_empty_type(TvmFunctionalBuilder& builder, const TreePtr<EmptyType>& empty_ty) {
    return TvmResult(NULL, Tvm::FunctionalBuilder::empty_type(builder.tvm_context(), empty_ty->location()));
  }
  
  static TvmResult build_pointer_type(TvmFunctionalBuilder& builder, const TreePtr<PointerType>& pointer_ty) {
    TvmResult target = builder.build(pointer_ty->target_type);
    return TvmResult(target.scope, Tvm::FunctionalBuilder::pointer_type(target.value, target.upref, pointer_ty->location()));
  }
  
  static Tvm::ValuePtr<> build_int_type(Tvm::Context& context, const SourceLocation& location, bool is_signed, const std::vector<std::string>& parts) {
    if (parts.size() != 3)
      return Tvm::ValuePtr<>();
    
    Tvm::IntegerType::Width width;
    if (parts[2] == "ptr") {
      width = Tvm::IntegerType::iptr;
    } else {
      unsigned bits;
      try {
        bits = boost::lexical_cast<unsigned>(parts[2]);
      } catch (...) {
        return Tvm::ValuePtr<>();
      }
      
      switch (bits) {
      case 8: width = Tvm::IntegerType::i8; break;
      case 16: width = Tvm::IntegerType::i16; break;
      case 32: width = Tvm::IntegerType::i32; break;
      case 64: width = Tvm::IntegerType::i64; break;
      case 128: width = Tvm::IntegerType::i128; break;
      default: return Tvm::ValuePtr<>();
      }
    }

    return Tvm::FunctionalBuilder::int_type(context, width, is_signed, location);
  }
  
  static TvmResult build_primitive_type(TvmFunctionalBuilder& builder, const TreePtr<PrimitiveType>& primitive_ty) {
    Tvm::ValuePtr<> tvm_type;
    
    std::vector<std::string> parts = string_split(primitive_ty->name, '.');
    if (parts[0] == "core") {
      if (parts.size() >= 1) {
        if (parts[1] == "int")
          tvm_type = build_int_type(builder.tvm_context(), primitive_ty->location(), true, parts);
        else if (parts[1] == "uint")
          tvm_type = build_int_type(builder.tvm_context(), primitive_ty->location(), false, parts);
      }
    }
    
    if (!tvm_type)
      primitive_ty->compile_context().error_throw(primitive_ty->location(), boost::format("Unknown primitive type '%s'") % primitive_ty->name);
    
    return TvmResult(NULL, tvm_type);
  }

  static TvmResult build_function_type(TvmFunctionalBuilder& builder, const TreePtr<FunctionType>& function_ty) {
    TvmScope *scope = NULL;
    unsigned n_phantom = 0;
    std::vector<Tvm::ValuePtr<> > parameter_types;
    for (PSI_STD::vector<FunctionParameterType>::const_iterator ii = function_ty->parameter_types.begin(), ie = function_ty->parameter_types.end(); ii != ie; ++ii) {
      TvmResult parameter = builder.build(ii->type);
      scope = TvmScope::join(scope, parameter.scope);
      switch (ii->mode) {
      case parameter_mode_phantom:
        ++n_phantom;
        parameter_types.push_back(parameter.value);
        break;

      case parameter_mode_functional:
        parameter_types.push_back(parameter.value);
        break;
        
      default:
        parameter_types.push_back(Tvm::FunctionalBuilder::pointer_type(parameter.value, function_ty->location()));
        break;
      }
    }
    
    TvmResult result = builder.build(function_ty->result_type);
    scope = TvmScope::join(scope, result.scope);
    Tvm::ValuePtr<> result_type;
    bool sret = false;
    switch (function_ty->result_mode) {
    case result_mode_by_value: {
      sret = true;
      result_type = Tvm::FunctionalBuilder::empty_type(builder.tvm_context(), function_ty->location());
      parameter_types.push_back(Tvm::FunctionalBuilder::pointer_type(result.value, function_ty->location()));
      break;
    }
    
    case result_mode_functional:
      result_type = result.value;
      break;
      
    case result_mode_lvalue:
    case result_mode_rvalue:
      result_type = Tvm::FunctionalBuilder::pointer_type(result.value, function_ty->location());
      break;
      
    default: PSI_FAIL("Unknown function result mode");
    }
    
    return TvmResult(scope, Tvm::FunctionalBuilder::function_type(Tvm::cconv_c, result_type, parameter_types, n_phantom, sret, function_ty->location()));
  }
  
  static TvmResult build_derived_type(TvmFunctionalBuilder& builder, const TreePtr<DerivedType>& derived_ty) {
    TvmResult inner = builder.build(derived_ty->value_type), upref = builder.build(derived_ty->upref);
    return TvmResult(TvmScope::join(inner.scope, upref.scope), inner.value, upref.value);
  }
  
  static TvmResult build_constant_type(TvmFunctionalBuilder& builder, const TreePtr<ConstantType>& constant_ty) {
    TvmResult inner = builder.build(constant_ty->value);
    return TvmResult(inner.scope, Tvm::FunctionalBuilder::const_type(inner.value, constant_ty->location()));
  }
  
  static TvmResult build_bottom_type(TvmFunctionalBuilder& builder, const TreePtr<BottomType>& type) {
    builder.compile_context().error_throw(type.location(), "Bottom type cannot be lowered to TVM");
  }
  
  static TvmResult build_type_instance(TvmFunctionalBuilder& builder, const TreePtr<TypeInstance>& type_instance) {
    TvmResult recursive = builder.build_generic(type_instance->generic);  
    TvmScope *scope = recursive.scope;
    std::vector<Tvm::ValuePtr<> > parameters;
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = type_instance->parameters.begin(), ie = type_instance->parameters.end(); ii != ie; ++ii) {
      TvmResult param = builder.build(*ii);
      scope = TvmScope::join(scope, param.scope);
      parameters.push_back(param.value);
    }
    
    return TvmResult(scope, Tvm::FunctionalBuilder::apply(recursive.value, parameters, type_instance->location()));
  }
  
  static TvmResult build_metatype(TvmFunctionalBuilder& builder, const TreePtr<Metatype>& meta) {
    return TvmResult(NULL, Tvm::FunctionalBuilder::type_type(builder.tvm_context(), meta->location()));
  }
  
  static TvmResult build_element_value(TvmFunctionalBuilder& builder, const TreePtr<ElementValue>& elem_val) {
    TvmResult child = builder.build(elem_val->value);
    TvmResult idx = builder.build(elem_val->index);
    TvmScope *scope = TvmScope::join(child.scope, idx.scope);
    switch (elem_val->mode) {
    case result_mode_lvalue:
    case result_mode_rvalue:
      return TvmResult(scope, Tvm::FunctionalBuilder::element_ptr(child.value, idx.value, elem_val->location()));

    case result_mode_functional:
      return TvmResult(scope, Tvm::FunctionalBuilder::element_value(child.value, idx.value, elem_val->location()));
      
    default:
      builder.compile_context().error_throw(elem_val->location(), "Cannot get element value from something which is neither a reference nor a functional value");
    }
  }
  
  static TvmResult build_pointer_target(TvmFunctionalBuilder& builder, const TreePtr<PointerTarget>& ptr_target) {
    return builder.build(ptr_target->value);
  }
  
  static TvmResult build_pointer_to(TvmFunctionalBuilder& builder, const TreePtr<PointerTo>& ptr_to) {
    return builder.build(ptr_to->value);
  }
  
  static TvmResult build_integer_value(TvmFunctionalBuilder& builder, const TreePtr<IntegerValue>& int_value) {
    TvmResult type = builder.build(int_value->type);
    return TvmResult(NULL, Tvm::FunctionalBuilder::int_value(Tvm::value_cast<Tvm::IntegerType>(type.value), int_value->value, int_value->location()));
  }
  
  static TvmResult build_string_value(TvmFunctionalBuilder& builder, const TreePtr<StringValue>& str_value) {
    Tvm::ValuePtr<Tvm::IntegerType> char_type = Tvm::FunctionalBuilder::int_type(builder.tvm_context(), Tvm::IntegerType::i8, false, str_value->location());
    std::vector<Tvm::ValuePtr<> > elements;
    const String& str = str_value->value;
    for (std::size_t ii = 0, ie = str.length() + 1; ii != ie; ++ii)
      elements.push_back(Tvm::FunctionalBuilder::int_value(char_type, str[ii], str_value->location()));
    return TvmResult(NULL, Tvm::FunctionalBuilder::array_value(char_type, elements, str_value->location()));
  }
  
  static TvmResult build_builtin_value(TvmFunctionalBuilder& builder, const TreePtr<BuiltinValue>& builtin_value) {
    PSI_NOT_IMPLEMENTED();
  }

  static TvmResult build_default_value(TvmFunctionalBuilder& builder, const TreePtr<DefaultValue>& default_value) {
    TvmResult type = builder.build(default_value->type);
    return TvmResult(type.scope, Tvm::FunctionalBuilder::undef(type.value, default_value->location()));
  }
  
  static TvmResult build_struct_value(TvmFunctionalBuilder& builder, const TreePtr<StructValue>& struct_value) {
    std::vector<Tvm::ValuePtr<> > entries;
    TvmScope *scope = NULL;
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = struct_value->members.begin(), ie = struct_value->members.end(); ii != ie; ++ii) {
      TvmResult r = builder.build(*ii);
      scope = TvmScope::join(scope, r.scope);
      entries.push_back(r.value);
    }
    return TvmResult(scope, Tvm::FunctionalBuilder::struct_value(builder.tvm_context(), entries, struct_value->location()));
  }
  
  static TvmResult build_array_value(TvmFunctionalBuilder& builder, const TreePtr<ArrayValue>& array_value) {
    TvmResult type = builder.build(array_value->array_type->element_type);
    std::vector<Tvm::ValuePtr<> > entries;
    TvmScope *scope = NULL;
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = array_value->element_values.begin(), ie = array_value->element_values.end(); ii != ie; ++ii) {
      TvmResult r = builder.build(*ii);
      scope = TvmScope::join(scope, r.scope);
      entries.push_back(r.value);
    }
    return TvmResult(scope, Tvm::FunctionalBuilder::array_value(type.value, entries, array_value->location()));
  }
  
  static TvmResult build_union_value(TvmFunctionalBuilder& builder, const TreePtr<UnionValue>& union_value) {
    TvmResult type = builder.build(union_value->union_type);
    TvmResult inner = builder.build(union_value->member_value);
    return TvmResult(TvmScope::join(type.scope, inner.scope), Tvm::FunctionalBuilder::union_value(type.value, inner.value, union_value->location()));
  }
  
  static TvmResult build_upward_reference(TvmFunctionalBuilder& builder, const TreePtr<UpwardReference>& upref_value) {
    PSI_ASSERT(upref_value->maybe_outer_type || upref_value->next);
    TvmResult outer_type = upref_value->maybe_outer_type ? builder.build(upref_value->maybe_outer_type) : TvmResult(NULL, Tvm::ValuePtr<>());
    TvmResult outer_index = builder.build(upref_value->outer_index);
    TvmResult next = upref_value->next ? builder.build(upref_value->next) : TvmResult(NULL, Tvm::ValuePtr<>());
    return TvmResult(TvmScope::join(TvmScope::join(outer_type.scope, outer_index.scope), next.scope),
                     Tvm::FunctionalBuilder::upref(outer_type.value, outer_index.value, next.value, upref_value->location()));
  }
  
  static TvmResult build_global_statement(TvmFunctionalBuilder& builder, const TreePtr<GlobalStatement>& stmt) {
    PSI_ASSERT((stmt->mode == statement_mode_functional) || (stmt->mode == statement_mode_ref));
    PSI_ASSERT(stmt->value->pure);
    return builder.build(stmt->value);
  }
  
  typedef TreeOperationMap<Term, TvmResult, TvmFunctionalBuilder&> CallbackMap;
  static CallbackMap callback_map;

  static CallbackMap::Initializer callback_map_initializer() {
    return CallbackMap::initializer()
      .add<ArrayType>(build_array_type)
      .add<StructType>(build_struct_type)
      .add<UnionType>(build_union_type)
      .add<EmptyType>(build_empty_type)
      .add<PointerType>(build_pointer_type)
      .add<PrimitiveType>(build_primitive_type)
      .add<FunctionType>(build_function_type)
      .add<DerivedType>(build_derived_type)
      .add<ConstantType>(build_constant_type)
      .add<BottomType>(build_bottom_type)
      .add<TypeInstance>(build_type_instance)
      .add<Metatype>(build_metatype)
      .add<ElementValue>(build_element_value)
      .add<PointerTarget>(build_pointer_target)
      .add<PointerTo>(build_pointer_to)
      .add<IntegerValue>(build_integer_value)
      .add<StringValue>(build_string_value)
      .add<BuiltinValue>(build_builtin_value)
      .add<DefaultValue>(build_default_value)
      .add<StructValue>(build_struct_value)
      .add<ArrayValue>(build_array_value)
      .add<UnionValue>(build_union_value)
      .add<UpwardReference>(build_upward_reference)
      .add<GlobalStatement>(build_global_statement);
  }
};

TvmFunctionalLowererMap::CallbackMap TvmFunctionalLowererMap::callback_map(TvmFunctionalLowererMap::callback_map_initializer());

/**
 * \brief Lower a term which translates to a functional TVM operation.
 */
TvmResult tvm_lower_functional(TvmFunctionalBuilder& builder, const TreePtr<Term>& term) {
  return TvmFunctionalLowererMap::callback_map.call(builder, term);
}
}
}