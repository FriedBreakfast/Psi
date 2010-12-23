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
  BuiltValue build_return(FunctionBuilder& builder, Return::Ptr insn) {
    if (builder.function()->function_type()->calling_convention() == cconv_tvm) {
      llvm::Value *return_area = &builder.llvm_function()->getArgumentList().front();
      builder.create_store(return_area, insn->value());
      builder.irbuilder().CreateRetVoid();
    } else {
      llvm::Value *return_value = builder.build_known_value(insn->value());
      builder.irbuilder().CreateRet(return_value);
    }

    return value_known(empty_value(builder));
  }

  BuiltValue build_conditional_branch(FunctionBuilder& builder, ConditionalBranch::Ptr insn) {
    llvm::Value *cond = builder.build_known_value(insn->condition());
    llvm::BasicBlock *true_target = llvm::cast<llvm::BasicBlock>(builder.build_known_value(insn->true_target()));
    llvm::BasicBlock *false_target = llvm::cast<llvm::BasicBlock>(builder.build_known_value(insn->false_target()));

    builder.irbuilder().CreateCondBr(cond, true_target, false_target);

    return value_known(empty_value(builder));
  }

  BuiltValue build_unconditional_branch(FunctionBuilder& builder, UnconditionalBranch::Ptr insn) {
    llvm::BasicBlock *target = llvm::cast<llvm::BasicBlock>(builder.build_known_value(insn->target()));

    builder.irbuilder().CreateBr(target);

    return value_known(empty_value(builder));
  }

  BuiltValue build_function_call(FunctionBuilder& builder, FunctionCall::Ptr insn) {
    IRBuilder& irbuilder = builder.irbuilder();

    FunctionTypeTerm* function_type = cast<FunctionTypeTerm>
      (cast<PointerType>(insn->target()->type())->target_type());

    llvm::Value *target = builder.build_known_value(insn->target());
    const llvm::Type* result_type = builder.build_type(insn->type());

    std::size_t n_parameters = function_type->n_parameters();
    std::size_t n_phantom = function_type->n_phantom_parameters();
    CallingConvention calling_convention = function_type->calling_convention();

    llvm::Value *stack_backup = NULL;
    llvm::Value *result_area;

    std::vector<llvm::Value*> parameters;
    if (calling_convention == cconv_tvm) {
      // allocate an area of memory to receive the result value
      if (result_type) {
        // stack pointer is saved here but not for unknown types
        // because memory for unknown types must survive their
        // scope.
        stack_backup = irbuilder.CreateCall(intrinsic_stacksave(builder.llvm_module()));
        result_area = irbuilder.CreateAlloca(result_type);
        parameters.push_back(builder.cast_pointer_to_generic(result_area));
      } else {
        result_area = builder.create_alloca_for(insn->type());
        parameters.push_back(result_area);
      }
    }

    const llvm::FunctionType* llvm_function_type =
      llvm::cast<llvm::FunctionType>(builder.build_type(function_type));
    if (!llvm_function_type)
      throw BuildError("cannot call function of unknown type");

    for (std::size_t i = n_phantom; i < n_parameters; ++i) {
      BuiltValue param = builder.build_value(insn->parameter(i));

      if (calling_convention == cconv_tvm) {
        if (param.known()) {
          if (!stack_backup)
            stack_backup = irbuilder.CreateCall(intrinsic_stacksave(builder.llvm_module()));

          llvm::Value *ptr = irbuilder.CreateAlloca(param.known_value()->getType());
          irbuilder.CreateStore(param.known_value(), ptr);
          parameters.push_back(builder.cast_pointer_to_generic(ptr));
        } else {
          PSI_ASSERT(param.unknown());
          parameters.push_back(param.unknown_value());
        }
      } else {
        if (!param.known())
          throw BuildError("Function parameter types must be known for non-TVM calling conventions");
        llvm::Value *val = param.known_value();
        if (val->getType()->isPointerTy())
          val = builder.cast_pointer_from_generic(val, llvm_function_type->getParamType(i));
        parameters.push_back(val);
      }
    }

    llvm::Value *llvm_target = builder.cast_pointer_from_generic(target, llvm_function_type->getPointerTo());
    llvm::Value *result = irbuilder.CreateCall(llvm_target, parameters.begin(), parameters.end());

    if ((calling_convention == cconv_tvm)  && result_type)
      result = irbuilder.CreateLoad(result_area);

    if (stack_backup)
      irbuilder.CreateCall(intrinsic_stackrestore(builder.llvm_module()), stack_backup);

    if (result_type) {
      return value_known(result);
    } else {
      return value_unknown(result_area);
    }
  }

  BuiltValue build_store(FunctionBuilder& builder, Store::Ptr term) {
    llvm::Value *target = builder.build_known_value(term->target());
    builder.create_store(target, term->value());
    return value_known(empty_value(builder));
  }

  BuiltValue build_load(FunctionBuilder& builder, Load::Ptr term) {
    llvm::Value *target = builder.build_known_value(term->target());

    Term *target_deref_type = cast<PointerType>(term->target()->type())->target_type();
    if (const llvm::Type *llvm_target_deref_type = builder.build_type(target_deref_type)) {
      llvm::Value *ptr = builder.cast_pointer_from_generic(target, llvm_target_deref_type->getPointerTo());
      return value_known(builder.irbuilder().CreateLoad(ptr));
    } else {
      llvm::Value *stack_ptr = builder.create_alloca_for(target_deref_type);
      builder.create_store_unknown(stack_ptr, target, target_deref_type);
      return value_unknown(stack_ptr);
    }
  }

  BuiltValue build_alloca(FunctionBuilder& builder, Alloca::Ptr term) {
    return value_known(builder.create_alloca_for(term->stored_type()));
  }

  struct CallbackMapValue {
    virtual BuiltValue build_instruction(FunctionBuilder&, InstructionTerm*) const = 0;
  };

  template<typename TermTagType, typename InsnCbType>
  class CallbackMapValueImpl : public CallbackMapValue {
    InsnCbType m_insn_cb;

  public:
    CallbackMapValueImpl(InsnCbType insn_cb)
      : m_insn_cb(insn_cb) {
    }

    virtual BuiltValue build_instruction(FunctionBuilder& builder, InstructionTerm* term) const {
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
    CALLBACK(FunctionCall, build_function_call);

  CallbackMapValue& get_callback(const char *s) {
    CallbackMapType::const_iterator it = callbacks.find(s);
    if (it == callbacks.end())
      throw BuildError("unknown instruction type");
    return *it->second;
  }
}

namespace Psi {
  namespace Tvm {
  }
}
