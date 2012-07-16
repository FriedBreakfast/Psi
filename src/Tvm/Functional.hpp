#ifndef HPP_PSI_TVM_FUNCTIONAL
#define HPP_PSI_TVM_FUNCTIONAL

#include "Core.hpp"

#include <boost/functional/hash.hpp>
#include <boost/concept_check.hpp>

namespace Psi {
  namespace Tvm {
    class FunctionalValueVisitor {
    public:
      virtual void next(const ValuePtr<>& ptr) = 0;
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
      
      virtual void visit(FunctionalValueVisitor& visitor) = 0;
      
      static bool isa_impl(const Value& ptr) {return ptr.term_type() == term_functional;}

    protected:
      FunctionalValue(Context& context, const ValuePtr<>& type, const HashableValueSetup& hash, const SourceLocation& location);

    private:
      const char *m_operation;
    };
    
#define PSI_TVM_FUNCTIONAL_DECL(Type) \
  public: \
    static const char operation[]; \
    virtual ValuePtr<FunctionalValue> rewrite(RewriteCallback& callback); \
    virtual void visit(FunctionalValueVisitor& callback); \
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
      SimpleOp(const ValuePtr<>& type, const HashableValueSetup& hash, const SourceLocation& location);
    };
    
    class UnaryOp : public FunctionalValue {
    protected:
      UnaryOp(const ValuePtr<>& type, const ValuePtr<>& parameter, const HashableValueSetup& hash, const SourceLocation& location);
      UnaryOp(const RewriteCallback& callback, const UnaryOp& src);
      
    private:
      ValuePtr<> m_parameter;
      
    public:
      /// \brief Return the single argument to this value
      const ValuePtr<>& parameter() const {return m_parameter;}
    };
    
#define PSI_TVM_UNARY_OP_DECL(name,base) \
    class name : public base { \
      PSI_TVM_FUNCTIONAL_DECL(name) \
      \
    public: \
      name(const ValuePtr<>& arg, const SourceLocation& location); \
      static ValuePtr<> get(const ValuePtr<>& arg, const SourceLocation& location); \
    };

#define PSI_TVM_UNARY_OP_IMPL(name,base,op_name) \
    PSI_TVM_FUNCTIONAL_IMPL(name,base,op_name) \
    name::name(const ValuePtr<>& arg, const SourceLocation& location) \
    : base(arg, hashable_setup<name>(), location) { \
    } \
    \
    ValuePtr<> name::get(const ValuePtr<>& arg, const SourceLocation& location) { \
      return arg->context().get_functional(name(arg, location)); \
    }
    
    class BinaryOp : public FunctionalValue {
    public:
      const ValuePtr<>& lhs() const {return m_lhs;}
      const ValuePtr<>& rhs() const {return m_rhs;}
      
    protected:
      BinaryOp(const ValuePtr<>& type, const ValuePtr<>& lhs, const ValuePtr<>& rhs, const HashableValueSetup& hash, const SourceLocation& location);
      BinaryOp(const RewriteCallback& callback, const BinaryOp& src);
      
    private:
      ValuePtr<> m_lhs;
      ValuePtr<> m_rhs;
    };

#define PSI_TVM_BINARY_OP_DECL(name,base) \
    class name : public base { \
      PSI_TVM_FUNCTIONAL_DECL(name) \
      \
    public: \
      name(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location); \
      static ValuePtr<> get(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location); \
    };
    
#define PSI_TVM_BINARY_OP_IMPL(name,base,op_name) \
    PSI_TVM_FUNCTIONAL_IMPL(name,base,op_name) \
    name::name(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location) \
    : base(lhs, rhs, hashable_setup<name>(), location) { \
    } \
    \
    ValuePtr<> name::get(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location) { \
      return lhs->context().get_functional(name(lhs, rhs, location)); \
    }
    
    class Type : public FunctionalValue {
    public:
      Type(Context& context, const HashableValueSetup& hash, const SourceLocation& location);
    };
    
    class SimpleType : public FunctionalValue {
    public:
      SimpleType(Context& context, const HashableValueSetup& hash, const SourceLocation& location);
      virtual void type_check();
    };
    
    class Constructor : public FunctionalValue {
    protected:
      Constructor(const ValuePtr<>& type, const HashableValueSetup& hash, const SourceLocation& location);
    };
    
    class SimpleConstructor : public FunctionalValue {
    public:
      SimpleConstructor(const ValuePtr<>& type, const HashableValueSetup& hash, const SourceLocation& location);
      virtual void type_check();
    };
    
    class AggregateOp : public FunctionalValue {
    protected:
      AggregateOp(const ValuePtr<>& type, const HashableValueSetup& hash, const SourceLocation& location);
    };
  }
}

#endif
