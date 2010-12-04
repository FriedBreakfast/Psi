#ifndef HPP_PSI_TVM_FUNCTIONAL
#define HPP_PSI_TVM_FUNCTIONAL

#include "Core.hpp"

namespace Psi {
  namespace Tvm {
    struct FunctionalTypeResult {
      FunctionalTypeResult(Term *type_, bool phantom_)  : type(type_), phantom(phantom_) {}
      Term *type;
      bool phantom;
    };

    /**
     * \brief Base class for building custom FunctionalTerm instances.
     */
    class FunctionalTermBackend : public HashTermBackend {
    public:
      virtual FunctionalTermBackend* clone(void *dest) const = 0;
      virtual FunctionalTypeResult type(Context& context, ArrayPtr<Term*const> parameters) const = 0;

      /**
       * Generate code to calculate the value for this term.
       *
       * \param builder Builder used to get functional values and to
       * create instructions.
       *
       * \param term Term (with parameters) to generate code for.
       *
       * \param result_area Area to store the result of this
       * instruction in. This will be NULL unless the value of this
       * instruction has unknown type.
       */
      virtual LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const = 0;

      virtual LLVMValue llvm_value_constant(LLVMValueBuilder&, FunctionalTerm&) const = 0;
      virtual LLVMType llvm_type(LLVMValueBuilder&, FunctionalTerm&) const = 0;
    };

    /**
     * \brief Base class of functional (machine state independent) terms.
     *
     * Functional terms are special since two terms of the same
     * operation and with the same parameters are equivalent; they are
     * therefore unified into one term so equivalence can be checked
     * via pointer equality. This is particularly required for type
     * checking, but also applies to other terms.
     */
    class FunctionalTerm : public HashTerm {
      friend class Context;

    public:
      const FunctionalTermBackend* backend() const {return m_backend;}
      std::size_t n_parameters() const {return Term::n_base_parameters();}
      Term* parameter(std::size_t n) const {return get_base_parameter(n);}

    private:
      class Setup;
      FunctionalTerm(const UserInitializer& ui, Context *context, Term* type,
		     bool phantom, std::size_t hash, FunctionalTermBackend *backend,
		     ArrayPtr<Term*const> parameters);
      ~FunctionalTerm();

      FunctionalTermBackend *m_backend;
    };

    template<>
    struct TermIteratorCheck<FunctionalTerm> {
      static bool check (TermType t) {
	return t == term_functional;
      }
    };

    /**
     * \brief Implementation of FunctionalTermBackend.
     *
     * Actual implementations of this type should be created by
     * creating a class that this can wrap and getting a context to
     * make the appropriate term.
     */
    template<typename T>
    class FunctionalTermBackendImpl : public FunctionalTermBackend {
    public:
      typedef T ImplType;
      typedef FunctionalTermBackendImpl<T> ThisType;

      FunctionalTermBackendImpl(const ImplType& impl) : m_impl(impl) {
      }

      virtual ~FunctionalTermBackendImpl() {
      }

      virtual std::pair<std::size_t, std::size_t> size_align() const {
        return std::make_pair(sizeof(ThisType), boost::alignment_of<ThisType>::value);
      }

      virtual bool equals(const FunctionalTermBackend& other) const {
	return m_impl == checked_cast<const ThisType&>(other).m_impl;
      }

      virtual FunctionalTermBackend* clone(void *dest) const {
        return new (dest) ThisType(*this);
      }

      virtual FunctionalTypeResult type(Context& context, ArrayPtr<Term*const> parameters) const {
        return m_impl.type(context, parameters);
      }

      virtual LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
        return m_impl.llvm_value_instruction(builder, term);
      }

      virtual LLVMValue llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const {
        return m_impl.llvm_value_constant(builder, term);
      }

      virtual LLVMType llvm_type(LLVMValueBuilder& builder, FunctionalTerm& term) const {
        return m_impl.llvm_type(builder, term);
      }

      const ImplType& impl() const {
        return m_impl;
      }

    private:
      virtual std::size_t hash_internal() const {
        boost::hash<ImplType> hasher;
        return hasher(m_impl);
      }

      ImplType m_impl;
    };

    template<typename T> FunctionalTermPtr<T> checked_cast_functional(FunctionalTerm* src) {
      checked_cast<const FunctionalTermBackendImpl<T>*>(src->backend());
      return FunctionalTermPtr<T>(src);
    }

    template<typename T, typename U> FunctionalTermPtr<T> checked_cast_functional(FunctionalTermPtr<U> src) {
      return checked_cast_functional<T>(src.get());
    }
    
    /**
     * \brief Perform a checked cast to a FunctionalTermPtr. This
     * checks both the term type and the backend type.
     */
    template<typename T> FunctionalTermPtr<T> checked_cast_functional(Term* src) {
      return checked_cast_functional<T>(checked_cast<FunctionalTerm*>(src));
    }

    template<typename T> FunctionalTermPtr<T> dynamic_cast_functional(FunctionalTerm* src) {
      if (!dynamic_cast<const FunctionalTermBackendImpl<T>*>(src->backend()))
        return FunctionalTermPtr<T>();
      return FunctionalTermPtr<T>(src);
    }

    template<typename T, typename U> FunctionalTermPtr<T> dynamic_cast_functional(FunctionalTermPtr<U> src) {
      return dynamic_cast_functional<T>(src.get());
    }

    /**
     * \brief Perform a dynamic cast to a FunctionalTermPtr. This
     * checks both the term type and the backend type.
     */
    template<typename T> FunctionalTermPtr<T> dynamic_cast_functional(Term* src) {
      FunctionalTerm *p = dynamic_cast<FunctionalTerm*>(src);
      return p ? dynamic_cast_functional<T>(p) : FunctionalTermPtr<T>();
    }

    template<typename T>
    FunctionalTermPtr<T> Context::get_functional(const T& proto, ArrayPtr<Term*const> parameters) {
      return FunctionalTermPtr<T>(get_functional_bare(FunctionalTermBackendImpl<T>(proto), parameters));
    }
  }
}

#endif
