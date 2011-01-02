#include "InstructionBuilder.hpp"
#include "Instructions.hpp"

namespace Psi {
  namespace Tvm {
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
     * \param type Type to allocate space for.
     */
    InstructionTerm* InstructionBuilder::alloca_(Term* type) {
      return Alloca::create(m_insert_point, type);
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
  }
}
