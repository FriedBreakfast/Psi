#ifndef HPP_PSI_TYPESYSTEM
#define HPP_PSI_TYPESYSTEM

#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Maybe.hpp"
#include "Variant.hpp"

namespace Psi {
  namespace TypeSystem {
    class FunctionType;
    class ReferenceType;
    class Constraint;
    class NumberExpression;
    class NumberPattern;
    class ConstructorType;

    class TypeVariable {
    public:
      bool operator == (const TypeVariable& rhs) const {return m_ptr == rhs.m_ptr;}
      std::size_t hash() const {return std::hash<void*>()(m_ptr.get());}

    private:
      std::shared_ptr<void> m_ptr;
    };
  }
}

namespace std {
  template<>
  struct hash<Psi::TypeSystem::TypeVariable> : std::unary_function<Psi::TypeSystem::TypeVariable, std::size_t> {
    std::size_t operator () (const Psi::TypeSystem::TypeVariable& var) const {
      return var.hash();
    }
  };
}

namespace Psi {
  namespace TypeSystem {
    typedef Variant<TypeVariable,
                    FunctionType,
                    ReferenceType,
                    ConstructorType,
                    NumberExpression,
                    NumberPattern> Type;

    struct Interface {
      std::vector<Constraint> constraints;
      std::vector<TypeVariable> parameters;
    };

    class Constraint {
    public:

    private:
      std::shared_ptr<Interface> interface;
      std::vector<TypeVariable> parameters;
    };

    class TypeContext {
    public:
      TypeContext(TypeContext&&) = default;
      TypeContext parent() const {return TypeContext({m_data->parent});}

      const std::unordered_set<Constraint>& constraints() {return m_data->constraints;}
      const std::unordered_set<TypeVariable>& variables() {return m_data->variables;}

    private:
      struct Data {
        std::shared_ptr<const Data> parent;
        std::unordered_set<TypeVariable> variables;
        std::unordered_set<Constraint> constraints;
      };

      std::shared_ptr<const Data> m_data;

      TypeContext(std::shared_ptr<const Data>&& data) : m_data(std::move(data)) {
      }
    };

    class ConstructorType {
    };

    /**
     * Function type - also support universal quantification.
     */
    class FunctionType {
    public:
      const std::unordered_set<Constraint>& constraints() const;
      const std::unordered_set<TypeVariable>& variables() const;
      const std::vector<Type>& parameters() const;
      const Type& result() const;

    private:
      struct Data;

      std::shared_ptr<const Data> m_data;
    };

    /**
     * Reference type - supports existential quantification.
     */
    class ReferenceType {
    public:
      const std::unordered_set<TypeVariable>& variables() const;
      const std::unordered_set<Constraint>& constraints() const;
      const Type& type() const;

    private:
      struct Data;
      std::shared_ptr<const Data> m_data;
    };

    template<typename T, typename Child=std::hash<typename T::value_type> >
    struct SetHasher : std::unary_function<T, std::size_t> {
      Child child;

      std::size_t operator () (const T& value) const {
        std::size_t result = 0;
        for (auto it = value.begin(); it != value.end(); ++it)
          result ^= child(*it);
        return result;
      }
    };

    class NumberExpression {
    public:
      /**
       * Type of a term. A term is an expression of the form a*b*c*... .
       */
      typedef std::unordered_multiset<TypeVariable> Term;
      typedef std::unordered_map<Term, int, SetHasher<Term> > TermSet;

      const TermSet& terms() const;

      explicit operator bool () const;
      friend bool operator == (const NumberExpression& lhs, const NumberExpression& rhs);
      friend bool operator != (const NumberExpression& lhs, const NumberExpression& rhs);
      friend Type operator + (const NumberExpression& lhs, const NumberExpression& rhs);
      friend Type operator - (const NumberExpression& lhs, const NumberExpression& rhs);
      friend Type operator * (const NumberExpression& lhs, const NumberExpression& rhs);

    private:
      struct Data;
      std::shared_ptr<const Data> m_data;

      NumberExpression(std::shared_ptr<const Data>&& data) : m_data(std::move(data)) {
      }

      static Type simplify(TermSet&& terms);
    };

    /**
     * Numbers must have a different pattern class since we can't
     * match the full general case. This allows n*T+k patterns.
     */
    class NumberPattern {
    public:
      int multiply() const {return m_multiply;}
      int add() const {return m_add;}
      const TypeVariable& variable() const {return m_variable;}

    private:
      int m_multiply;
      int m_add;
      TypeVariable m_variable;
    };

    struct FunctionType::Data {
      std::unordered_set<TypeVariable> variables;
      std::unordered_set<Constraint> constraints;
      std::vector<Type> parameters;
      Type result;
    };

    struct ReferenceType::Data {
      /**
       * Existentially quantified variables and interfaces.
       */
      std::unordered_set<TypeVariable> variables;
      std::unordered_set<Constraint> constraints;
      Type type;
    };

    struct NumberExpression::Data {
      Data(Data&&) = default;

      /**
       * Set of terms making up this expression. The keys are the
       * terms, the values are the term coefficients.
       */
      std::unordered_map<Term, int, SetHasher<Term> > terms;
    };

    inline const std::unordered_set<Constraint>& FunctionType::constraints() const {return m_data->constraints;}
    inline const std::unordered_set<TypeVariable>& FunctionType::variables() const {return m_data->variables;}
    inline const std::vector<Type>& FunctionType::parameters() const {return m_data->parameters;}
    inline const Type& FunctionType::result() const {return m_data->result;}

    inline const std::unordered_set<TypeVariable>& ReferenceType::variables() const {return m_data->variables;}
    inline const std::unordered_set<Constraint>& ReferenceType::constraints() const {return m_data->constraints;}
    inline const Type& ReferenceType::type() const {return m_data->type;}

    NumberExpression::operator bool () const {
      return !terms().empty();
    }

    inline const NumberExpression::TermSet& NumberExpression::terms() const {
      return m_data->terms;
    }

    /**
     * Apply a function to a given set of argument types, and return
     * the expected result type.
     */
    Maybe<Type> apply_function(const FunctionType& function, const std::vector<Type>& parameters);
  }
}

#endif
