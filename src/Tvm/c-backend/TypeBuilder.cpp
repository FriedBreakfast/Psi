#include "../TermOperationMap.hpp"
#include "../Aggregate.hpp"
#include "../Number.hpp"

#include "Builder.hpp"
#include "CModule.hpp"

namespace Psi {
namespace Tvm {
namespace CBackend {
struct TypeBuilderCallbacks {
  static const unsigned small_array_length = 16;
  
  static CType* empty_type_callback(TypeBuilder& builder, const ValuePtr<>& term) {
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
  
  static CType* byte_type_callback(TypeBuilder& builder, const ValuePtr<ByteType>& term) {
    return builder.integer_type(IntegerType::i8, false);
  }
  
  static CType* boolean_type_callback(TypeBuilder& builder, const ValuePtr<BooleanType>& term) {
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
: m_c_builder(module) {
  m_void_type = NULL;
  std::fill_n(m_signed_integer_types, IntegerType::i_max, static_cast<CType*>(NULL));
  std::fill_n(m_unsigned_integer_types, IntegerType::i_max, static_cast<CType*>(NULL));
  std::fill_n(m_float_types, FloatType::fp_max, static_cast<CType*>(NULL));
}

CType* TypeBuilder::build(const ValuePtr<>& term) {
  TypeMapType::const_iterator ii = m_types.find(term);
  if (ii != m_types.end())
    return ii->second;
  
  CType *ty = TypeBuilderCallbacks::callback_map.call(*this, value_cast<FunctionalValue>(term));
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
    const char *name = c_compiler().integer_type(c_builder().module(), width, is_signed);
    arr[width] = c_builder().builtin_type(name);
  }
  
  return arr[width];
}

CType* TypeBuilder::float_type(FloatType::Width width) {
  if (!m_float_types[width]) {
    const char *name = c_compiler().float_type(c_builder().module(), width);
    m_float_types[width] = c_builder().builtin_type(name);
  }
  
  return m_float_types[width];
}
}
}
}
