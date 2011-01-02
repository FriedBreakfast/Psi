#ifndef HPP_PSI_TVM_INSTRUCTIONBUILDER
#define HPP_PSI_TVM_INSTRUCTIONBUILDER

#include "Function.hpp"

namespace Psi {
  namespace Tvm {
    class InstructionBuilder {
      InstructionInsertPoint m_insert_point;
      
    public:
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
      InstructionTerm* alloca_(Term*);
      InstructionTerm* load(Term*);
      InstructionTerm* store(Term*,Term*);
      ///@}
    };
  }
}

#endif
