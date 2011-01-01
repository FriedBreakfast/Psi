#include "Builder.hpp"

#include "../Aggregate.hpp"

#include <boost/assign.hpp>
#include <boost/make_shared.hpp>

#include <llvm/Constant.h>

using namespace Psi;
using namespace Psi::Tvm;
using namespace Psi::Tvm::LLVM;

namespace {
  const llvm::Type* invalid_type_callback(ConstantBuilder&, Term*) {
    PSI_FAIL("term cannot be used as a type");
  }

  const llvm::Type* array_type_type(ConstantBuilder& builder, ArrayType::Ptr term) {
    const llvm::Type* element_type = builder.build_type(term->element_type());
    if (!element_type)
      return NULL;

    const llvm::APInt& length_value = builder.build_constant_integer(term->length());

    return llvm::ArrayType::get(element_type, length_value.getZExtValue());
  }

  FunctionValue* array_value_insn(FunctionBuilder& builder, ArrayValue::Ptr term) {
    if (const llvm::Type *simple_type = builder.build_type(term->type())) {
      PSI_ASSERT(llvm::isa<llvm::ArrayType>(simple_type));
      llvm::Value *array = llvm::UndefValue::get(simple_type);

      for (std::size_t i = 0; i < term->length(); ++i) {
        llvm::Value *element = builder.build_value_simple(term->value(i));
        array = builder.irbuilder().CreateInsertValue(array, element, i);
      }

      return builder.new_function_value_simple(term->type(), array);
    } else {
      llvm::SmallVector<BuiltValue*, 4> elements;
      for (std::size_t i = 0; i < term->length(); ++i)
	elements.push_back(builder.build_value(term->value(i)));
      return builder.new_function_value_aggregate(term->type(), elements);
    }
  }

  ConstantValue* array_value_const(GlobalBuilder& builder, ArrayValue::Ptr term) {
    if (const llvm::Type *simple_type = builder.build_type(term->type())) {
      PSI_ASSERT(llvm::isa<llvm::ArrayType>(simple_type));
      llvm::SmallVector<llvm::Constant*, 4> elements(term->length());
      for (std::size_t i = 0; i < term->length(); ++i)
        elements[i] = builder.build_constant_simple(term->value(i));

      llvm::Constant *array_val = llvm::ConstantArray::get(llvm::cast<llvm::ArrayType>(simple_type), &elements[0], elements.size());
      return builder.new_constant_value_simple(term->type(), array_val);
    } else {
      llvm::SmallVector<ConstantValue*, 4> elements;
      for (std::size_t i = 0; i < term->length(); ++i)
        elements.push_back(builder.build_constant(term->value(i)));

      return builder.new_constant_value_aggregate(term->type(), elements);
    }
  }

  const llvm::Type* struct_type_type(ConstantBuilder& builder, StructType::Ptr term) {
    std::vector<const llvm::Type*> member_types;
    for (std::size_t i = 0; i < term->n_members(); ++i) {
      if (const llvm::Type* param_result = builder.build_type(term->member_type(i))) {
        member_types.push_back(param_result);
      } else {
        return NULL;
      }
    }

    return llvm::StructType::get(builder.llvm_context(), member_types);
  }


  BuiltValue* struct_value_insn(FunctionBuilder& builder, StructValue::Ptr term) {
    if (const llvm::Type *simple_type = builder.build_type(term->type())) {
      PSI_ASSERT(llvm::isa<llvm::StructType>(simple_type));
      llvm::Value *result = llvm::UndefValue::get(simple_type);
      for (std::size_t i = 0; i < term->n_members(); ++i) {
        llvm::Value *val = builder.build_value_simple(term->member_value(i));
        result = builder.irbuilder().CreateInsertValue(result, val, i);
      }
      return builder.new_function_value_simple(term->type(), result);
    } else {
      llvm::SmallVector<BuiltValue*, 4> elements;
      for (std::size_t i = 0; i < term->n_members(); ++i)
        elements.push_back(builder.build_value(term->member_value(i)));
      return builder.new_function_value_aggregate(term->type(), elements);
    }
  }

