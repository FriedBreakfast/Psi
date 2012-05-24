#ifndef HPP_PSI_TVM_FUNCTIONAL
#define HPP_PSI_TVM_FUNCTIONAL

#include "Core.hpp"

namespace Psi {
  namespace Tvm {
    class RewriteCallback {
    public:
      virtual ValuePtr<>& rewrite(const ValuePtr<>& value) = 0;
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
      const char *operation() const {return m_operation;}

      /**
       * \brief Build a copy of this term with a new set of parameters.
       * 
       * \param context Context to create the new term in. This may be
       * different to the current context of this term.
       * 
       * \param callback Callback used to rewrite members.
       */
      virtual ValuePtr<FunctionalValue> rewrite(Context& context, RewriteCallback& callback) = 0;

      /**
       * Check whether this is a simple binary operation (for casting
       * implementation).
       */
      virtual bool is_binary_op() const;

      /**
       * Check whether this is a simple unary operation (for casting
       * implementation).
       */
      virtual bool is_unary_op() const;
      
      /**
       * Check whether this is a simple operation which takes no parameters
       * (it should also have no additional data associated with it).
       */
      virtual bool is_simple_op() const;

    protected:
      FunctionalValue(Context *context, const ValuePtr<>& type, std::size_t hash, const char *operation);

    private:
      const char *m_operation;
    };

    class SimpleOp : public FunctionalValue {
    public:
      virtual bool is_simple_op() const;
    };
    
    class UnaryOp : public FunctionalValue {
    public:
      virtual bool is_unary_op() const;
    };
    
    class BinaryOp : public FunctionalValue {
    public:
      virtual bool is_binary_op() const;
    };
    
    class Type : public FunctionalValue {
    public:
    };
    
    class Constructor : public FunctionalValue {
    };
    
    class AggregateOp : public FunctionalValue {
    };
  }
}

#endif
