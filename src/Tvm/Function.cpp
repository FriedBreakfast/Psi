#include "Aggregate.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "FunctionalBuilder.hpp"
#include "Instructions.hpp"
#include "Utility.hpp"

#include <boost/next_prior.hpp>
#include <boost/unordered_set.hpp>

namespace Psi {
  namespace Tvm {
    ValuePtr<FunctionTypeResolvedParameter> FunctionTypeResolvedParameter::get(const ValuePtr<>& type, unsigned depth, unsigned index, const SourceLocation& location) {
      return type->context().get_functional(FunctionTypeResolvedParameter(type, depth, index, location));
    }
    
    FunctionTypeResolvedParameter::FunctionTypeResolvedParameter(const ValuePtr<>& type, unsigned depth, unsigned index, const SourceLocation& location)
    : FunctionalValue(type->context(), location),
    m_parameter_type(type),
    m_depth(depth),
    m_index(index) {
    }
    
    template<typename V>
    void FunctionTypeResolvedParameter::visit(V& v) {
      visit_base<FunctionalValue>(v);
      v("parameter_type", &FunctionTypeResolvedParameter::m_parameter_type)
      ("depth", &FunctionTypeResolvedParameter::m_depth)
      ("index", &FunctionTypeResolvedParameter::m_index);
    }
    
