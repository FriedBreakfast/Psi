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
    
      /// \brief Get the current insertion point.
      const InstructionInsertPoint& insert_point() const {return m_insert_point;}
      /// \brief Get the block containing the current insertion block.
      const ValuePtr<Block>& block() const {return insert_point().block();}

      void set_insert_point(const InstructionInsertPoint&);
      void set_insert_point(const ValuePtr<Block>&);
      void set_insert_point(const ValuePtr<Instruction>&);
      ///@}
      
      /// \name Control flow
      ///@{
      ValuePtr<Instruction> return_(const ValuePtr<>& value, const SourceLocation& location);
      ValuePtr<Instruction> return_void(const SourceLocation& location);
      ValuePtr<Instruction> br(const ValuePtr<Block>& target, const SourceLocation& location);
      ValuePtr<Instruction> cond_br(const ValuePtr<>& condition, const ValuePtr<Block>& true_target, const ValuePtr<Block>& false_target, const SourceLocation& location);
      ValuePtr<Instruction> call(const ValuePtr<>& target, const std::vector<ValuePtr<> >& parameters, const SourceLocation& location);

      ValuePtr<Instruction> call0(const ValuePtr<>& target, const SourceLocation& location);
      ValuePtr<Instruction> call1(const ValuePtr<>& target, const ValuePtr<>& p1, const SourceLocation& location);
      ValuePtr<Instruction> call2(const ValuePtr<>& target, const ValuePtr<>& p1, const ValuePtr<>& p2, const SourceLocation& location);
      ValuePtr<Instruction> call3(const ValuePtr<>& target, const ValuePtr<>& p1, const ValuePtr<>& p2, const ValuePtr<>& p3, const SourceLocation& location);
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
      ValuePtr<Instruction> memzero(const ValuePtr<>& dest, const ValuePtr<>& count, const ValuePtr<>& alignment, const SourceLocation& location);
      ValuePtr<Instruction> memzero(const ValuePtr<>& dest, const ValuePtr<>& count, const SourceLocation& location);
      ///@}
      
      ValuePtr<Instruction> unreachable(const SourceLocation& location);
      ValuePtr<Instruction> solidify(const ValuePtr<>& value, const SourceLocation& location);
      
      bool is_terminated();
      ValuePtr<Phi> phi(const ValuePtr<>& type, const SourceLocation& location);
      ValuePtr<Block> new_block(const SourceLocation& location);
    };
  }
}

#endif