  ConstantValue* struct_value_const(GlobalBuilder& builder, StructValue::Ptr term) {
    if (builder.build_type(term->type())) {
      llvm::SmallVector<llvm::Constant*, 4> members(term->n_members());
      for (unsigned i = 0; i < term->n_members(); ++i)
        members[i] = builder.build_constant_simple(term->member_value(i));

      llvm::Constant *value = llvm::ConstantStruct::get(builder.llvm_context(), &members[0], members.size(), false);
      return builder.new_constant_value_simple(term->type(), value);
    } else {
      llvm::SmallVector<ConstantValue*, 4> elements;
      for (std::size_t i = 0; i < term->n_members(); ++i)
        elements.push_back(builder.build_constant(term->member_value(i)));
      return builder.new_constant_value_aggregate(term->type(), elements);
    }
  }

  const llvm::Type* union_type_type(ConstantBuilder&, UnionType::Ptr) {
    return NULL;
  }

  BuiltValue* union_value_insn(FunctionBuilder& builder, UnionValue::Ptr term) {
    BuiltValue *element_value = builder.build_value(term->value());

    llvm::SmallVector<BuiltValue*, 4> elements;
    for (unsigned i = 0, e = term->type()->n_members(); i != e; ++i) {
      BuiltValue *value = (term->type()->member_type(i) == element_value->type()) ? element_value : 0;
      elements.push_back(value);
    }

    return builder.new_function_value_aggregate(term->type(), elements);
  }

  ConstantValue* union_value_const(GlobalBuilder& builder, UnionValue::Ptr term) {
    ConstantValue *element_value = builder.build_constant(term->value());

    llvm::SmallVector<ConstantValue*, 4> elements;
    for (unsigned i = 0, e = term->type()->n_members(); i != e; ++i) {
      ConstantValue *value = (term->type()->member_type(i) == element_value->type()) ? element_value : 0;
      elements.push_back(value);
    }

    return builder.new_constant_value_aggregate(term->type(), elements);
  }

  BuiltValue* function_specialize_insn(FunctionBuilder& builder, FunctionSpecialize::Ptr term) {
    return builder.build_value(term->function());
  }

  ConstantValue* function_specialize_const(GlobalBuilder& builder, FunctionSpecialize::Ptr term) {
    return builder.build_constant(term->function());
  }

  struct CallbackMapValue {
    virtual BuiltValue* build_instruction(FunctionBuilder& builder, FunctionalTerm* term) const = 0;
    virtual ConstantValue* build_constant(GlobalBuilder& builder, FunctionalTerm* term) const = 0;
    virtual const llvm::Type* build_type(ConstantBuilder& builder, FunctionalTerm* term) const = 0;
  };

  template<typename TermTagType, typename InsnCbType, typename ConstCbType, typename TypeCbType>
  class CallbackMapValueImpl : public CallbackMapValue {
    InsnCbType m_insn_cb;
    ConstCbType m_const_cb;
    TypeCbType m_type_cb;

  public:
    CallbackMapValueImpl(InsnCbType insn_cb, ConstCbType const_cb, TypeCbType type_cb)
      : m_insn_cb(insn_cb), m_const_cb(const_cb), m_type_cb(type_cb) {
    }

    virtual BuiltValue* build_instruction(FunctionBuilder& builder, FunctionalTerm* term) const {
      return m_insn_cb(builder, cast<TermTagType>(term));
    }

    virtual ConstantValue* build_constant(GlobalBuilder& builder, FunctionalTerm* term) const {
      return m_const_cb(builder, cast<TermTagType>(term));
    }

    virtual const llvm::Type* build_type(ConstantBuilder& builder, FunctionalTerm* term) const {
      return m_type_cb(builder, cast<TermTagType>(term));
    }
  };

  template<typename TermTagType, typename InsnCbType, typename ConstCbType, typename TypeCbType>
  boost::shared_ptr<CallbackMapValue> make_callback_map_value(InsnCbType insn_cb, ConstCbType const_cb, TypeCbType type_cb) {
    return boost::make_shared<CallbackMapValueImpl<TermTagType, InsnCbType, ConstCbType, TypeCbType> >(insn_cb, const_cb, type_cb);
  }

  template<typename TermTagType, typename InsnCbType>
  class SimpleInsnWrapper {
    InsnCbType m_insn_cb;

  public:
    SimpleInsnWrapper(InsnCbType insn_cb) : m_insn_cb(insn_cb) {}

    BuiltValue* operator () (FunctionBuilder& builder, typename TermTagType::Ptr term) const {
      llvm::Value *value = m_insn_cb(builder, term);
      return builder.new_function_value_simple(term->type(), value);
    }
  };