    ValuePtr<> FunctionTypeResolvedParameter::check_type() const {
      if (!m_parameter_type->is_type())
        throw TvmUserError("First argument to function_type_resolved_parameter is not a type");
      return m_parameter_type;
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(FunctionTypeResolvedParameter, SimpleOp, function_type_resolved_parameter)

    FunctionType::FunctionType(CallingConvention calling_convention, const ValuePtr<>& result_type,
                               const std::vector<ValuePtr<> >& parameter_types, unsigned n_phantom, const SourceLocation& location)
    : HashableValue(result_type->context(), term_function_type, location),
    m_calling_convention(calling_convention),
    m_parameter_types(parameter_types),
    m_n_phantom(n_phantom),
    m_result_type(result_type) {
    }

    class Context::FunctionTypeResolverRewriter : public RewriteCallback {
      std::vector<ValuePtr<FunctionTypeParameter> > m_parameters;
      std::size_t m_depth;

    public:
      FunctionTypeResolverRewriter(Context& context, const std::vector<ValuePtr<FunctionTypeParameter> >& parameters)
      : RewriteCallback(context), m_parameters(parameters), m_depth(0) {
      }

      virtual ValuePtr<> rewrite(const ValuePtr<>& term) {
        if (ValuePtr<FunctionTypeParameter> parameter = dyn_cast<FunctionTypeParameter>(term)) {
          ValuePtr<> type = rewrite(term->type());
          for (unsigned i = 0, e = m_parameters.size(); i != e; ++i) {
            if (m_parameters[i] == term)
              return FunctionTypeResolvedParameter::get(type, m_depth, i, m_parameters[i]->location());
          }
          if (type != term->type())
            throw TvmUserError("type of unresolved function parameter cannot depend on type of resolved function parameter");
          return term;
        } else if (ValuePtr<FunctionType> function_type = dyn_cast<FunctionType>(term)) {
          ++m_depth;
          ValuePtr<> result = function_type->rewrite(*this);
          --m_depth;
          return result;
        } else if (ValuePtr<HashableValue> hashable = dyn_cast<HashableValue>(term)) {
          return hashable->rewrite(*this);
        } else {
          return term;
        }
      }
    };

    /**
     * Get a function type term.
     *
     * Phantom parameters which exist to allow functions to take
     * parameters of general types without having to know the details
     * of those types; similar to \c forall quantification in
     * Haskell. This is also how callback functions are passed since a
     * user-specified parameter to the callback will usually have an
     * unknown type, but this must be the same type as passed to the
     * callback.
     *
     * The distinction between phantom and regular parameters is not
     * just about types, since for example in order to access an array
     * the type of array elements must be known, and therefore must be
     * part of \c parameters.
     *
     * \param calling_convention Calling convention of this function.
     *
     * \param result_type The result type of the function. This may
     * depend on \c parameters and \c phantom_parameters.
     *
     * \param phantom_parameters Phantom parameters - these do not
     * actually cause any data to be passed at machine level.
     *
     * \param parameters Ordinary function parameters.
     */
    ValuePtr<FunctionType> Context::get_function_type(CallingConvention calling_convention,
                                                      const ValuePtr<>& result_type,
                                                      const std::vector<ValuePtr<FunctionTypeParameter> >& parameters,
                                                      unsigned n_phantom,
                                                      const SourceLocation& location) {
      PSI_ASSERT(n_phantom <= parameters.size());

      std::vector<ValuePtr<FunctionTypeParameter> > previous_parameters;
      std::vector<ValuePtr<> > resolved_parameter_types;
      for (unsigned ii = 0, ie = parameters.size(); ii != ie; ++ii) {
        resolved_parameter_types.push_back(FunctionTypeResolverRewriter(*this, previous_parameters).rewrite(parameters[ii]->type()));
        previous_parameters.push_back(parameters[ii]);
      }

      ValuePtr<> resolved_result_type = FunctionTypeResolverRewriter(*this, previous_parameters).rewrite(result_type);
      
      return get_functional(FunctionType(calling_convention, resolved_result_type, resolved_parameter_types,
                                         n_phantom, location));
    }

    /**
     * \brief Get a function type with fixed argument types.
     */
    ValuePtr<FunctionType> Context::get_function_type_fixed(CallingConvention calling_convention,
                                                            const ValuePtr<>& result_type,
                                                            const std::vector<ValuePtr<> >& parameter_types,
                                                            const SourceLocation& location) {
      std::vector<ValuePtr<FunctionTypeParameter> > parameters(parameter_types.size());
      for (std::size_t i = 0; i < parameter_types.size(); ++i)
        parameters[i] = new_function_type_parameter(parameter_types[i], parameter_types[i]->location());
      return get_function_type(calling_convention, result_type, parameters, 0, location);
    }

    namespace {
      class ParameterTypeRewriter : public RewriteCallback {
        std::vector<ValuePtr<> > m_previous;
        std::size_t m_depth;

      public:
        ParameterTypeRewriter(Context& context, const std::vector<ValuePtr<> >& previous)
        : RewriteCallback(context), m_previous(previous), m_depth(0) {}

        virtual ValuePtr<> rewrite(const ValuePtr<>& term) {
          if (ValuePtr<FunctionTypeResolvedParameter> parameter = dyn_cast<FunctionTypeResolvedParameter>(term)) {
            if (parameter->depth() == m_depth)
              return m_previous[parameter->index()];
            else
              return parameter->rewrite(*this);
          } else if (ValuePtr<FunctionType> function_type = dyn_cast<FunctionType>(term)) {
            ++m_depth;
            ValuePtr<> result = function_type->rewrite(*this);
            --m_depth;
            return result;
          } else if (ValuePtr<HashableValue> hashable = dyn_cast<HashableValue>(term)) {
            return hashable->rewrite(*this);
          } else {
            return term;
          }
        }
      };
    }

    /**
     * Get the type of a parameter, given previous parameters.
     *
     * \param previous Earlier parameters. Length of this array gives
     * the index of the parameter type to get.
     */
    ValuePtr<> FunctionType::parameter_type_after(const std::vector<ValuePtr<> >& previous) {
      return ParameterTypeRewriter(context(), previous).rewrite(parameter_types()[previous.size()]);
    }

    /**
     * Get the return type of a function of this type, given previous
     * parameters.
     */
    ValuePtr<> FunctionType::result_type_after(const std::vector<ValuePtr<> >& parameters) {
      if (parameters.size() != parameter_types().size())
        throw TvmUserError("incorrect number of parameters");
      return ParameterTypeRewriter(context(), parameters).rewrite(result_type());
    }

    template<typename V>
    void FunctionType::visit(V& v) {
      visit_base<HashableValue>(v);
      v("calling_convention", &FunctionType::m_calling_convention)
      ("parameter_type", &FunctionType::m_parameter_types)
      ("n_phantom", &FunctionType::m_n_phantom)
      ("result_type", &FunctionType::m_result_type);
    }
    
    ValuePtr<> FunctionType::check_type() const {
      for (std::vector<ValuePtr<> >::const_iterator ii = m_parameter_types.begin(), ie = m_parameter_types.end(); ii != ie; ++ii)
        if (!(*ii)->is_type())
          throw TvmUserError("Function argument type is not a type");
      return FunctionalBuilder::type_type(context(), location());
    }

    PSI_TVM_HASHABLE_IMPL(FunctionType, HashableValue, function)
    
    FunctionTypeParameter::FunctionTypeParameter(Context& context, const ValuePtr<>& type, const SourceLocation& location)
    : Value(context, term_function_type_parameter, type, this, location),
    m_parameter_type(type) {
    }
    
    template<typename V>
    void FunctionTypeParameter::visit(V& v) {
      visit_base<Value>(v);
    }
    
    PSI_TVM_VALUE_IMPL(FunctionTypeParameter, Value);

    ValuePtr<FunctionTypeParameter> Context::new_function_type_parameter(const ValuePtr<>& type, const SourceLocation& location) {
      return ValuePtr<FunctionTypeParameter>(::new FunctionTypeParameter(*this, type, location));
    }

    BlockMember::BlockMember(TermType term_type, const ValuePtr<>& type,
                             Value* source, const SourceLocation& location)
    : Value(type->context(), term_type, type, source, location),
    m_block(NULL) {
    }

    Instruction::Instruction(const ValuePtr<>& type, const char *operation, const SourceLocation& location)
    : BlockMember(term_instruction, type, this, location),
    m_operation(operation) {
    }
    
    /**
     * \brief Remove this instruction from its block.
     */
    void Instruction::remove() {
      PSI_ASSERT(block_ptr() && m_instruction_list_hook.is_linked());
      block_ptr()->erase_instruction(*this);
    }

    TerminatorInstruction::TerminatorInstruction(Context& context, const char* operation, const SourceLocation& location)
    : Instruction(FunctionalBuilder::empty_type(context, location), operation, location) {
    }

    /**
     * Utility function to check that the dominator of a jump target also dominates this instruction.
     */
    void TerminatorInstruction::check_dominated(const ValuePtr<Block>& target) {
      if (!source_dominated(target->dominator().get(), block().get()))
        throw TvmUserError("instruction jump target dominator block may not have run");
    }
    
    bool TerminatorInstruction::isa_impl(const Value& ptr) {
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

    /**
     * \brief Check whether this block is dominated by another.
     *
     * If \c block is NULL, this will return true since a NULL
     * dominator block refers to the function entry, i.e. before the
     * entry block is run, and therefore eveything is dominated by it.
     */
    bool Block::dominated_by(Block *block) {
      if (!block)
        return true;

      for (Block *b = this; b; b = b->m_dominator.get()) {
        if (block == b)
          return true;
      }
      return false;
    }
    
    /**
     * \brief Find the latest block which dominates both of the specified blocks.
     * 
     * \pre \code first->function() == second->function() \endcode
     */
    ValuePtr<Block> Block::common_dominator(const ValuePtr<Block>& first, const ValuePtr<Block>& second) {
      PSI_ASSERT(first->function() == second->function());

      for (ValuePtr<Block> i = first; i; i = i->dominator()) {
        if (second->dominated_by(i))
          return i;
      }
      
      for (ValuePtr<Block> i = second; i; i = i->dominator()) {
        if (first->dominated_by(i))
          return i;
      }
      
      return ValuePtr<Block>();
    }

    /**
     * Check if a term (i.e. all variables required by it) are
     * available in this block. If this block has not been added to a
     * function, the \c function parameter will be assigned to the
     * function it must be added to in order for the term to be
     * available, otherwise \c function will be set to the function
     * this block is part of.
     * 
     * \param term Term to check the availability of.
     * 
     * \param after If non-null, the value must be generated by an instruction
     * which occurs before this instruction is run or the instruction itself. If
     * null, the variable must be available at the start of the block.
     */
    bool Block::check_available(const ValuePtr<>& term, const ValuePtr<Instruction>& after) {
      Value *after_v = after.get();
      return source_dominated(term->source(), after_v ? after_v : this);
    }

    void Block::insert_instruction(const ValuePtr<Instruction>& insn, const ValuePtr<Instruction>& insert_before) {
      if (insn->m_block)
        throw TvmUserError("Instruction has already been inserted into a block");

      if (terminated() && !insert_before)
        throw TvmUserError("cannot add instruction at end of already terminated block");
      
      if (insert_before) {
        if (insert_before->block() != this)
          throw TvmUserError("instruction specified as insertion point is not part of this block");

        if (isa<TerminatorInstruction>(insn))
          throw TvmUserError("terminating instruction cannot be inserted other than at the end of a block");
      }

      m_instructions.insert(insert_before, *insn);
      insn->m_block = this;
    }
    
    void Block::erase_phi(Phi& phi) {
      PSI_ASSERT(phi.block_ptr() == this);
      m_phi_nodes.erase(phi);
    }
    
    void Block::erase_instruction(Instruction& instruction) {
      PSI_ASSERT(instruction.block_ptr() == this);
      m_instructions.erase(instruction);
    }

    /**
     * \brief Get the list of blocks which this one can exit to (including exceptions)
     */
    std::vector<ValuePtr<Block> > Block::successors() {
      std::vector<ValuePtr<Block> > result;
      if (ValuePtr<TerminatorInstruction> terminator = dyn_cast<TerminatorInstruction>(m_instructions.back()))
        result = terminator->successors();
      if (m_landing_pad)
        result.push_back(m_landing_pad);
      return result;
    }
    
    template<typename V>
    void Block::visit(V& v) {
      visit_base<Value>(v);
      v("function", &Block::m_function)
      ("dominator", &Block::m_dominator)
      ("landing_pad", &Block::m_landing_pad)
      ("instructions", &Block::m_instructions)
      ("phi_nodes", &Block::m_phi_nodes);
    }

    PSI_TVM_VALUE_IMPL(Block, Value)

    /**
     * \brief Add a value for a phi term along an incoming edge.
     *
     * \param block The block which jumps into the block containing
     * this phi node causing it to take on the given value.
     *
     * \param value Value the phi term takes on. This must not be a
     * phantom value, since it makes no sense for phi terms to allow
     * phantom values.
     */
    void Phi::add_edge(const ValuePtr<Block>& block, const ValuePtr<>& value) {
      if (value->phantom())
        throw TvmUserError("phi nodes cannot take on phantom values");
      
      for (std::vector<PhiEdge>::const_iterator ii = m_edges.begin(), ie = m_edges.end(); ii != ie; ++ii) {
        if (ii->block == block)
          throw TvmUserError("incoming edge added for the same block twice");
      }
      
      PhiEdge e;
      e.block = block;
      e.value = value;
      
      m_edges.push_back(e);
    }

    Phi::Phi(const ValuePtr<>& type, const SourceLocation& location)
    : BlockMember(term_phi, type, this, location) {
      if (type->phantom())
        throw TvmUserError("type of phi term cannot be phantom");
    }
    
    /**
     * \brief Find the value corresponding to a specific incoming block.
     */
    ValuePtr<> Phi::incoming_value_from(const ValuePtr<Block>& block) {
      for (std::vector<PhiEdge>::const_iterator ii = m_edges.begin(), ie = m_edges.end(); ii != ie; ++ii) {
        if (ii->block == block)
          return ii->value;
      }
      throw TvmUserError("Incoming block not found in PHI node");
    }
    
    /**
     * \brief Remove from its block.
     */
    void Phi::remove() {
      PSI_ASSERT(block_ptr() && m_phi_list_hook.is_linked());
      block_ptr()->erase_phi(*this);
    }
    
    template<typename V>
    void Phi::visit(V& v) {
      visit_base<Value>(v);
      v("edges", &Phi::m_edges);
    }

    PSI_TVM_VALUE_IMPL(Phi, Value)

    /**
     * \brief Create a new Phi node.
     *
     * Phi nodes allow values from non-dominating blocks to be used by
     * selecting a value based on which block was run immediately
     * before this one.
     *
     * \param type Type of this term. All values that this term can
     * take on must be of the same type.
     */
    ValuePtr<Phi> Block::insert_phi(const ValuePtr<>& type, const SourceLocation& location) {
      ValuePtr<Phi> phi(::new Phi(type, location));
      m_phi_nodes.push_back(*phi);
      phi->m_block = this;
      return phi;
    }

    Block::Block(Function *function, const ValuePtr<Block>& dominator,
                 bool is_landing_pad, const ValuePtr<Block>& landing_pad, const SourceLocation& location)
    : Value(function->context(), term_block, FunctionalBuilder::block_type(function->context(), location), this, location),
    m_function(function),
    m_dominator(dominator),
    m_landing_pad(landing_pad),
    m_is_landing_pad(is_landing_pad) {
    }

    FunctionParameter::FunctionParameter(Context& context, Function *function, const ValuePtr<>& type, bool phantom, const SourceLocation& location)
    : Value(context, term_function_parameter, type, this, location),
    m_phantom(phantom),
    m_function(function) {
      PSI_ASSERT(!type->parameterized());
    }
    
    template<typename V>
    void FunctionParameter::visit(V& v) {
      visit_base<Value>(v);
    }
    
    PSI_TVM_VALUE_IMPL(FunctionParameter, Value);

    /**
     * \brief Create a new function.
     */
    ValuePtr<Function> Module::new_function(const std::string& name, const ValuePtr<FunctionType>& type, const SourceLocation& location) {
      PSI_ASSERT(type);
      ValuePtr<Function> result(::new Function(context(), type, name, this, location));
      add_member(result);
      return result;
    }
    
    Function::Function(Context& context, const ValuePtr<FunctionType>& type, const std::string& name, Module* module, const SourceLocation& location)
    : Global(context, term_function, type, name, module, location) {

      std::vector<ValuePtr<> > previous;
      unsigned n_phantom = type->n_phantom();

      for (unsigned ii = 0, ie = type->parameter_types().size(); ii != ie; ++ii) {
        ValuePtr<FunctionParameter> p(::new FunctionParameter(context, this, type->parameter_type_after(previous), ii < n_phantom, location));
        m_parameters.push_back(*p);
        previous.push_back(p);
      }
      m_result_type = type->result_type_after(previous);
    }

    ValuePtr<FunctionType> Function::function_type() const {
      return value_cast<FunctionType>(value_cast<PointerType>(type())->target_type());
    }

    /**
     * \brief Create a new block.
     * 
     * \param dominator Dominating block. If this is NULL, only parameters
     * are available in this block.
     */
    ValuePtr<Block> Function::new_block(const SourceLocation& location, const ValuePtr<Block>& dominator, const ValuePtr<Block>& landing_pad) {
      ValuePtr<Block> b(::new Block(this, dominator, false, landing_pad, location));
      m_blocks.push_back(*b);
      return b;
    }

    /**
     * \brief Create a new block.
     * 
     * \param dominator Dominating block. If this is NULL, only parameters
     * are available in this block.
     */
    ValuePtr<Block> Function::new_landing_pad(const SourceLocation& location, const ValuePtr<Block>& dominator, const ValuePtr<Block>& landing_pad) {
      ValuePtr<Block> b(::new Block(this, dominator, true, landing_pad, location));
      m_blocks.push_back(*b);
      return b;
    }

    /**
     * Add a name for a term within this function.
     */
    void Function::add_term_name(const ValuePtr<>& term, const std::string& name) {
      m_name_map.insert(std::make_pair(term, name));
    }

    template<typename V>
    void Function::visit(V& v) {
      visit_base<Global>(v);
      v("parameters", &Function::m_parameters)
      ("result_type", &Function::m_result_type)
      ("exception_personality", &Function::m_exception_personality)
      ("name_map", &Function::m_name_map)
      ("blocks", &Function::m_blocks);
    }
    
    PSI_TVM_VALUE_IMPL(Function, Global);

    /**
     * \brief Return an insert point which is just after the given source term.
     * 
     * \param source Source to insert instructions after. This should be a
     * value source as returned by Term::source().
     */
    InstructionInsertPoint InstructionInsertPoint::after_source(const ValuePtr<>& source) {
      ValuePtr<Block> block;

      switch (source->term_type()) {
      // switch statements allow some weird stuff...
      {
      case term_function:
        block = value_cast<Function>(source)->blocks().front();
        goto block_function_common;

      case term_block:
        block = value_cast<Block>(source);
        goto block_function_common;
      
      block_function_common:
        if (block->instructions().empty())
          return InstructionInsertPoint(block);
        else
          return InstructionInsertPoint(*block->instructions().begin());
      }

      case term_instruction: {
        ValuePtr<Instruction> insn = value_cast<Instruction>(source);
        ValuePtr<Block> block = insn->block();
        Block::InstructionList::const_iterator insn_it = block->instructions().iterator_to(insn);
        ++insn_it;
        if (insn_it == block->instructions().end())
          return InstructionInsertPoint(block);
        else
          return InstructionInsertPoint(*insn_it);
      }

      default:
        PSI_FAIL("unexpected term type");
      }
    }
    
    /**
     * \brief Insert instruction at this point.
     * 
     * Need to check that all values are available!
     */
    void InstructionInsertPoint::insert(const ValuePtr<Instruction>& instruction) {
      m_block->insert_instruction(instruction, m_instruction);
    }
  }
}
