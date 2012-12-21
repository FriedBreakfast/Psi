#include "TvmLowering.hpp"

#include "Tvm/FunctionalBuilder.hpp"
#include "Tvm/Function.hpp"
#include "Tvm/Recursive.hpp"

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

namespace Psi {
namespace Compiler {
TvmFunctionalBuilder::TvmFunctionalBuilder(CompileContext *compile_context, Tvm::Context *tvm_context, TvmFunctionalBuilderCallback *callback)
: m_compile_context(compile_context), m_tvm_context(tvm_context), m_callback(callback) {
}

/**
 * \brief Convert a functional operation to TVM.
 */
TvmResult TvmFunctionalBuilder::build(const TreePtr<Term>& value) {
  FunctionalValueMap::iterator ii = m_values.find(value);
  if (ii != m_values.end())
    return ii->second;
  
  TvmResult result;
  if (TreePtr<Type> type = dyn_treeptr_cast<Type>(value)) {
    result = build_type_internal(type);
  } else if (TreePtr<TypeInstance> type_inst = dyn_treeptr_cast<TypeInstance>(value)) {
    result = build_type_instance(type_inst);
  } else if (TreePtr<Constructor> constructor = dyn_treeptr_cast<Constructor>(value)) {
    result = build_constructor(constructor);
  } else if (TreePtr<Functional> func = dyn_treeptr_cast<Functional>(value)) {
    result = build_other(func);
  } else {
    result = m_callback->build_hook(*this, value);
  }
  
  m_values.insert(std::make_pair(value, result));
  return result;
}

/**
 * \brief Call build(), and then convert the value to a functional value if required.
 */
Tvm::ValuePtr<> TvmFunctionalBuilder::build_value(const TreePtr<Term>& term) {
  TvmResult r = build(term);
  switch (r.storage()) {
  case tvm_storage_functional:
    return r.value();
    
  case tvm_storage_bottom:
    return Tvm::FunctionalBuilder::undef(build_type(term->type).value(), term->location());
    
  case tvm_storage_lvalue_ref:
  case tvm_storage_rvalue_ref:
    return m_callback->load_hook(*this, r.value(), term->location());
    
  default: PSI_FAIL("Unexpected enum value");
  }
}

/**
 * \brief Call build() and ensure the result is a type.
 */
TvmResult TvmFunctionalBuilder::build_type(const TreePtr< Term >& term) {
  TvmResult r = build(term);
  PSI_ASSERT((r.storage() == tvm_storage_functional) && (r.value()->is_type()));
  return r;
}

/**
 * \brief Check if a type is primitive.
 * 
 * Note that this does not convert the type to TVM, although it will use existing results.
 * The behaviour of this function must be consistent with \c build_type.
 */
bool TvmFunctionalBuilder::is_primitive(const TreePtr<Term>& type) {
  FunctionalValueMap::iterator ii = m_values.find(type);
  if (ii != m_values.end())
    return ii->second.primitive();

  if (tree_isa<EmptyType>(type) || tree_isa<PointerType>(type) || tree_isa<UnionType>(type) || tree_isa<PrimitiveType>(type) || tree_isa<BottomType>(type)) {
    return true;
  } else if (tree_isa<FunctionType>(type) || tree_isa<Parameter>(type)) {
    return false;
  } else if (TreePtr<ArrayType> array_ty = dyn_treeptr_cast<ArrayType>(type)) {
    return is_primitive(array_ty->element_type);
  } else if (TreePtr<StructType> struct_ty = dyn_treeptr_cast<StructType>(type)) {
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = struct_ty->members.begin(), ie = struct_ty->members.end(); ii != ie; ++ii) {
      if (!is_primitive(*ii))
        return false;
    }
    return true;
  } else if (TreePtr<GlobalDefine> def = dyn_treeptr_cast<GlobalDefine>(type)) {
    return is_primitive(def->value);
  } else if (TreePtr<TypeInstance> inst = dyn_treeptr_cast<TypeInstance>(type)) {
    switch (inst->generic->primitive_mode) {
    case GenericType::primitive_never: return false;
    case GenericType::primitive_always: return true;
    case GenericType::primitive_recurse: return is_primitive(inst->unwrap());
    default: PSI_FAIL("unrecognised GenericType primitive mode");
    }
  } else {
    PSI_FAIL(si_vptr(type.get())->classname);
  }
}

/**
 * \brief Convert a type to TVM.
 */
TvmResult TvmFunctionalBuilder::build_type_internal(const TreePtr<Type>& type) {
  if (TreePtr<ArrayType> array_ty = dyn_treeptr_cast<ArrayType>(type)) {
    TvmResult element = build_type(array_ty->element_type);
    Tvm::ValuePtr<> length = build_value(array_ty->length);
    return TvmResult::type(type->type, Tvm::FunctionalBuilder::array_type(element.value(), length, type->location()), element.primitive());
  } else if (tree_isa<EmptyType>(type)) {
    return TvmResult::type(type->type, Tvm::FunctionalBuilder::empty_type(tvm_context(), type->location()), true);
  } else if (TreePtr<PointerType> pointer_ty = dyn_treeptr_cast<PointerType>(type)) {
    TvmResult target = build_type(pointer_ty->target_type);
    return TvmResult::type(type->type, Tvm::FunctionalBuilder::pointer_type(target.value(), type->location()), true);
  } else if (TreePtr<StructType> struct_ty = dyn_treeptr_cast<StructType>(type)) {
    bool primitive = true;
    std::vector<Tvm::ValuePtr<> > members;
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = struct_ty->members.begin(), ie = struct_ty->members.end(); ii != ie; ++ii) {
      TvmResult f = build_type(*ii);
      members.push_back(f.value());
      primitive = primitive && f.primitive();
    }
    return TvmResult::type(type->type, Tvm::FunctionalBuilder::struct_type(tvm_context(), members, type->location()), primitive);
  } else if (TreePtr<UnionType> union_ty = dyn_treeptr_cast<UnionType>(type)) {
    PSI_STD::vector<Tvm::ValuePtr<> > members;
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = struct_ty->members.begin(), ie = struct_ty->members.end(); ii != ie; ++ii)
      members.push_back(build_type(*ii).value());
    // Unions are always primitive because the user is required to handle copy semantics manually.
    return TvmResult::type(type->type, Tvm::FunctionalBuilder::union_type(tvm_context(), members, type->location()), true);
  } else if (TreePtr<PrimitiveType> primitive_ty = dyn_treeptr_cast<PrimitiveType>(type)) {
    return build_primitive_type(primitive_ty);
  } else if (TreePtr<FunctionType> function_ty = dyn_treeptr_cast<FunctionType>(type)) {
    return build_function_type(function_ty);
  } else if (tree_isa<BottomType>(type)) {
    type.compile_context().error_throw(type.location(), "Bottom type cannot be lowered to TVM");
  } else {
    PSI_FAIL(si_vptr(type.get())->classname);
  }
}

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
  
  Tvm::ValuePtr<> build_int_type(Tvm::Context& context, const SourceLocation& location, bool is_signed, const std::vector<std::string>& parts) {
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
}

