#include "Aggregate.hpp"
#include "Instructions.hpp"
#include "FunctionalBuilder.hpp"

#include <boost/assign/list_of.hpp>

namespace Psi {
  namespace Tvm {
    Return::Return(const ValuePtr<>& value_, const SourceLocation& location)
    : TerminatorInstruction(value_->context(), operation, location),
    value(value_) {
    }
    
    void Return::type_check() {
      require_available(value);

      if (value->type() != function()->result_type())
        throw TvmUserError("return instruction argument has incorrect type");
    }
    
    std::vector<ValuePtr<Block> > Return::successors() {
      return std::vector<ValuePtr<Block> >();
    }
    
    template<typename V>
    void Return::visit(V& v) {
      visit_base<TerminatorInstruction>(v);
      v("value", &Return::value);
    }

    PSI_TVM_INSTRUCTION_IMPL(Return, TerminatorInstruction, return);

    ConditionalBranch::ConditionalBranch(const ValuePtr<>& condition_, const ValuePtr<Block>& true_target_, const ValuePtr<Block>& false_target_, const SourceLocation& location)
    : TerminatorInstruction(condition_->context(), operation, location),
    condition(condition_),
    true_target(true_target_),
    false_target(false_target_) {
    }
    
    /**
     * \todo Need to check that targets are dominated by an appropriate block to jump to.
     */
    void ConditionalBranch::type_check() {
      require_available(condition);
      require_available(true_target);
      require_available(false_target);

      if (condition->type() != FunctionalBuilder::bool_type(context(), location()))
        throw TvmUserError("first parameter to branch instruction must be of boolean type");

      if (!true_target || !false_target)
        throw TvmUserError("jump targets may not be null");
      
      if ((true_target->function() != function()) || (false_target->function() != function()))
        throw TvmUserError("jump target must be in the same function");
    }

    std::vector<ValuePtr<Block> > ConditionalBranch::successors() {
      std::vector<ValuePtr<Block> > r;
      r.push_back(true_target);
      r.push_back(false_target);
      return r;
    }

    template<typename V>
    void ConditionalBranch::visit(V& v) {
      visit_base<TerminatorInstruction>(v);
      v("condition", &ConditionalBranch::condition)
      ("true_target", &ConditionalBranch::true_target)
      ("false_target", &ConditionalBranch::false_target);
    }
    
    PSI_TVM_INSTRUCTION_IMPL(ConditionalBranch, TerminatorInstruction, cond_br);

    UnconditionalBranch::UnconditionalBranch(const ValuePtr<Block>& target_, const SourceLocation& location)
    : TerminatorInstruction(target_->context(), operation, location),
    target(target_) {
    }
    
    /**
     * \todo Need to check that target is dominated by an appropriate block to jump to.
     */
    void UnconditionalBranch::type_check() {
      require_available(target);
      
      if (!target)
        throw TvmUserError("jump targets may not be null");
      
      if (target->function() != function())
        throw TvmUserError("jump target must be in the same function");
    }

    std::vector<ValuePtr<Block> > UnconditionalBranch::successors() {
      std::vector<ValuePtr<Block> > r;
      r.push_back(target);
      return r;
    }
    
    template<typename V>
    void UnconditionalBranch::visit(V& v) {
      visit_base<TerminatorInstruction>(v);
      v("target", &UnconditionalBranch::target);
    }

    PSI_TVM_INSTRUCTION_IMPL(UnconditionalBranch, TerminatorInstruction, br);
    
    Unreachable::Unreachable(Context& context, const SourceLocation& location)
    : TerminatorInstruction(context, operation, location) {
    }
    
    void Unreachable::type_check() {
    }

    template<typename V>
    void Unreachable::visit(V& v) {
      visit_base<TerminatorInstruction>(v);
    }

    std::vector<ValuePtr<Block> > Unreachable::successors() {
      return std::vector<ValuePtr<Block> >();
    }
    
