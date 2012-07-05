#include "Aggregate.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "FunctionalBuilder.hpp"
#include "Rewrite.hpp"
#include "Utility.hpp"

#include <boost/next_prior.hpp>
#include <boost/unordered_set.hpp>

namespace Psi {
  namespace Tvm {
    PSI_TVM_FUNCTIONAL_IMPL(FunctionTypeResolvedParameter, SimpleOp, function_type_resolved_parameter)

    ValuePtr<FunctionTypeResolvedParameter> FunctionTypeResolvedParameter::get(const ValuePtr<>& type, unsigned depth, unsigned index, const SourceLocation& location) {
      return type->context().get_functional(FunctionTypeResolvedParameter(type, depth, index, location));
    }
    
    FunctionTypeResolvedParameter::FunctionTypeResolvedParameter(const ValuePtr<>& type, unsigned depth, unsigned index, const SourceLocation& location)
    : SimpleOp(type, hashable_setup<FunctionTypeResolvedParameter>(type), location),
    m_depth(depth),
    m_index(index) {
    }
    
    namespace {
      HashableValueSetup function_type_hashable_setup(CallingConvention calling_convention, const ValuePtr<>& result_type, const std::vector<ValuePtr<> >& parameter_types) {
        HashableValueSetup hv = hashable_setup<FunctionType>(result_type);
        hv.combine(calling_convention);
        for (std::vector<ValuePtr<> >::const_iterator ii = parameter_types.begin(), ie = parameter_types.end(); ii != ie; ++ii)
          hv.combine(*ii);
        return hv;
      }
    }

    FunctionType::FunctionType(CallingConvention calling_convention, const ValuePtr<>& result_type,
                               const std::vector<ValuePtr<> >& parameter_types, unsigned n_phantom, const SourceLocation& location)
    : HashableValue(&result_type->context(), term_function_type, Metatype::get(result_type->context(), location),
                    function_type_hashable_setup(calling_convention, result_type, parameter_types), location),
    m_parameter_types(parameter_types),
    m_n_phantom(n_phantom),
    m_result_type(result_type),
    m_calling_convention(calling_convention) {
    }

    class Context::FunctionTypeResolverRewriter {
      std::vector<ValuePtr<FunctionTypeParameter> > m_parameters;
      std::size_t m_depth;

    public:
      FunctionTypeResolverRewriter(const std::vector<ValuePtr<FunctionTypeParameter> >& parameters)
      : m_parameters(parameters), m_depth(0) {
      }

