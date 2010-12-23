#include "Aggregate.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Rewrite.hpp"
#include "Utility.hpp"

namespace Psi {
  namespace Tvm {
    /**
     * \brief Internal type used to build function types.
     */
    PSI_TVM_FUNCTIONAL_TYPE(FunctionTypeResolverParameter)
    struct Data {
      Data(unsigned depth_, unsigned index_)
        : depth(depth_), index(index_) {}

      unsigned depth;
      unsigned index;

      bool operator == (const Data& other) const {
        return (depth == other.depth) && (index == other.index);
      }

      friend std::size_t hash_value(const Data& self) {
        std::size_t h = 0;
        boost::hash_combine(h, self.depth);
        boost::hash_combine(h, self.index);
        return h;
      }
    };
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the depth of this parameter relative to the
    /// function type it is part of.
    ///
    /// If a parameter is used as the type of a parameter in its own
    /// functions argument list, then this is zero. For each function
    /// type it is then nested inside, this should increase by one.
    unsigned depth() const {return data().depth;}
    /// \brief Get the parameter number of this parameter in its
    /// function.
    unsigned index() const {return data().index;}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static FunctionTypeResolverParameter::Ptr get(Term* type, unsigned depth, unsigned index) {
      Term *parameters[1] = {type};
      return type->context().get_functional<FunctionTypeResolverParameter>
        (ArrayPtr<Term*const>(parameters, 1), Data(depth, index));
    }
    PSI_TVM_FUNCTIONAL_TYPE_END(FunctionTypeResolverParameter)

    FunctionTypeTerm::FunctionTypeTerm(const UserInitializer& ui,
				       Context *context,
				       Term *result_type,
                                       ArrayPtr<FunctionTypeParameterTerm*const> phantom_parameters,
				       ArrayPtr<FunctionTypeParameterTerm*const> parameters,
				       CallingConvention calling_convention)
      : Term(ui, context, term_function_type,
	     result_type->abstract() || any_abstract(parameters) || any_abstract(phantom_parameters), true, false,
             common_source(result_type->source(), common_source(common_source(parameters), common_source(phantom_parameters))),
	     Metatype::get(*context)),
	m_calling_convention(calling_convention),
        m_n_phantoms(phantom_parameters.size()) {
      set_base_parameter(0, result_type);

      for (std::size_t i = 0; i < phantom_parameters.size(); ++i)
        set_base_parameter(i+1, phantom_parameters[i]);

      for (std::size_t i = 0; i < parameters.size(); ++i)
	set_base_parameter(i+m_n_phantoms+1, parameters[i]);
    }

    class FunctionTypeTerm::Initializer : public InitializerBase<FunctionTypeTerm> {
    public:
      Initializer(Term *result_type,
		  ArrayPtr<FunctionTypeParameterTerm*const> phantom_parameters,
		  ArrayPtr<FunctionTypeParameterTerm*const> parameters,
		  CallingConvention calling_convention)
	: m_result_type(result_type),
          m_phantom_parameters(phantom_parameters),
	  m_parameters(parameters),
	  m_calling_convention(calling_convention) {
      }

      std::size_t n_uses() const {
	return m_phantom_parameters.size() + m_parameters.size() + 1;
      }

      FunctionTypeTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) FunctionTypeTerm(ui, context, m_result_type, m_phantom_parameters,
					   m_parameters, m_calling_convention);
      }

