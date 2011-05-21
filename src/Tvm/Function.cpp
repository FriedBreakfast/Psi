#include "Aggregate.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "FunctionalBuilder.hpp"
#include "Rewrite.hpp"
#include "Utility.hpp"

#include <boost/next_prior.hpp>

namespace Psi {
  namespace Tvm {
    bool FunctionTypeResolvedParameter::Data::operator == (const FunctionTypeResolvedParameter::Data& other) const {
      return (depth == other.depth) && (index == other.index);
    }

    std::size_t hash_value(const FunctionTypeResolvedParameter::Data& self) {
      std::size_t h = 0;
      boost::hash_combine(h, self.depth);
      boost::hash_combine(h, self.index);
      return h;
    }

    FunctionTypeResolvedParameter::Ptr FunctionTypeResolvedParameter::get(Term* type, unsigned depth, unsigned index) {
      return type->context().get_functional<FunctionTypeResolvedParameter>
        (StaticArray<Term*, 1>(type), Data(depth, index));
    }

    const char FunctionTypeResolvedParameter::operation[] = "function_type_resolved_parameter";

    FunctionalTypeResult FunctionTypeResolvedParameter::type(Context&, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
	throw TvmInternalError("FunctionTypeResolverParameter takes one parameter");

      return parameters[0];
    }

    FunctionTypeTerm::FunctionTypeTerm(const UserInitializer& ui, Context *context, std::size_t hash, Term* result_type, ArrayPtr<Term*const> parameter_types, std::size_t n_phantom, CallingConvention calling_convention)
      : HashTerm(ui, context, term_function_type,
                 common_source(result_type->source(), common_source(parameter_types)),
                 Metatype::get(*context), hash),
        m_n_phantom(n_phantom),
        m_calling_convention(calling_convention) {
      set_base_parameter(0, result_type);
      for (std::size_t i = 0; i < parameter_types.size(); i++)
        set_base_parameter(i+1, parameter_types[i]);
    }

    class FunctionTypeTerm::Setup : public InitializerBase<FunctionTypeTerm> {
    public:
      Setup(Term* result_type, ArrayPtr<Term*const> parameter_types,
            std::size_t n_phantom, CallingConvention calling_convention)
        : m_parameter_types(parameter_types),
          m_result_type(result_type),
          m_n_phantom(n_phantom),
          m_calling_convention(calling_convention) {
        m_hash = 0;
        boost::hash_combine(m_hash, result_type->hash_value());
        for (std::size_t i = 0; i < parameter_types.size(); ++i)
          boost::hash_combine(m_hash, parameter_types[i]->hash_value());
        boost::hash_combine(m_hash, calling_convention);
      }

      void prepare_initialize(Context*) {
      }

      FunctionTypeTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
        return new (base) FunctionTypeTerm(ui, context, m_hash, m_result_type, m_parameter_types, m_n_phantom, m_calling_convention);
      }

      std::size_t hash() const {
        return m_hash;
      }

      std::size_t n_uses() const {
        return m_parameter_types.size() + 1;
      }

      bool equals(HashTerm *term) const {
        if ((m_hash != term->m_hash) || (term->term_type() != term_function_type))
          return false;

        FunctionTypeTerm *cast_term = cast<FunctionTypeTerm>(term);

        if (m_parameter_types.size() != cast_term->n_parameters())
          return false;

        for (std::size_t i = 0; i < m_parameter_types.size(); ++i) {
          if (m_parameter_types[i] != cast_term->parameter_type(i))
            return false;
        }

        if (m_result_type != cast_term->result_type())
          return false;

        if (m_n_phantom != cast_term->n_phantom_parameters())
          return false;

        if (m_calling_convention != cast_term->calling_convention())
          return false;

        return true;
      }

    private:
      std::size_t m_hash;
      ArrayPtr<Term*const> m_parameter_types;
      Term* m_result_type;
      std::size_t m_n_phantom;
      CallingConvention m_calling_convention;
    };

    class Context::FunctionTypeResolverRewriter {
      ArrayPtr<FunctionTypeParameterTerm*const> m_parameters;
      std::size_t m_depth;

    public:
      FunctionTypeResolverRewriter(ArrayPtr<FunctionTypeParameterTerm*const> parameters)
      : m_parameters(parameters), m_depth(0) {
      }

