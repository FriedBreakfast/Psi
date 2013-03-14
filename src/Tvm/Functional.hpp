#ifndef HPP_PSI_TVM_FUNCTIONAL
#define HPP_PSI_TVM_FUNCTIONAL

#include "Core.hpp"
#include "../Visitor.hpp"

#include <boost/functional/hash.hpp>
#include <boost/concept_check.hpp>

namespace Psi {
  namespace Tvm {
    class PSI_TVM_EXPORT FunctionalValueVisitor {
    public:
      virtual void next(const ValuePtr<>& ptr) = 0;
    };
    
    class FunctionalValueVisitorWrapper : public ValuePtrVisitorBase<FunctionalValueVisitorWrapper> {
      FunctionalValueVisitor *m_callback;
    public:
      FunctionalValueVisitorWrapper(FunctionalValueVisitor *callback) : m_callback(callback) {}
      void visit_ptr(const ValuePtr<>& ptr) {m_callback->next(ptr);}
      template<typename T> bool do_visit_base(VisitorTag<T>) {return !boost::is_same<T,FunctionalValue>::value;}
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
    class PSI_TVM_EXPORT FunctionalValue : public HashableValue {
      friend class Context;
      template<typename> friend class FunctionalTermWithData;

    public:
      virtual void functional_visit(FunctionalValueVisitor& visitor) const = 0;
      
      static bool isa_impl(const Value& ptr) {return ptr.term_type() == term_functional;}
      template<typename V> static void visit(V& v) {visit_base<HashableValue>(v);}

    protected:
      FunctionalValue(Context& context, const SourceLocation& location);
    };
    
#define PSI_TVM_FUNCTIONAL_DECL(Type) \
    PSI_TVM_HASHABLE_DECL(Type) \
  public: \
    virtual void functional_visit(FunctionalValueVisitor& visitor) const; \
    static bool isa_impl(const Value& ptr) {return (ptr.term_type() == term_functional) && (operation == checked_cast<const FunctionalValue&>(ptr).operation_name());}

#define PSI_TVM_FUNCTIONAL_IMPL(Type,Base,Name) \
    PSI_TVM_HASHABLE_IMPL(Type,Base,Name) \
    \
    void Type::functional_visit(FunctionalValueVisitor& visitor) const { \
      FunctionalValueVisitorWrapper vw(&visitor); \
      boost::array<const Type*,1> c = {{this}}; \
      visit_members(vw, c); \
    }

    class PSI_TVM_EXPORT UnaryOp : public FunctionalValue {
    protected:
      UnaryOp(const ValuePtr<>& parameter, const SourceLocation& location);
      
    private:
      ValuePtr<> m_parameter;
      
    public:
      /// \brief Return the single argument to this value
      const ValuePtr<>& parameter() const {return m_parameter;}
      
      template<typename V>
      static void visit(V& v) {
        visit_base<FunctionalValue>(v);
        v("parameter", &UnaryOp::m_parameter);
      }
    };
    
#define PSI_TVM_UNARY_OP_DECL(name,base) \
    class PSI_TVM_EXPORT name : public base { \
      PSI_TVM_FUNCTIONAL_DECL(name) \
      \
    public: \
      name(const ValuePtr<>& arg, const SourceLocation& location); \
    };

#define PSI_TVM_UNARY_OP_IMPL(name,base,op_name) \
    PSI_TVM_FUNCTIONAL_IMPL(name,base,op_name) \
    name::name(const ValuePtr<>& arg, const SourceLocation& location) \
    : base(arg, location) { \
    } \
    \
    template<typename V> \
    void name::visit(V& v) { \
      visit_base<base>(v); \
    }
    
    class PSI_TVM_EXPORT BinaryOp : public FunctionalValue {
    public:
      const ValuePtr<>& lhs() const {return m_lhs;}
      const ValuePtr<>& rhs() const {return m_rhs;}
      
      template<typename V>
      static void visit(V& v) {
        visit_base<FunctionalValue>(v);
        v("lhs", &BinaryOp::m_lhs)
        ("rhs", &BinaryOp::m_rhs);
      }

    protected:
      BinaryOp(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location);
      BinaryOp(const RewriteCallback& callback, const BinaryOp& src);
      
    private:
      ValuePtr<> m_lhs;
      ValuePtr<> m_rhs;
    };

#define PSI_TVM_BINARY_OP_DECL(name,base) \
    class PSI_TVM_EXPORT name : public base { \
      PSI_TVM_FUNCTIONAL_DECL(name) \
    public: \
      name(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location); \
    };
    
#define PSI_TVM_BINARY_OP_IMPL(name,base,op_name) \
    PSI_TVM_FUNCTIONAL_IMPL(name,base,op_name) \
    name::name(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location) \
    : base(lhs, rhs, location) { \
    } \
    \
    template<typename V> \
    void name::visit(V& v) { \
      visit_base<base>(v); \
    }
    
    class PSI_TVM_EXPORT Type : public FunctionalValue {
    public:
      Type(Context& context, const SourceLocation& location);
    public:
      template<typename V> static void visit(V& v) {visit_base<FunctionalValue>(v);}
    };
    
    class PSI_TVM_EXPORT Constructor : public FunctionalValue {
    protected:
      Constructor(Context& context, const SourceLocation& location);
    public:
      template<typename V> static void visit(V& v) {visit_base<FunctionalValue>(v);}
    };
    
    class PSI_TVM_EXPORT AggregateOp : public FunctionalValue {
    protected:
      AggregateOp(Context& context, const SourceLocation& location);
    public:
      template<typename V> static void visit(V& v) {visit_base<FunctionalValue>(v);}
    };
  }
}

#endif
