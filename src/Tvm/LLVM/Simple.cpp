#include "Builder.hpp"

#include "../Aggregate.hpp"
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

  const llvm::Type *metatype_type(ConstantBuilder& builder, Metatype::Ptr) {
    std::vector<const llvm::Type*> elements(2, builder.get_intptr_type());
    return llvm::StructType::get(builder.llvm_context(), elements);
  }

  const llvm::Type* empty_type_type(ConstantBuilder& builder, EmptyType::Ptr) {
    return llvm::StructType::get(builder.llvm_context());
  }

  llvm::Constant* empty_value_const(ConstantBuilder& builder, EmptyValue::Ptr) {
    return llvm::ConstantStruct::get(builder.llvm_context(), 0, 0, false);
  }

  const llvm::Type* pointer_type_type(ConstantBuilder& builder, PointerType::Ptr) {
    return builder.get_pointer_type();
  }

  const llvm::Type* block_type_type(ConstantBuilder& builder, BlockType::Ptr) {
    return llvm::Type::getLabelTy(builder.llvm_context());
  }

  const llvm::Type* boolean_type_type(ConstantBuilder& builder, BooleanType::Ptr) {
    return builder.get_boolean_type();
  }

  llvm::Constant* boolean_value_const(ConstantBuilder& builder, BooleanValue::Ptr term) {
    return term->value() ? 
      llvm::ConstantInt::getTrue(builder.llvm_context())
      : llvm::ConstantInt::getFalse(builder.llvm_context());
  }

  const llvm::Type* integer_type_type(ConstantBuilder& builder, IntegerType::Ptr term) {
    return builder.get_integer_type(term->width());
  }

  llvm::Constant* integer_value_const(ConstantBuilder& builder, IntegerValue::Ptr term) {
    const llvm::IntegerType *llvm_type = builder.get_integer_type(term->type()->width());

    // Need to convert from a byte to a uint64_t representation
    const unsigned char *bytes = term->value().bytes;
    const unsigned num_words = sizeof(IntegerValue::Data::bytes) / 8;
    uint64_t words[num_words];
    for (std::size_t i = 0; i < num_words; ++i) {
      uint64_t word = 0;
      for (std::size_t j = 1; j <= 8; ++j)
	word = (word << 8) + bytes[(i+1)*8 - j];
      words[i] = word;
    }

    llvm::APInt llvm_value(llvm_type->getBitWidth(), num_words, words);
    return llvm::ConstantInt::get(llvm_type, llvm_value);
  }

  const llvm::Type* float_type_type(ConstantBuilder& builder, FloatType::Ptr term) {
    return builder.get_float_type(term->width());
  }

  llvm::Constant* float_value_const(ConstantBuilder& builder, FloatValue::Ptr term) {
    PSI_FAIL("not implemented");
  }

  template<typename TermTagType>
  struct InstructionBinaryOp {
    typedef llvm::Value* (IRBuilder::*CallbackType) (llvm::Value*,llvm::Value*,const llvm::Twine&);
    CallbackType callback;

    InstructionBinaryOp(CallbackType callback_) : callback(callback_) {}

    llvm::Value* operator () (FunctionBuilder& builder, typename TermTagType::Ptr term) const {
      llvm::Value* lhs = builder.build_value_simple(term->lhs());
      llvm::Value* rhs = builder.build_value_simple(term->rhs());
      return (builder.irbuilder().*callback)(lhs, rhs, "");
    }
  };

  template<typename TermTagType>
  struct IntegerConstantBinaryOp {
    typedef llvm::APInt (llvm::APInt::*CallbackType) (const llvm::APInt&) const;
    CallbackType ui_callback, si_callback;

    IntegerConstantBinaryOp(CallbackType ui_callback_, CallbackType si_callback_)
      : ui_callback(ui_callback_), si_callback(si_callback_) {}

    llvm::Constant* operator () (ConstantBuilder& builder, typename TermTagType::Ptr term) const {
      const llvm::IntegerType *llvm_type = builder.get_integer_type(term->type()->width());
      llvm::APInt lhs = builder.build_constant_integer(term->lhs());
      llvm::APInt rhs = builder.build_constant_integer(term->rhs());
      llvm::APInt result;
      if (term->type()->is_signed())
	result = (lhs.*si_callback)(rhs);
      else
	result = (lhs.*ui_callback)(rhs);
      return llvm::ConstantInt::get(llvm_type, result);
    }
  };

  template<typename TermTagType>
  struct IntegerInstructionBinaryOp {
    typedef llvm::Value* (IRBuilder::*CallbackType) (llvm::Value*,llvm::Value*,const llvm::Twine&);
    CallbackType ui_callback, si_callback;

    IntegerInstructionBinaryOp(CallbackType ui_callback_, CallbackType si_callback_)
      : ui_callback(ui_callback_), si_callback(si_callback_) {
    }

    llvm::Value* operator () (FunctionBuilder& builder, typename TermTagType::Ptr term) const {
      llvm::Value* lhs = builder.build_value_simple(term->lhs());
      llvm::Value* rhs = builder.build_value_simple(term->rhs());
      if (term->type()->is_signed())
	return (builder.irbuilder().*si_callback)(lhs, rhs, "");
      else
	return (builder.irbuilder().*ui_callback)(lhs, rhs, "");
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

  /**
   * Adapts a callback which generates an LLVM type to one which
   * generates the equivalent metatype value.
   */
  template<typename TermTagType, typename TypeCbType>
  class TypeAdapter {
    TypeCbType m_type_cb;

  public:
    TypeAdapter(TypeCbType type_cb) : m_type_cb(type_cb) {}

    llvm::Constant* operator () (ConstantBuilder& builder, typename TermTagType::Ptr term) const {
      return metatype_from_type(builder, m_type_cb(builder, term));
    }
  };

  template<typename TermTagType, typename TypeCbType>
  TypeAdapter<TermTagType, TypeCbType> make_type_adapter(TypeCbType type_cb) {
    return TypeAdapter<TermTagType, TypeCbType>(type_cb);
  }

#define CALLBACK(ty,cb_insn,cb_const,cb_type) (ty::operation, make_callback_map_value<ty>((cb_insn), (cb_const), (cb_type)))
#define OP_CALLBACK(ty,cb_insn,cb_const) CALLBACK(ty,(cb_insn),(cb_const),invalid_type_callback)
#define INTEGER_OP_CALLBACK(ty,ui_insn_op,ui_const_op,si_insn_op,si_const_op) OP_CALLBACK(ty, IntegerInstructionBinaryOp<ty>(&IRBuilder::ui_insn_op, &IRBuilder::si_insn_op), (IntegerConstantBinaryOp<ty>(&llvm::APInt::ui_const_op, &llvm::APInt::si_const_op)))
#define TYPE_CALLBACK(ty,cb_type) CALLBACK(ty, make_type_adapter<ty>((cb_type)), make_type_adapter<ty>((cb_type)), (cb_type))
#define VALUE_CALLBACK(ty,cb_const) CALLBACK(ty,(cb_const),(cb_const),invalid_type_callback)

  typedef std::tr1::unordered_map<const char*, boost::shared_ptr<CallbackMapValue> > CallbackMapType;

  const CallbackMapType callbacks =
    boost::assign::map_list_of<const char*, CallbackMapType::mapped_type>
    TYPE_CALLBACK(Metatype, metatype_type)
    TYPE_CALLBACK(EmptyType, empty_type_type)
    TYPE_CALLBACK(BlockType, block_type_type)
    TYPE_CALLBACK(PointerType, pointer_type_type)
    TYPE_CALLBACK(BooleanType, boolean_type_type)
    TYPE_CALLBACK(IntegerType, integer_type_type)
    TYPE_CALLBACK(FloatType, float_type_type)
    VALUE_CALLBACK(EmptyValue, empty_value_const)
    VALUE_CALLBACK(BooleanValue, boolean_value_const)
    VALUE_CALLBACK(IntegerValue, integer_value_const)
    VALUE_CALLBACK(FloatValue, float_value_const)
    INTEGER_OP_CALLBACK(IntegerAdd, CreateAdd, operator +, CreateAdd, operator +)
    INTEGER_OP_CALLBACK(IntegerSubtract, CreateSub, operator -, CreateSub, operator -)
    INTEGER_OP_CALLBACK(IntegerMultiply, CreateMul, operator *, CreateMul, operator *)
    INTEGER_OP_CALLBACK(IntegerDivide, CreateUDiv, udiv, CreateSDiv, sdiv);

  const CallbackMapValue& get_callback(const char *s) {
    CallbackMapType::const_iterator it = callbacks.find(s);
    if (it == callbacks.end()) {
      std::string msg = "unknown operation type in LLVM backend: ";
      msg += s;
      throw BuildError(msg);
    }
    return *it->second;
  }
}

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      /**
       * Build a value for a functional operation whose result always
       * (i.e. regardless of the arguments) has a known type. In
       * practise, this means numeric operations.
       */
      llvm::Value* FunctionBuilder::build_value_functional_simple(FunctionalTerm *term) {
	return get_callback(term->operation()).build_instruction(*this, term);
      }

      llvm::Constant* GlobalBuilder::build_constant_internal_simple(FunctionalTerm *term) {
	return get_callback(term->operation()).build_constant(*this, term);
      }

      /**
       * Attempt to build a type representing a term. If no LLVM
       * equivalent type exists, return NULL.
       */
      const llvm::Type* ConstantBuilder::build_type_internal_simple(FunctionalTerm *term) {
	return get_callback(term->operation()).build_value_type(*this, term);
      }
    }
  }
}
