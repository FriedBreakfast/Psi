#ifndef HPP_PSI_TVM_FUNCTIONAL
#define HPP_PSI_TVM_FUNCTIONAL

#include "Core.hpp"

namespace Psi {
  namespace Tvm {
    struct FunctionalTypeResult {
      FunctionalTypeResult(Term *type_) : type(type_), source(0), source_set(false) {}
      FunctionalTypeResult(Term *type_, Term *source_)  : type(type_), source(source_), source_set(true) {}
      Term *type, *source;
      bool source_set;
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
      template<typename> friend class FunctionalTermWithData;

    public:
      const char *operation() const {return m_operation;}
      unsigned n_parameters() const {return Term::n_base_parameters();}
      Term* parameter(std::size_t n) const {return get_base_parameter(n);}

      /**
       * \brief Build a copy of this term with a new set of parameters.
       * 
       * \param context Context to create the new term in. This may be
       * different to the current context of this term, but must match
       * the context of all terms in \c parameters.
       * 
       * \param parameters Parameters for the rewritten term.
       */
      virtual FunctionalTerm* rewrite(Context& context, ArrayPtr<Term*const> parameters) = 0;

      /**
       * Check whether this is a simple binary operation (for casting
       * implementation).
       */
      virtual bool is_binary_op() const = 0;

      /**
       * Check whether this is a simple unary operation (for casting
       * implementation).
       */
      virtual bool is_unary_op() const = 0;
      
      /**
       * Check whether this is a simple operation which takes no parameters
       * (it should also have no additional data associated with it).
       */
      virtual bool is_simple_op() const = 0;

    private:
      class Setup;
      FunctionalTerm(const UserInitializer& ui, Context *context, Term* type,
                     Term *source, std::size_t hash, const char *operation,
                     ArrayPtr<Term*const> parameters);

      const char *m_operation;
    };

#ifndef PSI_DOXYGEN
    template<> struct CastImplementation<FunctionalTerm> : CoreCastImplementation<FunctionalTerm, term_functional> {};
#endif

    template<typename DataType>
    class FunctionalTermWithData : public FunctionalTerm, CompressedBase<DataType> {
      template<typename> friend class FunctionalTermSpecialized;
      typedef DataType Data;

    public:
      const Data& data() const {return CompressedBase<Data>::get();}

    private:
      FunctionalTermWithData(const UserInitializer& ui, Context *context, Term* type,
                             Term *source, std::size_t hash, const char *operation,
                             ArrayPtr<Term*const> parameters, const Data& data)
        : FunctionalTerm(ui, context, type, source, hash, operation, parameters),
          CompressedBase<Data>(data) {
      }
    };

    template<typename> class FunctionalTermSetupSpecialized;

    /**
     * A specialization of FunctionalTerm which includes custom data.
     */
    template<typename TermTagType>
    class FunctionalTermSpecialized : public FunctionalTermWithData<typename TermTagType::Data> {
      friend class FunctionalTermSetupSpecialized<TermTagType>;
      typedef typename TermTagType::Data Data;
      typedef FunctionalTermWithData<Data> BaseType;

    public:
      virtual FunctionalTerm* rewrite(Context& context, ArrayPtr<Term*const> parameters) {
        return context.template get_functional<TermTagType>(parameters, this->data());
      }

      virtual bool is_binary_op() const {
        return TermTagType::is_binary_op;
      }

      virtual bool is_unary_op() const {
        return TermTagType::is_unary_op;
      }
      
      virtual bool is_simple_op() const {
        return TermTagType::is_simple_op;
      }
      
    private:
      FunctionalTermSpecialized(const UserInitializer& ui, Context *context, Term* type,
                                Term *source, std::size_t hash, const char *operation,
                                ArrayPtr<Term*const> parameters, const Data& data)
        : BaseType(ui, context, type, source, hash, operation, parameters, data) {
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
                                        const UserInitializer& ui, Context *context, Term* type, Term *source,
                                        std::size_t hash, const char *operation,
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
                                        const UserInitializer& ui, Context *context, Term* type, Term *source,
                                        std::size_t hash, const char *operation,
                                        ArrayPtr<Term*const> parameters) const {
        return new (ptr) FunctionalTermSpecialized<TermTagType>
          (ui, context, type, source, hash, operation, parameters, *data);
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
      /// \copydoc FunctionalTerm::rewrite
      FunctionalTerm* rewrite(Context& context, ArrayPtr<Term*const> parameters) const {return get()->rewrite(context, parameters);}
    };

