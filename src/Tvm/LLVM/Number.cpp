#include "Builder.hpp"

#include "../Number.hpp"

#include <functional>

#include <boost/assign.hpp>
#include <boost/make_shared.hpp>

#include <llvm/Constant.h>
#include <llvm/Target/TargetData.h>

using namespace Psi;
using namespace Psi::Tvm;
using namespace Psi::Tvm::LLVM;

namespace {
  const llvm::Type* invalid_type_callback(ConstantBuilder&, Term*) {
    PSI_FAIL("term cannot be used as a type");
  }

  const llvm::Type* boolean_type_type(ConstantBuilder& builder, BooleanType::Ptr) {
    return llvm::IntegerType::get(builder.llvm_context(), 1);
  }

  llvm::Constant* boolean_value_const(ConstantBuilder& builder, BooleanValue::Ptr term) {
    return term->value() ? 
      llvm::ConstantInt::getTrue(builder.llvm_context())
      : llvm::ConstantInt::getFalse(builder.llvm_context());
  }

  unsigned integer_type_bits(ConstantBuilder& builder, IntegerType::Ptr term) {
    switch (term->width()) {
    case IntegerType::i8: return 8;
    case IntegerType::i16: return 16;
    case IntegerType::i32: return 32;
    case IntegerType::i64: return 64;

    case IntegerType::iptr:
      return builder.intptr_type_bits();

    default:
      PSI_FAIL("unknown integer width");
    }
  }

  const llvm::Type* integer_type_type(ConstantBuilder& builder, IntegerType::Ptr term) {
    return llvm::IntegerType::get(builder.llvm_context(), integer_type_bits(builder, term));
  }

  llvm::Constant* integer_value_const(ConstantBuilder& builder, IntegerValue::Ptr term) {
    IntegerType::Ptr type = term->type();
    const llvm::Type *llvm_type = integer_type_type(builder, type);
    llvm::APInt llvm_value(integer_type_bits(builder, type), term->value(), type->is_signed());
    return llvm::ConstantInt::get(llvm_type, llvm_value);
  }

  template<typename TermTagType>
  struct InstructionBinaryOp {
    typedef llvm::Value* (IRBuilder::*CallbackType) (llvm::Value*,llvm::Value*,const llvm::Twine&);
    CallbackType callback;

    InstructionBinaryOp(CallbackType callback_) : callback(callback_) {}

    llvm::Value* operator () (FunctionBuilder& builder, typename TermTagType::Ptr term) const {
      llvm::Value* lhs = builder.build_known_value(term->lhs());
      llvm::Value* rhs = builder.build_known_value(term->rhs());
      return (builder.irbuilder().*callback)(lhs, rhs, "");
    }
  };

  template<typename TermTagType>
  struct IntegerConstantBinaryOp {
    typedef llvm::APInt (llvm::APInt::*CallbackType) (const llvm::APInt&) const;
    CallbackType callback;

    IntegerConstantBinaryOp(CallbackType callback_) : callback(callback_) {}

    llvm::Constant* operator () (ConstantBuilder& builder, typename TermTagType::Ptr term) const {
      unsigned type_bits = integer_type_bits(builder, term->type());
      llvm::APInt lhs = builder.build_constant_integer(term->lhs());
      llvm::APInt rhs = builder.build_constant_integer(term->rhs());
      llvm::APInt result = (lhs.*callback)(rhs);
      const llvm::IntegerType *type_llvm = llvm::IntegerType::get(builder.llvm_context(), type_bits);
      return llvm::ConstantInt::get(type_llvm, result);
    }
  };

  struct CallbackMapValue {
    virtual llvm::Value* build_instruction(FunctionBuilder&, FunctionalTerm*) const = 0;
    virtual llvm::Constant* build_constant(ConstantBuilder&, FunctionalTerm*) const = 0;
    virtual const llvm::Type* build_value_type(ConstantBuilder&, FunctionalTerm*) const = 0;
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

    virtual llvm::Value* build_instruction(FunctionBuilder& builder, FunctionalTerm* term) const {
      return m_insn_cb(builder, cast<TermTagType>(term));
    }

    virtual llvm::Constant* build_constant(ConstantBuilder& builder, FunctionalTerm* term) const {
      return m_const_cb(builder, cast<TermTagType>(term));
    }

    virtual const llvm::Type* build_value_type(ConstantBuilder& builder, FunctionalTerm* term) const {
      return m_type_cb(builder, cast<TermTagType>(term));
    }
  };

  template<typename TermTagType, typename InsnCbType, typename ConstCbType, typename TypeCbType>
  boost::shared_ptr<CallbackMapValue> make_callback_map_value(InsnCbType insn_cb, ConstCbType const_cb, TypeCbType type_cb) {
    return boost::make_shared<CallbackMapValueImpl<TermTagType, InsnCbType, ConstCbType, TypeCbType> >(insn_cb, const_cb, type_cb);
  }

#define CALLBACK(ty,cb_insn,cb_const,cb_type) (ty::operation, make_callback_map_value<ty>(cb_insn, cb_const, cb_type))
#define OP_CALLBACK(ty,cb_insn,cb_const) CALLBACK(ty,cb_insn,cb_const,invalid_type_callback)
#define INTEGER_OP_CALLBACK(ty,insn_op,const_op) OP_CALLBACK(ty, InstructionBinaryOp<ty>(&IRBuilder::insn_op), (IntegerConstantBinaryOp<ty>(&llvm::APInt::const_op)))
#define TYPE_CALLBACK(ty,cb_type) (ty::operation, 

  typedef std::tr1::unordered_map<const char*, boost::shared_ptr<CallbackMapValue> > CallbackMapType;

  const CallbackMapType callbacks =
    boost::assign::map_list_of<const char*, CallbackMapType::mapped_type>
    //TYPE_CALLBACK()
    INTEGER_OP_CALLBACK(IntegerAdd, CreateAdd, operator +)
    INTEGER_OP_CALLBACK(IntegerSubtract, CreateSub, operator -)
    INTEGER_OP_CALLBACK(IntegerMultiply, CreateMul, operator *)
    INTEGER_OP_CALLBACK(IntegerDivide, CreateSDiv, udiv);

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
llvm::Value* Psi::Tvm::LLVM::FunctionBuilder::build_value_known_functional(FunctionalTerm *term) {
}

std::pair<llvm::Constant*, std::size_t> Psi::Tvm::LLVM::GlobalBuilder::build_constant_internal(FunctionalTerm *term) {
}
