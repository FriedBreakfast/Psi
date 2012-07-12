#ifndef HPP_PSI_TVM_INSTRUCTIONS
#define HPP_PSI_TVM_INSTRUCTIONS

#include "Function.hpp"
#include "Aggregate.hpp"

namespace Psi {
  namespace Tvm {
    class Return : public TerminatorInstruction {
      PSI_TVM_INSTRUCTION_DECL(Return)
      
    public:
      Return(const ValuePtr<>& value, const SourceLocation& location);
      
      /// \brief The value returned to the caller.
      ValuePtr<> value;
    };
    
    class ConditionalBranch : public TerminatorInstruction {
      PSI_TVM_INSTRUCTION_DECL(ConditionalBranch)
    public:
      ConditionalBranch(const ValuePtr<>& condition, const ValuePtr<Block>& true_target, const ValuePtr<Block>& false_target, const SourceLocation& location);
      
      /// \brief The value used to choose the branch taken.
      ValuePtr<> condition;
      /// \brief The block jumped to if \c condition is true.
      ValuePtr<Block> true_target;
      /// \brief The block jumped to if \c condition is false.
      ValuePtr<Block> false_target;
    };
    
    class UnconditionalBranch : public TerminatorInstruction {
      PSI_TVM_INSTRUCTION_DECL(UnconditionalBranch)
      
    public:
      UnconditionalBranch(const ValuePtr<Block>& target, const SourceLocation& location);
      
      /// \brief The block jumped to.
      ValuePtr<Block> target;
    };
    
    /**
     * \brief Instruction used to mark unreachable end-of-block.
     * 
     * This is used after an instruction which in general may or may not throw,
     * but is known to in this case.
     */
    class Unreachable : public TerminatorInstruction {
      PSI_TVM_INSTRUCTION_DECL(Unreachable)
      
    public:
      Unreachable(const ValuePtr<Block>& block, const SourceLocation& location);
    };
    
    inline bool TerminatorInstruction::isa_impl(const Value& ptr) {
      const Instruction *insn = dyn_cast<Instruction>(&ptr);
      if (!insn)
        return false;
      
      const char *op = insn->operation_name();
      if ((op == ConditionalBranch::operation)
        || (op == UnconditionalBranch::operation)
        || (op == Unreachable::operation))
        return true;
      
      return false;
    }
    
    class Call : public Instruction {
      PSI_TVM_INSTRUCTION_DECL(Call)
      
    public:
      Call(const ValuePtr<>& target, const std::vector<ValuePtr<> >& parameters, const SourceLocation& location);
      
      /// \brief The function being called.
      ValuePtr<> target;
      /// \brief Parameters applied to the target function.
      std::vector<ValuePtr<> > parameters;
      
      /// \brief Get the function type of the function being called.
      ValuePtr<FunctionType> target_function_type() const {return value_cast<FunctionType>(value_cast<PointerType>(target->type())->target_type());}
    };

    class Store : public Instruction {
      PSI_TVM_INSTRUCTION_DECL(Store)
      
    public:
      Store(const ValuePtr<>& value, const ValuePtr<>& target, const SourceLocation& location);
      
      /// \brief The value to be stored
      ValuePtr<> value;
      /// \brief The memory address which is to be written to
      ValuePtr<> target;
    };
    
    class Load : public Instruction {
      PSI_TVM_INSTRUCTION_DECL(Load)
      
    public:
      Load(const ValuePtr<>& target, const SourceLocation& location);
      
      /// \brief The pointer being read from
      ValuePtr<> target;
    };

    /**
     * \brief Stack allocation instruction.
     * 
     * Strictly speaking since dynamically sized arrays are fully
     * supported the second parameter shouldn't be necessary and I
     * don't recommend it's use during code generation, however
     * it is useful in later passes because it allows enforcing the
     * rule that the first parameter is a simple type and the second
     * a number.
     * 
     * Regarding alignment, alignment requests are only honoured up
     * to the maximum alignment of any type on the system: for
     * alignments which are not known, the compiler may simply replace
     * them with a system-dependent maximum alignment.
     * 
     * As for minumum alignment, the pointer returned will always be
     * aligned to at least the alignment of \c stored_type, regardless
     * of the specified alignment. The value 1 is therefore a safe
     * default when no custom alignment is required.
     */
    class Alloca : public Instruction {
      PSI_TVM_INSTRUCTION_DECL(Alloca)
      
    public:
      Alloca(const ValuePtr<>& element_type, const ValuePtr<>& count, const ValuePtr<>& alignment, const SourceLocation& location);
      
      /// \brief Type which storage is allocated for
      ValuePtr<> element_type;
      /// \brief Number of elements of storage being allocated.
      ValuePtr<> count;
      /// \brief Minimum alignment of the returned pointer.
      ValuePtr<> alignment;
    };
    
    /**
     * \brief memcpy as an instruction.
     * 
     * This exists because during code generation load and store operations
     * on complex types may be replaced by memcpy.
     * 
     * Unlike most operations, which have their destination last, this
     * follows the ordinary memcpy convention and has the destination
     * first and source second.
     */
    class MemCpy : public Instruction {
      PSI_TVM_INSTRUCTION_DECL(MemCpy)
      
    public:
      MemCpy(const ValuePtr<>& dest, const ValuePtr<>& src, const ValuePtr<>& count, const ValuePtr<>& alignment, const SourceLocation& location);
      /// \brief Copy destination
      ValuePtr<> dest;
      /// \brief Copy source
      ValuePtr<> src;
      /// \brief Number of elements to copy
      ValuePtr<> count;
      /// \brief Alignment hint
      ValuePtr<> alignment;
    };
  }
}

#endif
