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

  llvm::Value* pointer_type_const(GlobalBuilder& builder, PointerType::Ptr) {
    return metatype_from_type(builder, llvm::Type::getInt8PtrTy(builder.llvm_context()));
  }

  const llvm::Type* pointer_type_type(ConstantBuilder& builder, PointerType::Ptr) {
    return llvm::Type::getInt8PtrTy(builder.llvm_context());
  }

  namespace {
    /**
     * Compute size and alignment of a type from its term.
     */
    class ConstantSizeAlign {
    public:
      ConstantSizeAlign(ConstantBuilder *builder, Term *type) {
        llvm::Constant *metatype_val = builder->build_constant_simple(type);
        m_size = &metatype_constant_size(metatype_val);
        m_align = &metatype_constant_align(metatype_val);
      }

      const llvm::APInt& size() const {return *m_size;}
      const llvm::APInt& align() const {return *m_align;}

    private:
      const llvm::APInt *m_size, *m_align;
    };

    /**
     *
     */
    class InstructionSizeAlign {
    public:
      InstructionSizeAlign(FunctionBuilder *builder, Term *type)
        : m_builder(builder),
          m_type(type),
          m_llvm_type(builder->build_type(type)),
          m_llvm_size(0),
          m_llvm_align(0) {
      }

    public:
      llvm::Value* size() {
        if (!m_llvm_size) {
          if (m_llvm_type) {
            m_llvm_size = llvm::ConstantInt::get(m_builder->intptr_type(), m_builder->type_size(m_llvm_type));
          } else {
            m_llvm_size = metatype_value_size(*m_builder, build_value());
          }
        }

        return m_llvm_size;
      }

      llvm::Value* align() {
        if (!m_llvm_align) {
          if (m_llvm_type)
            m_llvm_align = llvm::ConstantInt::get(m_builder->intptr_type(), m_builder->type_alignment(m_llvm_type));
          else
            m_llvm_align = metatype_value_align(*m_builder, build_value());
        }

        return m_llvm_align;
      }

    private:
      FunctionBuilder *m_builder;
      Term *m_type;
      const llvm::Type *m_llvm_type;
      llvm::Value *m_llvm_size, *m_llvm_align;
      llvm::Value *m_llvm_value;

      llvm::Value* build_value() {
        if (!m_llvm_value)
          m_llvm_value = m_builder->build_value_simple(m_type);
        return m_llvm_value;
      }
    };

    /**
     * Align an offset to a specified alignment, which must be a
     * power of two.
     *
     * The formula used is: <tt>(offset + align - 1) & ~(align - 1)</tt>
     */
    llvm::APInt constant_align(const llvm::APInt& offset, const llvm::APInt& align) {
      llvm::APInt x = align;
      --x;
      llvm::APInt y = offset;
      y += x;
      x.flip();
      y &= x;
      return y;
    }

    /**
     * Compute the maximum of two values.
     */
    llvm::Value* instruction_max(IRBuilder& irbuilder, llvm::Value *left, llvm::Value *right) {
      llvm::Value *cmp = irbuilder.CreateICmpULT(left, right);
      return irbuilder.CreateSelect(cmp, left, right);
    }

    /*
     * Align a size to a boundary. The formula is: <tt>(size + align
     * - 1) & ~(align - 1)</tt>. <tt>align</tt> must be a power of two.
     */
    llvm::Value* instruction_align(IRBuilder& irbuilder, llvm::Value* size, llvm::Value* align) {
      llvm::Constant* one = llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(size->getType()), 1);
      llvm::Value* a = irbuilder.CreateSub(align, one);
      llvm::Value* b = irbuilder.CreateAdd(size, a);
      llvm::Value* c = irbuilder.CreateNot(align);
      return irbuilder.CreateAnd(b, c);
    }
  }

  llvm::Value* array_type_insn(FunctionBuilder& builder, ArrayType::Ptr term) {
    InstructionSizeAlign element(&builder, term->element_type());
    llvm::Value *length = builder.build_value_simple(term->length());
    llvm::Value *array_size = builder.irbuilder().CreateMul(element.size(), length);
    return metatype_from_value(builder, array_size, element.align());
  }

  llvm::Constant* array_type_const(GlobalBuilder& builder, ArrayType::Ptr term) {
    ConstantSizeAlign element(&builder, term->element_type());
    llvm::APInt length = builder.build_constant_integer(term->length());
    return metatype_from_constant(builder, element.size() * length, element.align());
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

  llvm::Value* struct_type_insn(FunctionBuilder& builder, StructType::Ptr term) {
    PSI_ASSERT(term->n_members() > 0);

    IRBuilder& irbuilder = builder.irbuilder();
    const llvm::Type *i64 = llvm::Type::getInt64Ty(builder.llvm_context());
    llvm::Value *size = llvm::ConstantInt::get(i64, 0);
    llvm::Value *align = llvm::ConstantInt::get(i64, 1);

    for (std::size_t i = 0; i < term->n_members(); ++i) {
      InstructionSizeAlign member(&builder, term->member_type(i));
      size = irbuilder.CreateAdd(instruction_align(irbuilder, size, member.align()), member.size());
      align = instruction_max(irbuilder, align, member.align());
    }

    // size should always be a multiple of align
    size = instruction_align(irbuilder, size, align);
    return metatype_from_value(builder, size, align);
  }

  llvm::Constant* struct_type_const(GlobalBuilder& builder, StructType::Ptr term) {
    llvm::APInt size(builder.intptr_type_bits(), 0);
    llvm::APInt align(builder.intptr_type_bits(), 1);

    for (std::size_t i = 0; i < term->n_members(); ++i) {
      ConstantSizeAlign member(&builder, term->member_type(i));
      size = constant_align(size, member.align()) + member.size();
      if (member.align().ugt(align))
        align = member.align();
    }

    // size should always be a multiple of align
    size = constant_align(size, align);
    return metatype_from_constant(builder, size, align);
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

  llvm::Value* union_type_insn(FunctionBuilder& builder, UnionType::Ptr term) {
    IRBuilder& irbuilder = builder.irbuilder();
    const llvm::Type *i64 = llvm::Type::getInt64Ty(builder.llvm_context());
    llvm::Value *size = llvm::ConstantInt::get(i64, 0);
    llvm::Value *align = llvm::ConstantInt::get(i64, 1);

    for (std::size_t i = 0; i < term->n_members(); ++i) {
      InstructionSizeAlign member(&builder, term->member_type(i));
      size = instruction_max(irbuilder, size, member.size());
      align = instruction_max(irbuilder, align, member.align());
    }

    // size should always be a multiple of align
    size = instruction_align(irbuilder, size, align);
    return metatype_from_value(builder, size, align);
  }

  llvm::Constant* union_type_const(GlobalBuilder& builder, UnionType::Ptr term) {
    llvm::APInt size(builder.intptr_type_bits(), 0);
    llvm::APInt align(builder.intptr_type_bits(), 1);

    for (std::size_t i = 0; i < term->n_members(); ++i) {
      ConstantSizeAlign member(&builder, term->member_type(i));
      if (member.size().ugt(size))
        size = member.size();
      if (member.align().ugt(align))
        align = member.align();
    }

    // size should always be a multiple of align
    size = constant_align(size, align);
    return metatype_from_constant(builder, size, align);
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

  BuiltValue* function_specialize_const(GlobalBuilder& builder, FunctionSpecialize::Ptr term) {
    return builder.build_constant(term->function());
  }

#if 0
  struct CallbackMapValue {
    virtual BuiltValue* build_instruction(FunctionBuilder& builder, FunctionalTerm* term) const = 0;
    virtual llvm::Constant* build_constant(ConstantBuilder& builder, FunctionalTerm* term) const = 0;
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

    virtual llvm::Constant* build_constant(ConstantBuilder& builder, FunctionalTerm* term) const {
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

#define CALLBACK(ty,cb_insn,cb_const,cb_type) (ty::operation, make_callback_map_value<ty>(cb_insn, cb_const, cb_type))
#define OP_CALLBACK(ty,cb_insn,cb_const) CALLBACK(ty,cb_insn,cb_const,invalid_type_callback)

  typedef std::tr1::unordered_map<const char*, boost::shared_ptr<CallbackMapValue> > CallbackMapType;

  const CallbackMapType callbacks =
    boost::assign::map_list_of<const char*, CallbackMapType::mapped_type>
    OP_CALLBACK(FunctionSpecialize, function_specialize_insn, function_specialize_const);

  CallbackMapValue& get_callback(const char *s) {
    CallbackMapType::const_iterator it = callbacks.find(s);
    if (it == callbacks.end()) {
      std::string msg = "unknown operation type in LLVM backend: ";
      msg += s;
      throw BuildError(msg);
    }
    return *it->second;
  }
#endif
}

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      BuiltValue::BuiltValue(ConstantBuilder& builder, Term *type)
	: m_type(type), m_state(state_unknown) {
	if (type) {
	  m_simple_type = builder.build_type(m_type);
	  unsigned n_elements = 0;
	  if (m_simple_type) {
	    m_state = state_simple;
	  } else {
	    if (ArrayType::Ptr array_ty = dyn_cast<ArrayType>(m_type)) {
	      if (array_ty->length()->global()) {
		m_state = state_sequence;
		n_elements = builder.build_constant_integer(array_ty->length()).getZExtValue();
	      }
	    } else if (StructType::Ptr struct_ty = dyn_cast<StructType>(type)) {
	      m_state = state_sequence;
	      n_elements = struct_ty->n_members();
	    } else if (UnionType::Ptr union_ty = dyn_cast<UnionType>(type)) {
	      m_state = state_union;
	      n_elements = union_ty->n_members();
	    }
	  }
	  m_elements.resize(n_elements, 0);
	} else {
	  // special case for metatype, which has a NULL type
	  m_state = state_simple;
	  m_simple_type = metatype_type(builder);
	}
      }

      ConstantValue::ConstantValue(GlobalBuilder *builder, Term *type)
	: BuiltValue(*builder, type), m_builder(builder), m_simple_value(0) {
      }

      llvm::Constant *ConstantValue::simple_value() const {
	if (!m_simple_value) {
	  PSI_FAIL("not implemented");
	}

	return m_simple_value;
      }

      llvm::Constant *ConstantValue::raw_value() const {
	PSI_FAIL("not implemented");
      }

      FunctionValue::FunctionValue(FunctionBuilder *builder, Term *type, llvm::Instruction *origin)
	: BuiltValue(*builder, type), m_builder(builder), m_origin(origin), m_simple_value(0), m_raw_value(0) {
      }

      llvm::Value *FunctionValue::simple_value() const {
	if (!m_simple_value) {
	  PSI_FAIL("not implemented");
	}

	return m_simple_value;
      }

      llvm::Value *FunctionValue::raw_value() const {
	if (!m_raw_value) {
	  PSI_FAIL("not implemented");
	}

	return m_raw_value;
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

      llvm::Value* GlobalBuilder::value_to_llvm(BuiltValue *value) {
	PSI_FAIL("not implemented");
      }

      llvm::Value* FunctionBuilder::value_to_llvm(BuiltValue *value) {
	PSI_FAIL("not implemented");
      }

      BuiltValue* GlobalBuilder::get_element_value(BuiltValue *value, unsigned index) {
	PSI_FAIL("not implemented");
      }

      BuiltValue* FunctionBuilder::get_element_value(BuiltValue *value, unsigned index) {
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
	const llvm::Type *i8_type = llvm::Type::getInt8Ty(llvm_context());
	const llvm::Type *i8ptr_type = llvm::Type::getInt8PtrTy(llvm_context());

	llvm::PHINode *phi = llvm::PHINode::Create(i8ptr_type);
	irbuilder().GetInsertBlock()->getInstList().push_front(phi);

	llvm::Value *type_size = metatype_value_size(*this, build_value_simple(type));
	llvm::AllocaInst *copy_dest = irbuilder().CreateAlloca(i8_type, type_size);
	copy_dest->setAlignment(unknown_alloca_align());
	llvm::Instruction *memcpy_insn = create_memcpy(copy_dest, phi, type_size);

	return new_function_value_raw(type, memcpy_insn, memcpy_insn);
      }

      /**
       * Assign a PHI node a given value on an incoming edge from a
       * block.
       */
      void FunctionBuilder::populate_phi_node(BuiltValue *phi_node, llvm::BasicBlock *incoming_block, BuiltValue *value) {
	FunctionValue *phi_node_cast = checked_cast<FunctionValue*>(phi_node);

	if (const llvm::Type *simple_type = build_type(phi_node->type())) {
	  llvm::PHINode *llvm_phi = llvm::cast<llvm::PHINode>(phi_node_cast->m_simple_value);
	  llvm_phi->addIncoming(value->simple_value(), incoming_block);
	  return;
	} else if (StructType::Ptr struct_ty = dyn_cast<StructType>(phi_node->type())) {
	  for (unsigned i = 0, e = struct_ty->n_members(); i != e; ++i)
	    populate_phi_node(phi_node->m_elements[i], incoming_block, value->m_elements[i]);
	  return;
	} else if (ArrayType::Ptr array_ty = dyn_cast<ArrayType>(phi_node->type())) {
	  if (array_ty->length()->global()) {
	    unsigned length = build_constant_integer(array_ty->length()).getZExtValue();
	    for (unsigned i = 0; i != length; ++i)
	      populate_phi_node(phi_node->m_elements[i], incoming_block, value->m_elements[i]);
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
	  llvm::Value *value = build_value_instruction_simple(term);
	  return new_function_value_simple(term->type(), value);
	}
      }

      /**
       * Build a value for a functional operation.
       *
       * This handles aggregate types. Primitive types are forwarded
       * to build_value_functional_simple.
       */
      BuiltValue* FunctionBuilder::build_value_functional(FunctionalTerm *term) {
        if (false) {
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
        if (false) {
        } else {
	  llvm::Constant *value = build_constant_internal_simple(term);
	  return new_constant_value_simple(term->type(), value);
        }
      }

      /**
       * Return a type which will cause a field of the given type to
       * have the right alignment, or NULL if no padding field is
       * necessary.
       *
       * \param size Current size of the structure being built.
       */
      std::pair<uint64_t, const llvm::Type*> GlobalBuilder::pad_to_alignment(const llvm::Type *field, unsigned alignment, uint64_t size) {
	unsigned natural_alignment = type_alignment(field);
	PSI_ASSERT(alignment >= natural_alignment);

	uint64_t field_offset = (size + alignment - 1) & (alignment - 1);
	uint64_t field_end = field_offset + type_size(field);
	// Offset from size to correct position
	unsigned padding = field_offset - size;
	if (padding < natural_alignment)
	  return std::make_pair(field_end, static_cast<const llvm::Type*>(0));

	// Bytes of padding needed to get to a position where the natural alignment will work
	unsigned required_padding = padding - natural_alignment + 1;
	return std::make_pair(field_end, llvm::ArrayType::get(llvm::Type::getInt8Ty(llvm_context()), required_padding));
      }

      class GlobalBuilder::GlobalSequenceTypeBuilder {
      public:
	GlobalSequenceTypeBuilder(GlobalBuilder *builder_) : builder(builder_), size(0), alignment(1) {}

	void add_member(const GlobalBuilder::GlobalResult<const llvm::Type>& member) {
	  // Pad to alignment
	  std::pair<uint64_t, const llvm::Type*> padding = builder->pad_to_alignment(member.value, member.alignment, size);
	  if (padding.second)
	    members.push_back(padding.second);
	  alignment = std::max(alignment, member.alignment);
	  size = padding.first;
	  members.push_back(member.value);
	}

	GlobalBuilder::GlobalResult<const llvm::Type> result() {
	  const llvm::Type *ty = llvm::StructType::get(builder->llvm_context(), members, false);
	  return GlobalBuilder::GlobalResult<const llvm::Type>(ty, alignment);
	}

      private:
	GlobalBuilder *builder;
	uint64_t size;
	unsigned alignment;
	std::vector<const llvm::Type*> members;
      };

      class GlobalBuilder::GlobalSequenceValueBuilder {
      public:
	GlobalSequenceValueBuilder(GlobalBuilder *builder_) : builder(builder_), size(0), alignment(1) {}

	void add_member(const GlobalBuilder::GlobalResult<llvm::Constant>& member) {
	  // Pad to alignment
	  std::pair<uint64_t, const llvm::Type*> padding = builder->pad_to_alignment(member.value->getType(), member.alignment, size);
	  if (padding.second) {
	    llvm::Constant *padding_val = llvm::UndefValue::get(padding.second);
	    members.push_back(padding_val);
	  }
	  alignment = std::max(alignment, member.alignment);
	  size = padding.first;
	  members.push_back(member.value);
	}

	GlobalBuilder::GlobalResult<llvm::Constant> result() {
	  llvm::Constant *val = llvm::ConstantStruct::get(builder->llvm_context(), members, false);
	  return GlobalBuilder::GlobalResult<llvm::Constant>(val, alignment);
	}

      private:
	GlobalBuilder *builder;
	uint64_t size;
	unsigned alignment;
	std::vector<llvm::Constant*> members;
      };

      /**
       * Build a value for assigning to a global variable.
       */
      GlobalBuilder::GlobalResult<llvm::Constant> GlobalBuilder::build_global_value(Term *term) {
	if (StructValue::Ptr struct_val = dyn_cast<StructValue>(term)) {
	  GlobalSequenceValueBuilder builder(this);
	  for (unsigned i = 0, e = struct_val->n_members(); i != e; ++i)
	    builder.add_member(build_global_value(struct_val->member_value(i)));
	  return builder.result();
	} else if (ArrayValue::Ptr array_val = dyn_cast<ArrayValue>(term)) {
	  // arrays are represented as structs in global variables
	  // because they could be an array of unions, which would
	  // then have different types.
	  GlobalSequenceValueBuilder builder(this);
	  for (unsigned i = 0, e = array_val->length(); i != e; ++i)
	    builder.add_member(build_global_value(array_val->value(i)));
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
	  const llvm::Type *ty = build_type(term->type());
	  return GlobalResult<llvm::Constant>(build_constant_simple(term), type_alignment(ty));
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
	  for (unsigned i = 0, e = struct_val->n_members(); i != e; ++i)
	    builder.add_member(build_global_type(struct_val->member_value(i)));
	  return builder.result();
	} else if (ArrayValue::Ptr array_val = dyn_cast<ArrayValue>(term)) {
	  // arrays are represented as structs in global variables
	  // because they could be an array of unions, which would
	  // then have different types.
	  GlobalSequenceTypeBuilder builder(this);
	  for (unsigned i = 0, e = array_val->length(); i != e; ++i)
	    builder.add_member(build_global_type(array_val->value(i)));
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
        if (false) {
        } else {
          const llvm::Type *result = build_type_internal_simple(term);
          PSI_ASSERT_MSG(result, "all primitive types should map directly to LLVM");
          return result;
        }
      }
    }
  }
}
