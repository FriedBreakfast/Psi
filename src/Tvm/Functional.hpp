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
      template<typename> friend class FunctionalTermSpecialized;

    public:
      const char *operation() const {return m_operation;}
      std::size_t n_parameters() const {return Term::n_base_parameters();}
      Term* parameter(std::size_t n) const {return get_base_parameter(n);}

      /// Build a copy of this term with a new set of parameters.
      virtual FunctionalTerm* rewrite(ArrayPtr<Term*const> parameters) = 0;
 
    private:
      class Setup;
      FunctionalTerm(const UserInitializer& ui, Context *context, Term* type,
		     bool phantom, std::size_t hash, const char *operation,
		     ArrayPtr<Term*const> parameters);

      const char *m_operation;
    };

    template<> struct CastImplementation<FunctionalTerm> : CoreCastImplementation<FunctionalTerm, term_functional> {};

    template<typename> class FunctionalTermSetupSpecialized;

    /**
     * A specialization of FunctionalTerm which includes custom data.
     */
    template<typename TermTagType>
    class FunctionalTermSpecialized : public FunctionalTerm, CompressedBase<typename TermTagType::Data> {
      friend class FunctionalTermSetupSpecialized<TermTagType>;
      typedef typename TermTagType::Data Data;

    public:
      const Data& data() const {return CompressedBase<Data>::get();}

      virtual FunctionalTerm* rewrite(ArrayPtr<Term*const> parameters) {
        return context().template get_functional<TermTagType>(parameters, data());
      }
 
    private:
      FunctionalTermSpecialized(const UserInitializer& ui, Context *context, Term* type,
                                bool phantom, std::size_t hash, const char *operation,
                                ArrayPtr<Term*const> parameters, const Data& data)
        : FunctionalTerm(ui, context, type, phantom, hash, operation, parameters),
          CompressedBase<Data>(data) {
      }
    };

    class FunctionalTermSetup {
      template<typename> friend struct FunctionalTermSetupSpecialized;

      FunctionalTermSetup(const char *operation_, std::size_t data_hash_, std::size_t term_size_)
        : operation(operation_), data_hash(data_hash_), term_size(term_size_) {}

    public:
      const char *operation;
      std::size_t data_hash;
      std::size_t term_size;
      virtual bool data_equals(FunctionalTerm *term) const = 0;
      virtual FunctionalTerm* construct(void *ptr,
                                        const UserInitializer& ui, Context *context, Term* type,
                                        bool phantom, std::size_t hash, const char *operation,
                                        ArrayPtr<Term*const> parameters) const = 0;

      virtual FunctionalTypeResult type(Context& context, ArrayPtr<Term*const> parameters) const = 0;
    };

    template<typename TermTagType>
    struct FunctionalTermSetupSpecialized : FunctionalTermSetup {
      typedef typename TermTagType::Data Data;

      FunctionalTermSetupSpecialized(const Data *data_)
        : FunctionalTermSetup(TermTagType::operation, boost::hash<Data>()(*data_),
                              sizeof(FunctionalTermSpecialized<TermTagType>)), data(data_) {}

      const Data *data;

      virtual bool data_equals(FunctionalTerm *term) const {
        return *data == checked_cast<FunctionalTermSpecialized<TermTagType>*>(term)->data();
      }

      virtual FunctionalTerm* construct(void *ptr,
                                        const UserInitializer& ui, Context *context, Term* type,
                                        bool phantom, std::size_t hash, const char *operation,
                                        ArrayPtr<Term*const> parameters) const {
        return new (ptr) FunctionalTermSpecialized<TermTagType>
          (ui, context, type, phantom, hash, operation, parameters, *data);
      }

      virtual FunctionalTypeResult type(Context& context, ArrayPtr<Term*const> parameters) const {
        return TermTagType::type(context, *data, parameters);
      }
    };

    /**
     * Base class for pointers to functional terms. This provides
     * generic functions for all functional terms in a non-template
     * class.
     */
    class FunctionalTermPtr : public TermPtrBase {
    public:
      /// \brief To comply with the \c PtrAdapter interface.
      typedef FunctionalTerm GetType;
      FunctionalTermPtr() {}
      explicit FunctionalTermPtr(FunctionalTerm *term) : TermPtrBase(term) {}
      /// \brief To comply with the \c PtrAdapter interface.
      FunctionalTerm* get() const {return checked_cast<FunctionalTerm*>(m_ptr);}
      /// \copydoc FunctionalTerm::operation
      const char *operation() const {return get()->operation();}
    };

    /**
     * Base class for pointers to functional terms - this is
     * specialized to individual term types.
     */
    template<typename TermTagType>
    class FunctionalTermPtrBase : public FunctionalTermPtr {
    public:
      FunctionalTermPtrBase() {}
      explicit FunctionalTermPtrBase(FunctionalTerm *term) : FunctionalTermPtr(term) {}

      /// \copydoc FunctionalTerm::rewrite
      PtrDecayAdapter<typename TermTagType::PtrHook> rewrite(ArrayPtr<Term*const> parameters) {
        return cast<TermTagType>(get()->rewrite(parameters));
      }

    protected:
      const typename TermTagType::Data& data() const {
        return checked_cast<FunctionalTermSpecialized<TermTagType>*>(get())->data();
      }
    };

    template<typename T>
    struct FunctionalCastImplementation {
      typedef typename T::Ptr Ptr;
      typedef typename T::PtrHook Reference;

      static Ptr null() {
        return Ptr();
      }

      static Ptr cast(Term *t) {
        return cast(checked_cast<FunctionalTerm*>(t));
      }

      static Ptr cast(FunctionalTerm *t) {
        PSI_ASSERT(T::operation == t->operation());
        return Ptr(typename T::PtrHook(checked_cast<FunctionalTermSpecialized<T>*>(t)));
      }

      static bool isa(Term *t) {
        FunctionalTerm *ft = dyn_cast<FunctionalTerm>(t);
        if (!ft)
          return false;

        return T::operation == ft->operation();
      }
    };