TvmResult TvmFunctionalBuilder::build_primitive_type(const TreePtr<PrimitiveType>& type) {
  Tvm::ValuePtr<> tvm_type;
  
  std::vector<std::string> parts = string_split(type->name, '.');
  if (parts[0] == "core") {
    if (parts.size() >= 1) {
      if (parts[1] == "int")
        tvm_type = build_int_type(tvm_context(), type.location(), true, parts);
      else if (parts[1] == "uint")
        tvm_type = build_int_type(tvm_context(), type.location(), false, parts);
    }
  }
  
  if (!tvm_type)
    type.compile_context().error_throw(type.location(), boost::format("Unknown primitive type '%s'") % type->name);
  
  return TvmResult::type(type->type, tvm_type, true);
}

TvmResult TvmFunctionalBuilder::build_function_type(const TreePtr<FunctionType>& type) {
  std::vector<Tvm::ValuePtr<> > parameter_types;
  for (PSI_STD::vector<FunctionParameterType>::const_iterator ii = type->parameter_types.begin(), ie = type->parameter_types.end(); ii != ie; ++ii) {
    TvmResult parameter = build(ii->type);
    
    bool by_value;
    switch (ii->mode) {
    case parameter_mode_input:
    case parameter_mode_rvalue:
      by_value = parameter.primitive();
      break;
      
    case parameter_mode_output:
    case parameter_mode_io:
      by_value  = false;
      break;
      
    case parameter_mode_functional:
      by_value = true;
      if (!parameter.primitive())
        type.compile_context().error_throw(type.location(), "Functional parameter does not have a primitive type");
      break;
      
    default: PSI_FAIL("Unrecognised function parameter mode");
    }
    
    parameter_types.push_back(by_value ? parameter.value() : Tvm::FunctionalBuilder::pointer_type(parameter.value(), type.location()));
  }
  
  TvmResult result = build(type->result_type);
  Tvm::ValuePtr<> result_type;
  bool sret;
  switch (type->result_mode) {
  case result_mode_by_value:
    if (result.primitive()) {
      sret = false;
      result_type = result.value();
    } else {
      sret = true;
      result_type = Tvm::FunctionalBuilder::pointer_type(result.value(), type.location());
    }
    break;
    
  case result_mode_functional:
    sret = false;
    result_type = result.value();
    break;
    
  case result_mode_rvalue:
  case result_mode_lvalue:
    sret = false;
    result_type = Tvm::FunctionalBuilder::pointer_type(result.value(), type.location());
    break;
    
  default: PSI_FAIL("Unrecognised function result mode");
  }
  
  // Function types are not primitive because there is a function cannot be copied
  return TvmResult::type(type->type, Tvm::FunctionalBuilder::function_type(Tvm::cconv_c, result_type, parameter_types, 0, sret, type.location()), false);
}

