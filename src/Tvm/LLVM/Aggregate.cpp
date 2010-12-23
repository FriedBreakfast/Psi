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

  BuiltValue pointer_type_const(ConstantBuilder& builder, PointerType::Ptr) {
    return value_known(metatype_from_type(builder, llvm::Type::getInt8PtrTy(builder.llvm_context())));
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
        llvm::Constant *metatype_val = builder->build_constant(type);
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
          m_llvm_value = m_builder->build_known_value(m_type);
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

  BuiltValue array_type_insn(FunctionBuilder& builder, ArrayType::Ptr term) {
    InstructionSizeAlign element(&builder, term->element_type());
    llvm::Value *length = builder.build_known_value(term->length());
    llvm::Value *array_size = builder.irbuilder().CreateMul(element.size(), length);
    return metatype_from_value(builder, array_size, element.align());
  }

  llvm::Constant* array_type_const(ConstantBuilder& builder, ArrayType::Ptr term) {
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

  BuiltValue array_value_instruction(FunctionBuilder& builder, ArrayValue::Ptr term) {
    if (const llvm::Type *array_type = builder.build_type(term->type())) {
      llvm::Value *array = llvm::UndefValue::get(array_type);

      for (std::size_t i = 0; i < term->length(); ++i) {
        llvm::Value *element = builder.build_known_value(term->value(i));
        array = builder.irbuilder().CreateInsertValue(array, element, i);
      }

      return value_known(array);
    } else {
      // Length should be known, so if the element type is known the
      // whole array type should be known.
      PSI_ASSERT(!builder.build_type(term->element_type()));

      llvm::Value *storage = builder.create_alloca_for(term->type());

      InstructionSizeAlign element(&builder, term->element_type());
      llvm::Value *offset = llvm::ConstantInt::get(llvm::Type::getInt64Ty(builder.llvm_context()), 0);
      for (std::size_t i = 0; i < term->length(); ++i) {
        llvm::Value *ptr = builder.irbuilder().CreateInBoundsGEP(storage, offset);
        builder.create_store(ptr, term->value(i));
      }

      return value_unknown(storage);
    }
  }

  llvm::Constant* array_value_const_constant(ConstantBuilder& builder, ArrayValue::Ptr term) {
    const llvm::Type* array_type = builder.build_type(term->type());
    PSI_ASSERT(array_type);

    std::vector<llvm::Constant*> elements;
    for (std::size_t i = 0; i < term->length(); ++i)
      elements.push_back(builder.build_constant(term->value(i)));

    return llvm::ConstantArray::get(llvm::cast<llvm::ArrayType>(array_type), elements);
  }

  BuiltValue struct_type_insn(FunctionBuilder& builder, StructType::Ptr term) {
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

  llvm::Constant* struct_type_const(ConstantBuilder& builder, StructType::Ptr term) {
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


  BuiltValue struct_value_insn(FunctionBuilder& builder, StructValue::Ptr term) {
    PSI_ASSERT(!term->phantom());

    if (const llvm::Type* ty = builder.build_type(term->type())) {
      llvm::Value *result = llvm::UndefValue::get(ty);
      for (std::size_t i = 0; i < term->n_members(); ++i) {
        llvm::Value *val = builder.build_known_value(term->member_value(i));
        result = builder.irbuilder().CreateInsertValue(result, val, i);
      }
      return value_known(result);
    } else {
      llvm::Value *storage = builder.create_alloca_for(term->type());

      llvm::Value *offset = llvm::ConstantInt::get(llvm::Type::getInt64Ty(builder.llvm_context()), 0);
      for (std::size_t i = 0; i < term->n_members(); ++i) {
        InstructionSizeAlign member_type(&builder, term->member_value(i)->type());
        offset = instruction_align(builder.irbuilder(), offset, member_type.align());
        llvm::Value *ptr = builder.irbuilder().CreateInBoundsGEP(storage, offset);
        builder.create_store(ptr, term->member_value(i));
        offset = builder.irbuilder().CreateAdd(offset, member_type.size());
      }

      return value_unknown(storage);
    }
  }

  llvm::Constant* struct_value_const(ConstantBuilder& builder, StructValue::Ptr term) {
    PSI_ASSERT(!term->phantom());

    std::vector<llvm::Constant*> members;
    for (unsigned i = 0; i < term->n_members(); ++i)
      members.push_back(builder.build_constant(term->member_value(i)));

    return llvm::ConstantStruct::get(builder.llvm_context(), members, false);
  }

  BuiltValue union_type_insn(FunctionBuilder& builder, UnionType::Ptr term) {
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

  llvm::Constant* union_type_const(ConstantBuilder& builder, UnionType::Ptr term) {
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

  const llvm::Type* union_type_type(ConstantBuilder& builder, UnionType::Ptr term) {
    std::vector<const llvm::Type*> member_types;
    for (std::size_t i = 0; i < term->n_members(); ++i) {
      if (const llvm::Type *ty = builder.build_type(term->member_type(i))) {
        member_types.push_back(ty);
      } else {
        return NULL;
      }
    }

    return llvm::UnionType::get(builder.llvm_context(), member_types);
  }

  BuiltValue union_value_insn(FunctionBuilder& builder, UnionValue::Ptr term) {
    PSI_ASSERT(!term->phantom());

    if (const llvm::Type* ty = builder.build_type(term->type())) {
      llvm::Value *undef = llvm::UndefValue::get(ty);
      llvm::Value *val = builder.build_known_value(term->value());
      int index = llvm::cast<llvm::UnionType>(ty)->getElementTypeIndex(val->getType());
      llvm::Value *result = builder.irbuilder().CreateInsertValue(undef, val, index);
      return value_known(result);
    } else {
      llvm::Value *storage = builder.create_alloca_for(term->type());
      builder.create_store(storage, term->value());
      return value_unknown(storage);
    }
  }

  llvm::Constant* union_value_const(ConstantBuilder& builder, UnionValue::Ptr term) {
    PSI_ASSERT(!term->phantom());

    const llvm::Type *ty = builder.build_type(term->type());
    llvm::Constant *val = builder.build_constant(term->value());
    return llvm::ConstantUnion::get(llvm::cast<llvm::UnionType>(ty), val);
  }

  BuiltValue function_specialize_insn(FunctionBuilder& builder, FunctionSpecialize::Ptr term) {
    return builder.build_value(term->function());
  }

  llvm::Constant* function_specialize_const(ConstantBuilder& builder, FunctionSpecialize::Ptr term) {
    return builder.build_constant(term->function());
  }

  struct CallbackMapValue {
    virtual BuiltValue build_instruction(FunctionBuilder& builder, FunctionalTerm* term) const = 0;
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

    virtual BuiltValue build_instruction(FunctionBuilder& builder, FunctionalTerm* term) const {
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
    if (it == callbacks.end())
      throw BuildError("unknown operation type");
    return *it->second;
  }
}

/**
 * Build a value for a functional operation whose result always
 * (i.e. regardless of the arguments) has a known type. In practise,
 * this means numeric operations.
 */
BuiltValue Psi::Tvm::LLVM::FunctionBuilder::build_value_functional(FunctionalTerm *term) {
  return value_known(build_value_known_functional(term));
}

/**
 * Build an LLVM constant. The second component of the return value is
 * the required alignment of the return value.
 */
std::pair<llvm::Constant*, uint64_t> Psi::Tvm::LLVM::GlobalBuilder::build_constant_internal(FunctionalTerm *term) {
}

const llvm::Type* Psi::Tvm::LLVM::ConstantBuilder::build_type_internal(FunctionalTerm *term) {
}