#define PSI_TVM_FUNCTIONAL_TYPE(name) \
    struct name : NonConstructible {  \
    typedef name ThisType;            \
    static const char operation[];

#define PSI_TVM_FUNCTIONAL_PTR_HOOK()                                   \
    struct PtrHook : FunctionalTermPtrBase<ThisType> {                  \
    template<typename> friend struct FunctionalCastImplementation;      \
  private:                                                              \
  explicit PtrHook(FunctionalTerm *t) : FunctionalTermPtrBase<ThisType>(t) {} \
  public:                                                               \
  PtrHook() {}

#define PSI_TVM_FUNCTIONAL_PTR_HOOK_END() }; typedef PtrDecayAdapter<PtrHook> Ptr;

#ifndef PSI_DOXYGEN
#define PSI_TVM_FUNCTIONAL_TYPE_CAST(name) template<> struct CastImplementation<name> : FunctionalCastImplementation<name> {};
#else
#define PSI_TVM_FUNCTIONAL_TYPE_CAST(name)
#endif

#define PSI_TVM_FUNCTIONAL_TYPE_END(name)                               \
    static FunctionalTypeResult type(Context&, const Data&, ArrayPtr<Term*const>); \
  };                                                                    \
    PSI_TVM_FUNCTIONAL_TYPE_CAST(name)

#define PSI_TVM_FUNCTIONAL_TYPE_SIMPLE(name)    \
    PSI_TVM_FUNCTIONAL_TYPE(name)               \
    typedef Empty Data;                         \
    PSI_TVM_FUNCTIONAL_PTR_HOOK()               \
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()           \
    static Ptr get(Context&);                   \
    PSI_TVM_FUNCTIONAL_TYPE_END(name)

    template<typename T> typename T::Ptr Context::get_functional(ArrayPtr<Term*const> parameters, const typename T::Data& data) {
      return cast<T>(get_functional_bare(FunctionalTermSetupSpecialized<T>(&data), parameters));
    }
  }
}

#endif
