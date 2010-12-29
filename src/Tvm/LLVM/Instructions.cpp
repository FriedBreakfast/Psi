#include "Builder.hpp"

#include "../Aggregate.hpp"
#include "../Instructions.hpp"

#include <boost/assign.hpp>
#include <boost/make_shared.hpp>

#include <llvm/Function.h>

using namespace Psi;
using namespace Psi::Tvm;
using namespace Psi::Tvm::LLVM;

namespace {
  BuiltValue* build_return(FunctionBuilder& builder, Return::Ptr insn) {
    builder.target_fixes()->function_return(builder, builder.function()->function_type(), builder.llvm_function(), insn->value());
    return builder.empty_value();
  }

  BuiltValue* build_conditional_branch(FunctionBuilder& builder, ConditionalBranch::Ptr insn) {
    llvm::Value *cond = builder.build_value_simple(insn->condition());
    llvm::BasicBlock *true_target = llvm::cast<llvm::BasicBlock>(builder.build_value_simple(insn->true_target()));
    llvm::BasicBlock *false_target = llvm::cast<llvm::BasicBlock>(builder.build_value_simple(insn->false_target()));

    builder.irbuilder().CreateCondBr(cond, true_target, false_target);

    return builder.empty_value();
  }

  BuiltValue* build_unconditional_branch(FunctionBuilder& builder, UnconditionalBranch::Ptr insn) {
    llvm::BasicBlock *target = llvm::cast<llvm::BasicBlock>(builder.build_value_simple(insn->target()));

    builder.irbuilder().CreateBr(target);

    return builder.empty_value();
  }

  BuiltValue* build_function_call(FunctionBuilder& builder, FunctionCall::Ptr insn) {
    FunctionTypeTerm* function_type = cast<FunctionTypeTerm>
      (cast<PointerType>(insn->target()->type())->target_type());

    llvm::Value *target = builder.build_value_simple(insn->target());

    std::size_t n_phantom = function_type->n_phantom_parameters();
    std::size_t n_passed_parameters = function_type->n_parameters() - n_phantom;

    llvm::SmallVector<Term*,4> parameters(n_passed_parameters);
    for (std::size_t i = 0; i < n_passed_parameters; ++i)
      parameters[i] = insn->parameter(i + n_phantom);

    return builder.target_fixes()->function_call(builder, target, function_type, insn);
  }

  BuiltValue* build_store(FunctionBuilder& builder, Store::Ptr term) {
    llvm::Value *target = builder.build_value_simple(term->target());
    BuiltValue *value = builder.build_value(term->value());
    builder.store_value(value, target);
    return builder.empty_value();
  }

  BuiltValue* build_load(FunctionBuilder& builder, Load::Ptr term) {
    llvm::Value *target = builder.build_value_simple(term->target());
    return builder.load_value(term->type(), target);
  }

  BuiltValue* build_alloca(FunctionBuilder& builder, Alloca::Ptr term) {
    llvm::Value *size_align = builder.build_value_simple(term->stored_type());
    llvm::Value *size = metatype_value_size(builder, size_align);
    llvm::Value *align = metatype_value_align(builder, size_align);
    llvm::AllocaInst *inst = builder.irbuilder().CreateAlloca(builder.get_byte_type(), size);
    if (llvm::ConstantInt *ci = llvm::dyn_cast<llvm::ConstantInt>(align)) {
      inst->setAlignment(ci->getValue().getZExtValue());
    } else {
      inst->setAlignment(builder.unknown_alloca_align());
    }

    return builder.new_function_value_simple(term->type(), inst);
  }

  struct CallbackMapValue {
    virtual BuiltValue* build_instruction(FunctionBuilder&, InstructionTerm*) const = 0;
  };

  template<typename TermTagType, typename InsnCbType>
  class CallbackMapValueImpl : public CallbackMapValue {
    InsnCbType m_insn_cb;

  public:
    CallbackMapValueImpl(InsnCbType insn_cb)
      : m_insn_cb(insn_cb) {
    }

    virtual BuiltValue* build_instruction(FunctionBuilder& builder, InstructionTerm* term) const {
      return m_insn_cb(builder, cast<TermTagType>(term));
    }
  };

  template<typename TermTagType, typename InsnCbType>
  boost::shared_ptr<CallbackMapValue> make_callback_map_value(InsnCbType insn_cb) {
    return boost::make_shared<CallbackMapValueImpl<TermTagType, InsnCbType> >(insn_cb);
  }

#define CALLBACK(ty,cb) (ty::operation, make_callback_map_value<ty>(cb))

  typedef std::tr1::unordered_map<const char*, boost::shared_ptr<CallbackMapValue> > CallbackMapType;

  const CallbackMapType callbacks =
    boost::assign::map_list_of<const char*, CallbackMapType::mapped_type>
    CALLBACK(Return, build_return)
    CALLBACK(ConditionalBranch, build_conditional_branch)
    CALLBACK(UnconditionalBranch, build_unconditional_branch)
    CALLBACK(FunctionCall, build_function_call)
    CALLBACK(Alloca, build_alloca);

  const CallbackMapValue& get_callback(const char *s) {
    CallbackMapType::const_iterator it = callbacks.find(s);
    if (it == callbacks.end()) {
      std::string msg = "unknown instruction type in LLVM backend: ";
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
       * Build a value for an instruction whose result always
       * (i.e. regardless of the arguments) has a known type. In
       * practise, this means numeric operations.
       */
      BuiltValue* FunctionBuilder::build_value_instruction_simple(InstructionTerm *term) {
	return get_callback(term->operation()).build_instruction(*this, term);
      }
    }
  }
}