TvmResult TvmFunctionalBuilder::build_type_instance(const TreePtr<TypeInstance>& type) {
  TvmGenericResult recursive = m_callback->build_generic_hook(*this, type->generic);  
  std::vector<Tvm::ValuePtr<> > parameters;
  for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = type->parameters.begin(), ie = type->parameters.end(); ii != ie; ++ii)
    parameters.push_back(build_value(*ii));
  Tvm::ValuePtr<> inst = Tvm::FunctionalBuilder::apply(recursive.generic, parameters, type->location());
  bool primitive;
  switch (recursive.primitive_mode) {
  case GenericType::primitive_recurse: primitive = is_primitive(type->unwrap()); break;
  case GenericType::primitive_never: primitive = false; break;
  case GenericType::primitive_always: primitive = true; break;
  default: PSI_FAIL("unrecognised GenericType primitive mode");
  }
  return TvmResult::type(type->type, inst, primitive);
}

TvmResult TvmFunctionalBuilder::build_other(const TreePtr<Functional>& value) {
  if (TreePtr<Metatype> meta = dyn_treeptr_cast<Metatype>(value)) {
    return TvmResult::type(TreePtr<Term>(), Tvm::FunctionalBuilder::type_type(tvm_context(), value->location()), true);
  } else if (TreePtr<ElementPtr> elem_ptr = dyn_treeptr_cast<ElementPtr>(value)) {
    Tvm::ValuePtr<> child = build_value(elem_ptr->value);
    Tvm::ValuePtr<> idx = build_value(elem_ptr->index);
    Tvm::ValuePtr<> result = Tvm::FunctionalBuilder::element_ptr(child, idx, value->location());
    return TvmResult::in_register(elem_ptr->type, tvm_storage_functional, result);
  } else if (TreePtr<ElementValue> elem_val = dyn_treeptr_cast<ElementValue>(value)) {
    TvmResult child = build(elem_val->value);
    Tvm::ValuePtr<> idx = build_value(elem_val->index);
    switch (child.storage()) {
    case tvm_storage_lvalue_ref:
    case tvm_storage_rvalue_ref:
      return TvmResult::in_register(value->type, child.storage(), Tvm::FunctionalBuilder::element_ptr(child.value(), idx, value->location()));
      
    case tvm_storage_functional:
      return TvmResult::in_register(value->type, tvm_storage_functional, Tvm::FunctionalBuilder::element_value(child.value(), idx, value->location()));
      
    case tvm_storage_bottom:
      return TvmResult::in_register(value->type, tvm_storage_functional, Tvm::FunctionalBuilder::undef(build_value(value->type), value->location()));

    default:
      compile_context().error_throw(value->location(), "Cannot get element value from something which is neither a reference nor a functional value");
    }
  } else if (TreePtr<PointerTarget> ptr_target = dyn_treeptr_cast<PointerTarget>(value)) {
    Tvm::ValuePtr<> child = build_value(ptr_target->value);
    return TvmResult::in_register(value->type, tvm_storage_lvalue_ref, child);
  } else if (TreePtr<PointerTo> ptr_to = dyn_treeptr_cast<PointerTo>(value)) {
    TvmResult child = build(ptr_to->value);
    switch (child.storage()) {
    case tvm_storage_lvalue_ref:
    case tvm_storage_rvalue_ref:
      return TvmResult::in_register(value->type, tvm_storage_functional, child.value());
      
    case tvm_storage_bottom:
      return TvmResult::in_register(value->type, tvm_storage_functional, Tvm::FunctionalBuilder::undef(build_value(value->type), value->location()));
      
    default:
      compile_context().error_throw(value->location(), "Cannot get pointer from non-reference");
    }
  } else if (TreePtr<GlobalDefine> define = dyn_treeptr_cast<GlobalDefine>(value)) {
    return m_callback->build_define_hook(*this, define);
  } else {
    PSI_FAIL(si_vptr(value.get())->classname);
  }
}

