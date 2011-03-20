#include "Aggregate.hpp"
#include "Instructions.hpp"
#include "FunctionalBuilder.hpp"

#include <boost/assign/list_of.hpp>

namespace Psi {
  namespace Tvm {
    const char Return::operation[] = "return";
    const char ConditionalBranch::operation[] = "cond_br";
    const char UnconditionalBranch::operation[] = "br";
    const char FunctionCall::operation[] = "call";
    const char Store::operation[] = "store";
    const char Load::operation[] = "load";
    const char Eager::operation[] = "eager";
    const char Alloca::operation[] = "alloca";
    const char MemCpy::operation[] = "memcpy";
    const char SetLandingPad::operation[] = "set_landing_pad";

    InstructionTypeResult Return::type(FunctionTerm *function, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("return instruction takes one argument");

      Term *ret_val = parameters[0];
      if (ret_val->type() != function->result_type())
        throw TvmUserError("return instruction argument has incorrect type");

      if (ret_val->phantom())
        throw TvmUserError("cannot return a phantom value");

      return InstructionTypeResult(FunctionalBuilder::empty_type(function->context()), std::vector<BlockTerm*>());
    }

    /**
     * Create a return instruction.
     * 
     * \param value Value to return.
     */
    Return::Ptr Return::create(InstructionInsertPoint insert_point, Term *value) {
      Term *parameters[] = {value};
      return insert_point.create<Return>(ArrayPtr<Term*const>(parameters,1));
    }

    InstructionTypeResult ConditionalBranch::type(FunctionTerm* function, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 3)
        throw TvmUserError("branch instruction takes three arguments: cond, trueTarget, falseTarget");

      Term *cond = parameters[0];

      if (cond->type() != BooleanType::get(function->context()))
        throw TvmUserError("first parameter to branch instruction must be of boolean type");

      Term* true_target(parameters[1]);
      Term* false_target(parameters[2]);
      if (!isa<BlockTerm>(true_target) || !isa<BlockTerm>(false_target))
        throw TvmUserError("second and third parameters to branch instruction must be blocks");

      PSI_ASSERT(!true_target->phantom() && !false_target->phantom());

      if (cond->phantom())
        throw TvmUserError("cannot conditionally branch on a phantom value");

      return InstructionTypeResult(FunctionalBuilder::empty_type(function->context()),
                                   boost::assign::list_of(cast<BlockTerm>(true_target))(cast<BlockTerm>(false_target)));
    }

    /**
     * Create a cond_br instruction.
     * 
     * \param condition Used to select which branch is taken.
     * 
     * \param true_target Branch taken if \c condition is true.
     * 
     * \param false_target Branch taken if \c condition is false.
     */
    ConditionalBranch::Ptr ConditionalBranch::create(InstructionInsertPoint insert_point, Term *condition, Term *true_target, Term *false_target) {
      Term *parameters[] = {condition, true_target, false_target};
      return insert_point.create<ConditionalBranch>(ArrayPtr<Term*const>(parameters,3));
    }

    InstructionTypeResult UnconditionalBranch::type(FunctionTerm *function, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("unconditional branch instruction takes one argument - the branch target");

      Term* target(parameters[0]);
      if (target->term_type() != term_block)
        throw TvmUserError("second parameter to branch instruction must be blocks");

      PSI_ASSERT(!target->phantom());

      return InstructionTypeResult(FunctionalBuilder::empty_type(function->context()),
                                   boost::assign::list_of(cast<BlockTerm>(target)));
    }

    /**
     * Create a br instruction.
     * 
     * \param target Block to jump to.
     */
    UnconditionalBranch::Ptr UnconditionalBranch::create(InstructionInsertPoint insert_point, Term *target) {
      Term *parameters[] = {target};
      return insert_point.create<UnconditionalBranch>(ArrayPtr<Term*const>(parameters,1));
    }