  template<typename TermTagType, typename InsnCbType>
  SimpleInsnWrapper<TermTagType, InsnCbType> make_simple_insn_wrapper(InsnCbType insn_cb) {
    return SimpleInsnWrapper<TermTagType, InsnCbType>(insn_cb);
  }

  template<typename TermTagType, typename ConstCbType>
  class SimpleConstWrapper {
    ConstCbType m_insn_cb;

  public:
    SimpleConstWrapper(ConstCbType insn_cb) : m_insn_cb(insn_cb) {}

    ConstantValue* operator () (GlobalBuilder& builder, typename TermTagType::Ptr term) const {
      llvm::Constant *value = m_insn_cb(builder, term);
      return builder.new_constant_value_simple(term->type(), value);
    }
  };

  template<typename TermTagType, typename ConstCbType>
  SimpleConstWrapper<TermTagType, ConstCbType> make_simple_const_wrapper(ConstCbType insn_cb) {
    return SimpleConstWrapper<TermTagType, ConstCbType>(insn_cb);
  }

#define CALLBACK(ty,cb_insn,cb_const,cb_type) (ty::operation, make_callback_map_value<ty>(cb_insn, cb_const, cb_type))
#define OP_CALLBACK(ty,cb_insn,cb_const) CALLBACK(ty,cb_insn,cb_const,invalid_type_callback)
#define TYPE_CALLBACK(ty,cb_insn,cb_const,cb_type) CALLBACK(ty,make_simple_insn_wrapper<ty>(cb_insn),make_simple_const_wrapper<ty>(cb_const),cb_type)

  typedef std::tr1::unordered_map<const char*, boost::shared_ptr<CallbackMapValue> > CallbackMapType;

  const CallbackMapType callbacks/* =
    boost::assign::map_list_of<const char*, CallbackMapType::mapped_type>
    TYPE_CALLBACK(ArrayType, array_type_insn, array_type_const, array_type_type)
    TYPE_CALLBACK(StructType, struct_type_insn, struct_type_const, struct_type_type)
    TYPE_CALLBACK(UnionType, union_type_insn, union_type_const, union_type_type)
    OP_CALLBACK(ArrayValue, array_value_insn, array_value_const)
    OP_CALLBACK(StructValue, struct_value_insn, struct_value_const)
    OP_CALLBACK(UnionValue, union_value_insn, union_value_const)
    OP_CALLBACK(FunctionSpecialize, function_specialize_insn, function_specialize_const)*/;

