#ifndef HPP_PSI_TVM_INSTRUCTIONS
#define HPP_PSI_TVM_INSTRUCTIONS

#include "Function.hpp"
#include "Aggregate.hpp"

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
    /// \brief Get the function type of the function being called.
    FunctionTypeTerm* target_function_type() const {return cast<FunctionTypeTerm>(cast<PointerType>(target()->type())->target_type());}
    PSI_TVM_INSTRUCTION_PTR_HOOK_END()
    static Ptr create(InstructionInsertPoint,Term*,ArrayPtr<Term*const>);
    PSI_TVM_INSTRUCTION_TYPE_END(FunctionCall)

    PSI_TVM_INSTRUCTION_TYPE(FunctionInvoke)
    typedef Empty Data;
    PSI_TVM_INSTRUCTION_PTR_HOOK()
    /// \brief Get the normal continuation edge
    BlockTerm *normal_edge() const {return cast<BlockTerm>(get()->parameter(0));}
    /// \brief Get the unwind edge
    BlockTerm *unwind_edge() const {return cast<BlockTerm>(get()->parameter(1));}
    /// \brief Get the function being called.
    Term* target() const {return get()->parameter(2);}
    /// \brief Get the value of the <tt>n</tt>th parameter used.
    Term* parameter(std::size_t n) const {return get()->parameter(n+3);}
    /// \brief Get the function type of the function being called.
    FunctionTypeTerm* target_function_type() const {return cast<FunctionTypeTerm>(cast<PointerType>(target()->type())->target_type());}
    PSI_TVM_INSTRUCTION_PTR_HOOK_END()
    static Ptr create(InstructionInsertPoint,BlockTerm*,BlockTerm*,Term*,ArrayPtr<Term*const>);
    PSI_TVM_INSTRUCTION_TYPE_END(FunctionInvoke)

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
    PSI_TVM_INSTRUCTION_TYPE(Alloca)
    typedef Empty Data;
    PSI_TVM_INSTRUCTION_PTR_HOOK()
    /// \brief Get the type which storage is allocated for
    Term* stored_type() const {return get()->parameter(0);}
    /// \brief Get the number of elements of storage being allocated.
    Term* count() const {return get()->parameter(1);}
    /// \brief Get the minimum alignment of the returned pointer.
    Term* alignment() const {return get()->parameter(2);}
    PSI_TVM_INSTRUCTION_PTR_HOOK_END()
    static Ptr create(InstructionInsertPoint,Term*,Term*,Term*);
    PSI_TVM_INSTRUCTION_TYPE_END(Alloca)
    
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
    PSI_TVM_INSTRUCTION_TYPE(MemCpy)
    typedef Empty Data;
    PSI_TVM_INSTRUCTION_PTR_HOOK()
    /// \brief Copy destination
    Term* dest() const {return get()->parameter(0);}
    /// \brief Copy source
    Term* src() const {return get()->parameter(1);}
    /// \brief Number of elements to copy
    Term* count() const {return get()->parameter(2);}
    /// \brief Alignment hint
    Term* alignment() const {return get()->parameter(3);}
    PSI_TVM_INSTRUCTION_PTR_HOOK_END()
    static Ptr create(InstructionInsertPoint,Term*,Term*,Term*,Term*);
    PSI_TVM_INSTRUCTION_TYPE_END(MemCpy)
  }
}

#endif