    InstructionTypeResult FunctionCall::type(FunctionTerm*, const Data&, ArrayPtr<Term*const> parameters) {
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
      
      if (target_function_type->source())
        throw TvmUserError("cannot call a function which has an abstract type");

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

    /**
     * Create a call instruction.
     * 
     * \param target Function to call.
     * 
     * \param parameters Parameters to pass.
     */
    FunctionCall::Ptr FunctionCall::create(InstructionInsertPoint insert_point, Term *target, ArrayPtr<Term*const> parameters) {
      ScopedArray<Term*> insn_params(parameters.size() + 1);
      insn_params[0] = target;
      for (std::size_t i = 0, e = parameters.size(); i != e; ++i)
        insn_params[i+1] = parameters[i];
      return insert_point.create<FunctionCall>(insn_params);
    }
    
    InstructionTypeResult Store::type(FunctionTerm* function, const Store::Data&, ArrayPtr<Term*const> parameters) {
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

      return FunctionalBuilder::empty_type(function->context());
    }

    /**
     * Create a store instruction.
     * 
     * \param value Value to store.
     * 
     * \param ptr Location to store \c value to.
     */
    Store::Ptr Store::create(InstructionInsertPoint insert_point, Term *value, Term *ptr) {
      Term *parameters[] = {value, ptr};
      return insert_point.create<Store>(ArrayPtr<Term*const>(parameters, 2));
    }

    InstructionTypeResult Load::type(FunctionTerm*, const Data&, ArrayPtr<Term*const> parameters) {
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

    /**
     * Create a load instruction.
     * 
     * \param ptr Location to load a value from.
     */
    Load::Ptr Load::create(InstructionInsertPoint insert_point, Term *ptr) {
      Term *parameters[] = {ptr};
      return insert_point.create<Load>(ArrayPtr<Term*const>(parameters, 1));
    }

    InstructionTypeResult Eager::type(FunctionTerm *function, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("eager instruction takes one parameter");

      Term *target = parameters[0];

      if (target->phantom())
        throw TvmUserError("parameter for eager instruction cannot be phantom");
      
      return FunctionalBuilder::empty_type(function->context());
    }

    /**
     * Create an eager instruction.
     * 
     * \param ptr Location to load a value from.
     */
    Eager::Ptr Eager::create(InstructionInsertPoint insert_point, Term *ptr) {
      Term *parameters[] = {ptr};
      return insert_point.create<Eager>(ArrayPtr<Term*const>(parameters, 1));
    }

    InstructionTypeResult Alloca::type(FunctionTerm *function, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 3)
        throw TvmUserError("alloca instruction takes three parameters");

      if (!parameters[0]->is_type())
        throw TvmUserError("first parameter to alloca is not a type");
      
      Term *size_type = FunctionalBuilder::size_type(function->context());

      if (parameters[1]->type() != size_type)
        throw TvmUserError("second parameter to alloca is not a uintptr");

      if (parameters[2]->type() != size_type)
        throw TvmUserError("third parameter to alloca is not a uintptr");
      
      if (parameters[0]->phantom() || parameters[1]->phantom() || parameters[2]->phantom())
        throw TvmUserError("parameter to alloca cannot be phantom");
      
      return FunctionalBuilder::pointer_type(parameters[0]);
    }

    /**
     * Create an alloca instruction.
     * 
     * \param type Type to allocate memory for on the stack.
     * 
     * \param count Number of elements of type \c type to allocate.
     * 
     * \param alignment Minimum alignment of the returned pointer.
     * This is not always honored - see the Alloca class documentation
     * for details.
     */
    Alloca::Ptr Alloca::create(InstructionInsertPoint insert_point, Term *type, Term *count, Term* alignment) {
      Term *parameters[] = {type, count, alignment};
      return insert_point.create<Alloca>(ArrayPtr<Term*const>(parameters, 3));
    }
    
    InstructionTypeResult MemCpy::type(FunctionTerm *function, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 4)
        throw TvmUserError("memcpy instruction takes four parameters");
      
      if (!isa<PointerType>(parameters[0]->type()))
        throw TvmUserError("first parameter to memcpy instruction is not a pointer");
      
      if (parameters[0]->type() != parameters[1]->type())
        throw TvmUserError("first two parameters to memcpy instruction must have the same type");
      
      Term *size_type = FunctionalBuilder::size_type(function->context());
      if ((parameters[2]->type() != size_type) || (parameters[3]->type() != size_type))
        throw TvmUserError("third and fourth parameters to memcpy instruction must be uintptr");
      
      return FunctionalBuilder::empty_type(function->context());
    }
    
    /**
     * Create a memcpy instruction.
     * 
     * \param dest Copy destination.
     * 
     * \param src Copy source.
     * 
     * \param count Number of bytes to copy.
     * 
     * \param alignment Alignment hint. Both source and destination pointers
     * must be aligned to this boundary.
     */
    MemCpy::Ptr MemCpy::create(InstructionInsertPoint insert_point, Term *dest, Term *src, Term *count, Term *alignment) {
      Term *parameters[] = {dest, src, count, alignment};
      return insert_point.create<MemCpy>(ArrayPtr<Term*const>(parameters, 4));
    }
    
    InstructionTypeResult SetLandingPad::type(FunctionTerm *function, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("landing_pad instruction takes one parameter");
      
      if (parameters[0]) {
        BlockTerm *block = dyn_cast<BlockTerm>(parameters[0]);
        if (!block)
          throw TvmUserError("parameter to landing_pad is not a block");
        
        if (!block->landing_pad())
          throw TvmUserError("parameter to landing_pad is a block but it is not marked as a landing pad");
      }
      
      return FunctionalBuilder::empty_type(function->context());
    }

    /**
     * Create a landing_pad instruction.
     * 
     * \param landing_pad Landing pad to use from this point in the function onward.
     */
    SetLandingPad::Ptr SetLandingPad::create(InstructionInsertPoint insert_point, BlockTerm *landing_pad) {
      return insert_point.create<SetLandingPad>(StaticArray<Term*,1>(landing_pad));
    }
  }
}