    private:
      Term *m_result_type;
      ArrayPtr<FunctionTypeParameterTerm*const> m_phantom_parameters;
      ArrayPtr<FunctionTypeParameterTerm*const> m_parameters;
      CallingConvention m_calling_convention;
    };

    /**
     * Check whether part of function type term is complete,
     * i.e. whether there are still function parameters which have
     * to be resolved by further function types (this happens in the
     * case of nested function types).
     */
    bool Context::check_function_type_complete(Term* term, CheckCompleteMap& functions)
    {
      if (!term->parameterized())
	return true;

      if (!check_function_type_complete(term->type(), functions))
	return false;

      switch(term->term_type()) {
      case term_functional: {
	FunctionalTerm *cast_term = cast<FunctionalTerm>(term);
	for (std::size_t i = 0; i < cast_term->n_parameters(); i++) {
	  if (!check_function_type_complete(cast_term->parameter(i), functions))
	    return false;
	}
	return true;
      }

      case term_function_type: {
	FunctionTypeTerm *cast_term = cast<FunctionTypeTerm>(term);
	std::pair<CheckCompleteMap::iterator, bool> it_pair = functions.insert(std::make_pair(cast_term, 0));
	PSI_ASSERT(it_pair.second);
	for (std::size_t i = 0; i < cast_term->n_parameters(); ++i, ++it_pair.first->second) {
	  if (!check_function_type_complete(cast_term->parameter(i)->type(), functions))
	    return false;
	}
	// this comes after the parameter checking so that the
	// available parameter index is set correctly.
	if (!check_function_type_complete(cast_term->result_type(), functions))
	  return false;
	functions.erase(it_pair.first);
	return true;
      }

      case term_function_type_parameter: {
	FunctionTypeParameterTerm *cast_term = cast<FunctionTypeParameterTerm>(term);
	FunctionTypeTerm* source = cast_term->source();
	if (!source)
	  return false;

	CheckCompleteMap::iterator it = functions.find(source);
	if (it == functions.end())
	  throw TvmUserError("type of function parameter appeared outside of function type definition");

	if (cast_term->index() >= it->second)
	  throw TvmUserError("function parameter used before it is available (index out of range)");

	return true;
      }

      default:
	// all terms should either be amongst the handled cases or complete
	PSI_FAIL("unknown term type");
      }
    }

    class Context::FunctionTypeResolverRewriter {
    public:
      struct FunctionResolveStatus {
	/// Depth of this function
	std::size_t depth;
	/// Index of parameter currently being resolved
	std::size_t index;
      };
      typedef std::tr1::unordered_map<FunctionTypeTerm*, FunctionResolveStatus> FunctionResolveMap;

      FunctionTypeResolverRewriter(std::size_t depth, FunctionResolveMap *functions)
	: m_depth(depth), m_functions(functions) {
      }

      class FunctionTypeParameterListCallback {
      public:
	FunctionTypeParameterListCallback(const FunctionTypeResolverRewriter *self, FunctionResolveStatus *status)
	  : m_self(self), m_status(status) {}

	Term* operator () (Term* term, ArrayPtr<Term*const> previous) const {
	  m_status->index = previous.size();
	  FunctionTypeParameterTerm *param = cast<FunctionTypeParameterTerm>(term);	  
	  return (*m_self)(param->type());
	}

      private:
	const FunctionTypeResolverRewriter *m_self;
	FunctionResolveStatus *m_status;
      };

      Term* operator () (Term* term) const {
	if (!term->parameterized())
	  return term;

	switch(term->term_type()) {
	case term_function_type: {
	  FunctionTypeTerm *cast_term = cast<FunctionTypeTerm>(term);
	  PSI_ASSERT_MSG(m_functions->find(cast_term) == m_functions->end(), "recursive function types not supported");
	  FunctionResolveStatus& status = (*m_functions)[cast_term];
	  status.depth = m_depth + 1;
	  status.index = 0;

	  FunctionTypeResolverRewriter child(m_depth+1, m_functions);
	  ParameterListRewriter<> parameters(cast_term,
                                             FunctionTypeParameterListCallback(&child, &status));
          status.index = cast_term->n_parameters();
	  Term* result_type = child(cast_term->result_type());
	  m_functions->erase(cast_term);

	  return term->context().get_function_type_resolver(result_type, parameters.array(),
                                                            cast_term->n_phantom_parameters(),
                                                            cast_term->calling_convention());
	}

	case term_function_type_parameter: {
	  FunctionTypeParameterTerm *cast_term = cast<FunctionTypeParameterTerm>(term);
	  FunctionTypeTerm* source = cast_term->source();

	  FunctionResolveMap::iterator it = m_functions->find(source);
	  PSI_ASSERT(it != m_functions->end());

	  if (cast_term->index() >= it->second.index)
	    throw TvmInternalError("function type parameter definition refers to value of later parameter");

	  Term* type = (*this)(cast_term->type());

	  return FunctionTypeResolverParameter::get(type, m_depth - it->second.depth, cast_term->index());
	}

	default:
	  return rewrite_term_default(*this, term);
	}
      }

    private:
      std::size_t m_depth;
      FunctionResolveMap *m_functions;
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
      for (std::size_t i = 0; i < phantom_parameters.size(); ++i)
	PSI_ASSERT(!phantom_parameters[i]->source());
      for (std::size_t i = 0; i < parameters.size(); ++i)
	PSI_ASSERT(!parameters[i]->source());

      FunctionTypeTerm* term = allocate_term(FunctionTypeTerm::Initializer(result_type, phantom_parameters, parameters, calling_convention));

      for (std::size_t i = 0; i < phantom_parameters.size(); ++i) {
        phantom_parameters[i]->m_index = i;
        phantom_parameters[i]->set_source(term);
      }

      for (std::size_t i = 0; i < parameters.size(); ++i) {
	parameters[i]->m_index = i + phantom_parameters.size();
	parameters[i]->set_source(term);
      }

      // it's only possible to merge complete types, since incomplete
      // types depend on higher up terms which have not yet been
      // built.
      CheckCompleteMap check_functions;
      if (!check_function_type_complete(term, check_functions))
	return term;

      term->m_parameterized = false;

      FunctionTypeResolverRewriter::FunctionResolveMap functions;
      FunctionTypeResolverRewriter::FunctionResolveStatus& status = functions[term];
      status.depth = 0;
      status.index = 0;

      FunctionTypeResolverRewriter rewriter(0, &functions);
      ParameterListRewriter<> internal_parameters(term, FunctionTypeResolverRewriter::FunctionTypeParameterListCallback(&rewriter, &status));
      PSI_ASSERT(parameters.size() + phantom_parameters.size() == internal_parameters.size());
      status.index = internal_parameters.size();
      Term* internal_result_type = rewriter(term->result_type());
      PSI_ASSERT((functions.erase(term), functions.empty()));

      FunctionTypeResolverTerm* internal = get_function_type_resolver(internal_result_type, internal_parameters.array(),
                                                                      term->n_phantom_parameters(), calling_convention);
      if (internal->get_function_type()) {
	// A matching type exists
	return internal->get_function_type();
      } else {
	internal->set_function_type(term);
	return term;
      }
    }

    /**
     * \brief Get a function type with fixed argument types.
     */
    FunctionTypeTerm* Context::get_function_type_fixed(CallingConvention calling_convention,
                                                       Term* result,
                                                       ArrayPtr<Term*const> parameter_types) {
      ScopedTermPtrArray<FunctionTypeParameterTerm> parameters(parameter_types.size());
      for (std::size_t i = 0; i < parameter_types.size(); ++i)
	parameters[i] = new_function_type_parameter(parameter_types[i]);
      return get_function_type(calling_convention, result, ArrayPtr<FunctionTypeParameterTerm*const>(), parameters.array());
    }

    namespace {
      class ParameterTypeRewriter {
      public:
	ParameterTypeRewriter(const FunctionTypeTerm *function, ArrayPtr<Term*const> previous)
	  : m_function(function), m_previous(previous), m_base(this) {}

	Term* operator () (Term* term) const {
          if (term->term_type() == term_function_type_parameter) {
	    FunctionTypeParameterTerm *cast_term = cast<FunctionTypeParameterTerm>(term);
	    if (cast_term->source() == m_function) {
	      return m_previous[cast_term->index()];
	    } else {
              return m_base(term);
	    }
	  } else {
            return m_base(term);
          }
	}

      private:
	const FunctionTypeTerm *m_function;
	ArrayPtr<Term*const> m_previous;
        mutable TermRewriter<ParameterTypeRewriter> m_base;
      };
    }

    /**
     * Get the type of a parameter, given previous parameters.
     *
     * \param previous Earlier parameters. Length of this array gives
     * the index of the parameter type to get.
     */
    Term* FunctionTypeTerm::parameter_type_after(ArrayPtr<Term*const> previous) {
      ParameterTypeRewriter rewriter(this, previous);
      return rewriter(parameter(previous.size())->type());
    }

    /**
     * Get the return type of a function of this type, given previous
     * parameters.
     */
    Term* FunctionTypeTerm::result_type_after(ArrayPtr<Term*const> parameters) {
      if (parameters.size() != n_parameters())
	throw TvmUserError("incorrect number of parameters");
      ParameterTypeRewriter rewriter(this, parameters);
      return rewriter(result_type());
    }

    FunctionTypeParameterTerm::FunctionTypeParameterTerm(const UserInitializer& ui, Context *context, Term* type)
      : Term(ui, context, term_function_type_parameter, type->abstract(), true, false, type->source(), type),
	m_index(0) {
    }

    class FunctionTypeParameterTerm::Initializer : public InitializerBase<FunctionTypeParameterTerm> {
    public:
      Initializer(Term* type) : m_type(type) {}

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

    FunctionTypeResolverTerm::FunctionTypeResolverTerm(const UserInitializer& ui, Context *context, std::size_t hash, Term* result_type, ArrayPtr<Term*const> parameter_types, std::size_t n_phantom, CallingConvention calling_convention)
      : HashTerm(ui, context, term_function_type_resolver,
		 result_type->abstract() || any_abstract(parameter_types), true, false,
                 common_source(result_type->source(), common_source(parameter_types)),
		 Metatype::get(*context), hash),
        m_n_phantom(n_phantom),
	m_calling_convention(calling_convention) {
      set_base_parameter(1, result_type);
      for (std::size_t i = 0; i < parameter_types.size(); i++)
	set_base_parameter(i+2, parameter_types[i]);
    }

    class FunctionTypeResolverTerm::Setup : public InitializerBase<FunctionTypeResolverTerm> {
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

      FunctionTypeResolverTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
	return new (base) FunctionTypeResolverTerm(ui, context, m_hash, m_result_type, m_parameter_types, m_n_phantom, m_calling_convention);
      }

      std::size_t hash() const {
	return m_hash;
      }

      std::size_t n_uses() const {
	return m_parameter_types.size() + 2;
      }

      bool equals(HashTerm *term) const {
	if ((m_hash != term->m_hash) || (term->term_type() != term_function_type_resolver))
	  return false;

	FunctionTypeResolverTerm *cast_term = cast<FunctionTypeResolverTerm>(term);

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

    FunctionTypeResolverTerm* Context::get_function_type_resolver(Term* result, ArrayPtr<Term*const> parameter_types,
                                                                  std::size_t n_phantom, CallingConvention calling_convention) {
      FunctionTypeResolverTerm::Setup setup(result, parameter_types, n_phantom, calling_convention);
      return hash_term_get(setup);
    }

#if 0
    FunctionalTypeResult FunctionTypeResolverParameter::type(Context&, const Data&, ArrayPtr<Term*const> parameters) {
      PSI_ASSERT(parameters.size() == 1);
      return FunctionalTypeResult(parameters[0], false);
    }
#endif

    InstructionTerm::InstructionTerm(const UserInitializer& ui, Context *context,
				     Term* type, const char *operation,
                                     ArrayPtr<Term*const> parameters,
                                     BlockTerm* block)
      : Term(ui, context, term_instruction,
	     false, false, false, block, type),
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
     * Checks whether block \c a dominates block \c b. Handles cases
     * where \c a or \c b are NULL correctly.
     */
    bool block_dominates(BlockTerm *a, BlockTerm *b) {
      if (!a)
        return true;

      if (!b)
        return false;

      return b->dominated_by(a);
    }

    /**
     * Check if a term (i.e. all variables required by it) are
     * available in this block. If this block has not been added to a
     * function, the \c function parameter will be assigned to the
     * function it must be added to in order for the term to be
     * available, otherwise \c function will be set to the function
     * this block is part of.
     */
    bool BlockTerm::check_available(Term* term) {
      if (term->global())
        return true;

      PSI_ASSERT(!term->abstract() && !term->parameterized());

      switch (term->term_type()) {
      case term_functional: {
        FunctionalTerm* cast_term = cast<FunctionalTerm>(term);
        std::size_t n = cast_term->n_parameters();
        for (std::size_t i = 0; i < n; ++i) {
          if (!check_available(cast_term->parameter(i)))
            return false;
        }
        return true;
      }

      case term_instruction:
        return dominated_by(cast<InstructionTerm>(term)->block());

      case term_phi:
        return dominated_by(cast<PhiTerm>(term)->block());

      case term_function_parameter:
        return cast<FunctionParameterTerm>(term)->function() == function();

      case term_block:
        return cast<BlockTerm>(term)->function() == function();

      default:
	throw TvmUserError("unexpected term type");
      }
    }

    /**
     * \brief Get blocks that can run immediately after this one.
     */
    std::vector<BlockTerm*> BlockTerm::successors() {
      std::vector<BlockTerm*> targets;
      for (InstructionList::const_iterator it = m_instructions.begin();
           it != m_instructions.end(); ++it) {
        m_instructions.back().jump_targets(targets);
      }
      return targets;
    }

    /**
     * \brief Get blocks that can run between this one and the end of
     * the function (including this one).
     */
    std::vector<BlockTerm*> BlockTerm::recursive_successors() {
      std::tr1::unordered_set<BlockTerm*> all_blocks;
      std::vector<BlockTerm*> queue;
      all_blocks.insert(const_cast<BlockTerm*>(this));
      queue.push_back(const_cast<BlockTerm*>(this));

      for (std::size_t queue_pos = 0; queue_pos != queue.size(); ++queue_pos) {
        BlockTerm *bl = queue[queue_pos];

        if (bl->terminated()) {
          std::vector<BlockTerm*> successors = bl->successors();
          for (std::vector<BlockTerm*>::iterator it = successors.begin();
               it != successors.end(); ++it) {
            std::pair<std::tr1::unordered_set<BlockTerm*>::iterator, bool> r = all_blocks.insert(*it);
            if (r.second)
              queue.push_back(*it);
          }
        }
      }

      return queue;
    }

    /**
     * \brief Get a list of blocks dominated by this one.
     */
    std::vector<BlockTerm*> BlockTerm::dominated_blocks() {
      BlockTerm *self = const_cast<BlockTerm*>(this);
      std::vector<BlockTerm*> result;
      for (TermIterator<BlockTerm> it = self->term_users_begin<BlockTerm>();
           it != self->term_users_end<BlockTerm>(); ++it) {
        if (this == it->dominator())
          result.push_back(it.get_ptr());
      }
      std::sort(result.begin(), result.end());
      std::vector<BlockTerm*>::iterator last = std::unique(result.begin(), result.end());
      result.erase(last, result.end());
      return result;
    }

    InstructionTerm* BlockTerm::new_instruction_bare(const InstructionTermSetup& setup, ArrayPtr<Term*const> parameters) {
      if (m_terminated)
        throw TvmUserError("cannot add instruction to already terminated block");

      // Check parameters are valid and adjust dominator blocks
      for (std::size_t i = 0; i < parameters.size(); ++i) {
        Term *param = parameters[i];
        if (param->abstract() || param->parameterized())
          throw TvmUserError("instructions cannot accept abstract parameters");
        if (!check_available(param))
          throw TvmUserError("parameter value is not available in this block");
      }

      Term* type = setup.type(function(), parameters);
      bool terminator = false;
      if (!type) {
        terminator = true;
        type = EmptyType::get(context());
      }

      InstructionTerm* insn = context().allocate_term(InstructionTerm::Initializer(type, parameters, &setup, this));

      // End-of-block: need to find where we could be jumping to
      std::vector<BlockTerm*> jump_targets;
      insn->jump_targets(jump_targets);
      for (std::vector<BlockTerm*>::iterator it = jump_targets.begin();
           it != jump_targets.end(); ++it) {
        if (!dominated_by((*it)->dominator()))
          throw TvmUserError("instruction jump target dominator block may not have run");
      }

      m_instructions.push_back(*insn);
      if (terminator)
        m_terminated = true;

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
      : Term(ui, context, term_phi, false, false, false, block, type),
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

    BlockTerm::BlockTerm(const UserInitializer& ui, Context *context, FunctionTerm* function, BlockTerm* dominator)
      : Term(ui, context, term_block, false, false, false, function, BlockType::get(*context)),
        m_terminated(false) {

      set_base_parameter(0, function);
      set_base_parameter(1, dominator);
    }

    class BlockTerm::Initializer : public InitializerBase<BlockTerm> {
    public:
      Initializer(FunctionTerm* function, BlockTerm* dominator)
        : m_function(function), m_dominator(dominator) {
      }

      BlockTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) BlockTerm(ui, context, m_function, m_dominator);
      }

      std::size_t n_uses() const {
	return 2;
      }

    private:
      FunctionTerm* m_function;
      BlockTerm* m_dominator;
    };

    FunctionParameterTerm::FunctionParameterTerm(const UserInitializer& ui, Context *context, FunctionTerm* function, Term* type, bool phantom)
      : Term(ui, context, term_function_parameter, false, false, phantom, function, type) {
      PSI_ASSERT(!type->parameterized() && !type->abstract());
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

    FunctionTerm::FunctionTerm(const UserInitializer& ui, Context *context, FunctionTypeTerm* type, const std::string& name)
      : GlobalTerm(ui, context, term_function, type, name) {
      ScopedTermPtrArray<> parameters(type->n_parameters());
      for (std::size_t i = 0; i < parameters.size(); ++i) {
	Term* param_type = type->parameter_type_after(parameters.array().slice(0, i));
	FunctionParameterTerm* param = context->allocate_term(FunctionParameterTerm::Initializer(this, param_type, i<type->n_phantom_parameters()));
        set_base_parameter(i+2, param);
	parameters[i] = param;
      }

      set_base_parameter(1, type->result_type_after(parameters.array()));
    }

    class FunctionTerm::Initializer : public InitializerBase<FunctionTerm> {
    public:
      Initializer(FunctionTypeTerm* type, const std::string& name) : m_type(type), m_name(name) {
      }

      FunctionTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) FunctionTerm(ui, context, m_type, m_name);
      }

      std::size_t n_uses() const {
	return m_type->n_parameters() + 2;
      }

    private:
      FunctionTypeTerm* m_type;
      std::string m_name;
    };

    /**
     * \brief Create a new function.
     */
    FunctionTerm* Context::new_function(FunctionTypeTerm* type, const std::string& name) {
      return allocate_term(FunctionTerm::Initializer(type, name));
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
      
      set_base_parameter(0, block);
    }

    /**
     * \brief Create a new block.
     */
    BlockTerm* FunctionTerm::new_block(BlockTerm* dominator) {
      return context().allocate_term(BlockTerm::Initializer(this, dominator));
    }

    /**
     * \brief Create a new block.
     */
    BlockTerm* FunctionTerm::new_block() {
      return new_block(NULL);
    }

    /**
     * Add a name for a term within this function.
     */
    void FunctionTerm::add_term_name(Term *term, const std::string& name) {
      m_name_map.insert(std::make_pair(term, name));
    }
  }
}
