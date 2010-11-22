#include "Function.hpp"
#include "Functional.hpp"
#include "Primitive.hpp"
#include "Utility.hpp"
#include "Rewrite.hpp"

#include <stdexcept>

namespace Psi {
  namespace Tvm {
    FunctionTypeTerm::FunctionTypeTerm(const UserInitializer& ui,
				       Context *context,
				       Term *result_type,
				       TermRefArray<FunctionTypeParameterTerm> parameters,
				       CallingConvention calling_convention)
      : Term(ui, context, term_function_type,
	     result_type->abstract() || any_abstract(parameters), true,
	     result_type->global() && all_global(parameters),
	     context->get_metatype().get()),
	m_calling_convention(calling_convention) {
      set_base_parameter(0, result_type);
      for (std::size_t i = 0; i < parameters.size(); ++i) {
	set_base_parameter(i+1, parameters[i]);
      }
    }

    class FunctionTypeTerm::Initializer : public InitializerBase<FunctionTypeTerm> {
    public:
      Initializer(Term *result_type,
		  TermRefArray<FunctionTypeParameterTerm> parameters,
		  CallingConvention calling_convention)
	: m_result_type(result_type),
	  m_parameters(parameters),
	  m_calling_convention(calling_convention) {
      }

      std::size_t n_uses() const {
	return m_parameters.size() + 1;
      }

      FunctionTypeTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) FunctionTypeTerm(ui, context, m_result_type,
					   m_parameters, m_calling_convention);
      }