    PSI_TVM_INSTRUCTION_IMPL(Unreachable, TerminatorInstruction, unreachable);

    namespace {
      ValuePtr<> call_type(const ValuePtr<>& target, const std::vector<ValuePtr<> >& parameters) {
        ValuePtr<PointerType> ptr_type = dyn_cast<PointerType>(target->type());
        if (!ptr_type)
          throw TvmUserError("Function call target is not a pointer");
        
        ValuePtr<FunctionType> target_type = dyn_cast<FunctionType>(ptr_type->target_type());
        if (!target_type)
          throw TvmUserError("Function call target does not have function type");
        
        return target_type->result_type_after(parameters);
      }
    }
    
    Call::Call(const ValuePtr<>& target_, const std::vector<ValuePtr<> >& parameters_, const SourceLocation& location)
    : Instruction(call_type(target_, parameters_), operation, location),
    target(target_),
    parameters(parameters_) {
    }

    void Call::type_check() {
      require_available(target);

      if (call_type(target, parameters) != type())
        throw TvmUserError("Type of function call has changed since instruction was created");

      for (std::size_t ii = target_function_type()->n_phantom(), ie = parameters.size(); ii < ie; ++ii)
        require_available(parameters[ii]);
    }

    template<typename V>
    void Call::visit(V& v) {
      visit_base<Instruction>(v);
      v("target", &Call::target)
      ("parameters", &Call::parameters);
    }
    
    PSI_TVM_INSTRUCTION_IMPL(Call, Instruction, call);

    namespace {
      /**
       * Get the pointed-to type from a pointer.
       */
      ValuePtr<> pointer_target_type(const ValuePtr<>& ptr) {
        ValuePtr<PointerType> target_ptr_type = dyn_cast<PointerType>(ptr->type());
        if (!target_ptr_type)
          throw TvmUserError("store target is not a pointer type");
        return target_ptr_type->target_type();
      }
    }

    Store::Store(const ValuePtr<>& value_, const ValuePtr<>& target_, const SourceLocation& location)
    : Instruction(FunctionalBuilder::empty_type(value_->context(), location), operation, location),
    value(value_),
    target(target_) {
    }
    
    void Store::type_check() {
      require_available(target);
      require_available(value);
      
      if (pointer_target_type(target) != value->type())
        throw TvmUserError("store target type is not a pointer to the type of value");
    }

    template<typename V>
    void Store::visit(V& v) {
      visit_base<Instruction>(v);
      v("value", &Store::value)
      ("target", &Store::target);
    }

    PSI_TVM_INSTRUCTION_IMPL(Store, Instruction, store);

    Load::Load(const ValuePtr<>& target_, const SourceLocation& location)
    : Instruction(pointer_target_type(target_), operation, location),
    target(target_) {
    }

    void Load::type_check() {
      require_available(target);
      
      if (type() != pointer_target_type(target))
        throw TvmUserError("load target type has changed since instruction creation");
    }

    template<typename V>
    void Load::visit(V& v) {
      visit_base<Instruction>(v);
      v("target", &Load::target);
    }

    PSI_TVM_INSTRUCTION_IMPL(Load, Instruction, load);

    Alloca::Alloca(const ValuePtr<>& element_type_, const ValuePtr<>& count_, const ValuePtr<>& alignment_, const SourceLocation& location)
    : Instruction(FunctionalBuilder::pointer_type(element_type_, location), operation, location),
    element_type(element_type_),
    count(count_),
    alignment(alignment_) {
    }

    void Alloca::type_check() {
      require_available(element_type);
      
      if (!element_type->is_type())
        throw TvmUserError("first parameter to alloca is not a type");
      
      ValuePtr<> size_type = FunctionalBuilder::size_type(context(), location());

      if (count) {
        require_available(count);
        if (count->type() != size_type)
          throw TvmUserError("second parameter to alloca is not a uintptr");
      }

      if (alignment) {
        require_available(alignment);
        if (alignment->type() != size_type)
          throw TvmUserError("third parameter to alloca is not a uintptr");
      }
    }
    
