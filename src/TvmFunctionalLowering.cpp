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
    TvmResultScope scope;
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = struct_ty->members.begin(), ie = struct_ty->members.end(); ii != ie; ++ii) {
      TvmResult f = builder.build(*ii);
      members.push_back(f.value);
      scope = TvmScope::join(scope, f.scope);
    }
    return TvmResult(scope, Tvm::FunctionalBuilder::struct_type(builder.tvm_context(), members, struct_ty->location()));
  }

  static TvmResult build_union_type(TvmFunctionalBuilder& builder, const TreePtr<UnionType>& union_ty) {
    std::vector<Tvm::ValuePtr<> > members;
    TvmResultScope scope;
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = union_ty->members.begin(), ie = union_ty->members.end(); ii != ie; ++ii) {
      TvmResult f = builder.build(*ii);
      members.push_back(f.value);
      scope = TvmScope::join(scope, f.scope);
    }
    return TvmResult(scope, Tvm::FunctionalBuilder::struct_type(builder.tvm_context(), members, union_ty->location()));
  }
  
  static TvmResult build_empty_type(TvmFunctionalBuilder& builder, const TreePtr<EmptyType>& empty_ty) {
    return TvmResult(TvmResultScope(), Tvm::FunctionalBuilder::empty_type(builder.tvm_context(), empty_ty->location()));
  }
  
  static TvmResult build_pointer_type(TvmFunctionalBuilder& builder, const TreePtr<PointerType>& pointer_ty) {
    TvmResult target = builder.build(pointer_ty->target_type);
    TvmResult upref = builder.build(pointer_ty->upref);
    return TvmResult(target.scope, Tvm::FunctionalBuilder::pointer_type(target.value, upref.value, pointer_ty->location()));
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
  
  static TvmResult build_number_type(TvmFunctionalBuilder& builder, const TreePtr<NumberType>& number_ty) {
    PSI_ASSERT(number_ty->vector_size == 0);
    Tvm::ValuePtr<> tvm_type;
    switch (number_ty->scalar_type) {
    case NumberType::n_bool: tvm_type = Tvm::FunctionalBuilder::bool_type(builder.tvm_context(), number_ty->location()); break;

    case NumberType::n_i8: tvm_type = Tvm::FunctionalBuilder::int_type(builder.tvm_context(), Tvm::IntegerType::i8, true, number_ty->location()); break;
    case NumberType::n_i16: tvm_type = Tvm::FunctionalBuilder::int_type(builder.tvm_context(), Tvm::IntegerType::i16, true, number_ty->location()); break;
    case NumberType::n_i32: tvm_type = Tvm::FunctionalBuilder::int_type(builder.tvm_context(), Tvm::IntegerType::i32, true, number_ty->location()); break;
    case NumberType::n_i64: tvm_type = Tvm::FunctionalBuilder::int_type(builder.tvm_context(), Tvm::IntegerType::i64, true, number_ty->location()); break;
    case NumberType::n_iptr: tvm_type = Tvm::FunctionalBuilder::int_type(builder.tvm_context(), Tvm::IntegerType::iptr, true, number_ty->location()); break;
    
    case NumberType::n_u8: tvm_type = Tvm::FunctionalBuilder::int_type(builder.tvm_context(), Tvm::IntegerType::i8, false, number_ty->location()); break;
    case NumberType::n_u16: tvm_type = Tvm::FunctionalBuilder::int_type(builder.tvm_context(), Tvm::IntegerType::i16, false, number_ty->location()); break;
    case NumberType::n_u32: tvm_type = Tvm::FunctionalBuilder::int_type(builder.tvm_context(), Tvm::IntegerType::i32, false, number_ty->location()); break;
    case NumberType::n_u64: tvm_type = Tvm::FunctionalBuilder::int_type(builder.tvm_context(), Tvm::IntegerType::i64, false, number_ty->location()); break;
    case NumberType::n_uptr: tvm_type = Tvm::FunctionalBuilder::int_type(builder.tvm_context(), Tvm::IntegerType::iptr, false, number_ty->location()); break;

    case NumberType::n_f32: tvm_type = Tvm::FunctionalBuilder::float_type(builder.tvm_context(), Tvm::FloatType::fp32, number_ty->location()); break;
    case NumberType::n_f64: tvm_type = Tvm::FunctionalBuilder::float_type(builder.tvm_context(), Tvm::FloatType::fp64, number_ty->location()); break;
    
    default: PSI_FAIL("Unknown number type");
    }
    
    return TvmResult(TvmResultScope(), tvm_type);
  }

  static TvmResult build_function_type(TvmFunctionalBuilder& builder, const TreePtr<FunctionType>& function_ty) {
    TvmResultScope scope;
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
  
  static TvmResult build_exists(TvmFunctionalBuilder& builder, const TreePtr<Exists>& exists) {
    TvmResultScope scope;
    std::vector<Tvm::ValuePtr<> > parameter_types;
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = exists->parameter_types.begin(), ie = exists->parameter_types.end(); ii != ie; ++ii) {
      TvmResult parameter = builder.build(*ii);
      scope = TvmScope::join(scope, parameter.scope);
      parameter_types.push_back(parameter.value);
    }
    
    TvmResult result = builder.build(exists->result);
    scope = TvmScope::join(scope, result.scope);

    return TvmResult(scope, Tvm::FunctionalBuilder::exists(result.value, parameter_types, exists->location()));
  }
  
  static TvmResult build_parameter(TvmFunctionalBuilder& builder, const TreePtr<Parameter>& parameter) {
    TvmResult r = builder.build(parameter->parameter_type);
    return TvmResult(r.scope, Tvm::FunctionalBuilder::parameter(r.value, parameter->depth, parameter->index, parameter->location()));
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
    TvmResultScope scope = recursive.scope;
    std::vector<Tvm::ValuePtr<> > parameters;
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = type_instance->parameters.begin(), ie = type_instance->parameters.end(); ii != ie; ++ii) {
      TvmResult param = builder.build(*ii);
      scope = TvmScope::join(scope, param.scope);
      parameters.push_back(param.value);
    }
    
    return TvmResult(scope, Tvm::FunctionalBuilder::apply_type(recursive.value, parameters, type_instance->location()));
  }
  
  static TvmResult build_type_instance_value(TvmFunctionalBuilder& builder, const TreePtr<TypeInstanceValue>& type_instance_value) {
    TvmResult type = builder.build(type_instance_value->type_instance);
    TvmResult inner = builder.build(type_instance_value->member_value);
    return TvmResult(TvmScope::join(type.scope, inner.scope),
                     Tvm::FunctionalBuilder::apply_value(type.value, inner.value, type_instance_value->location()));
  }
  
  static TvmResult build_interface_value(TvmFunctionalBuilder& builder, const TreePtr<InterfaceValue>& interface_value) {
    return builder.build_implementation(interface_value->interface, interface_value->parameters, interface_value->location(), interface_value->implementation);
  }
  
  static TvmResult build_metatype(TvmFunctionalBuilder& builder, const TreePtr<Metatype>& meta) {
    return TvmResult(TvmResultScope(), Tvm::FunctionalBuilder::type_type(builder.tvm_context(), meta->location()));
  }
  
  static TvmResult build_element_value(TvmFunctionalBuilder& builder, const TreePtr<ElementValue>& elem_val) {
    TvmResult child = builder.build(elem_val->value);
    TvmResult idx = builder.build(elem_val->index);
    TvmResultScope scope = TvmScope::join(child.scope, idx.scope);
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
    TvmResult child = builder.build(ptr_target->value);
    switch (ptr_target->value->mode) {
    case term_mode_lref:
    case term_mode_rref:
      return builder.load(child.value, ptr_target->location());
      
    default:
      return child;
    }
  }
  
  static TvmResult build_pointer_to(TvmFunctionalBuilder& builder, const TreePtr<PointerTo>& ptr_to) {
    return builder.build(ptr_to->value);
  }
  
  static TvmResult build_movable_value(TvmFunctionalBuilder& builder, const TreePtr<MovableValue>& value) {
    return builder.build(value->value);
  }
  
  static TvmResult build_integer_constant(TvmFunctionalBuilder& builder, const TreePtr<IntegerConstant>& int_value) {
    TvmResult type = builder.build(int_value->type);
    Tvm::ValuePtr<Tvm::IntegerType> ty = Tvm::value_cast<Tvm::IntegerType>(type.value);
    return TvmResult(TvmResultScope(), Tvm::FunctionalBuilder::int_value(ty, int_value->value, int_value->location()));
  }
  
  static TvmResult build_string_value(TvmFunctionalBuilder& builder, const TreePtr<StringValue>& str_value) {
    Tvm::ValuePtr<Tvm::IntegerType> char_type = Tvm::FunctionalBuilder::int_type(builder.tvm_context(), Tvm::IntegerType::i8, false, str_value->location());
    std::vector<Tvm::ValuePtr<> > elements;
    const String& str = str_value->value;
    for (std::size_t ii = 0, ie = str.length() + 1; ii != ie; ++ii)
      elements.push_back(Tvm::FunctionalBuilder::int_value(char_type, str[ii], str_value->location()));
    return TvmResult(TvmResultScope(), Tvm::FunctionalBuilder::array_value(char_type, elements, str_value->location()));
  }
  
  static TvmResult build_default_value(TvmFunctionalBuilder& builder, const TreePtr<DefaultValue>& default_value) {
    TvmResult type = builder.build(default_value->type);
    return TvmResult(type.scope, Tvm::FunctionalBuilder::undef(type.value, default_value->location()));
  }
  
  static TvmResult build_struct_value(TvmFunctionalBuilder& builder, const TreePtr<StructValue>& struct_value) {
    std::vector<Tvm::ValuePtr<> > entries;
    TvmResultScope scope;
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
    TvmResultScope scope;
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
  
  static TvmResult build_upward_reference_type(TvmFunctionalBuilder& builder, const TreePtr<UpwardReferenceType>& upref_type) {
    return TvmResult(TvmResultScope(), Tvm::FunctionalBuilder::upref_type(builder.tvm_context(), upref_type->location()));
  }
  
  static TvmResult build_upward_reference(TvmFunctionalBuilder& builder, const TreePtr<UpwardReference>& upref_value) {
    PSI_ASSERT(upref_value->maybe_outer_type || upref_value->next);
    TvmResult outer_type = upref_value->maybe_outer_type ? builder.build(upref_value->maybe_outer_type) : TvmResult();
    TvmResult outer_index = builder.build(upref_value->outer_index);
    TvmResult next = upref_value->next ? builder.build(upref_value->next) : TvmResult();
    return TvmResult(TvmScope::join(TvmScope::join(outer_type.scope, outer_index.scope), next.scope),
                     Tvm::FunctionalBuilder::upref(outer_type.value, outer_index.value, next.value, upref_value->location()));
  }
  
  static TvmResult build_upward_reference_null(TvmFunctionalBuilder& builder, const TreePtr<UpwardReferenceNull>& upref_null) {
    return TvmResult(TvmResultScope(), Tvm::FunctionalBuilder::upref_null(builder.tvm_context(), upref_null.location()));
  }
  
  static TvmResult build_global_statement(TvmFunctionalBuilder& builder, const TreePtr<GlobalStatement>& stmt) {
    switch (stmt->mode) {
    case statement_mode_functional:
    case statement_mode_ref:
      PSI_ASSERT(stmt->value->pure);
      return builder.build(stmt->value);
      
    case statement_mode_value:
      return builder.build_global(stmt);
      
    default: PSI_FAIL("Unrecognised statement mode");
    }
  }

  static TvmResult build_global_evaluate(TvmFunctionalBuilder& builder, const TreePtr<GlobalEvaluate>& term) {
    return builder.build_global_evaluate(term);
  }
  
  static TvmResult build_global_symbol(TvmFunctionalBuilder& builder, const TreePtr<Global>& term) {
    return builder.build_global(term);
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
      .add<NumberType>(build_number_type)
      .add<FunctionType>(build_function_type)
      .add<Exists>(build_exists)
      .add<Parameter>(build_parameter)
      .add<ConstantType>(build_constant_type)
      .add<BottomType>(build_bottom_type)
      .add<TypeInstance>(build_type_instance)
      .add<TypeInstanceValue>(build_type_instance_value)
      .add<InterfaceValue>(build_interface_value)
      .add<Metatype>(build_metatype)
      .add<ElementValue>(build_element_value)
      .add<PointerTarget>(build_pointer_target)
      .add<PointerTo>(build_pointer_to)
      .add<MovableValue>(build_movable_value)
      .add<IntegerConstant>(build_integer_constant)
      .add<StringValue>(build_string_value)
      .add<DefaultValue>(build_default_value)
      .add<StructValue>(build_struct_value)
      .add<ArrayValue>(build_array_value)
      .add<UnionValue>(build_union_value)
      .add<UpwardReferenceType>(build_upward_reference_type)
      .add<UpwardReference>(build_upward_reference)
      .add<UpwardReferenceNull>(build_upward_reference_null)
      .add<GlobalStatement>(build_global_statement)
      .add<GlobalEvaluate>(build_global_evaluate)
      .add<GlobalVariable>(build_global_symbol)
      .add<Function>(build_global_symbol)
      .add<LibrarySymbol>(build_global_symbol);
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