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
      
      /// \name Insert point control
      ///@{
      void set_insert_point(const InstructionInsertPoint&);
      void set_insert_point(BlockTerm *insert_at_end);
      void set_insert_point(InstructionTerm *insert_before);
      ///@}
      
      /// \name Control flow
      ///@{
      InstructionTerm* return_(Term*);
      InstructionTerm* br(Term*);
      InstructionTerm* cond_br(Term*,Term*,Term*);
      InstructionTerm* call(Term*,ArrayPtr<Term*const>);
      ///@}
      
      /// \name Memory operations
      ///@{
      InstructionTerm* alloca_(Term*,Term*,Term*);
      InstructionTerm* alloca_(Term*,Term*);
      InstructionTerm* alloca_(Term*,unsigned);
      InstructionTerm* alloca_(Term*);
      InstructionTerm* load(Term*);
      InstructionTerm* store(Term*,Term*);
      InstructionTerm* memcpy(Term*,Term*,Term*,Term*);
      InstructionTerm* memcpy(Term*,Term*,Term*);
      InstructionTerm* memcpy(Term*,Term*,unsigned);
      ///@}
    };
  }
}

#endif