    template<typename V>
    void Alloca::visit(V& v) {
      visit_base<Instruction>(v);
      v("element_type", &Alloca::element_type)
      ("count", &Alloca::count)
      ("alignment", &Alloca::alignment);
    }

    PSI_TVM_INSTRUCTION_IMPL(Alloca, Instruction, alloca);
    
    MemCpy::MemCpy(const ValuePtr<>& dest_, const ValuePtr<>& src_, const ValuePtr<>& count_, const ValuePtr<>& alignment_, const SourceLocation& location)
    : Instruction(FunctionalBuilder::empty_type(dest_->context(), location), operation, location),
    dest(dest_),
    src(src_),
    count(count_),
    alignment(alignment_) { 
    }

    void MemCpy::type_check() {
      require_available(dest);
      require_available(src);
      require_available(count);

      if (!isa<PointerType>(dest->type()))
        throw TvmUserError("first parameter to memcpy instruction is not a pointer");
      
      if (dest->type() != src->type())
        throw TvmUserError("first two parameters to memcpy instruction must have the same type");
      
      ValuePtr<> size_type = FunctionalBuilder::size_type(context(), location());
      if (count->type() != size_type)
        throw TvmUserError("size parameter to memcpy instruction must be uintptr");
      
      if (alignment) {
        require_available(alignment);
        if (alignment->type() != size_type)
          throw TvmUserError("alignment parameter to memcpy instruction must be uintptr");
      }
    }
    
    template<typename V>
    void MemCpy::visit(V& v) {
      visit_base<Instruction>(v);
      v("dest", &MemCpy::dest)
      ("src", &MemCpy::src)
      ("count", &MemCpy::count)
      ("alignment", &MemCpy::alignment);
    }

    PSI_TVM_INSTRUCTION_IMPL(MemCpy, Instruction, memcpy);
    
    MemZero::MemZero(const ValuePtr<>& dest_, const ValuePtr<>& count_, const ValuePtr<>& alignment_, const SourceLocation& location)
    : Instruction(FunctionalBuilder::empty_type(dest_->context(), location), operation, location),
    dest(dest_),
    count(count_),
    alignment(alignment_) { 
    }

    void MemZero::type_check() {
      require_available(dest);
      require_available(count);
      
      if (!isa<PointerType>(dest->type()))
        throw TvmUserError("first parameter to memzero instruction is not a pointer");
      
      if (!isa<ByteType>(value_cast<PointerType>(dest->type())->target_type()))
        throw TvmUserError("first parameter to memzero instruction is not a byte pointer");
      
      ValuePtr<> size_type = FunctionalBuilder::size_type(context(), location());
      if (count->type() != size_type)
        throw TvmUserError("size parameter to memzero instruction must be uintptr");
      
      if (alignment) {
        require_available(alignment);
        if (alignment->type() != size_type)
          throw TvmUserError("alignment parameter to memzero instruction must be uintptr");
      }
    }
    
    template<typename V>
    void MemZero::visit(V& v) {
      visit_base<Instruction>(v);
      v("dest", &MemZero::dest)
      ("count", &MemZero::count)
      ("alignment", &MemZero::alignment);
    }

    PSI_TVM_INSTRUCTION_IMPL(MemZero, Instruction, memzero);
    
    Solidify::Solidify(const ValuePtr<>& value_, const SourceLocation& location)
    : Instruction(FunctionalBuilder::empty_type(value_->context(), location), operation, location),
    value(value_) {
    }
    
    void Solidify::type_check() {
      if (!isa<ConstantType>(value->type()))
        throw TvmUserError("Parameter to solidify is not const");
    }
    
    template<typename V>
    void Solidify::visit(V& v) {
      visit_base<Instruction>(v);
      v("value", &Solidify::value);
    }
    
    PSI_TVM_INSTRUCTION_IMPL(Solidify, Instruction, solidify);
  }
}