      Term* operator () (Term* term) {
        switch (term->term_type()) {
        case term_function_type: {
          ++m_depth;
          Term *result = rewrite_term_default(*this, term);
          --m_depth;
          return result;
        }
          
        case term_function_type_parameter: {
          Term *type = this->operator() (term->type());
          for (unsigned i = 0, e = m_parameters.size(); i != e; ++i) {
            if (m_parameters[i] == term)
              return FunctionTypeResolvedParameter::get(type, m_depth, i);
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
    FunctionTypeTerm* Context::get_function_type(CallingConvention calling_convention,
                                                 Term* result_type,
                                                 ArrayPtr<FunctionTypeParameterTerm*const> phantom_parameters,
                                                 ArrayPtr<FunctionTypeParameterTerm*const> parameters) {
      ScopedArray<FunctionTypeParameterTerm*> all_parameters(phantom_parameters.size() + parameters.size());
      std::copy(phantom_parameters.get(), phantom_parameters.get() + phantom_parameters.size(), all_parameters.get());
      std::copy(parameters.get(), parameters.get() + parameters.size(), all_parameters.get() + phantom_parameters.size());
      
      ScopedArray<Term*> resolved_parameter_types(phantom_parameters.size() + parameters.size());
      for (unsigned i = 0; i != phantom_parameters.size(); ++i)
        resolved_parameter_types[i] = FunctionTypeResolverRewriter(all_parameters.slice(0, i))(phantom_parameters[i]->type());

      for (unsigned i = 0; i != parameters.size(); ++i) {
        unsigned index = i + phantom_parameters.size();
        resolved_parameter_types[index] = FunctionTypeResolverRewriter(all_parameters.slice(0, index))(parameters[i]->type());
      }

      Term* resolved_result_type = FunctionTypeResolverRewriter(all_parameters)(result_type);

      FunctionTypeTerm::Setup setup(resolved_result_type, resolved_parameter_types, phantom_parameters.size(), calling_convention);
      return hash_term_get(setup);
    }

    /**
     * \brief Get a function type with fixed argument types.
     */
    FunctionTypeTerm* Context::get_function_type_fixed(CallingConvention calling_convention,
                                                       Term* result,
                                                       ArrayPtr<Term*const> parameter_types) {
      ScopedArray<FunctionTypeParameterTerm*> parameters(parameter_types.size());
      for (std::size_t i = 0; i < parameter_types.size(); ++i)
	parameters[i] = new_function_type_parameter(parameter_types[i]);
      return get_function_type(calling_convention, result, ArrayPtr<FunctionTypeParameterTerm*const>(), parameters);
    }

    namespace {
      class ParameterTypeRewriter {
        ArrayPtr<Term*const> m_previous;
        std::size_t m_depth;

      public:
	ParameterTypeRewriter(ArrayPtr<Term*const> previous) : m_previous(previous), m_depth(0) {}

	Term* operator () (Term* term) {
          if (FunctionTypeResolvedParameter::Ptr parameter = dyn_cast<FunctionTypeResolvedParameter>(term)) {
            if (parameter->depth() == m_depth)
              return m_previous[parameter->index()];
          } else if (FunctionTypeTerm *function_type = dyn_cast<FunctionTypeTerm>(term)) {
            ++m_depth;
            Term *result = rewrite_term_default(*this, function_type);
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
    Term* FunctionTypeTerm::parameter_type_after(ArrayPtr<Term*const> previous) {
      return ParameterTypeRewriter(previous)(parameter_type(previous.size()));
    }

    /**
     * Get the return type of a function of this type, given previous
     * parameters.
     */
    Term* FunctionTypeTerm::result_type_after(ArrayPtr<Term*const> parameters) {
      if (parameters.size() != n_parameters())
	throw TvmUserError("incorrect number of parameters");
      return ParameterTypeRewriter(parameters)(result_type());
    }
    
    FunctionTypeParameterTerm::FunctionTypeParameterTerm(const UserInitializer& ui, Context *context, Term* type)
      : Term(ui, context, term_function_type_parameter, this, type) {
    }

    class FunctionTypeParameterTerm::Initializer : public InitializerBase<FunctionTypeParameterTerm> {
    public:
      explicit Initializer(Term* type) : m_type(type) {}

      std::size_t n_uses() const {
	return 1;
      }

      FunctionTypeParameterTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) FunctionTypeParameterTerm(ui, context, m_type);
      }

    private:
      Term* m_type;
    };

    FunctionTypeParameterTerm* Context::new_function_type_parameter(Term* type) {
      return allocate_term(FunctionTypeParameterTerm::Initializer(type));
    }
    
    BlockMemberTerm::BlockMemberTerm(const Psi::UserInitializer& ui, Context* context, TermType term_type, Term* source, Term* type, BlockTerm* block)
    : Term(ui, context, term_type, source, type), m_block(block) {
    }

    InstructionTerm::InstructionTerm(const UserInitializer& ui, Context *context,
				     Term* type, const char *operation,
                                     ArrayPtr<Term*const> parameters,
                                     BlockTerm* block)
      : BlockMemberTerm(ui, context, term_instruction, this, type, block),
        m_operation(operation) {

      for (std::size_t i = 0; i < parameters.size(); ++i)
	set_base_parameter(i, parameters[i]);
    }
    
    class InstructionTerm::Initializer {
    public:
      typedef InstructionTerm TermType;

      Initializer(Term* type,
		  ArrayPtr<Term*const> parameters,
                  const InstructionTermSetup *setup,
                  BlockTerm* block)
	: m_type(type),
	  m_parameters(parameters),
          m_setup(setup),
          m_block(block) {
      }

      InstructionTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
        return m_setup->construct(base, ui, context, m_type, m_setup->operation, m_parameters, m_block);
      }

      std::size_t term_size() const {
        return m_setup->term_size;
      }

      std::size_t n_uses() const {
	return m_parameters.size();
      }

    private:
      Term* m_type;
      ArrayPtr<Term*const> m_parameters;
      const InstructionTermSetup *m_setup;
      BlockTerm* m_block;
    };

    /**
     * \brief Check whether this block is dominated by another.
     *
     * If \c block is NULL, this will return true since a NULL
     * dominator block refers to the function entry, i.e. before the
     * entry block is run, and therefore eveything is dominated by it.
     */
    bool BlockTerm::dominated_by(BlockTerm* block) {
      if (!block)
        return true;

      for (BlockTerm* b = this; b; b = b->dominator()) {
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
    BlockTerm* BlockTerm::common_dominator(BlockTerm *first, BlockTerm *second) {
      PSI_ASSERT(first->function() == second->function());

      for (BlockTerm *i = first; i; i = i->dominator()) {
        if (second->dominated_by(i))
          return i;
      }
      
      for (BlockTerm *i = second; i; i = i->dominator()) {
        if (first->dominated_by(i))
          return i;
      }
      
      return NULL;
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
    bool BlockTerm::check_available(Term* term, InstructionTerm *after) {
      return source_dominated(term->source(), after ? static_cast<Term*>(after) : this);
    }

    InstructionTerm* BlockTerm::new_instruction_bare(const InstructionTermSetup& setup, ArrayPtr<Term*const> parameters, InstructionTerm *insert_before) {
      if (m_terminated && !insert_before)
        throw TvmUserError("cannot add instruction at end of already terminated block");
      
      if (insert_before && (insert_before->block() != this))
        throw TvmUserError("instruction specified as insertion point is not part of this block");

      InstructionTerm *insert_after;
      InstructionList::iterator insert_before_it;
      if (insert_before) {
        insert_before_it = m_instructions.iterator_to(*insert_before);
        insert_after = (insert_before_it != m_instructions.begin()) ? &*boost::prior(insert_before_it) : 0;
      } else {
        insert_before_it = m_instructions.end();
        insert_after = !m_instructions.empty() ? &m_instructions.back() : 0;
      }

      // Check parameters are valid and adjust dominator blocks
      for (std::size_t i = 0; i < parameters.size(); ++i) {
        Term *param = parameters[i];
        if (param->parameterized() || param->phantom())
          throw TvmUserError("instructions cannot accept abstract or phantom parameters");
        if (!check_available(param, insert_after))
          throw TvmUserError("parameter value is not available in this block");
      }

      InstructionTypeResult type = setup.type(function(), parameters);

      if (type.terminator) {
        if (insert_before)
          throw TvmUserError("terminating instruction cannot be inserted other than at the end of a block");
        
        for (std::vector<BlockTerm*>::const_iterator ii = type.successors.begin(), ie = type.successors.end(); ii != ie; ++ii) {
          if (!source_dominated((*ii)->dominator(), this))
            throw TvmUserError("instruction jump target dominator block may not have run");
        }
      }

      InstructionTerm* insn = context().allocate_term(InstructionTerm::Initializer(type.type, parameters, &setup, this));
      m_instructions.insert(insert_before_it, *insn);
      
      if (type.terminator) {
        m_terminated = true;
        m_successors.swap(type.successors);
      }

      return insn;
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
    void PhiTerm::add_incoming(BlockTerm* block, Term* value) {
      if (value->phantom())
        throw TvmUserError("phi nodes cannot take on phantom values");

      std::size_t free_slots = (n_base_parameters() / 2) - m_n_incoming;
      if (!free_slots)
        resize_base_parameters(m_n_incoming * 4);

      set_base_parameter(m_n_incoming*2, block);
      set_base_parameter(m_n_incoming*2+1, value);
      m_n_incoming++;
    }

    PhiTerm::PhiTerm(const UserInitializer& ui, Context *context, Term* type, BlockTerm *block)
      : BlockMemberTerm(ui, context, term_phi, this, type, block),
        m_n_incoming(0) {
    }

    class PhiTerm::Initializer : public InitializerBase<PhiTerm> {
    public:
      Initializer(Term* type, BlockTerm *block)
	: m_type(type),
          m_block(block) {
      }

      PhiTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
        return new (base) PhiTerm(ui, context, m_type, m_block);
      }

      std::size_t n_uses() const {
        return 8;
      }

    private:
      Term* m_type;
      BlockTerm* m_block;
    };
    
    /**
     * \brief Find the value corresponding to a specific incoming block.
     */
    Term *PhiTerm::incoming_value_from(BlockTerm *block) {
      for (unsigned ii = 0, ie = n_incoming(); ii != ie; ++ii) {
        if (block == incoming_block(ii))
          return incoming_value(ii);
      }
      throw TvmUserError("Incoming block not found in PHI node");
    }
    
    CatchClauseTerm::CatchClauseTerm(const Psi::UserInitializer& ui, Context *context, BlockTerm *block)
    : BlockMemberTerm(ui, context, term_catch_clause, this, CatchClauseNameType::get(*context), block) {
      set_base_parameter(0, block);
    }
    
    class CatchClauseTerm::Initializer : public InitializerBase<CatchClauseTerm> {
    public:
      Initializer(BlockTerm *block) : m_block(block) {
      }

      CatchClauseTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
        return new (base) CatchClauseTerm(ui, context, m_block);
      }

      std::size_t n_uses() const {
        return 1;
      }
      
    private:
      BlockTerm *m_block;
    };
    
    const char CatchClauseNameType::operation[] = "catch_type";
    
    FunctionalTypeResult CatchClauseNameType::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 0)
        throw TvmUserError("catch_type type takes no parameters");
      return FunctionalTypeResult(Metatype::get(context));
    }

    CatchClauseNameType::Ptr CatchClauseNameType::get(Context& context) {
      return context.get_functional<CatchClauseNameType>(ArrayPtr<Term*>());
    }
    
    const char CatchClauseName::operation[] = "catch";
        
    FunctionalTypeResult CatchClauseName::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("catch term takes one parameter");
      
      if (!isa<CatchClauseTerm>(parameters[0]))
        throw TvmUserError("argument to catch term must be a catch clause");
      
      return FunctionalBuilder::catch_type(context);
    }

    CatchClauseName::Ptr CatchClauseName::get(CatchClauseTerm *catch_clause, unsigned index) {
      return catch_clause->context().get_functional<CatchClauseName>(StaticArray<Term*,1>(catch_clause), index);
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
    PhiTerm* BlockTerm::new_phi(Term* type) {
      if (type->phantom())
        throw TvmUserError("type of phi term cannot be phantom");

      PhiTerm *phi = context().allocate_term(PhiTerm::Initializer(type, this));
      m_phi_nodes.push_back(*phi);
      return phi;
    }

    BlockTerm::BlockTerm(const UserInitializer& ui, Context *context, FunctionTerm* function, BlockTerm* dominator, bool landing_pad)
      : Term(ui, context, term_block, this, BlockType::get(*context)),
        m_terminated(false) {

      PSI_ASSERT(function);
      set_base_parameter(0, function);
      set_base_parameter(1, dominator);
      
      if (landing_pad) {
        CatchClauseTerm *catch_clause = context->allocate_term(CatchClauseTerm::Initializer(this));
        set_base_parameter(2, catch_clause);
      }
    }

    class BlockTerm::Initializer : public InitializerBase<BlockTerm> {
    public:
      Initializer(FunctionTerm* function, BlockTerm* dominator, bool landing_pad)
        : m_function(function), m_dominator(dominator), m_landing_pad(landing_pad) {
      }

      BlockTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) BlockTerm(ui, context, m_function, m_dominator, m_landing_pad);
      }

      std::size_t n_uses() const {
	return 3;
      }

    private:
      FunctionTerm* m_function;
      BlockTerm* m_dominator;
      bool m_landing_pad;
    };
    
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
      std::tr1::unordered_set<BlockTerm*> visited_blocks;

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
