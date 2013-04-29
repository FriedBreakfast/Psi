#include "../TermOperationMap.hpp"
#include "../Aggregate.hpp"
#include "../FunctionalBuilder.hpp"
#include "../Number.hpp"

#include "Builder.hpp"
#include "CModule.hpp"

namespace Psi {
namespace Tvm {
namespace CBackend {
struct TypeBuilderCallbacks {
  static const unsigned small_array_length = 16;
  
  static CType* empty_type_callback(TypeBuilder& builder, const ValuePtr<>&) {
    return builder.void_type();
  }
  
  static CType* struct_type_callback(TypeBuilder& builder, const ValuePtr<StructType>& term) {
    unsigned n = term->n_members();
    
    if (n == 0)
      return builder.void_type();
    
    SmallArray<CTypeAggregateMember, small_array_length> members;
    members.resize(n);
    for (unsigned i = 0; i != n; ++i) {
      members[i].type = builder.build(term->member_type(i));
      if (members[i].type->type != c_type_void) {
        members[i].name.prefix = "a";
        members[i].name.index = i+1;
      } else {
        members[i].name.prefix = NULL;
      }
    }
    
    return builder.c_builder().struct_type(&term->location(), n, members.get());
  }
  
  static CType* union_type_callback(TypeBuilder& builder, const ValuePtr<UnionType>& term) {
    unsigned n = term->n_members();
    
    if (n == 0)
      return builder.void_type();
    
    SmallArray<CTypeAggregateMember, small_array_length> members;
    members.resize(n);
    for (unsigned i = 0; i != n; ++i) {
      members[i].type = builder.build(term->member_type(i));
      if (members[i].type->type != c_type_void) {
        members[i].name.prefix = "a";
        members[i].name.index = i+1;
      } else {
        members[i].name.prefix = NULL;
      }
    }
    
    return builder.c_builder().union_type(&term->location(), n, members.get());
  }
  
  static CType* pointer_type_callback(TypeBuilder& builder, const ValuePtr<PointerType>& term) {
    return builder.c_builder().pointer_type(builder.build(term->target_type()));
  }
  
  static CType* array_type_callback(TypeBuilder& builder, const ValuePtr<ArrayType>& term) {
    unsigned length = value_cast<IntegerValue>(term->length())->value().unsigned_value_checked(builder.error_context().bind(term->location()));
    if (length == 0)
      return builder.void_type();
    
    CType *element_type = builder.build(term->element_type());
    if (element_type->type == c_type_void)
      return builder.void_type();
    
    CType *array_type = builder.c_builder().array_type(element_type, length);
    /*
     * The array type is boxed in a struct so that it has TVM semantics; that is it can
     * be passed to and returned from a function by value, rather than decaying to a
     * pointer.
     * 
     * Of course this relies on TVM only generating a single type for any given array
     * element type and length pair.
     */
    CTypeAggregateMember member = {array_type, {"a", 0}};
    return builder.c_builder().struct_type(&term->location(), 1, &member);
  }
  
  static CType* byte_type_callback(TypeBuilder& builder, const ValuePtr<ByteType>&) {
    return builder.integer_type(IntegerType::i8, false);
  }
  
  static CType* boolean_type_callback(TypeBuilder& builder, const ValuePtr<BooleanType>&) {
    return builder.integer_type(IntegerType::i8, false);
  }
  
  static CType* integer_type_callback(TypeBuilder& builder, const ValuePtr<IntegerType>& term) {
    return builder.integer_type(term->width(), term->is_signed());
  }
  
  static CType* float_type_callback(TypeBuilder& builder, const ValuePtr<FloatType>& term) {
    return builder.float_type(term->width());
  }
  
