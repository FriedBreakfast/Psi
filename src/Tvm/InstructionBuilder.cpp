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
     * \brief Constructor which sets the insertion point.
     * 
     * \param insert_at_end Block to insert instructions at the end of
     */
    InstructionBuilder::InstructionBuilder(const ValuePtr<Block>& insert_at_end)
    : m_insert_point(insert_at_end) {
    }

    /**
     * \brief Constructor which sets the insertion point.
     * 
     * \param insert_before Point to create new instructions before.
     */
    InstructionBuilder::InstructionBuilder(const ValuePtr<Instruction>& insert_before)
    : m_insert_point(insert_before) {
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
    void InstructionBuilder::set_insert_point(const ValuePtr<Block>& insert_at_end) {
      m_insert_point = InstructionInsertPoint(insert_at_end);
    }
    
    /**
     * Set the insert point to insert before an instruction.
     * 
     * \param insert_before Point to create new instructions before.
     */
    void InstructionBuilder::set_insert_point(const ValuePtr<Instruction>& insert_before) {
      m_insert_point = InstructionInsertPoint(insert_before);
    }

    /**
     * \brief Create a return instruction.
     * 
     * \param value Value to return from the function.
     */
    ValuePtr<Instruction> InstructionBuilder::return_(const ValuePtr<>& value, const SourceLocation& location) {
      ValuePtr<Instruction> insn(::new Return(value, location));
      m_insert_point.insert(insn);
      return insn;
    }
    
    /**
     * \brief Create a return instruction returning void.
     * 
     * "Void" in this context means a struct with no members (this is the same as LLVM).
     */
    ValuePtr<Instruction> InstructionBuilder::return_void(const SourceLocation& location) {
      return return_(FunctionalBuilder::empty_value(block()->context(), location), location);
    }
    
    /**
     * \brief Jump to a block.
     * 
     * \param target Block to jump to. This must be a block value
     * not an indirect pointer so that control flow can be tracked.
     */
    ValuePtr<Instruction> InstructionBuilder::br(const ValuePtr<Block>& target, const SourceLocation& location) {
      ValuePtr<Instruction> insn(::new UnconditionalBranch(target, location));
      m_insert_point.insert(insn);
      return insn;
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
    ValuePtr<Instruction> InstructionBuilder::cond_br(const ValuePtr<>& condition, const ValuePtr<Block>& if_true, const ValuePtr<Block>& if_false, const SourceLocation& location) {
      ValuePtr<Instruction> insn(::new ConditionalBranch(condition, if_true, if_false, location));
      m_insert_point.insert(insn);
      return insn;
    }
    
    /**
     * \brief Call a function.
     * 
     * \param target Function to call
     * 
     * \param parameters Parameters to the function.
     */
    ValuePtr<Instruction> InstructionBuilder::call(const ValuePtr<>& target, const std::vector<ValuePtr<> >& parameters, const SourceLocation& location) {
      ValuePtr<Instruction> insn(::new Call(target, parameters, location));
      m_insert_point.insert(insn);
      return insn;
    }

    /// \brief Call a function with no parameters
    ValuePtr<Instruction> InstructionBuilder::call0(const ValuePtr<>& target, const SourceLocation& location) {
      std::vector<ValuePtr<> > parameters;
      return call(target, parameters, location);
    }
    
    /// \brief Call a function with one parameter
    ValuePtr<Instruction> InstructionBuilder::call1(const ValuePtr<>& target, const ValuePtr<>& p1, const SourceLocation& location) {
      std::vector<ValuePtr<> > parameters;
      parameters.push_back(p1);
      return call(target, parameters, location);
    }
    
    /// \brief Call a function with two parameters
    ValuePtr<Instruction> InstructionBuilder::call2(const ValuePtr<>& target, const ValuePtr<>& p1, const ValuePtr<>& p2, const SourceLocation& location) {
      std::vector<ValuePtr<> > parameters;
      parameters.push_back(p1);
      parameters.push_back(p2);
      return call(target, parameters, location);
    }
    
    /// \brief Call a function with three parameters
    ValuePtr<Instruction> InstructionBuilder::call3(const ValuePtr<>& target, const ValuePtr<>& p1, const ValuePtr<>& p2, const ValuePtr<>& p3, const SourceLocation& location) {
      std::vector<ValuePtr<> > parameters;
      parameters.push_back(p1);
      parameters.push_back(p2);
      parameters.push_back(p3);
      return call(target, parameters, location);
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
    ValuePtr<Instruction> InstructionBuilder::alloca_(const ValuePtr<>& type, const ValuePtr<>& count, const ValuePtr<>& alignment, const SourceLocation& location) {
      ValuePtr<Instruction> insn(::new Alloca(type, count, alignment, location));
      m_insert_point.insert(insn);
      return insn;
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
    ValuePtr<Instruction> InstructionBuilder::alloca_(const ValuePtr<>& type, const ValuePtr<>& count, const SourceLocation& location) {
      return alloca_(type, count, ValuePtr<>(), location);
    }
    
    /// \copydoc InstructionBuilder::alloca_(const ValuePtr<>&,const ValuePtr<>&,const SourceLocation&)
    ValuePtr<Instruction> InstructionBuilder::alloca_(const ValuePtr<>& type, unsigned count, const SourceLocation& location) {
      return alloca_(type, FunctionalBuilder::size_value(type->context(), count, location), ValuePtr<>(), location);
    }

    /**
     * \brief Allocate memory for a variable on the stack.
     * 
     * This version of the function allocates space for one
     * unit of the given type.
     * 
     * \param type Type to allocate space for.
     */
    ValuePtr<Instruction> InstructionBuilder::alloca_(const ValuePtr<>& type, const SourceLocation& location) {
      return alloca_(type, ValuePtr<>(), ValuePtr<>(), location);
    }
    
    /**
     * \brief Load a value from memory.
     * 
     * \param ptr Pointer to value.
     */
    ValuePtr<Instruction> InstructionBuilder::load(const ValuePtr<>& ptr, const SourceLocation& location) {
      ValuePtr<Instruction> insn(::new Load(ptr, location));
      m_insert_point.insert(insn);
      return insn;
    }
    
    /**
     * \brief Store a value to memory.
     * 
     * \param value Value to store.
     * 
     * \param ptr Pointer to store \c value to.
     */
    ValuePtr<Instruction> InstructionBuilder::store(const ValuePtr<>& value, const ValuePtr<>& ptr, const SourceLocation& location) {
      ValuePtr<Instruction> insn(::new Store(value, ptr, location));
      m_insert_point.insert(insn);
      return insn;
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
    ValuePtr<Instruction> InstructionBuilder::memcpy(const ValuePtr<>& dest, const ValuePtr<>& src, const ValuePtr<>& count, const ValuePtr<>& alignment, const SourceLocation& location) {
      ValuePtr<Instruction> insn(::new MemCpy(dest, src, count, alignment, location));
      m_insert_point.insert(insn);
      return insn;
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
    ValuePtr<Instruction> InstructionBuilder::memcpy(const ValuePtr<>& dest, const ValuePtr<>& src, const ValuePtr<>& count, const SourceLocation& location) {
      return memcpy(dest, src, count, ValuePtr<>(), location);
    }

    /// \copydoc InstructionBuilder::memcpy(const ValuePtr<>&,const ValuePtr<>&,const ValuePtr<>&)
    ValuePtr<Instruction> InstructionBuilder::memcpy(const ValuePtr<>& dest, const ValuePtr<>& src, unsigned count, const SourceLocation& location) {
      return memcpy(dest, src, FunctionalBuilder::size_value(dest->context(), count, location), ValuePtr<>(), location);
    }
    
    /**
     * \brief Whether the current block has been terminated.
     * 
     * If it has been, then any instruction insertion at the end of the block will fail.
     * Instructions before the end of the block and PHI nodes can still be generated.
     */
    bool InstructionBuilder::is_terminated() {
      return block()->terminated();
    }
    
    /// \brief Create a PHI node at the start of the current block.
    ValuePtr<Phi> InstructionBuilder::phi(const ValuePtr<>& type, const SourceLocation& location) {
      return m_insert_point.block()->insert_phi(type, location);
    }

    /**
     * \brief Create a new block whose dominator is the current insertion block.
     */
    ValuePtr<Block> InstructionBuilder::new_block(const SourceLocation& location) {
      const ValuePtr<Block>& dominator = m_insert_point.block();
      return dominator->function()->new_block(location, dominator);
    }
  }
}
