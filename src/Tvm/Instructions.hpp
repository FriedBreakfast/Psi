#ifndef HPP_PSI_TVM_INSTRUCTIONS
#define HPP_PSI_TVM_INSTRUCTIONS

#include "Function.hpp"

namespace Psi {
  namespace Tvm {
    PSI_TVM_INSTRUCTION_TYPE(Return)
    typedef Empty Data;
    PSI_TVM_INSTRUCTION_PTR_HOOK()
    /// \brief Get the value returned to the caller.
    Term* value() const {return get()->parameter(0);}
    PSI_TVM_INSTRUCTION_PTR_HOOK_END()
    static Ptr create(InstructionInsertPoint, Term*);
    PSI_TVM_INSTRUCTION_TYPE_END(Return)

    PSI_TVM_INSTRUCTION_TYPE(ConditionalBranch)
    typedef Empty Data;
    PSI_TVM_INSTRUCTION_PTR_HOOK()
    /// \brief Get the value used to choose the branch taken.
    Term* condition() const {return get()->parameter(0);}
    /// \brief Get the block jumped to if \c condition is true.
    BlockTerm* true_target() const {return cast<BlockTerm>(get()->parameter(1));}
    /// \brief Get the block jumped to if \c condition is false.
    BlockTerm* false_target() const {return cast<BlockTerm>(get()->parameter(2));}
    PSI_TVM_INSTRUCTION_PTR_HOOK_END()
    static Ptr create(InstructionInsertPoint, Term*, Term*, Term*);
    PSI_TVM_INSTRUCTION_TYPE_END(ConditionalBranch)

    PSI_TVM_INSTRUCTION_TYPE(UnconditionalBranch)
    typedef Empty Data;
    PSI_TVM_INSTRUCTION_PTR_HOOK()
    /// \brief Get the block jumped to.
    BlockTerm* target() const {return cast<BlockTerm>(get()->parameter(0));}
    PSI_TVM_INSTRUCTION_PTR_HOOK_END()
    static Ptr create(InstructionInsertPoint, Term*);
    PSI_TVM_INSTRUCTION_TYPE_END(UnconditionalBranch)

    PSI_TVM_INSTRUCTION_TYPE(FunctionCall)
    typedef Empty Data;
    PSI_TVM_INSTRUCTION_PTR_HOOK()
    /// \brief Get the function being called.
    Term* target() const {return get()->parameter(0);}
    /// \brief Get the value of the <tt>n</tt>th parameter used.
    Term* parameter(std::size_t n) const {return get()->parameter(n+1);}
    PSI_TVM_INSTRUCTION_PTR_HOOK_END()
    static Ptr create(InstructionInsertPoint,Term*,ArrayPtr<Term*const>);
    PSI_TVM_INSTRUCTION_TYPE_END(FunctionCall)

    PSI_TVM_INSTRUCTION_TYPE(Store)
    typedef Empty Data;
    PSI_TVM_INSTRUCTION_PTR_HOOK()
    /// \brief Get the value to be stored
    Term* value() const {return get()->parameter(0);}
    /// \brief Get the memory address which is to be written to
    Term* target() const {return get()->parameter(1);}
    PSI_TVM_INSTRUCTION_PTR_HOOK_END()
    static Ptr create(InstructionInsertPoint,Term*,Term*);
    PSI_TVM_INSTRUCTION_TYPE_END(Store)

    PSI_TVM_INSTRUCTION_TYPE(Load)
    typedef Empty Data;
    PSI_TVM_INSTRUCTION_PTR_HOOK()
    /// \brief Get the pointer being read from
    Term* target() const {return get()->parameter(0);}
    PSI_TVM_INSTRUCTION_PTR_HOOK_END()
    static Ptr create(InstructionInsertPoint,Term*);
    PSI_TVM_INSTRUCTION_TYPE_END(Load)

    PSI_TVM_INSTRUCTION_TYPE(Alloca)
    typedef Empty Data;
    PSI_TVM_INSTRUCTION_PTR_HOOK()
    /// \brief Get the type which storage is allocated for
    Term* stored_type() const {return get()->parameter(0);}
    PSI_TVM_INSTRUCTION_PTR_HOOK_END()
    static Ptr create(InstructionInsertPoint,Term*);
    PSI_TVM_INSTRUCTION_TYPE_END(Alloca)
  }
}

#endif
