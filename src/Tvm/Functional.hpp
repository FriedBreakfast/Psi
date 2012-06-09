#ifndef HPP_PSI_TVM_FUNCTIONAL
#define HPP_PSI_TVM_FUNCTIONAL

#include "Core.hpp"

#include <boost/functional/hash.hpp>

namespace Psi {
  namespace Tvm {
    class FunctionalOperationSetup {
      OperationSetup m_base;
      std::size_t m_hash;
      
      template<typename T> void base_combine(const T&) {}
      template<typename T> void base_combine(const ValuePtr<T>& ptr) {m_base.combine(ptr);}
      
    public:
      explicit FunctionalOperationSetup(const char *operation)
      : m_base(operation),
      m_hash(boost::hash_value(operation)) {
      }
      
      template<typename T>
      void combine(T& x) {
        boost::hash_combine(m_hash, x);
        base_combine(x);
      }
      
      template<typename T>
      FunctionalOperationSetup operator () (const T& x) const {
        FunctionalOperationSetup copy(*this);
        copy.combine(x);
        return copy;
      }
    };
    
    template<typename T>
    FunctionalOperationSetup functional_setup() {
      return FunctionalOperationSetup(T::operation);
    }

    template<typename T, typename U>
    FunctionalOperationSetup functional_setup(const U& value) {
      return functional_setup<T>()(value);
    }

    /**
     * \brief Base class of functional (machine state independent) terms.
     *
     * Functional terms are special since two terms of the same
     * operation and with the same parameters are equivalent; they are
     * therefore unified into one term so equivalence can be checked
     * via pointer equality. This is particularly required for type
     * checking, but also applies to other terms.
     */
    class FunctionalValue : public HashableValue {
      friend class Context;
      template<typename> friend class FunctionalTermWithData;

    public:
      const char *operation_name() const {return m_operation;}

      /**
       * \brief Build a copy of this term with a new set of parameters.
       * 
       * \param context Context to create the new term in. This may be
       * different to the current context of this term.
       * 
       * \param callback Callback used to rewrite members.
       */
      virtual ValuePtr<FunctionalValue> rewrite(RewriteCallback& callback) = 0;
      
      static bool isa_impl(const Value& ptr) {return ptr.term_type() == term_functional;}

    protected:
      FunctionalValue(Context& context, const ValuePtr<>& type, const FunctionalOperationSetup& hash, const SourceLocation& location);

    private:
      const char *m_operation;
    };
    
#define PSI_TVM_FUNCTIONAL_DECL(Type) \
  public: \
    static const char operation[]; \
    virtual ValuePtr<FunctionalValue> rewrite(RewriteCallback& callback); \
    static bool isa_impl(const Value& ptr) {return (ptr.term_type() == term_functional) && (operation == checked_cast<const Type&>(ptr).operation_name());} \
  private: \
    Type(const RewriteCallback& callback, const Type& src); \
    virtual ValuePtr<FunctionalValue> clone(void *ptr);
    
#define PSI_TVM_FUNCTIONAL_IMPL(Type,Base,Name) \
    const char Type::operation[] = #Name; \
    \
    ValuePtr<FunctionalValue> Type::clone(void *ptr) { \
      return ValuePtr<FunctionalValue>(::new (ptr) Type(*this)); \
    } \
    \
    ValuePtr<FunctionalValue> Type::rewrite(RewriteCallback& callback) { \
      return callback.context().get_functional(Type(callback, *this)); \
    }

    class SimpleOp : public FunctionalValue {
    public:
      SimpleOp(const ValuePtr<>& type, const FunctionalOperationSetup& hash, const SourceLocation& location);
    };
    
    class UnaryOp : public FunctionalValue {
    protected:
      UnaryOp(const ValuePtr<>& type, const ValuePtr<>& parameter, const FunctionalOperationSetup& hash, const SourceLocation& location);
      UnaryOp(const RewriteCallback& callback, const UnaryOp& src);
      
    private:
      ValuePtr<> m_parameter;
      
    public:
      /// \brief Return the single argument to this value
      const ValuePtr<>& parameter() const {return m_parameter;}
    };
    
    class BinaryOp : public FunctionalValue {
    protected:
      BinaryOp(const ValuePtr<>& type, const ValuePtr<>& lhs, const ValuePtr<>& rhs, const FunctionalOperationSetup& hash, const SourceLocation& location);
      BinaryOp(const RewriteCallback& callback, const BinaryOp& src);
    };
    
    class Type : public FunctionalValue {
    public:
      Type(Context& context, const FunctionalOperationSetup& hash, const SourceLocation& location);
    };
    
    class Constructor : public FunctionalValue {
    protected:
      Constructor(const ValuePtr<>& type, const FunctionalOperationSetup& hash, const SourceLocation& location);
    };
    
    class AggregateOp : public FunctionalValue {
    protected:
      AggregateOp(const ValuePtr<>& type, const FunctionalOperationSetup& hash, const SourceLocation& location);
    };
  }
}

#endif
