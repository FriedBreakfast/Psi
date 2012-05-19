#include "Core.hpp"
#include "Recursive.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Utility.hpp"

namespace Psi {
  namespace Tvm {
    RecursiveParameterTerm::RecursiveParameterTerm(const UserInitializer& ui, Context *context, Term* type)
      : Term(ui, context, term_recursive_parameter, type->source(), type) {
    }

    class RecursiveParameterTerm::Initializer : public InitializerBase<RecursiveParameterTerm> {
    public:
      Initializer(Term* type) : m_type(type) {}

      RecursiveParameterTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
        return new (base) RecursiveParameterTerm(ui, context, m_type);
      }

      std::size_t n_uses() const {return 0;}

    private:
      Term* m_type;
    };

    /**
     * \brief Create a new parameter for a recursive term.
     *
     * \param type The term's type.
     *
     * \param phantom Whether this term should be created as a phantom
     * term. This mechanism is used to inform the compiler which
     * parameters can have phantom values in them without making the
     * overall value a phantom (unless it is always a phantom).
     */
    RecursiveParameterTerm* Context::new_recursive_parameter(Term* type) {
      return allocate_term(RecursiveParameterTerm::Initializer(type));
    }

    RecursiveTerm::RecursiveTerm(const UserInitializer& ui, Context *context, Term* result_type,
                                 Term *source, ArrayPtr<RecursiveParameterTerm*const> parameters)
      : Term(ui, context, term_recursive, source, result_type) {
      for (std::size_t i = 0; i < parameters.size(); ++i)
        set_base_parameter(i+1, parameters[i]);
    }

    class RecursiveTerm::Initializer : public InitializerBase<RecursiveTerm> {
    public:
      Initializer(Term *source, Term* type, ArrayPtr<RecursiveParameterTerm*const> parameters)
        : m_source(source), m_type(type), m_parameters(parameters) {
      }

      RecursiveTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
        return new (base) RecursiveTerm(ui, context, m_type, m_source, m_parameters);
      }

      std::size_t n_uses() const {return m_parameters.size() + 1;}

    private:
      Term *m_source;
      Term *m_type;
      ArrayPtr<RecursiveParameterTerm*const> m_parameters;
    };

    /**
     * \brief Create a new recursive term.
     *
     * \param phantom Whether all applications of this term are
     * considered phantom; in this case the value assigned to this
     * term may itself be a phantom.
     */
    RecursiveTerm* Context::new_recursive(Term *source,
                                          Term* result_type,
                                          ArrayPtr<Term*const> parameter_types) {
      if (source_dominated(result_type->source(), source))
        goto throw_dominator;

      for (std::size_t i = 0; i < parameter_types.size(); ++i) {
        if (source_dominated(parameter_types[i]->source(), source))
          goto throw_dominator;
      }

      if (true) {
        ScopedArray<RecursiveParameterTerm*> parameters(parameter_types.size());
        for (std::size_t i = 0; i < parameters.size(); ++i)
          parameters[i] = new_recursive_parameter(parameter_types[i]);
        return allocate_term(RecursiveTerm::Initializer(source, result_type, parameters));
      } else {
      throw_dominator:
        throw TvmUserError("source specified for recursive term is not dominated by parameter and result type blocks");
      }
    }

    /**
     * \brief Resolve this term to its actual value.
     */
    void RecursiveTerm::resolve(Term* term) {
      return context().resolve_recursive(this, term);
    }

    ApplyTerm* RecursiveTerm::apply(ArrayPtr<Term*const> parameters) {
      return context().apply_recursive(this, parameters);
    }

    ApplyTerm::ApplyTerm(const UserInitializer& ui, Context *context, RecursiveTerm *recursive,
                         ArrayPtr<Term*const> parameters, std::size_t hash)
      : HashTerm(ui, context, term_apply,
                 common_source(recursive->source(), common_source(parameters)),
                 recursive->type(), hash) {
      set_base_parameter(0, recursive);
      for (std::size_t i = 0; i < parameters.size(); ++i)
        set_base_parameter(i+1, parameters[i]);
    }

    class ApplyTerm::Setup : public InitializerBase<ApplyTerm> {
    public:
      Setup(RecursiveTerm *recursive, ArrayPtr<Term*const> parameters)
        : m_recursive(recursive), m_parameters(parameters) {

        m_hash = 0;
        boost::hash_combine(m_hash, recursive->hash_value());
        for (std::size_t i = 0; i < parameters.size(); ++i)
          boost::hash_combine(m_hash, parameters[i]->hash_value());
      }

      void prepare_initialize(Context*) {
      }

      ApplyTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
        return new (base) ApplyTerm(ui, context, m_recursive, m_parameters, m_hash);
      }

      std::size_t n_uses() const {return m_parameters.size() + 1;}
      std::size_t hash() const {return m_hash;}

      bool equals(HashTerm *term) const {
        if ((m_hash != term->m_hash) || (term->term_type() != term_apply))
          return false;

        ApplyTerm *cast_term = cast<ApplyTerm>(term);

        if (m_parameters.size() != cast_term->n_parameters())
          return false;

        for (std::size_t i = 0; i < m_parameters.size(); ++i) {
          if (m_parameters[i] != cast_term->parameter(i))
            return false;
        }

        return true;
      }

    private:
      RecursiveTerm *m_recursive;
      ArrayPtr<Term*const> m_parameters;
      std::size_t m_hash;
    };

    ApplyTerm* Context::apply_recursive(RecursiveTerm* recursive,
                                        ArrayPtr<Term*const> parameters) {
      ApplyTerm::Setup setup(recursive, parameters);
      return hash_term_get(setup);
    }

    Term* ApplyTerm::unpack() {
      PSI_NOT_IMPLEMENTED();
    }

    /**
     * \brief Resolve an opaque term.
     */
    void Context::resolve_recursive(RecursiveTerm* recursive, Term* to) {
      if (recursive->type() != to->type())
        throw TvmUserError("mismatch between recursive term type and resolving term type");

      if (to->parameterized())
        throw TvmUserError("cannot resolve recursive term to parameterized term");

      if (to->term_type() == term_apply)
        throw TvmUserError("cannot resolve recursive term to apply term, since this leads to an infinite loop in the code generator");

      if (recursive->result())
        throw TvmUserError("resolving a recursive term which has already been resolved");

      if (!source_dominated(to->source(), recursive->source()))
        throw TvmUserError("term used to resolve recursive term is not in scope");

      if (to->phantom() && !recursive->phantom())
        throw TvmUserError("non-phantom recursive term cannot be resolved to a phantom term");

      recursive->set_base_parameter(1, to);
    }
  }
}