  const CallbackMapValue *get_callback(const char *s) {
    CallbackMapType::const_iterator it = callbacks.find(s);
    if (it != callbacks.end())
      return it->second.get();
    else
      return 0;
  }
}

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      BuiltValue::BuiltValue(ConstantBuilder& builder, Term *type)
	: m_type(type), m_state(state_unknown), m_n_elements(0) {
	if (type) {
	  m_simple_type = builder.build_type(m_type);
	  if (m_simple_type) {
	    m_state = state_simple;
	  } else {
	    if (ArrayType::Ptr array_ty = dyn_cast<ArrayType>(m_type)) {
	      if (array_ty->length()->global()) {
		m_state = state_sequence;
		m_n_elements = builder.build_constant_integer(array_ty->length()).getZExtValue();
	      }
	    } else if (StructType::Ptr struct_ty = dyn_cast<StructType>(type)) {
	      m_state = state_sequence;
	      m_n_elements = struct_ty->n_members();
	    } else if (UnionType::Ptr union_ty = dyn_cast<UnionType>(type)) {
	      m_state = state_union;
	      m_n_elements = union_ty->n_members();
	    }
	  }
	} else {
	  // special case for metatype, which has a NULL type
	  m_state = state_simple;
	  m_simple_type = metatype_type(builder);
	}
      }

      ConstantValue::ConstantValue(GlobalBuilder *builder, Term *type)
	: BuiltValue(*builder, type), m_builder(builder),
	  m_simple_value(0), m_elements(n_elements(), 0) {
      }

      llvm::Constant *ConstantValue::simple_value() {
	if (!m_simple_value) {
	  PSI_FAIL("not implemented");
	}

	return m_simple_value;
      }

      llvm::Constant *ConstantValue::raw_value() {
	PSI_FAIL("not implemented");
      }

      ConstantValue* ConstantValue::struct_element_value(unsigned index) {
	PSI_ASSERT(isa<StructType>(type()) && (index < m_elements.size()));

	if (!m_elements[index])
	  m_elements[index] = struct_or_array_element_value(cast<StructType>(type())->member_type(index), index);

	return m_elements[index];
      }

      ConstantValue* ConstantValue::array_element_value(unsigned index) {
	PSI_ASSERT(isa<ArrayType>(type()) && (index < m_elements.size()));

	if (!m_elements[index])
	  m_elements[index] = struct_or_array_element_value(cast<ArrayType>(type())->element_type(), index);

	return m_elements[index];
      }

      /**
       * Common code for implementing struct_element_value and
       * array_element_value, since accessing structs and arrays in
       * LLVM is basically the same.
       */
      ConstantValue* ConstantValue::struct_or_array_element_value(Term *element_type, unsigned index) {
	PSI_FAIL("not implemented");
      }

      ConstantValue* ConstantValue::union_element_value(unsigned index) {
	PSI_ASSERT(isa<UnionType>(type()) && (index < m_elements.size()));

	if (!m_elements[index]) {
	  PSI_FAIL("not implemented");
	}

	return m_elements[index];
      }

      FunctionValue::FunctionValue(FunctionBuilder *builder, Term *type, llvm::Instruction *origin)
	: BuiltValue(*builder, type), m_builder(builder), m_origin(origin),
	  m_simple_value(0), m_raw_value(0), m_elements(n_elements(), 0) {
      }

      llvm::Value *FunctionValue::simple_value() {
	if (!m_simple_value) {
	  PSI_FAIL("not implemented");
	}

	return m_simple_value;
      }

      llvm::Value *FunctionValue::raw_value() {
	if (!m_raw_value) {
	  PSI_FAIL("not implemented");
	}

	return m_raw_value;
      }


      BuiltValue* FunctionValue::struct_element_value(unsigned index) {
	PSI_ASSERT(isa<StructType>(type()) && (index < m_elements.size()));

	if (!m_elements[index])
	  m_elements[index] = struct_or_array_element_value(cast<StructType>(type())->member_type(index), index);

	return m_elements[index];
      }

      BuiltValue* FunctionValue::array_element_value(unsigned index) {
	PSI_ASSERT(isa<ArrayType>(type()) && (index < m_elements.size()));

	if (!m_elements[index])
	  m_elements[index] = struct_or_array_element_value(cast<ArrayType>(type())->element_type(), index);

	return m_elements[index];
      }

      /**
       * Common code for implementing struct_element_value and
       * array_element_value, since accessing structs and arrays in
       * LLVM is basically the same.
       */
      FunctionValue* FunctionValue::struct_or_array_element_value(Term *element_type, unsigned index) {
	PSI_ASSERT(m_simple_value || m_raw_value);
	IRBuilder irbuilder(m_builder->irbuilder());
	irbuilder.SetInsertPoint(m_origin->getParent(), m_origin);
	FunctionValue *result = m_builder->new_function_value(element_type, m_origin);
	result->m_simple_value = m_simple_value ? irbuilder.CreateExtractValue(m_simple_value, index) : 0;
	//result->m_raw_value = m_raw_value ? irbuilder.CreateInBoundsGEP(m_raw_value, struct_element_offset_insn(irbuilder, index)) : 0;
	return result;
      }

      BuiltValue* FunctionValue::union_element_value(unsigned index) {
	PSI_ASSERT(isa<UnionType>(type()) && (index < m_elements.size()));

	if (!m_elements[index]) {
	  PSI_FAIL("not implemented");
	}

	return m_elements[index];
      }

      /**
       * Create a new ConstantValue object. This is an internal
       * function to handle the pool allocation and construction of
       * the object.
       */
      ConstantValue* GlobalBuilder::new_constant_value(Term *type) {
	ConstantValue *p = m_constant_value_pool.malloc();
	try {
	  return new (p) ConstantValue (this, type);
	} catch (...) {
	  m_constant_value_pool.free(p);
	  throw;
	}
      }

      /**
       * Create a new ConstantValue for a simple type with a known
       * LLVM value.
       */
      ConstantValue* GlobalBuilder::new_constant_value_simple(Term *type, llvm::Constant *value) {
	ConstantValue *cv = new_constant_value(type);
	PSI_ASSERT(value->getType() == cv->simple_type());
	cv->m_simple_value = value;
	return cv;
      }

      /**
       * Create a new ConstantValue, from a machine-specific
       * representation of its data.
       */
      ConstantValue* GlobalBuilder::new_constant_value_raw(Term *type, const llvm::SmallVectorImpl<char>& data) {
	ConstantValue *cv = new_constant_value(type);
	/// \todo add an assert to check the given data has the
	/// correct size.
	PSI_ASSERT(cv->m_raw_value.empty());
	cv->m_raw_value.append(data.begin(), data.end());
	return cv;
      }

      /**
       * Create a new ConstantValue for an aggregate type, given a
       * value for each of its elements (unless it is a union, in
       * which case some elements may be NULL).
       */
      ConstantValue* GlobalBuilder::new_constant_value_aggregate(Term *type, const llvm::SmallVectorImpl<ConstantValue*>& elements) {
	ConstantValue *cv = new_constant_value(type);
	PSI_ASSERT(cv->m_elements.size() == elements.size());
	PSI_ASSERT((cv->state() == BuiltValue::state_union) ||
		   (std::find(elements.begin(), elements.end(), static_cast<ConstantValue*>(0)) == elements.end()));
	std::copy(elements.begin(), elements.end(), cv->m_elements.begin());
	return cv;
      }

      /**
       * Create a new FunctionValue object. This is an internal
       * function to handle the pool allocation and construction of
       * the object.
       */
      FunctionValue* FunctionBuilder::new_function_value(Term *type, llvm::Instruction *origin) {
	if (!origin)
	  origin = insert_placeholder_instruction();

	FunctionValue *p = m_function_value_pool.malloc();
	try {
	  return new (p) FunctionValue (this, type, origin);
	} catch (...) {
	  m_function_value_pool.free(p);
	  throw;
	}
      }

      /**
       * Create a new ConstantValue for a simple type with a known
       * LLVM value.
       */
      FunctionValue* FunctionBuilder::new_function_value_simple(Term *type, llvm::Value *value, llvm::Instruction *origin) {
	FunctionValue *cv = new_function_value(type, origin);
	PSI_ASSERT(value->getType() == cv->simple_type());
	cv->m_simple_value = value;
	return cv;
      }

      /**
       * Create a new FunctionValue, from a machine-specific
       * representation of its data.
       */
      FunctionValue* FunctionBuilder::new_function_value_raw(Term *type, llvm::Value *ptr, llvm::Instruction *origin) {
	FunctionValue *cv = new_function_value(type, origin);
	cv->m_raw_value = ptr;
	return cv;
      }

      /**
       * Create a new FunctionValue for an aggregate type, given a
       * value for each of its elements (unless it is a union, in
       * which case some elements may be NULL).
       */
      FunctionValue* FunctionBuilder::new_function_value_aggregate(Term *type, const llvm::SmallVectorImpl<BuiltValue*>& elements, llvm::Instruction *origin) {
	FunctionValue *cv = new_function_value(type, origin);
	PSI_ASSERT(cv->m_elements.size() == elements.size());
	PSI_ASSERT((cv->state() == BuiltValue::state_sequence) || (cv->state() == BuiltValue::state_union));
	PSI_ASSERT((cv->state() == BuiltValue::state_union) ||
		   (std::find(elements.begin(), elements.end(), static_cast<BuiltValue*>(0)) == elements.end()));
	std::copy(elements.begin(), elements.end(), cv->m_elements.begin());
	return cv;
      }

      /**
       * Store a value to the specified memory address.
       */
      void FunctionBuilder::store_value(BuiltValue *value, llvm::Value *ptr) {
	PSI_FAIL("not implemented");
      }

      /**
       * Load a value of the specified type from the specified memory address.
       */
      BuiltValue* FunctionBuilder::load_value(Term *type, llvm::Value *ptr) {
	PSI_FAIL("not implemented");
      }

      /**
       * Create a PHI node for a given value type, by traversing the
       * type and handling each component in a default way
       * (i.e. unions are treated as opaque byte arrays).
       *
       * \param insert_point Instruction to insert conversion
       * instructions after.
       */
      BuiltValue* FunctionBuilder::build_phi_node(Term *type, llvm::Instruction *insert_point) {
	if (const llvm::Type *simple_type = build_type(type)) {
	  llvm::PHINode *phi = llvm::PHINode::Create(simple_type);
	  irbuilder().GetInsertBlock()->getInstList().push_front(phi);
	  return new_function_value_simple(type, phi, insert_point);
	} else if (StructType::Ptr struct_ty = dyn_cast<StructType>(type)) {
	  llvm::SmallVector<BuiltValue*,4> elements;
	  for (unsigned i = 0, e = struct_ty->n_members(); i != e; ++i)
	    elements.push_back(build_phi_node(struct_ty->member_type(i), insert_point));
	  return new_function_value_aggregate(type, elements);
	} else if (ArrayType::Ptr array_ty = dyn_cast<ArrayType>(type)) {
	  if (array_ty->length()->global()) {
	    unsigned length = build_constant_integer(array_ty->length()).getZExtValue();
	    Term *element_type = array_ty->element_type();
	    llvm::SmallVector<BuiltValue*,4> elements;
	    for (unsigned i = 0; i != length; ++i)
	      elements.push_back(build_phi_node(element_type, insert_point));
	    return new_function_value_aggregate(type, elements);
	  }
	}

	// Type is neither a known simple type nor an aggregate I
	// can handle, so create it as an unknown type.
	llvm::PHINode *phi = llvm::PHINode::Create(get_pointer_type());
	irbuilder().GetInsertBlock()->getInstList().push_front(phi);

	llvm::Value *type_size = build_value_simple(MetatypeSize::get(type));
	llvm::AllocaInst *copy_dest = irbuilder().CreateAlloca(get_byte_type(), type_size);
	copy_dest->setAlignment(unknown_alloca_align());
	llvm::Instruction *memcpy_insn = create_memcpy(copy_dest, phi, type_size);

	return new_function_value_raw(type, memcpy_insn, memcpy_insn);
      }

      /**
       * Assign a PHI node a given value on an incoming edge from a
       * block.
       */
      void FunctionBuilder::populate_phi_node(BuiltValue *phi_node, llvm::BasicBlock *incoming_block, BuiltValue *value) {
	PSI_ASSERT(phi_node);
	FunctionValue *phi_node_cast = checked_cast<FunctionValue*>(phi_node);

	if (phi_node->simple_type()) {
	  llvm::PHINode *llvm_phi = llvm::cast<llvm::PHINode>(phi_node_cast->m_simple_value);
	  llvm_phi->addIncoming(value->simple_value(), incoming_block);
	  return;
	} else if (StructType::Ptr struct_ty = dyn_cast<StructType>(phi_node->type())) {
	  for (unsigned i = 0, e = struct_ty->n_members(); i != e; ++i)
	    populate_phi_node(phi_node_cast->m_elements[i], incoming_block, value->struct_element_value(i));
	  return;
	} else if (ArrayType::Ptr array_ty = dyn_cast<ArrayType>(phi_node->type())) {
	  if (array_ty->length()->global()) {
	    unsigned length = build_constant_integer(array_ty->length()).getZExtValue();
	    for (unsigned i = 0; i != length; ++i)
	      populate_phi_node(phi_node_cast->m_elements[i], incoming_block, value->array_element_value(i));
	    return;
	  }
	}

	llvm::PHINode *llvm_phi = llvm::cast<llvm::PHINode>(phi_node_cast->m_raw_value);
	llvm_phi->addIncoming(value->raw_value(), incoming_block);
      }

      /**
       * Build a value for an instruction operation.
       *
       * This handles complex operations on aggregate types; numeric
       * operations are forwarded to build_value_instruction_simple.
       */
      BuiltValue* FunctionBuilder::build_value_instruction(InstructionTerm *term) {
	if (false) {
	} else {
	  return build_value_instruction_simple(term);
	}
      }

      /**
       * Build a value for a functional operation.
       *
       * This handles aggregate types. Primitive types are forwarded
       * to build_value_functional_simple.
       */
      BuiltValue* FunctionBuilder::build_value_functional(FunctionalTerm *term) {
	if (const CallbackMapValue *cb = get_callback(term->operation())) {
	  return cb->build_instruction(*this, term);
        } else {
	  llvm::Value *value = build_value_functional_simple(term);
	  return new_function_value_simple(term->type(), value);
        }
      }

      /**
       * Build an LLVM constant. The second component of the return value is
       * the required alignment of the return value.
       *
       * This handles aggregate types. Primitive types are forwarded
       * to build_constant_internal_simple.
       */
      ConstantValue* GlobalBuilder::build_constant_internal(FunctionalTerm *term) {
	if (const CallbackMapValue *cb = get_callback(term->operation())) {
	  return cb->build_constant(*this, term);
        } else {
	  llvm::Constant *value = build_constant_internal_simple(term);
	  return new_constant_value_simple(term->type(), value);
        }
      }

      GlobalBuilder::PaddingStatus::PaddingStatus()
	: size(0), llvm_size(0) {
      }

      GlobalBuilder::PaddingStatus::PaddingStatus(uint64_t size_, uint64_t llvm_size_)
	: size(size_), llvm_size(llvm_size_) {
	PSI_ASSERT(size >= llvm_size);
      }

      /**
       * Return a type which will cause a field of the given type to
       * have the right alignment, or NULL if no padding field is
       * necessary.
       *
       * \param size Current size of the structure being built.
       *
       * \param llvm_size Current size of the fields visible to LLVM -
       * this may differ from \c size since individual union values
       * may be much smaller than the union itself.
       */
      std::pair<GlobalBuilder::PaddingStatus, const llvm::Type*>
      GlobalBuilder::pad_to_alignment(Term *field_type, const llvm::Type *llvm_field_type, unsigned alignment, PaddingStatus status) {
	unsigned natural_alignment = type_alignment(llvm_field_type);
	PSI_ASSERT(alignment >= natural_alignment);

	uint64_t field_offset = (status.size + alignment - 1) & ~(alignment - 1);
	// Offset from size to correct position
	uint64_t padding = field_offset - status.llvm_size;

	PaddingStatus new_status(field_offset + constant_type_size(field_type),
				 field_offset + type_size(llvm_field_type));
	if (padding < natural_alignment)
	  return std::make_pair(new_status, static_cast<const llvm::Type*>(0));

	// Bytes of padding needed to get to a position where the natural alignment will work
	uint64_t required_padding = padding - natural_alignment + 1;
	return std::make_pair(new_status, llvm::ArrayType::get(get_byte_type(), required_padding));
      }

      class GlobalBuilder::GlobalSequenceTypeBuilder {
      public:
	GlobalSequenceTypeBuilder(GlobalBuilder *builder_) : builder(builder_), alignment(1) {}

	void add_member(Term *member_type, const GlobalBuilder::GlobalResult<const llvm::Type>& member) {
	  // Pad to alignment
	  std::pair<PaddingStatus, const llvm::Type*> padding = builder->pad_to_alignment(member_type, member.value, member.alignment, padding_status);
	  if (padding.second)
	    members.push_back(padding.second);
	  alignment = std::max(alignment, member.alignment);
	  padding_status = padding.first;
	  members.push_back(member.value);
	}

	GlobalBuilder::GlobalResult<const llvm::Type> result() {
	  const llvm::Type *ty = llvm::StructType::get(builder->llvm_context(), members, false);
	  return GlobalBuilder::GlobalResult<const llvm::Type>(ty, alignment);
	}

      private:
	GlobalBuilder *builder;
	PaddingStatus padding_status;
	unsigned alignment;
	std::vector<const llvm::Type*> members;
      };

      class GlobalBuilder::GlobalSequenceValueBuilder {
      public:
	GlobalSequenceValueBuilder(GlobalBuilder *builder_) : builder(builder_), alignment(1) {}

	void add_member(Term *member_type, const GlobalBuilder::GlobalResult<llvm::Constant>& member) {
	  // Pad to alignment
	  std::pair<PaddingStatus, const llvm::Type*> padding = builder->pad_to_alignment(member_type, member.value->getType(), member.alignment, padding_status);
	  if (padding.second) {
	    llvm::Constant *padding_val = llvm::UndefValue::get(padding.second);
	    members.push_back(padding_val);
	  }
	  alignment = std::max(alignment, member.alignment);
	  padding_status = padding.first;
	  members.push_back(member.value);
	}

	GlobalBuilder::GlobalResult<llvm::Constant> result() {
	  llvm::Constant *val = llvm::ConstantStruct::get(builder->llvm_context(), members, false);
	  return GlobalBuilder::GlobalResult<llvm::Constant>(val, alignment);
	}

      private:
	GlobalBuilder *builder;
	PaddingStatus padding_status;
	unsigned alignment;
	std::vector<llvm::Constant*> members;
      };

      /**
       * Build a value for assigning to a global variable.
       */
      GlobalBuilder::GlobalResult<llvm::Constant> GlobalBuilder::build_global_value(Term *term) {
	if (StructValue::Ptr struct_val = dyn_cast<StructValue>(term)) {
	  GlobalSequenceValueBuilder builder(this);
	  for (unsigned i = 0, e = struct_val->n_members(); i != e; ++i) {
	    Term *member_value = struct_val->member_value(i);
	    builder.add_member(member_value->type(), build_global_value(member_value));
	  }
	  return builder.result();
	} else if (ArrayValue::Ptr array_val = dyn_cast<ArrayValue>(term)) {
	  // arrays are represented as structs in global variables
	  // because they could be an array of unions, which would
	  // then have different types.
	  GlobalSequenceValueBuilder builder(this);
	  for (unsigned i = 0, e = array_val->length(); i != e; ++i) {
	    Term *member_value = array_val->value(i);
	    builder.add_member(member_value->type(), build_global_value(member_value));
	  }
	  return builder.result();
	} else if (UnionValue::Ptr union_val = dyn_cast<UnionValue>(term)) {
	  UnionType::Ptr union_ty = union_val->type();
	  unsigned alignment = 1;
	  for (unsigned i = 0, e = union_ty->n_members(); i != e; ++i)
	    alignment = std::max(alignment, constant_type_alignment(union_ty->member_type(i)));
	  GlobalResult<llvm::Constant> value_result = build_global_value(union_val->value());
	  PSI_ASSERT(alignment >= value_result.alignment);
	  return GlobalResult<llvm::Constant>(value_result.value, alignment);
	} else {
	  llvm::Constant *value = build_constant_simple(term);
	  return GlobalResult<llvm::Constant>(value, type_alignment(value->getType()));
	}
      }

      /**
       * Build a type for a global variable - this returns the type
       * used to store this term, rather than the type to store terms
       * of this type.
       */
      GlobalBuilder::GlobalResult<const llvm::Type> GlobalBuilder::build_global_type(Term *term) {
	if (StructValue::Ptr struct_val = dyn_cast<StructValue>(term)) {
	  GlobalSequenceTypeBuilder builder(this);
	  for (unsigned i = 0, e = struct_val->n_members(); i != e; ++i) {
	    Term *member_value = struct_val->member_value(i);
	    builder.add_member(member_value->type(), build_global_type(member_value));
	  }
	  return builder.result();
	} else if (ArrayValue::Ptr array_val = dyn_cast<ArrayValue>(term)) {
	  // arrays are represented as structs in global variables
	  // because they could be an array of unions, which would
	  // then have different types.
	  GlobalSequenceTypeBuilder builder(this);
	  for (unsigned i = 0, e = array_val->length(); i != e; ++i) {
	    Term *member_value = array_val->value(i);
	    builder.add_member(member_value->type(), build_global_type(member_value));
	  }
	  return builder.result();
	} else if (UnionValue::Ptr union_val = dyn_cast<UnionValue>(term)) {
	  UnionType::Ptr union_ty = union_val->type();
	  unsigned alignment = 1;
	  for (unsigned i = 0, e = union_ty->n_members(); i != e; ++i)
	    alignment = std::max(alignment, constant_type_alignment(union_ty->member_type(i)));
	  GlobalResult<const llvm::Type> value_result = build_global_type(union_val->value());
	  PSI_ASSERT(alignment >= value_result.alignment);
	  return GlobalResult<const llvm::Type>(value_result.value, alignment);
	} else {
	  const llvm::Type *ty = build_type(term->type());
	  return GlobalResult<const llvm::Type>(ty, type_alignment(ty));
	}
      }

      /**
       * Internal function to do the actual work of building a
       * type. This function handles aggregate types, primitive types
       * are forwarded to build_type_internal_simple.
       */
      const llvm::Type* ConstantBuilder::build_type_internal(FunctionalTerm *term) {
	if (const CallbackMapValue *cb = get_callback(term->operation())) {
	  return cb->build_type(*this, term);
        } else {
          const llvm::Type *result = build_type_internal_simple(term);
          PSI_ASSERT_MSG(result, "all primitive types should map directly to LLVM");
          return result;
        }
      }
    }
  }
}