  typedef TermOperationMap<FunctionalValue, CType*, TypeBuilder&> CallbackMap;
  static CallbackMap callback_map;
  static CallbackMap::Initializer callback_map_initializer() {
    return CallbackMap::initializer()
      .add<EmptyType>(empty_type_callback)
      .add<StructType>(struct_type_callback)
      .add<UnionType>(union_type_callback)
      .add<PointerType>(pointer_type_callback)
      .add<ArrayType>(array_type_callback)
      .add<ByteType>(byte_type_callback)
      .add<BooleanType>(boolean_type_callback)
      .add<IntegerType>(integer_type_callback)
      .add<FloatType>(float_type_callback);
  }
};

TypeBuilderCallbacks::CallbackMap TypeBuilderCallbacks::callback_map(TypeBuilderCallbacks::callback_map_initializer());

TypeBuilder::TypeBuilder(CModule *module)
: m_c_builder(module),
m_psi_alloca(NULL),
m_psi_freea(NULL),
m_memcpy(NULL),
m_null(NULL) {
  m_void_type = NULL;
  std::fill_n(m_signed_integer_types, array_size(m_signed_integer_types), static_cast<CType*>(NULL));
  std::fill_n(m_unsigned_integer_types, array_size(m_unsigned_integer_types), static_cast<CType*>(NULL));
  std::fill_n(m_float_types, array_size(m_float_types), static_cast<CType*>(NULL));
}

CType* TypeBuilder::build(const ValuePtr<>& term, bool name_used) {
  TypeMapType::const_iterator ii = m_types.find(term);
  if (ii != m_types.end()) {
    if (name_used)
      ii->second->name_used = true;
    return ii->second;
  }
  
  CType *ty;
  if (ValuePtr<FunctionType> function_type = dyn_cast<FunctionType>(term))
    ty = build_function_type(function_type);
  else
    ty = TypeBuilderCallbacks::callback_map.call(*this, value_cast<FunctionalValue>(term));

  if (name_used)
    ty->name_used = true;

  m_types.insert(std::make_pair(term, ty));
  return ty;
}

CType* TypeBuilder::void_type() {
  if (!m_void_type)
    m_void_type = c_builder().void_type();
  
  return m_void_type;
}

CType* TypeBuilder::integer_type(IntegerType::Width width, bool is_signed) {
  CType **arr = is_signed ? m_signed_integer_types : m_unsigned_integer_types;
  if (!arr[width]) {
    const PrimitiveType& pt = (is_signed ? c_compiler().primitive_types.int_types : c_compiler().primitive_types.uint_types)[width];
    if (pt.name.empty())
      error_context().error_throw(module().location(), "Primitive type not supported");
    arr[width] = c_builder().builtin_type(pt.name.c_str());
  }
  
  return arr[width];
}

CType* TypeBuilder::float_type(FloatType::Width width) {
  if (!m_float_types[width]) {
    const PrimitiveType& pt = c_compiler().primitive_types.float_types[width];
    if (pt.name.empty())
      error_context().error_throw(module().location(), "Primitive type not supported");
    m_float_types[width] = c_builder().builtin_type(pt.name.c_str());
  }
  
  return m_float_types[width];
}

CType* TypeBuilder::build_function_type(const ValuePtr<FunctionType>& ftype) {
  SmallArray<CTypeFunctionArgument, 8> arguments;
  arguments.resize(ftype->parameter_types().size());
  for (std::size_t ii = 0, ie = arguments.size(); ii != ie; ++ii) {
    arguments[ii].type = build(ftype->parameter_types()[ii]);
  }
  CType *result_type = build(ftype->result_type());
  return c_builder().function_type(&ftype->location(), result_type, arguments.size(), arguments.get());
}

CExpression *TypeBuilder::get_psi_alloca() {
  if (!m_psi_alloca) {
    CType *size_type = integer_type(IntegerType::iptr, false);
    CTypeFunctionArgument args[2];
    args[0].type = size_type;
    args[1].type = size_type;
    CType *type = c_builder().function_type(&module().location(), c_builder().pointer_type(void_type()), 2, args);
    m_psi_alloca = module().new_function(&module().location(), type, "__psi_alloca");
  }
  return m_psi_alloca;
}

CExpression *TypeBuilder::get_psi_freea() {
  if (!m_psi_freea) {
    CType *size_type = integer_type(IntegerType::iptr, false);
    CTypeFunctionArgument args[3];
    args[0].type = c_builder().pointer_type(void_type());
    args[1].type = size_type;
    args[2].type = size_type;
    CType *type = c_builder().function_type(&module().location(), void_type(), 3, args);
    m_psi_freea = module().new_function(&module().location(), type, "__psi_freea");
  }
  return m_psi_freea;
}

CExpression *TypeBuilder::get_memcpy() {
  if (!m_memcpy) {
    CType *vptr_type = c_builder().pointer_type(void_type());
    CTypeFunctionArgument args[2];
    args[0].type = vptr_type;
    args[1].type = vptr_type;
    CType *type = c_builder().function_type(&module().location(), vptr_type, 2, args);
    m_memcpy = module().new_function(&module().location(), type, "memcpy");
  }
  return m_memcpy;
}

CExpression *TypeBuilder::get_memset() {
  if (!m_memset) {
    CType *vptr_type = c_builder().pointer_type(void_type());
    CTypeFunctionArgument args[3];
    args[0].type = vptr_type;
    args[1].type = c_builder().builtin_type("int");
    args[2].type = integer_type(IntegerType::iptr, false);
    CType *type = c_builder().function_type(&module().location(), vptr_type, 3, args);
    m_memset = module().new_function(&module().location(), type, "memset");
  }
  return m_memset;
}

CExpression *TypeBuilder::get_null() {
  if (!m_null) {
    CType *vptr_type = c_builder().pointer_type(void_type());
    CExpression *zero = c_builder().literal(&module().location(), integer_type(IntegerType::i8, false), "0");
    m_null = c_builder().cast(&module().location(), vptr_type, zero);
  }
  return m_null;
}

/// \brief Does a type lower to \c void
bool TypeBuilder::is_void_type(const ValuePtr<>& type) {
  return build(type) == void_type();
}
}
}
}