    /**
     * Base class for pointers to functional terms - this is
     * specialized to individual term types.
     */
    template<typename TermTagType, typename BasePtr>
    class FunctionalTermPtrBase : public BasePtr {
    public:
      FunctionalTermPtrBase() {}
      explicit FunctionalTermPtrBase(FunctionalTerm *term) : BasePtr(term) {}

      /// \copydoc FunctionalTerm::rewrite
      PtrDecayAdapter<typename TermTagType::PtrHook> rewrite(Context& context, ArrayPtr<Term*const> parameters) const {
        return cast<TermTagType>(FunctionalTermPtr::rewrite(context, parameters));
      }

    protected:
      const typename TermTagType::Data& data() const {
        return checked_cast<FunctionalTermSpecialized<TermTagType>*>(this->get())->data();
      }
    };

    template<typename Ptr_, typename PtrHook, typename Backend>
    struct FunctionalCastImplementation {
      typedef Ptr_ Ptr;
      typedef PtrHook Reference;

      static Ptr null() {
        return Ptr();
      }

      static Ptr cast(Term *t) {
        return cast(checked_cast<FunctionalTerm*>(t));
      }

      static Ptr cast(FunctionalTerm *t) {
        return Backend::cast(t);
      }

      static bool isa(Term *t) {
        FunctionalTerm *ft = dyn_cast<FunctionalTerm>(t);
        if (!ft)
          return false;
        return isa(ft);
      }

      static bool isa(FunctionalTerm *t) {
        return Backend::isa(t);
      }
    };

    /**
     * Base class from which all functional term types should inherit.
     */
    struct FunctionalOperation : NonConstructible {
      static const bool is_binary_op = false;
      static const bool is_unary_op = false;
      static const bool is_simple_op = false;
      typedef FunctionalTermPtr PtrHook;
      typedef PtrDecayAdapter<PtrHook> Ptr;
    };

    struct UnaryOperation : FunctionalOperation {
      static const bool is_unary_op = true;
      typedef Empty Data;
      struct PtrHook : public FunctionalTermPtr {
        PtrHook() {}
        PtrHook(FunctionalTerm *t) : FunctionalTermPtr(t) {PSI_ASSERT(t->is_unary_op());}
        /// \brief Get the parameter to this operation.
        Term *parameter() const {return get()->parameter(0);}
      };
      typedef PtrDecayAdapter<PtrHook> Ptr;
    };

    struct UnaryOperationCastImplementation {
      static UnaryOperation::Ptr cast(FunctionalTerm *t) {
        PSI_ASSERT(t->is_unary_op());
        return UnaryOperation::Ptr(UnaryOperation::PtrHook(t));
      }

      static bool isa(FunctionalTerm *t) {
        return t->is_unary_op();
      }
    };

#ifndef PSI_DOXYGEN
    template<> struct CastImplementation<UnaryOperation>
    : FunctionalCastImplementation<UnaryOperation::Ptr, UnaryOperation::PtrHook, UnaryOperationCastImplementation> {};
#endif

    struct BinaryOperation : FunctionalOperation {
      static const bool is_binary_op = true;
      typedef Empty Data;
      struct PtrHook : public FunctionalTermPtr {
        PtrHook() {}
        PtrHook(FunctionalTerm *t) : FunctionalTermPtr(t) {PSI_ASSERT(t->is_binary_op());}
        /// \brief Get the first parameter to this operation.
        Term *lhs() const {return get()->parameter(0);}
        /// \brief Get the second parameter to this operation.
        Term *rhs() const {return get()->parameter(1);}
      };
      typedef PtrDecayAdapter<PtrHook> Ptr;
    };

    struct BinaryOperationCastImplementation {
      static BinaryOperation::Ptr cast(FunctionalTerm *t) {
        PSI_ASSERT(t->is_binary_op());
        return BinaryOperation::Ptr(BinaryOperation::PtrHook(t));
      }

      static bool isa(FunctionalTerm *t) {
        return t->is_binary_op();
      }
    };

#ifndef PSI_DOXYGEN
    template<> struct CastImplementation<BinaryOperation>
    : FunctionalCastImplementation<BinaryOperation::Ptr, BinaryOperation::PtrHook, BinaryOperationCastImplementation> {};
#endif
    
    struct SimpleOperation : FunctionalOperation {
      static const bool is_simple_op = true;
      typedef Empty Data;
      typedef FunctionalTermPtr PtrHook;
      typedef PtrDecayAdapter<PtrHook> Ptr;
    };
    
