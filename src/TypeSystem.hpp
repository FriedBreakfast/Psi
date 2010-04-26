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
  template<typename T> struct hash<Psi::TypeSystem::Identifier<T> > : Psi::TypeSystem::ContextHash<Psi::TypeSystem::Identifier<T> > {};
  //template<> struct hash<Psi::TypeSystem::Constraint> : Psi::TypeSystem::ContextHash<Psi::TypeSystem::Constraint> {};
}

namespace Psi {
  namespace TypeSystem {
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
      /**
       * \brief Find which of the specified variables are used in a given type.
       */
      std::unordered_set<Variable> occurs(const std::unordered_set<Variable>& variables) const;

      bool operator == (const Type& rhs) const;
      bool operator != (const Type& rhs) const;

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

    Type substitute(const Type& type, const std::unordered_map<Variable, Type>& substitutions);

    Maybe<Type> function_apply(const Type& function, const std::unordered_map<unsigned, Type>& arguments);

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

      void reset();

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

      friend std::ostream& operator << (std::ostream& os, const BoundTypePrinter& pr) {
        pr.m_tp.reset();
        pr.m_tp.print(os, *pr.m_t);
        return os;
      }

    private:
      mutable TypePrinter m_tp;
      const T *m_t;
    };

    BoundTypePrinter<Type> print(const Type& ty, TermNamer term_namer) {
      return BoundTypePrinter<Type>(TypePrinter(std::move(term_namer)), &ty);
    }
  }
}

#endif
