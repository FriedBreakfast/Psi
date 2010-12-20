#include "Instructions.hpp"
#include "OperationMap.hpp"
#include "Type.hpp"

#include <boost/assign.hpp>

using namespace Psi;
using namespace Psi::Tvm;

namespace Psi {
  namespace Tvm {
    Term* Return::type(FunctionTerm *function, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("return instruction takes one argument");

      Term *ret_val = parameters[0];
      if (ret_val->type() != function->result_type())
        throw TvmUserError("return instruction argument has incorrect type");

      if (ret_val->phantom())
        throw TvmUserError("cannot return a phantom value");

      return NULL;
    }

    void Return::jump_targets(Ptr, std::vector<BlockTerm*>&) {
    }

    Term* ConditionalBranch::type(FunctionTerm* function, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 3)
        throw TvmUserError("branch instruction takes three arguments: cond, trueTarget, falseTarget");

      Term *cond = parameters[0];

      if (cond->type() != BooleanType::get(function->context()))
        throw TvmUserError("first parameter to branch instruction must be of boolean type");

      Term* true_target(parameters[1]);
      Term* false_target(parameters[2]);
      if ((true_target->term_type() != term_block) || (false_target->term_type() != term_block))
        throw TvmUserError("second and third parameters to branch instruction must be blocks");

      PSI_ASSERT(!true_target->phantom() && !false_target->phantom());

      if (cond->phantom())
        throw TvmUserError("cannot conditionally branch on a phantom value");

      return NULL;
    }

    void ConditionalBranch::jump_targets(ConditionalBranch::Ptr insn, std::vector<BlockTerm*>& targets) {
      targets.push_back(insn->true_target());
      targets.push_back(insn->false_target());
    }

    Term* UnconditionalBranch::type(FunctionTerm*, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("unconditional branch instruction takes one argument - the branch target");

      Term* target(parameters[0]);
      if (target->term_type() != term_block)
        throw TvmUserError("second parameter to branch instruction must be blocks");

      PSI_ASSERT(!target->phantom());

      return NULL;
    }

    void UnconditionalBranch::jump_targets(UnconditionalBranch::Ptr insn, std::vector<BlockTerm*>& targets) {
      targets.push_back(insn->target());
    }

    Term* FunctionCall::type(FunctionTerm*, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() < 1)
        throw TvmUserError("function call instruction must have at least one parameter: the function being called");

      Term *target = parameters[0];
      if (target->phantom())
        throw TvmUserError("function call target cannot have phantom value");

      PointerType::Ptr target_ptr_type = dyn_cast<PointerType>(target->type());
      if (!target_ptr_type)
        throw TvmUserError("function call target is not a pointer type");

      FunctionTypeTerm* target_function_type =
        dyn_cast<FunctionTypeTerm>(target_ptr_type->target_type());
      if (!target_function_type)
        throw TvmUserError("function call target is not a function pointer");

      std::size_t n_parameters = target_function_type->n_parameters();
      if (parameters.size() != n_parameters + 1)
        throw TvmUserError("wrong number of arguments to function");

      for (std::size_t i = 0; i < n_parameters; ++i) {
        if ((i >= target_function_type->n_phantom_parameters()) && parameters[i]->phantom())
          throw TvmUserError("cannot pass phantom value to non-phantom function parameter");

        Term* expected_type = target_function_type->parameter_type_after(parameters.slice(1, i+1));
        if (parameters[i+1]->type() != expected_type)
          throw TvmUserError("function argument has the wrong type");
      }

      Term* result_type = target_function_type->result_type_after(parameters.slice(1, 1+n_parameters));
      if (result_type->phantom())
        throw TvmUserError("cannot create function call which leads to unknown result type");

      return result_type;
    }

    void FunctionCall::jump_targets(Ptr, std::vector<BlockTerm*>&) {
    }

    Term* Store::type(FunctionTerm* function, const Store::Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 2)
        throw TvmUserError("store instruction takes two parameters");

      Term *value = parameters[0];
      Term *target = parameters[1];

      if (target->phantom() || value->phantom())
        throw TvmUserError("value and target for store instruction cannot have phantom values");

      PointerType::Ptr target_ptr_type = dyn_cast<PointerType>(target->type());
      if (!target_ptr_type)
        throw TvmUserError("store target is not a pointer type");

      if (target_ptr_type->target_type() != value->type())
        throw TvmUserError("store target type is not a pointer to the type of value");

      return EmptyType::get(function->context());
    }

    void Store::jump_targets(Ptr, std::vector<BlockTerm*>&) {
    }

    Term* Load::type(FunctionTerm*, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("load instruction takes one parameter");

      Term *target = parameters[0];

      if (target->phantom())
        throw TvmUserError("value and target for load instruction cannot have phantom values");

      PointerType::Ptr target_ptr_type = dyn_cast<PointerType>(target->type());
      if (!target_ptr_type)
        throw TvmUserError("load target is not a pointer type");

      if (target_ptr_type->target_type()->phantom())
        throw TvmUserError("load target has phantom type");

      return target_ptr_type->target_type();
    }

    void Load::jump_targets(Ptr, std::vector<BlockTerm*>&) {
    }

    Term* Alloca::type(FunctionTerm*, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("alloca instruction takes one parameter");

      if (!parameters[0]->is_type())
        throw TvmUserError("parameter to alloca is not a type");

      if (parameters[0]->phantom())
        throw TvmUserError("parameter to alloca cannot be phantom");

      return PointerType::get(parameters[0]);
    }

    void Alloca::jump_targets(Ptr, std::vector<BlockTerm*>&) {
    }
  }
}
