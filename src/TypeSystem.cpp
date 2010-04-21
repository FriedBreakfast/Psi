#include "TypeSystem.hpp"

namespace Psi {
  namespace TypeSystem {
    bool operator == (const NumberExpression& lhs, const NumberExpression& rhs) {
      return lhs.terms() == rhs.terms();
    }

    bool operator != (const NumberExpression& lhs, const NumberExpression& rhs) {
      return lhs.terms() != rhs.terms();
    }

    Type NumberExpression::simplify(TermSet&& terms) {
      if (terms.size() == 1) {
      }

      return NumberExpression(std::make_shared<Data>(Data{std::move(terms)}));
    }

    Type operator + (const NumberExpression& lhs, const NumberExpression& rhs) {
      auto result = rhs.terms();

      for (auto it = lhs.terms().begin(); it != lhs.terms().end(); ++it) {
        auto jt = result.find(it->first);
        if (jt == result.end()) {
          result.insert(*it);
        } else {
          jt->second += it->second;
          if (jt->second == 0)
            result.erase(jt);
        }
      }

      return NumberExpression::simplify(std::move(result));
    }

    Type operator - (const NumberExpression& lhs, const NumberExpression& rhs) {
      auto result = lhs.terms();

      for (auto it = rhs.terms().begin(); it != rhs.terms().end(); ++it) {
        auto jt = result.find(it->first);
        if (jt == result.end()) {
          result.insert(*it);
        } else {
          jt->second += it->second;
          if (jt->second == 0)
            result.erase(jt);
        }
      }

      return NumberExpression::simplify(std::move(result));
    }

    Type operator * (const NumberExpression& lhs, const NumberExpression& rhs) {
      NumberExpression::TermSet result;

      for (auto it = lhs.terms().begin(); it != lhs.terms().end(); ++it) {
        for (auto jt = rhs.terms().begin(); jt != rhs.terms().end(); ++jt) {
          auto new_term = it->first;
          new_term.insert(jt->first.begin(), jt->first.end());
          int coefficient = it->second * jt->second;

          auto kt = result.find(new_term);
          if (kt == result.end()) {
            result.insert({std::move(new_term), coefficient});
          } else {
            kt->second += coefficient;
            if (kt->second == 0)
              result.erase(kt);
          }
        }
      }

      return NumberExpression::simplify(std::move(result));
    }

    namespace {
      bool pattern_match(std::unordered_map<TypeVariable, Type>& variable_map,
                         const std::unordered_set<TypeVariable>& match_variables,
                         const Type& pattern, const Type& check) {
        return check.visit_default
          (
           false,
           [&] (const TypeVariable& check_var) {
             auto it = variable_map.find(check_var);
             if (it == variable_map.end()) {
               variable_map.insert({check_var, pattern});
               return true;
             } else {
               return it->second == pattern;
             }
           },
           [&] (const FunctionType& check_func) {
             return pattern.visit_default
               (
                false,
                [&] (const FunctionType& pattern_func) {
                  if (pattern_func.parameters.size() != check_func.parameters.size())
                    return false;
                  // This is going to get nasty!
                  return false;
                });
           },
           [&] (const ReferenceType& check_ref) {
             return pattern.visit_default
               (
                false,
                [&] (const ReferenceType& pattern_ref) {
                  return false;
                });
           },
           [&] (const NumberExpression& check_num) {
             return pattern.visit_default
               (
                false,
                [&] (const NumberPattern& pattern_num) {
                  return false;
                });
           });
      }
    }

    Maybe<Type> apply_function(const FunctionType& function, const std::vector<Type>& parameters) {
      std::unordered_map<TypeVariable, Type> variable_map;

      if (function.parameters().size() != parameters.size()) {
        return {};
      }

      for (auto it = function.parameters().begin(), jt = parameters.begin();
           it != function.parameters().end(); ++it, ++jt) {
        if (!pattern_match(variable_map, function.variables(), *it, *jt))
          return {};
      }

      return {};
    }
  }
}
