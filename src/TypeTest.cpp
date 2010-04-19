#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>

#include "Variant.hpp"

namespace Psi {
  namespace TypeSystem {
    struct FunctionType;
    struct ReferenceType;

    struct TypeVariable {
    };

    struct NumberVariable {
    };

    typedef Variant<std::shared_ptr<TypeVariable>,
                    std::shared_ptr<FunctionType>,
                    std::shared_ptr<ReferenceType>,
                    std::shared_ptr<NumberVariable> > Type;

    struct Constraint {
    };

    struct TypeContext {
      std::unordered_set<TypeVariable> variables;
      std::unordered_set<Constraint> constraints;
    };

    /**
     * Function type - also support universal quantification.
     */
    struct FunctionType {
      TypeContext context;
      std::vector<Type> arguments;

      typedef Variant<TypeVariable, std::shared_ptr<FunctionType> > ResultType;
      ResultType result;
    };

    /**
     * Reference type - supports existential quantification.
     */
    struct ReferenceType {
      /**
       * Existentially quantified variables and interfaces.
       */
      TypeContext context;
      Type type;
    };

    struct NumberExpression {
      /**
       * Type of a term. A term is an expression of the form a*b*c*... .
       */
      typedef std::unordered_set<NumberVariable> Term;

      /**
       * Set of terms making up this expression. The keys are the
       * terms, the values are the term coefficients.
       */
      std::unordered_map<Term, int> terms;
    };
  }
}

int main() {
}
