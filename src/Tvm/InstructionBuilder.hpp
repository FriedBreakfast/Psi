#ifndef HPP_PSI_TVM_INSTRUCTIONBUILDER
#define HPP_PSI_TVM_INSTRUCTIONBUILDER

#include "Function.hpp"

namespace Psi {
  namespace Tvm {
    /**
     * \brief Utility class for creating instructions.
     * 
     * Use this in preference to the direct <tt>Insn::create</tt>
     * methods since these can more easily be updated in the case
     * that the underlying functioning of an operation changes.
     * 
     * \see FunctionalBuilder: the corresponding class for
     * creating functional terms.
     */
    class InstructionBuilder {
      InstructionInsertPoint m_insert_point;
      
    public:
      InstructionBuilder();
      explicit InstructionBuilder(const InstructionInsertPoint&);
      explicit InstructionBuilder(const ValuePtr<Block>&);
      explicit InstructionBuilder(const ValuePtr<Instruction>&);
      
      /// \name Insert point control
      ///@{
      const InstructionInsertPoint& insert_point() const;
      void set_insert_point(const InstructionInsertPoint&);
      void set_insert_point(const ValuePtr<Block>&);
      void set_insert_point(const ValuePtr<Instruction>&);
      ///@}
      
      /// \name Control flow
      ///@{
      ValuePtr<Instruction> return_(const ValuePtr<>& value, const SourceLocation& location);
      ValuePtr<Instruction> br(const ValuePtr<Block>& target, const SourceLocation& location);
      ValuePtr<Instruction> cond_br(const ValuePtr<>& condition, const ValuePtr<Block>& true_target, const ValuePtr<Block>& false_target, const SourceLocation& location);
      ValuePtr<Instruction> call(const ValuePtr<>& target, const std::vector<ValuePtr<> >& parameters, const SourceLocation& location);
      ///@}
      
      /// \name Memory operations
      ///@{
      ValuePtr<Instruction> alloca_(const ValuePtr<>& type, const ValuePtr<>& count, const ValuePtr<>& alignment, const SourceLocation& location);
      ValuePtr<Instruction> alloca_(const ValuePtr<>& type, const ValuePtr<>& count, const SourceLocation& location);
      ValuePtr<Instruction> alloca_(const ValuePtr<>& type, unsigned count, const SourceLocation& location);
      ValuePtr<Instruction> alloca_(const ValuePtr<>& type, const SourceLocation& location);
      ValuePtr<Instruction> load(const ValuePtr<>& src, const SourceLocation& location);
      ValuePtr<Instruction> store(const ValuePtr<>& value, const ValuePtr<>& dest, const SourceLocation& location);
      ValuePtr<Instruction> memcpy(const ValuePtr<>& dest, const ValuePtr<>& src, const ValuePtr<>& count, const ValuePtr<>& alignment, const SourceLocation& location);
      ValuePtr<Instruction> memcpy(const ValuePtr<>& dest, const ValuePtr<>& src, const ValuePtr<>& count, const SourceLocation& location);
      ValuePtr<Instruction> memcpy(const ValuePtr<>& dest, const ValuePtr<>& src, unsigned count, const SourceLocation& location);
      ///@}
    };
  }
}

#endif
