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
  llvm::Instruction* build_return(FunctionBuilder& builder, Return::Ptr insn) {
    return builder.irbuilder().CreateRet(builder.build_value(insn->value()));
  }

  llvm::Instruction* build_conditional_branch(FunctionBuilder& builder, ConditionalBranch::Ptr insn) {
    llvm::Value *cond = builder.build_value(insn->condition());
    llvm::BasicBlock *true_target = llvm::cast<llvm::BasicBlock>(builder.build_value(insn->true_target()));
    llvm::BasicBlock *false_target = llvm::cast<llvm::BasicBlock>(builder.build_value(insn->false_target()));
    return builder.irbuilder().CreateCondBr(cond, true_target, false_target);
  }

  llvm::Instruction* build_unconditional_branch(FunctionBuilder& builder, UnconditionalBranch::Ptr insn) {
    llvm::BasicBlock *target = llvm::cast<llvm::BasicBlock>(builder.build_value(insn->target()));
    return builder.irbuilder().CreateBr(target);
  }

  llvm::Instruction* build_function_call(FunctionBuilder& builder, FunctionCall::Ptr insn) {
    FunctionTypeTerm* function_type = cast<FunctionTypeTerm>
      (cast<PointerType>(insn->target()->type())->target_type());
      
    const llvm::Type *llvm_function_type = builder.build_type(function_type)->getPointerTo();

    llvm::Value *target = builder.build_value(insn->target());

    std::size_t n_phantom = function_type->n_phantom_parameters();
    std::size_t n_passed_parameters = function_type->n_parameters() - n_phantom;

    llvm::SmallVector<llvm::Value*, 4> parameters(n_passed_parameters);
    for (std::size_t i = 0; i < n_passed_parameters; ++i)
      parameters[i] = builder.build_value(insn->parameter(i + n_phantom));
    
    llvm::Value *cast_target = builder.irbuilder().CreatePointerCast(target, llvm_function_type);
    return builder.irbuilder().CreateCall(cast_target, parameters.begin(), parameters.end());
  }

  llvm::Instruction* build_store(FunctionBuilder& builder, Store::Ptr term) {
    llvm::Value *target = builder.build_value(term->target());
    llvm::Value *value = builder.build_value(term->value());
    return builder.irbuilder().CreateStore(value, target);
  }

  llvm::Instruction* build_load(FunctionBuilder& builder, Load::Ptr term) {
    llvm::Value *target = builder.build_value(term->target());
    return builder.irbuilder().CreateLoad(target);
  }

  llvm::Instruction* build_alloca(FunctionBuilder& builder, Alloca::Ptr term) {
    const llvm::Type *stored_type = builder.build_type(term->stored_type());
    llvm::Value *count = builder.build_value(term->count());
    llvm::Value *alignment = builder.build_value(term->alignment());
    llvm::AllocaInst *inst = builder.irbuilder().CreateAlloca(stored_type, count);
    
    if (llvm::ConstantInt *const_alignment = llvm::dyn_cast<llvm::ConstantInt>(alignment)) {
      inst->setAlignment(const_alignment->getValue().getZExtValue());
    } else {
      inst->setAlignment(builder.unknown_alloca_align());
    }
    
    return inst;
  }

  struct CallbackMapValue {
    virtual llvm::Instruction* build_instruction(FunctionBuilder&, InstructionTerm*) const = 0;
  };

  template<typename TermTagType, typename InsnCbType>
  class CallbackMapValueImpl : public CallbackMapValue {
    InsnCbType m_insn_cb;

  public:
    CallbackMapValueImpl(InsnCbType insn_cb)
      : m_insn_cb(insn_cb) {
    }

    virtual llvm::Instruction* build_instruction(FunctionBuilder& builder, InstructionTerm* term) const {
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
       * Build a value for an instruction operation.
       *
       * This handles complex operations on aggregate types; numeric
       * operations are forwarded to build_value_instruction_simple.
       */
      llvm::Instruction* FunctionBuilder::build_value_instruction(InstructionTerm *term) {
	return get_callback(term->operation()).build_instruction(*this, term);
      }
    }
  }
}