TvmResult TvmFunctionalBuilder::build_constructor(const TreePtr<Constructor>& value) {
  if (TreePtr<IntegerValue> int_value = dyn_treeptr_cast<IntegerValue>(value)) {
    TvmResult type = build_type(int_value->type);
    PSI_ASSERT(Tvm::isa<Tvm::IntegerType>(type.value()));
    return TvmResult::in_register(int_value->type, tvm_storage_functional,
                                  Tvm::FunctionalBuilder::int_value(Tvm::value_cast<Tvm::IntegerType>(type.value()), int_value->value, int_value->location()));
  } else if (TreePtr<StringValue> str_value = dyn_treeptr_cast<StringValue>(value)) {
    Tvm::ValuePtr<Tvm::IntegerType> char_type = Tvm::FunctionalBuilder::int_type(tvm_context(), Tvm::IntegerType::i8, false, str_value->location());
    std::vector<Tvm::ValuePtr<> > elements;
    const String& str = str_value->value;
    for (std::size_t ii = 0, ie = str.length() + 1; ii != ie; ++ii)
      elements.push_back(Tvm::FunctionalBuilder::int_value(char_type, str[ii], str_value->location()));
    return TvmResult::in_register(str_value->type, tvm_storage_functional,
                                  Tvm::FunctionalBuilder::array_value(char_type, elements, value->location()));
  } else if (TreePtr<BuiltinValue> builtin_value = dyn_treeptr_cast<BuiltinValue>(value)) {
    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<DefaultValue> default_value = dyn_treeptr_cast<DefaultValue>(value)) {
    TvmResult type = build_type(default_value->type);
    if (type.primitive()) {
      return TvmResult::in_register(default_value->type, tvm_storage_functional, Tvm::FunctionalBuilder::undef(type.value(), default_value->location()));
    } else {
      PSI_NOT_IMPLEMENTED();
    }
  } else if (TreePtr<StructValue> struct_value = dyn_treeptr_cast<StructValue>(struct_value)) {
    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<ArrayValue> array_value = dyn_treeptr_cast<ArrayValue>(array_value)) {
    PSI_NOT_IMPLEMENTED();
  } else {
    PSI_FAIL(si_vptr(value.get())->classname);
  }
}
}
}