    private:
      Term *m_result_type;
      TermRefArray<FunctionTypeParameterTerm> m_parameters;
      CallingConvention m_calling_convention;
    };

    /**
     * Check whether part of function type term is complete,
     * i.e. whether there are still function parameters which have
     * to be resolved by further function types (this happens in the
     * case of nested function types).
     */
    bool Context::check_function_type_complete(TermRef<> term, CheckCompleteMap& functions)
    {
      if (!term->parameterized())
	return true;

      if (!check_function_type_complete(term->type(), functions))
	return false;

      switch(term->term_type()) {
      case term_functional: {
	FunctionalTerm *cast_term = checked_cast<FunctionalTerm*>(term.get());
	for (std::size_t i = 0; i < cast_term->n_parameters(); i++) {
	  if (!check_function_type_complete(cast_term->parameter(i), functions))
	    return false;
	}
	return true;
      }

      case term_function_type: {
	FunctionTypeTerm *cast_term = checked_cast<FunctionTypeTerm*>(term.get());
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
	FunctionTypeParameterTerm *cast_term = checked_cast<FunctionTypeParameterTerm*>(term.get());
	TermPtr<FunctionTypeTerm> source = cast_term->source();
	if (!source)
	  return false;

	CheckCompleteMap::iterator it = functions.find(source.get());
	if (it == functions.end())
	  throw std::logic_error("type of function parameter appeared outside of function type definition");

	if (cast_term->index() >= it->second)
	  throw std::logic_error("function parameter used before it is available (index out of range)");

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

	TermPtr<> operator () (TermRef<> term, std::size_t index) const {
	  m_status->index = index;
	  FunctionTypeParameterTerm *param = checked_cast<FunctionTypeParameterTerm*>(term.get());	  
	  return (*m_self)(param->type());
	}

      private:
	const FunctionTypeResolverRewriter *m_self;
	FunctionResolveStatus *m_status;
      };

      TermPtr<> operator () (TermRef<> term) const {
	if (!term->parameterized())
	  return TermPtr<>(term.get());

	switch(term->term_type()) {
	case term_function_type: {
	  FunctionTypeTerm *cast_term = checked_cast<FunctionTypeTerm*>(term.get());
	  PSI_ASSERT(m_functions->find(cast_term) == m_functions->end());
	  FunctionResolveStatus& status = (*m_functions)[cast_term];
	  status.depth = m_depth + 1;
	  status.index = 0;

	  FunctionTypeResolverRewriter child(m_depth+1, m_functions);
	  ParameterListRewriter parameters(TermRef<FunctionTypeTerm>(cast_term),
					   FunctionTypeParameterListCallback(&child, &status));
	  TermPtr<> result_type = child(term);
	  m_functions->erase(cast_term);

	  return term->context().get_function_type_resolver(result_type, parameters, cast_term->calling_convention());
	}

	case term_function_type_parameter: {
	  FunctionTypeParameterTerm *cast_term = checked_cast<FunctionTypeParameterTerm*>(term.get());
	  TermPtr<FunctionTypeTerm> source = cast_term->source();

	  FunctionResolveMap::iterator it = m_functions->find(source.get());
	  PSI_ASSERT(it != m_functions->end());

	  if (cast_term->index() >= it->second.index)
	    throw std::logic_error("function type parameter definition refers to value of later parameter");

	  TermPtr<> type = (*this)(cast_term->type());

	  return term->context().get_function_type_resolver_parameter(type, m_depth - it->second.depth, cast_term->index());
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
     */
    TermPtr<FunctionTypeTerm> Context::get_function_type(CallingConvention calling_convention,
							 TermRef<> result_type,
							 TermRefArray<FunctionTypeParameterTerm> parameters) {
      for (std::size_t i = 0; i < parameters.size(); ++i)
	PSI_ASSERT(!parameters[i]->source());

      TermPtr<FunctionTypeTerm> term = allocate_term(FunctionTypeTerm::Initializer(result_type.get(), parameters, calling_convention));

      for (std::size_t i = 0; i < parameters.size(); ++i) {
	parameters[i]->m_index = i;
	parameters[i]->set_source(term.get());
      }

      // it's only possible to merge complete types, since incomplete
      // types depend on higher up terms which have not yet been
      // built.
      CheckCompleteMap check_functions;
      if (!check_function_type_complete(term.get(), check_functions))
	return term;

      term->m_parameterized = false;

      FunctionTypeResolverRewriter::FunctionResolveMap functions;
      FunctionTypeResolverRewriter::FunctionResolveStatus& status = functions[term.get()];
      status.depth = 0;
      status.index = 0;

      FunctionTypeResolverRewriter rewriter(0, &functions);
      ParameterListRewriter internal_parameters(TermRef<FunctionTypeTerm>(term), FunctionTypeResolverRewriter::FunctionTypeParameterListCallback(&rewriter, &status));
      PSI_ASSERT(parameters.size() == internal_parameters.size());
      TermPtr<> internal_result_type = rewriter(term->result_type());
      PSI_ASSERT((functions.erase(term.get()), functions.empty()));

      TermPtr<FunctionTypeResolverTerm> internal = get_function_type_resolver(internal_result_type, internal_parameters, calling_convention);
      if (internal->get_function_type()) {
	// A matching type exists
	return TermPtr<FunctionTypeTerm>(internal->get_function_type());
      } else {
	internal->set_function_type(term.get());
	return term;
      }
    }

    /**
     * \brief Get a function type with fixed argument types.
     */
    TermPtr<FunctionTypeTerm> Context::get_function_type_fixed(CallingConvention calling_convention,
							       TermRef<> result,
							       TermRefArray<> parameter_types) {
      TermPtrArray<FunctionTypeParameterTerm> parameters(parameter_types.size());
      for (std::size_t i = 0; i < parameter_types.size(); ++i)
	parameters.set(i, new_function_type_parameter(parameter_types[i]));
      return get_function_type(calling_convention, result, parameters);
    }

    namespace {
      class ParameterTypeRewriter {
      public:
	ParameterTypeRewriter(const FunctionTypeTerm *function, TermRefArray<> previous)
	  : m_function(function), m_previous(previous) {}

	TermPtr<> operator () (TermRef<> term) const {
	  switch (term->term_type()) {
	  case term_function_type_parameter: {
	    FunctionTypeParameterTerm *cast_term = checked_cast<FunctionTypeParameterTerm*>(term.get());
	    if (cast_term->source().get() == m_function) {
	      std::size_t n = cast_term->index();
	      PSI_ASSERT(n < m_previous.size());
	      return TermPtr<>(m_previous[n]);
	    } else {
	      return TermPtr<>(term.get());
	    }	      
	  }

	  default:
	    return rewrite_term_default(*this, term);
	  }
	}

      private:
	const FunctionTypeTerm *m_function;
	TermRefArray<> m_previous;
      };
    }

    /**
     * Get the type of a parameter, given previous parameters.
     *
     * \param previous Earlier parameters. Length of this array gives
     * the index of the parameter type to get.
     */
    TermPtr<> FunctionTypeTerm::parameter_type_after(TermRefArray<> previous) const {
      ParameterTypeRewriter rewriter(this, previous);
      return rewriter(parameter(previous.size())->type());
    }

    /**
     * Get the return type of a function of this type, given previous
     * parameters.
     */
    TermPtr<> FunctionTypeTerm::result_type_after(TermRefArray<> parameters) const {
      if (parameters.size() != n_parameters())
	throw std::logic_error("incorrect number of parameters");
      ParameterTypeRewriter rewriter(this, parameters);
      return rewriter(result_type());
    }

    FunctionTypeParameterTerm::FunctionTypeParameterTerm(const UserInitializer& ui, Context *context, TermRef<> type)
      : Term(ui, context, term_function_type_parameter, type->abstract(), true, type->global(), type),
	m_index(0) {
    }

    class FunctionTypeParameterTerm::Initializer : public InitializerBase<FunctionTypeParameterTerm> {
    public:
      Initializer(TermRef<> type) : m_type(type) {}

      std::size_t n_uses() const {
	return 1;
      }

      FunctionTypeParameterTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) FunctionTypeParameterTerm(ui, context, m_type);
      }

    private:
      TermRef<> m_type;
    };

    TermPtr<FunctionTypeParameterTerm> Context::new_function_type_parameter(TermRef<> type) {
      return allocate_term(FunctionTypeParameterTerm::Initializer(type.get()));
    }

    FunctionTypeResolverTerm::FunctionTypeResolverTerm(const UserInitializer& ui, Context *context, std::size_t hash, TermRef<> result_type, TermRefArray<> parameter_types, CallingConvention calling_convention)
      : HashTerm(ui, context, term_function_type_resolver,
		 result_type->abstract() || any_abstract(parameter_types), true,
		 result_type->global() && all_global(parameter_types),
		 context->get_metatype().get(), hash),
	m_calling_convention(calling_convention) {
      set_base_parameter(1, result_type);
      for (std::size_t i = 0; i < parameter_types.size(); i++)
	set_base_parameter(i+2, parameter_types[i]);
    }

    class FunctionTypeResolverTerm::Setup : public InitializerBase<FunctionTypeResolverTerm> {
    public:
      Setup(TermRef<> result_type, TermRefArray<> parameter_types,
	    CallingConvention calling_convention)
	: m_parameter_types(parameter_types),
	  m_result_type(result_type),
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
	return new (base) FunctionTypeResolverTerm(ui, context, m_hash, m_result_type, m_parameter_types, m_calling_convention);
      }

      std::size_t hash() const {
	return m_hash;
      }

      std::size_t n_uses() const {
	return m_parameter_types.size() + 2;
      }

      bool equals(const HashTerm& term) const {
	if ((m_hash != term.m_hash) || (term.term_type() != term_function_type_resolver))
	  return false;

	const FunctionTypeResolverTerm& cast_term =
	  checked_cast<const FunctionTypeResolverTerm&>(term);

	if (m_parameter_types.size() != cast_term.n_parameters())
	  return false;

	for (std::size_t i = 0; i < m_parameter_types.size(); ++i) {
	  if (m_parameter_types[i] != cast_term.parameter_type(i).get())
	    return false;
	}

	if (m_result_type.get() != cast_term.result_type().get())
	  return false;

	if (m_calling_convention != cast_term.calling_convention())
	  return false;

	return true;
      }

    private:
      std::size_t m_hash;
      TermRefArray<> m_parameter_types;
      TermRef<> m_result_type;
      CallingConvention m_calling_convention;
    };

    TermPtr<FunctionTypeResolverTerm> Context::get_function_type_resolver(TermRef<> result, TermRefArray<> parameter_types, CallingConvention calling_convention) {
      FunctionTypeResolverTerm::Setup setup(result, parameter_types, calling_convention);
      return hash_term_get(setup);
    }

    FunctionTypeResolverParameter::FunctionTypeResolverParameter(std::size_t depth, std::size_t index)
      : m_depth(depth), m_index(index) {
    }

    TermPtr<> FunctionTypeResolverParameter::type(Context&, TermRefArray<> parameters) const {
      PSI_ASSERT(parameters.size() == 1);
      return TermPtr<>(parameters[0]);
    }

    LLVMValue FunctionTypeResolverParameter::llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm&) const {
      throw std::logic_error("resolver parameter should never interact with LLVM");
    }

    LLVMValue FunctionTypeResolverParameter::llvm_value_constant(LLVMValueBuilder&, FunctionalTerm&) const {
      throw std::logic_error("resolver parameter should never interact with LLVM");
    }

    LLVMType FunctionTypeResolverParameter::llvm_type(LLVMValueBuilder&, Term&) const {
      throw std::logic_error("resolver parameter should never interact with LLVM");
    }

    bool FunctionTypeResolverParameter::operator == (const FunctionTypeResolverParameter& o) const {
      return (m_depth == o.m_depth) && (m_index == o.m_index);
    }

    std::size_t hash_value(const FunctionTypeResolverParameter& s) {
      std::size_t h = 0;
      boost::hash_combine(h, s.m_depth);
      boost::hash_combine(h, s.m_index);
      return h;
    }

    FunctionalTermPtr<FunctionTypeResolverParameter> Context::get_function_type_resolver_parameter(TermRef<> type, std::size_t depth, std::size_t index) {
      return get_functional_v(FunctionTypeResolverParameter(depth, index), type);
    }

    InstructionTerm::InstructionTerm(const UserInitializer& ui, Context *context,
				     TermRef<> type, TermRefArray<> parameters,
				     InstructionTermBackend *backend,
                                     TermRef<BlockTerm> block)
      : Term(ui, context, term_instruction,
	     false, false, false, type),
	m_backend(backend) {

      set_base_parameter(0, block);

      for (std::size_t i = 0; i < parameters.size(); ++i)
	set_base_parameter(i+1, parameters[i]);
    }

    InstructionTerm::~InstructionTerm() {
      m_backend->~InstructionTermBackend();
    }

    class InstructionTerm::Initializer {
    public:
      typedef InstructionTerm TermType;

      Initializer(TermRef<> type,
		  TermRefArray<> parameters,
		  const InstructionTermBackend *backend,
                  TermRef<BlockTerm> block,
		  std::size_t proto_offset,
		  std::size_t size)
	: m_type(type),
	  m_parameters(parameters),
	  m_backend(backend),
          m_block(block),
	  m_proto_offset(proto_offset),
	  m_size(size) {
      }

      InstructionTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
	InstructionTermBackend *new_backend = m_backend->clone(ptr_offset(base, m_proto_offset));
	try {
	  return new (base) InstructionTerm(ui, context, m_type, m_parameters, new_backend, m_block);
	} catch(...) {
	  new_backend->~InstructionTermBackend();
	  throw;
	}
      }

      std::size_t term_size() const {
	return m_size;
      }

      std::size_t n_uses() const {
	return m_parameters.size() + 1;
      }

    private:
      TermRef<> m_type;
      TermRefArray<> m_parameters;
      const InstructionTermBackend *m_backend;
      TermRef<BlockTerm> m_block;
      std::size_t m_proto_offset;
      std::size_t m_size;
    };

    /**
     * \brief Check whether this block is dominated by another.
     *
     * If \c block is NULL, this will return true since a NULL
     * dominator block refers to the function entry, i.e. before the
     * entry block is run, and therefore eveything is dominated by it.
     */
    bool BlockTerm::dominated_by(TermRef<BlockTerm> block) const {
      if (!block.get())
        return true;

      for (TermPtr<const BlockTerm> b(this); b; b = b->dominator()) {
        if (block.get() == b.get())
          return true;
      }
      return false;
    }

    /**
     * Check if a term (i.e. all variables required by it) are
     * available in this block. If this block has not been added to a
     * function, the \c function parameter will be assigned to the
     * function it must be added to in order for the term to be
     * available, otherwise \c function will be set to the function
     * this block is part of.
     */
    bool BlockTerm::check_available(TermRef<> term) const {
      if (term->global())
        return true;

      PSI_ASSERT(!term->abstract() && !term->parameterized());

      switch (term->term_type()) {
      case term_functional: {
        FunctionalTerm* cast_term = checked_cast<FunctionalTerm*>(term.get());
        std::size_t n = cast_term->n_parameters();
        for (std::size_t i = 0; i < n; ++i) {
          if (!check_available(cast_term->parameter(i)))
            return false;
        }
        return true;
      }

      case term_instruction:
        return dominated_by(checked_cast<InstructionTerm*>(term.get())->block());

      case term_phi:
        return dominated_by(checked_cast<PhiTerm*>(term.get())->block());

      case term_function_parameter:
        return checked_cast<FunctionParameterTerm*>(term.get())->function() == function();

      case term_block:
        return checked_cast<BlockTerm*>(term.get())->function() == function();

      default:
	throw std::logic_error("unexpected term type");
      }
    }

    /**
     * \brief Get blocks that can run immediately after this one.
     */
    std::vector<TermPtr<BlockTerm> > BlockTerm::successors() const {
      std::vector<TermPtr<BlockTerm> > targets;
      for (InstructionList::const_iterator it = m_instructions.begin();
           it != m_instructions.end(); ++it) {
        InstructionTerm& insn = const_cast<InstructionTerm&>(m_instructions.back());
        insn.backend()->jump_targets(context(), insn, targets);
      }
      return targets;
    }

    /**
     * \brief Get blocks that can run between this one and the end of
     * the function (including this one).
     */
    std::vector<TermPtr<BlockTerm> > BlockTerm::recursive_successors() const {
      std::tr1::unordered_set<BlockTerm*> all_blocks;
      std::vector<TermPtr<BlockTerm> > queue;
      all_blocks.insert(const_cast<BlockTerm*>(this));
      queue.push_back(TermPtr<BlockTerm>(const_cast<BlockTerm*>(this)));

      for (std::size_t queue_pos = 0; queue_pos != queue.size(); ++queue_pos) {
        BlockTerm *bl = queue[queue_pos].get();

        if (bl->terminated()) {
          std::vector<TermPtr<BlockTerm> > successors = bl->successors();
          for (std::vector<TermPtr<BlockTerm> >::iterator it = successors.begin();
               it != successors.end(); ++it) {
            std::pair<std::tr1::unordered_set<BlockTerm*>::iterator, bool> r = all_blocks.insert(it->get());
            if (r.second)
              queue.push_back(*it);
          }
        }
      }

      return queue;
    }

    TermPtr<InstructionTerm> BlockTerm::new_instruction_internal(const InstructionTermBackend& backend, TermRefArray<> parameters) {
      if (m_terminated)
        throw std::logic_error("cannot add instruction to already terminated block");

      // Check parameters are valid and adjust dominator blocks
      for (std::size_t i = 0; i < parameters.size(); ++i) {
        Term *param = parameters[i];
        if (param->abstract() || param->parameterized())
          throw std::logic_error("instructions cannot accept abstract parameters");
        if (!check_available(param))
          throw std::logic_error("parameter value is not available in this block");
      }

      std::pair<std::size_t, std::size_t> backend_size_align = backend.size_align();
      PSI_ASSERT_MSG((backend_size_align.second & (backend_size_align.second - 1)) == 0, "alignment is not a power of two");
      std::size_t proto_offset = struct_offset(0, sizeof(InstructionTerm), backend_size_align.second);
      std::size_t size = proto_offset + backend_size_align.first;

      TermPtr<> type = backend.type(context(), *function(), parameters);
      bool terminator = false;
      if (!type) {
        terminator = true;
        type = context().get_empty_type();
      }

      TermPtr<InstructionTerm> insn = context().allocate_term(InstructionTerm::Initializer(type, parameters, &backend, this, proto_offset, size));

      // End-of-block: need to find where we could be jumping to
      std::vector<TermPtr<BlockTerm> > jump_targets;
      backend.jump_targets(context(), *insn, jump_targets);
      for (std::vector<TermPtr<BlockTerm> >::iterator it = jump_targets.begin();
           it != jump_targets.end(); ++it) {
        if (!dominated_by((*it)->dominator()))
          throw std::logic_error("instruction jump target dominator block may not have run");
      }

      m_instructions.push_back(*insn);
      if (terminator)
        m_terminated = true;

      return insn;
    }

    BlockTerm::BlockTerm(const UserInitializer& ui, Context *context, TermRef<FunctionTerm> function, TermRef<BlockTerm> dominator)
      : Term(ui, context, term_block, false, false, false, context->get_block_type()),
        m_terminated(false) {

      set_base_parameter(0, function);
      set_base_parameter(1, dominator);
    }

    class BlockTerm::Initializer : public InitializerBase<BlockTerm> {
    public:
      Initializer(TermRef<FunctionTerm> function, TermRef<BlockTerm> dominator)
        : m_function(function), m_dominator(dominator) {
      }

      BlockTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) BlockTerm(ui, context, m_function, m_dominator);
      }

      std::size_t n_uses() const {
	return 2;
      }

    private:
      TermRef<FunctionTerm> m_function;
      TermRef<BlockTerm> m_dominator;
    };

    FunctionParameterTerm::FunctionParameterTerm(const UserInitializer& ui, Context *context, TermRef<FunctionTerm> function, TermRef<> type)
      : Term(ui, context, term_function_parameter, false, false, false, type) {
      PSI_ASSERT(!type->parameterized() && !type->abstract());
      set_base_parameter(0, function);
    }

    class FunctionParameterTerm::Initializer : public InitializerBase<FunctionParameterTerm> {
    public:
      Initializer(TermRef<FunctionTerm> function, TermRef<> type)
	: m_function(function), m_type(type) {
      }

      std::size_t n_uses() const {
	return 1;
      }

      FunctionParameterTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
	return new (base) FunctionParameterTerm(ui, context, m_function, m_type);
      }

    private:
      TermRef<FunctionTerm> m_function;
      TermRef<> m_type;
    };

    FunctionTerm::FunctionTerm(const UserInitializer& ui, Context *context, TermRef<FunctionTypeTerm> type)
      : GlobalTerm(ui, context, term_function, type) {
      TermPtrArray<> parameters(type->n_parameters());
      for (std::size_t i = 0; i < parameters.size(); ++i) {
	TermPtr<> param_type = type->parameter_type_after(TermRefArray<>(i, parameters.get()));
	TermPtr<FunctionParameterTerm> param = context->allocate_term(FunctionParameterTerm::Initializer(this, param_type));
        set_base_parameter(i+2, param);
	parameters.set(i, param);
      }

      set_base_parameter(1, type->result_type_after(parameters));
    }

    class FunctionTerm::Initializer : public InitializerBase<FunctionTerm> {
    public:
      Initializer(TermRef<FunctionTypeTerm> type) : m_type(type) {
      }

      FunctionTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) FunctionTerm(ui, context, m_type);
      }

      std::size_t n_uses() const {
	return m_type->n_parameters() + 2;
      }

    private:
      TermRef<FunctionTypeTerm> m_type;
    };

    /**
     * \brief Create a new function.
     */
    TermPtr<FunctionTerm> Context::new_function(TermRef<FunctionTypeTerm> type) {
      return TermPtr<FunctionTerm>(allocate_term(FunctionTerm::Initializer(type)));
    }

    /**
     * Get the signature of this function.
     */
    TermPtr<FunctionTypeTerm> FunctionTerm::function_type() const {
      return checked_term_cast<FunctionTypeTerm>(value_type());
    }

    /**
     * \brief Set the entry point for a function.
     *
     * If a function has no entry point, it will be treated as
     * external. Once an entry point has been set, it cannot be
     * changed.
     */
    void FunctionTerm::set_entry(TermRef<BlockTerm> block) {
      if (entry())
        throw std::logic_error("Cannot change the entry point of a function once it is set");
      
      set_base_parameter(0, block);
    }

    /**
     * \brief Create a new block.
     */
    TermPtr<BlockTerm> FunctionTerm::new_block(TermRef<BlockTerm> dominator) {
      TermPtr<BlockTerm> bl = context().allocate_term(BlockTerm::Initializer(this, dominator));
      return bl;
    }

    /**
     * \brief Create a new block.
     */
    TermPtr<BlockTerm> FunctionTerm::new_block() {
      return new_block(NULL);
    }
  }
}
