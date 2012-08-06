#ifndef HPP_PSI_TVM_FUNCTIONAL
#define HPP_PSI_TVM_FUNCTIONAL

#include "Core.hpp"
#include "../Visitor.hpp"

#include <boost/functional/hash.hpp>
#include <boost/concept_check.hpp>

namespace Psi {
  namespace Tvm {
    class FunctionalValueVisitor {
    public:
      virtual void next(const ValuePtr<>& ptr) = 0;
    };
    
    class FunctionalValueVisitorWrapper : public ValuePtrVistorBase<FunctionalValueVisitorWrapper> {
      FunctionalValueVisitor *m_callback;
    public:
      FunctionalValueVisitorWrapper(FunctionalValueVisitor *callback) : m_callback(callback) {}
      void visit_ptr(const ValuePtr<>& ptr) {m_callback->next(ptr);}
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
      virtual void visit(FunctionalValueVisitor& visitor) const = 0;
      
      static bool isa_impl(const Value& ptr) {return ptr.term_type() == term_functional;}

    protected:
      FunctionalValue(Context& context, const ValuePtr<>& type, const HashableValueSetup& hash, const SourceLocation& location);
    };
    
#define PSI_TVM_FUNCTIONAL_DECL(Type) \
    PSI_TVM_HASHABLE_DECL(Type) \
  public: \
    virtual void visit(FunctionalValueVisitor& visitor) const; \
    static bool isa_impl(const Value& ptr) {return (ptr.term_type() == term_functional) && (operation == checked_cast<const Type&>(ptr).operation_name());}

#define PSI_TVM_FUNCTIONAL_IMPL(Type,Base,Name) \
    PSI_TVM_HASHABLE_IMPL(Type,Base,Name) \
    \
    void Type::visit(FunctionalValueVisitor& visitor) const { \
      FunctionalValueVisitorWrapper vw(&visitor); \
      boost::array<const Type*,1> c = {{this}}; \
      visit_members(vw, c); \
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
    };
    
    class Constructor : public FunctionalValue {
    protected:
      Constructor(const ValuePtr<>& type, const HashableValueSetup& hash, const SourceLocation& location);
    };
    
    class SimpleConstructor : public FunctionalValue {
    public:
      SimpleConstructor(const ValuePtr<>& type, const HashableValueSetup& hash, const SourceLocation& location);
    };
    
    class AggregateOp : public FunctionalValue {
    protected:
      AggregateOp(const ValuePtr<>& type, const HashableValueSetup& hash, const SourceLocation& location);
    };
  }
}

#endif
