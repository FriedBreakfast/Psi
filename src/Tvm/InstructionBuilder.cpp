#include "InstructionBuilder.hpp"
#include "Instructions.hpp"
#include "FunctionalBuilder.hpp"

namespace Psi {
  namespace Tvm {
    /**
     * \brief Default constructor.
     * 
     * Before this object can be used, the insertion point must be
     * set using the various set_insert_point methods.
     */
    InstructionBuilder::InstructionBuilder() {
    }
    
    /**
     * \brief Constructor which sets the insertion point.
     * 
     * \param ip Insertion point to initialize this builder with.
     */
    InstructionBuilder::InstructionBuilder(const InstructionInsertPoint& ip)
    : m_insert_point(ip) {
    }
    
    /**
     * Set the insert point.
     */
    void InstructionBuilder::set_insert_point(const InstructionInsertPoint& ip) {
      m_insert_point = ip;
    }
    
    /**
     * Set the insert point to insert at the end of a block.
     * 
     * \param insert_at_end Block to append instructions to.
     */
    void InstructionBuilder::set_insert_point(BlockTerm *insert_at_end) {
      m_insert_point = InstructionInsertPoint(insert_at_end);
    }
    
    /**
     * Set the insert point to insert before an instruction.
     * 
     * \param insert_before Point to create new instructions before.
     */
    void InstructionBuilder::set_insert_point(InstructionTerm *insert_before) {
      m_insert_point = InstructionInsertPoint(insert_before);
    }

    /**
     * \brief Create a return instruction.
     * 
     * \param value Value to return from the function.
     */
    InstructionTerm* InstructionBuilder::return_(Term* value) {
      return Return::create(m_insert_point, value);
    }
    
    /**
     * \brief Jump to a block.
     * 
     * \param target Block to jump to. This must be a block value
     * not an indirect pointer so that control flow can be tracked.
     */
    InstructionTerm* InstructionBuilder::br(Term* target) {
      return UnconditionalBranch::create(m_insert_point, target);
    }
    
    /**
     * \brief Conditionally jump to one of two blocks.
     * 
     * \param condition Condition to select jump target.
     * 
     * \param if_true Block to jump to if \c condition is true.
     * 
     * \param if_false Block to jump to if \c condition is false.
     */
    InstructionTerm* InstructionBuilder::cond_br(Term* condition, Term* if_true, Term* if_false) {
      return ConditionalBranch::create(m_insert_point, condition, if_true, if_false);
    }
    
    /**
     * \brief Call a function.
     * 
     * \param target Function to call
     * 
     * \param parameters Parameters to the function.
     */
    InstructionTerm* InstructionBuilder::call(Term* target, ArrayPtr<Term*const> parameters) {
      return FunctionCall::create(m_insert_point, target, parameters);
    }
    
    /**
     * \brief Allocate memory for a variable on the stack.
     * 
     * This version of the function allocates space for one
     * unit of the given type.
     * 
     * \param type Type to allocate space for.
     * 
     * \param count Number of elements of type \c type to
     * allocate space for.
     * 
     * \param alignment Minimum alignment of the returned
     * pointer. This is only honored up to a system-dependent
     * maximum alignment - see Alloca class documentation for
     * details.
     */
    InstructionTerm* InstructionBuilder::alloca_(Term* type, Term *count, Term* alignment) {
      return Alloca::create(m_insert_point, type, count, alignment);
    }

    /**
     * \brief Allocate memory for a variable on the stack.
     * 
     * This version of the function allocates space for one
     * unit of the given type.
     * 
     * \param type Type to allocate space for.
     * 
     * \param count Number of elements of type \c type to
     * allocate space for.
     */
    InstructionTerm* InstructionBuilder::alloca_(Term* type, Term *count) {
      return alloca_(type, count, FunctionalBuilder::size_value(type->context(), 1));
    }
    
    /// \copydoc InstructionBuilder::alloca_(Term*,Term*)
    InstructionTerm* InstructionBuilder::alloca_(Term *type, unsigned count) {
      return alloca_(type, FunctionalBuilder::size_value(type->context(), count));
    }

    /**
     * \brief Allocate memory for a variable on the stack.
     * 
     * This version of the function allocates space for one
     * unit of the given type.
     * 
     * \param type Type to allocate space for.
     */
    InstructionTerm* InstructionBuilder::alloca_(Term* type) {
      Term *one = FunctionalBuilder::size_value(type->context(), 1);
      return alloca_(type, one, one);
    }
    
    /**
     * \brief Load a value from memory.
     * 
     * \param ptr Pointer to value.
     */
    InstructionTerm* InstructionBuilder::load(Term* ptr) {
      return Load::create(m_insert_point, ptr);
    }
    
    /**
     * \brief Store a value to memory.
     * 
     * \param value Value to store.
     * 
     * \param ptr Pointer to store \c value to.
     */
    InstructionTerm* InstructionBuilder::store(Term* value, Term* ptr) {
      return Store::create(m_insert_point, value, ptr);
    }
    
    /**
     * \brief Create a memcpy instruction.
     * 
     * \param dest Copy destination.
     * 
     * \param src Copy source.
     * 
     * \param count Number of bytes to copy.
     * 
     * \param alignment Alignment hint.
     */
    InstructionTerm* InstructionBuilder::memcpy(Term *dest, Term *src, Term *count, Term *alignment) {
      return MemCpy::create(m_insert_point, dest, src, count, alignment);
    }
    
    /**
     * \brief Create a memcpy instruction.
     * 
     * This sets the alignment hint to 1, i.e. unaligned.
     * 
     * \param dest Copy destination.
     * 
     * \param src Copy source.
     * 
     * \param count Number of bytes to copy.
     */
    InstructionTerm* InstructionBuilder::memcpy(Term *dest, Term *src, Term *count) {
      Term *one = FunctionalBuilder::size_value(dest->context(), 1);
      return memcpy(dest, src, count, one);
    }

    /// \copydoc InstructionBuilder::memcpy(Term*,Term*,Term*)
    InstructionTerm* InstructionBuilder::memcpy(Term *dest, Term *src, unsigned count) {
      Term *count_term = FunctionalBuilder::size_value(dest->context(), count);
      return memcpy(dest, src, count_term);
    }
  }
}