      ValuePtr<> operator () (const ValuePtr<>& term) {
        switch (term->term_type()) {
        case term_function_type: {
          ++m_depth;
          ValuePtr<> result = rewrite_term_default(*this, term);
          --m_depth;
          return result;
        }
          
        case term_function_type_parameter: {
          ValuePtr<> type = this->operator() (term->type());
          for (unsigned i = 0, e = m_parameters.size(); i != e; ++i) {
            if (m_parameters[i] == term)
              return FunctionTypeResolvedParameter::get(type, m_depth, i, m_parameters[i]->location());
          }
          if (type != term->type())
            throw TvmUserError("type of unresolved function parameter cannot depend on type of resolved function parameter");
          return term;
        }
        
        default:
          return rewrite_term_default(*this, term);
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
      for (unsigned ii = 0, ie = n_phantom; ii != ie; ++ii) {
        resolved_parameter_types.push_back(FunctionTypeResolverRewriter(previous_parameters)(parameters[ii]->type()));
        previous_parameters.push_back(parameters[ii]);
      }

      for (unsigned ii = n_phantom, ie = parameters.size(); ii != ie; ++ii) {
        resolved_parameter_types.push_back(FunctionTypeResolverRewriter(previous_parameters)(parameters[ii]->type()));
        previous_parameters.push_back(parameters[ii]);
      }

      ValuePtr<> resolved_result_type = FunctionTypeResolverRewriter(previous_parameters)(result_type);
      
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
      class ParameterTypeRewriter {
        std::vector<ValuePtr<> > m_previous;
        std::size_t m_depth;

      public:
        ParameterTypeRewriter(const std::vector<ValuePtr<> >& previous)
        : m_previous(previous), m_depth(0) {}

        ValuePtr<> operator () (const ValuePtr<>& term) {
          if (ValuePtr<FunctionTypeResolvedParameter> parameter = dyn_cast<FunctionTypeResolvedParameter>(term)) {
            if (parameter->depth() == m_depth)
              return m_previous[parameter->index()];
          } else if (ValuePtr<FunctionType> function_type = dyn_cast<FunctionType>(term)) {
            ++m_depth;
            ValuePtr<> result = rewrite_term_default(*this, function_type);
            --m_depth;
            return result;
          }

          return rewrite_term_default(*this, term);
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
      return ParameterTypeRewriter(previous)(parameter_types()[previous.size()]);
    }

    /**
     * Get the return type of a function of this type, given previous
     * parameters.
     */
    ValuePtr<> FunctionType::result_type_after(const std::vector<ValuePtr<> >& parameters) {
      if (parameters.size() != parameter_types().size())
        throw TvmUserError("incorrect number of parameters");
      return ParameterTypeRewriter(parameters)(result_type());
    }
    
    FunctionTypeParameter::FunctionTypeParameter(Context *context, const ValuePtr<>& type, const SourceLocation& location)
    : Value(context, term_function_type_parameter, type, this, location) {
    }

    ValuePtr<FunctionTypeParameter> Context::new_function_type_parameter(const ValuePtr<>& type, const SourceLocation& location) {
      return ValuePtr<FunctionTypeParameter>(new FunctionTypeParameter(this, type, location));
    }

    BlockMember::BlockMember(TermType term_type, const ValuePtr<>& type, const ValuePtr<Block>& block,
                             Value* source, const SourceLocation& location)
    : Value(&block->context(), term_type, type, source, location),
    m_block(block) {
    }

    Instruction::Instruction(const ValuePtr<>& type, const char *operation,
                             const ValuePtr<Block>& block, const SourceLocation& location)
    : BlockMember(term_instruction, type, block, this, location),
    m_operation(operation) {
    }

    TerminatorInstruction::TerminatorInstruction(const char* operation, const ValuePtr<Block>& block, const SourceLocation& location)
    : Instruction(EmptyType::get(block->context(), location), operation, block, location) {
    }

    /**
     * Utility function to check that the dominator of a jump target also dominates this instruction.
     */
    void TerminatorInstruction::check_dominated(const ValuePtr<Block>& target) {
      if (!source_dominated(target->dominator().get(), block().get()))
        throw TvmUserError("instruction jump target dominator block may not have run");
    }
    
    /**
     * \brief Check whether this block is dominated by another.
     *
     * If \c block is NULL, this will return true since a NULL
     * dominator block refers to the function entry, i.e. before the
     * entry block is run, and therefore eveything is dominated by it.
     */
    bool Block::dominated_by(const ValuePtr<Block>& block) {
      if (!block)
        return true;

      for (ValuePtr<Block> b(this); b; b = b->dominator()) {
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
      if (m_terminated && !insert_before)
        throw TvmUserError("cannot add instruction at end of already terminated block");
      
      if (insert_before && (insert_before->block() != this))
        throw TvmUserError("instruction specified as insertion point is not part of this block");

      InstructionList::iterator insert_before_it = std::find(m_instructions.begin(), m_instructions.end(), insert_before);
      if (insert_before_it == m_instructions.end())
        throw TvmUserError("instruction specified as insertion point cannot be found in this block");

      ValuePtr<TerminatorInstruction> term = dyn_cast<TerminatorInstruction>(insn);
      if (term)
        throw TvmUserError("terminating instruction cannot be inserted other than at the end of a block");

      m_instructions.insert(insert_before_it, insn);
      
      if (term)
        m_terminated = true;
    }

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

    Phi::Phi(const ValuePtr<>& type, const ValuePtr<Block>& block, const SourceLocation& location)
    : BlockMember(term_phi, type, block, this, location) {
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
      ValuePtr<Phi> phi(::new Phi(type, ValuePtr<Block>(this), location));
      m_phi_nodes.push_back(phi);
      return phi;
    }

    Block::Block(const ValuePtr<Function>& function, const ValuePtr<Block>& dominator,
                 bool is_landing_pad, const ValuePtr<Block>& landing_pad, const SourceLocation& location)
    : Value(&function->context(), term_block, BlockType::get(function->context(), location), this, location),
    m_function(function),
    m_dominator(dominator),
    m_landing_pad(landing_pad),
    m_is_landing_pad(is_landing_pad),
    m_terminated(false) {
    }

    
    FunctionParameterTerm::FunctionParameterTerm(const UserInitializer& ui, Context *context, FunctionTerm* function, Term* type, bool phantom)
      : Term(ui, context, term_function_parameter, this, type),
      m_phantom(phantom) {
      PSI_ASSERT(!type->parameterized());
      set_base_parameter(0, function);
    }

    class FunctionParameterTerm::Initializer : public InitializerBase<FunctionParameterTerm> {
    public:
      Initializer(FunctionTerm* function, Term* type, bool phantom)
        : m_function(function), m_type(type), m_phantom(phantom) {
      }

      std::size_t n_uses() const {
        return 1;
      }

      FunctionParameterTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
        return new (base) FunctionParameterTerm(ui, context, m_function, m_type, m_phantom);
      }

    private:
      FunctionTerm* m_function;
      Term* m_type;
      bool m_phantom;
    };

    FunctionTerm::FunctionTerm(const UserInitializer& ui, Context *context, FunctionTypeTerm* type, const std::string& name, Module *module)
      : GlobalTerm(ui, context, term_function, type, name, module) {
      ScopedArray<Term*> parameters(type->n_parameters());
      for (std::size_t i = 0; i < parameters.size(); ++i) {
        Term* param_type = type->parameter_type_after(parameters.slice(0, i));
        FunctionParameterTerm* param = context->allocate_term(FunctionParameterTerm::Initializer(this, param_type, i<type->n_phantom_parameters()));
        set_base_parameter(i+3, param);
        parameters[i] = param;
      }

      set_base_parameter(2, type->result_type_after(parameters));
    }

    class FunctionTerm::Initializer : public InitializerBase<FunctionTerm> {
    public:
      Initializer(FunctionTypeTerm* type, const std::string& name, Module *module)
      : m_type(type), m_name(name), m_module(module) {
      }

      FunctionTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
        return new (base) FunctionTerm(ui, context, m_type, m_name, m_module);
      }

      std::size_t n_uses() const {
        return m_type->n_parameters() + 3;
      }

    private:
      FunctionTypeTerm* m_type;
      std::string m_name;
      Module *m_module;
    };

    /**
     * \brief Create a new function.
     */
    FunctionTerm* Module::new_function(const std::string& name, FunctionTypeTerm* type) {
      FunctionTerm *result = context().allocate_term(FunctionTerm::Initializer(type, name, this));
      add_member(result);
      return result;
    }

    /**
     * \brief Set the entry point for a function.
     *
     * If a function has no entry point, it will be treated as
     * external. Once an entry point has been set, it cannot be
     * changed.
     */
    void FunctionTerm::set_entry(BlockTerm* block) {
      if (entry())
        throw TvmUserError("Cannot change the entry point of a function once it is set");
      
      set_base_parameter(1, block);
    }

    /**
     * \brief Create a new block.
     * 
     * \param dominator Dominating block. If this is NULL, only parameters
     * are available in this block.
     */
    BlockTerm* FunctionTerm::new_block(BlockTerm* dominator) {
      return context().allocate_term(BlockTerm::Initializer(this, dominator, false));
    }

    /**
     * \brief Create a new block.
     * 
     * Equivalent to <tt>new_block(NULL)</tt>.
     */
    BlockTerm* FunctionTerm::new_block() {
      return new_block(NULL);
    }

    /**
     * \brief Create a new landing pad.
     * 
     * \param dominator Dominating block.
     */
    BlockTerm* FunctionTerm::new_landing_pad(BlockTerm* dominator) {
      return context().allocate_term(BlockTerm::Initializer(this, dominator, true));
    }
    
    /**
     * \brief Create a new landing pad which is not dominated by any block.
     */
    BlockTerm* FunctionTerm::new_landing_pad() {
      return new_landing_pad(NULL);
    }

    /**
     * Add a name for a term within this function.
     */
    void FunctionTerm::add_term_name(Term *term, const std::string& name) {
      m_name_map.insert(std::make_pair(term, name));
    }

    /**
     * Topologically sort blocks in this function so that the result is a
     * list of all blocks in the function where each block that appears
     * is guaranteed to appear after its dominating block.
     * 
     * In addition, the first block in the resulting list will always
     * be the function entry.
     */
    std::vector<BlockTerm*> FunctionTerm::topsort_blocks() {
      std::vector<BlockTerm*> blocks;
      boost::unordered_set<BlockTerm*> visited_blocks;

      blocks.push_back(entry());
      visited_blocks.insert(entry());
      
      for (std::size_t i = 0; i != blocks.size(); ++i) {
        std::vector<BlockTerm*> successors = blocks[i]->successors();
        for (std::vector<BlockTerm*>::iterator it = successors.begin(); it != successors.end(); ++it) {
          if (visited_blocks.find((*it)->dominator()) != visited_blocks.end()) {
            if (visited_blocks.insert(*it).second)
              blocks.push_back(*it);
          }
        }
      }
      
      return blocks;
    }

    /**
     * \brief Return an insert point which is just after the given source term.
     * 
     * \param source Source to insert instructions after. This should be a
     * value source as returned by Term::source().
     */
    InstructionInsertPoint InstructionInsertPoint::after_source(Term *source) {
      switch (source->term_type()) {
      // switch statements allow some weird stuff...
      {
        BlockTerm *block;
        
      case term_function:
        block = cast<FunctionTerm>(source)->entry();
        goto block_function_common;

      case term_block:
        block = cast<BlockTerm>(source);
        goto block_function_common;
      
      block_function_common:
        BlockTerm::InstructionList& insn_list = block->instructions();
        if (insn_list.empty())
          return InstructionInsertPoint(block);
        else
          return InstructionInsertPoint(&insn_list.front());
      }

      case term_instruction: {
        InstructionTerm *insn = cast<InstructionTerm>(source);
        BlockTerm *block = insn->block();
        BlockTerm::InstructionList& insn_list = block->instructions();
        BlockTerm::InstructionList::iterator insn_it = insn_list.iterator_to(*insn);
        ++insn_it;
        if (insn_it == insn_list.end())
          return InstructionInsertPoint(block);
        else
          return InstructionInsertPoint(&*insn_it);
        break;
      }

      default:
        PSI_FAIL("unexpected term type");
      }
    }
  }
}