    struct SimpleOperationCastImplementation {
      static SimpleOperation::Ptr cast(FunctionalTerm *t) {
        PSI_ASSERT(t->is_simple_op());
        return SimpleOperation::Ptr(FunctionalTermPtr(t));
      }
      
      static bool isa(FunctionalTerm *t) {
        return t->is_simple_op();
      }
    };
    
    template<typename TermTagType>
    struct NamedOperationCastImplementation {
      static typename TermTagType::Ptr cast(FunctionalTerm *t) {
        PSI_ASSERT(isa(t));
        return typename TermTagType::Ptr(typename TermTagType::PtrHook(t));
      }
      
      static bool isa(FunctionalTerm *t) {
        return t->operation() == TermTagType::operation;
      }
    };
    
    /**
     * Base class for type terms. This does not have any functionality,
     * but is here to group together types in the documentation.
     */
    struct TypeOperation : FunctionalOperation {
    };
    
    /**
     * Base class for value terms, i.e. constructor terms - it is not
     * possible to optimize these terms since they do not represent
     * operations on other terms, but construct values from other values.
     */
    struct ConstructorOperation : FunctionalOperation {
    };

#define PSI_TVM_FUNCTIONAL_TYPE(name,base) \
    struct name : base {                   \
    typedef name ThisType;                 \
    typedef base BaseType;                 \
    static const char operation[];

#define PSI_TVM_FUNCTIONAL_PTR_HOOK()                                     \
    struct PtrHook : FunctionalTermPtrBase<ThisType, BaseType::PtrHook> { \
      friend struct NamedOperationCastImplementation<ThisType>;           \
    private:                                                              \
      typedef FunctionalTermPtrBase<ThisType, BaseType::PtrHook> PtrBaseType; \
      explicit PtrHook(FunctionalTerm *t) : PtrBaseType(t) {}             \
    public:                                                               \
      PtrHook() {}

#define PSI_TVM_FUNCTIONAL_PTR_HOOK_END() }; typedef PtrDecayAdapter<PtrHook> Ptr;

#ifndef PSI_DOXYGEN
#define PSI_TVM_FUNCTIONAL_TYPE_CAST(name) template<> struct CastImplementation<name> : FunctionalCastImplementation<name::Ptr, name::PtrHook, NamedOperationCastImplementation<name> > {};
#else
#define PSI_TVM_FUNCTIONAL_TYPE_CAST(name)
#endif

#define PSI_TVM_FUNCTIONAL_TYPE_END(name)                               \
    static FunctionalTypeResult type(Context&, const Data&, ArrayPtr<Term*const>); \
  };                                                                    \
    PSI_TVM_FUNCTIONAL_TYPE_CAST(name)

  /**
   * Macro for defining functional types which take no arguments and have no data.
   */
#define PSI_TVM_FUNCTIONAL_TYPE_SIMPLE(name)       \
    PSI_TVM_FUNCTIONAL_TYPE(name, SimpleOperation) \
    static Ptr get(Context&);                      \
    PSI_TVM_FUNCTIONAL_TYPE_END(name)

  /**
   * Macro for defining functional types which take one argument and
   * have no data.
   */
#define PSI_TVM_FUNCTIONAL_TYPE_UNARY(name, result_type)     \
  PSI_TVM_FUNCTIONAL_TYPE(name, UnaryOperation) \
  PSI_TVM_FUNCTIONAL_PTR_HOOK()                          \
  CastImplementation<result_type>::Ptr type() const {return cast<result_type>(PtrBaseType::type());} \
  PSI_TVM_FUNCTIONAL_PTR_HOOK_END()                      \
  static Ptr get(Term *parameter);              \
  PSI_TVM_FUNCTIONAL_TYPE_END(name)

  /**
   * Macro for defining functional types which take two arguments and
   * have no data.
   */
#define PSI_TVM_FUNCTIONAL_TYPE_BINARY(name,result_type) \
  PSI_TVM_FUNCTIONAL_TYPE(name, BinaryOperation)         \
  PSI_TVM_FUNCTIONAL_PTR_HOOK()                          \
  CastImplementation<result_type>::Ptr type() const {return cast<result_type>(PtrBaseType::type());} \
  PSI_TVM_FUNCTIONAL_PTR_HOOK_END()                      \
  static Ptr get(Term *lhs, Term *rhs);                  \
  PSI_TVM_FUNCTIONAL_TYPE_END(name)

    template<typename T> typename T::Ptr Context::get_functional(ArrayPtr<Term*const> parameters, const typename T::Data& data) {
      return cast<T>(get_functional_bare(FunctionalTermSetupSpecialized<T>(&data), parameters));
    }
  }
}

#endif
