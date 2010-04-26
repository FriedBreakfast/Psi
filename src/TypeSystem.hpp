#ifndef HPP_PSI_TYPESYSTEM
#define HPP_PSI_TYPESYSTEM

#include <functional>
#include <iosfwd>
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

namespace Psi {
  namespace TypeSystem2 {
    template<typename Tag> class Identifier;
    class Constraint;

    template<typename T>
    struct ContextHash : std::unary_function<T, std::size_t> {
      std::size_t operator () (const T& var) const {
	return hash(var);
      }
    };
  }
}

namespace std {
  template<typename T> struct hash<Psi::TypeSystem2::Identifier<T> > : Psi::TypeSystem2::ContextHash<Psi::TypeSystem2::Identifier<T> > {};
  //template<> struct hash<Psi::TypeSystem2::Constraint> : Psi::TypeSystem2::ContextHash<Psi::TypeSystem2::Constraint> {};
}

namespace Psi {
  namespace TypeSystem2 {
    template<typename Tag>
    class Identifier {
    public:
      Identifier() {}
      Identifier(Identifier&&) = default;

      static Identifier new_() {
        return {std::make_shared<char>()};
      }

      friend std::size_t hash(const Identifier& self) {
        return std::hash<void*>()(self.m_ptr.get());
      }

      bool operator == (const Identifier& rhs) const {
        return m_ptr == rhs.m_ptr;
      }

      bool operator != (const Identifier& rhs) const {
        return m_ptr != rhs.m_ptr;
      }

      explicit operator bool () const {
        return m_ptr;
      }

    private:
      Identifier(std::shared_ptr<char> ptr) : m_ptr(std::move(ptr)) {}

      std::shared_ptr<char> m_ptr;
    };

    struct VariableTag;
    struct ConstructorTag;
    struct PredicateTag;

    typedef Identifier<VariableTag> Variable;
    typedef Identifier<ConstructorTag> Constructor;
    typedef Identifier<PredicateTag> Predicate;

    struct ForAll;

    struct Apply {
      Constructor constructor;
      std::vector<ForAll> parameters;
    };

    struct Constraint {
      Predicate predicate;
      std::vector<ForAll> parameters;
    };

    struct Quantifier {
      std::unordered_set<Constraint> constraints;
      std::unordered_set<Variable> variables;
    };

    struct Exists {
      Quantifier quantifier;
      Variant<Variable, Apply> term;
    };

    struct Implies {
      std::vector<ForAll> lhs;
      Exists rhs;
    };

    struct ForAll {
      Quantifier quantifier;
      Implies term;
    };

    class Type {
    public:
      Type(Variable var);
      Type(Apply apply);
      Type(Exists exists);
      Type(Implies implies);
      Type(ForAll for_all);
      ~Type();

      bool is_variable();
      const Variable* as_variable() const;
      bool occurs(const std::unordered_set<Variable>& variables) const;

      const ForAll& for_all() const {return m_for_all;}
      ForAll& for_all() {return m_for_all;}

    private:
      ForAll m_for_all;
    };

    Type for_all(const std::unordered_set<Variable>& variables, const Type& term);
    Type exists(const std::unordered_set<Variable>& variables, const Type& term);
    Type for_all(const std::unordered_set<Variable>& variables, const Type& term, const std::unordered_set<Constraint>& constraints);
    Type exists(const std::unordered_set<Variable>& variables, const Type& term, const std::unordered_set<Constraint>& constraints);
    Type implies(const std::vector<Type>& lhs, const Type& rhs);
    Type apply(Constructor constructor, std::vector<Type> parameters);

    Maybe<Type> apply(const Type& function, const std::unordered_map<unsigned, Type>& arguments);

    /**
     * \brief Do an occurs check on \c type.
     *
     * This checks whether any of the variables listed in \c variables
     * appear anywhere in \c type. Note that they should not be
     * quantified over anywhere in \c type; if they are the result of
     * this check is meaningless. However, this error condition is not
     * checked for.
     */
    bool occurs(const Type& type, const std::unordered_set<Variable>& variables);

    struct TermNamer {
      std::function<std::string(const Variable&)> variable_namer;
      std::function<std::string(const Constructor&)> constructor_namer;
      std::function<std::string(const Predicate&)> predicate_namer;
    };

    class TypePrinter {
    public:
      TypePrinter(TermNamer term_namer);
      ~TypePrinter();

      void print(std::ostream& os, const Type& t) {print_forall(os, t.for_all(), false);}
      void print(std::ostream& os, const ForAll& fa) {print_forall(os, fa, false);}

    private:
      TermNamer m_term_namer;

      std::unordered_map<Variable, std::string> m_generated_names;
      std::vector<char> m_last_name;

      std::string generate_name();

      class ScopeEnter;
      class BracketHandler;

      void print_forall(std::ostream& os, const ForAll& fa, bool bracket);
      void print_implies(std::ostream& os, const Implies& im, bool bracket);
      void print_exists(std::ostream& os, const Exists& ex, bool bracket);
      void print_quantifier(std::ostream& os, const Quantifier& q);
      void print_variable(std::ostream& os, const Variable& v);
      void print_apply(std::ostream& os, const Apply& apply, bool bracket);
    };

    template<typename T>
    class BoundTypePrinter {
    public:
      BoundTypePrinter(BoundTypePrinter&&) = default;

      BoundTypePrinter(TypePrinter tp, const T* t) : m_tp(std::move(tp)), m_t(t) {
      }

      friend std::ostream& operator << (std::ostream& os, BoundTypePrinter&& pr) {
	pr.m_tp.print(os, *pr.m_t);
	return os;
      }

    private:
      TypePrinter m_tp;
      const T *m_t;
    };

    BoundTypePrinter<Type> print(const Type& ty, TermNamer term_namer) {
      return BoundTypePrinter<Type>(TypePrinter(std::move(term_namer)), &ty);
    }
  }
}

#endif
