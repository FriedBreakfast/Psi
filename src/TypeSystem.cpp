#include "TypeSystem.hpp"
#include "LazyCopy.hpp"

#include <algorithm>
#include <list>

#if 0

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

#endif


namespace Psi {
  namespace TypeSystem2 {
    bool is_variable(const Type& type) {
      return type.quantifier.variables.empty() &&
        type.term.lhs.empty() &&
        type.term.rhs.quantifier.variables.empty() &&
        type.term.rhs.term.contains<Variable>();
    }

    const Variable *as_variable(const Type& type) {
      if (type.quantifier.variables.empty() &&
          type.term.lhs.empty() &&
          type.term.rhs.quantifier.variables.empty()) {
        return type.term.rhs.term.get<Variable>();
      }

      return {};
    }

    namespace {
      class OccursChecker {
        const std::unordered_set<Variable> m_variables;

      public:
        OccursChecker(const std::unordered_set<Variable>& variables) : m_variables(variables) {
        }

        bool forall_list(const std::vector<ForAll>& list);
        bool forall(const ForAll& type);
        bool quantifier(const Quantifier& quantifier);
      };

      bool OccursChecker::forall(const ForAll& type) {
        return 
          quantifier(type.quantifier) ||
          quantifier(type.term.rhs.quantifier) ||
          forall_list(type.term.lhs) ||
          type.term.rhs.term.visit2([this] (const Apply& apply) {return this->forall_list(apply.parameters);},
                                    [this] (const Variable& v) {return m_variables.find(v) != m_variables.end();});
      }

      bool OccursChecker::forall_list(const std::vector<ForAll>& list) {
        for (auto it = list.begin(); it != list.end(); ++it) {
          if (forall(*it))
            return true;
        }
        return false;
      }

      bool OccursChecker::quantifier(const Quantifier& quantifier) {
        for (auto it = quantifier.constraints.begin(); it != quantifier.constraints.end(); ++it) {
          if (forall_list(it->parameters))
            return true;
        }
        return false;
      }
    }

    bool occurs(const Type& type, const std::unordered_set<Variable>& variables) {
      return OccursChecker(variables).forall(type);
    }

    namespace {
      Type to_type(Variable variable) {
        return {{}, {{}, {{}, std::move(variable)}}};
      }

      Type to_type(Apply apply) {
        return {{}, {{}, {{}, std::move(apply)}}};
      }

      Type to_type(Exists exists) {
        return {{}, {{}, std::move(exists)}};
      }

      Type to_type(Implies implies) {
        return {{}, std::move(implies)};
      }

      class Matcher {
      public:
        Matcher(const std::unordered_set<Variable>& pattern_quantified)
          : m_quantified(pattern_quantified.begin(), pattern_quantified.end()) {
        }

        bool forall(const ForAll& pattern, const ForAll& binding, bool exact);
        bool exists(const Exists& pattern, const Exists& binding, bool exact);
        bool variable(const Variable& pattern, const Variable& binding, bool exact);
        bool apply(const Apply& pattern, const Apply& binding, bool exact);

      private:
        std::list<Variable> m_quantified;
        std::unordered_map<Variable, Type> m_substitutions;
        std::unordered_map<Variable, Type> m_rename_map;

        bool variable_apply(const Apply& pattern, const Variable& binding, bool exact);

        class StackEnter {
        public:
          StackEnter(Matcher *matcher, const std::unordered_set<Variable>& variables) : m_matcher(matcher) {
            m_it = matcher->m_quantified.end();
            matcher->m_quantified.insert(matcher->m_quantified.end(), variables.begin(), variables.end());
          }

          ~StackEnter() {
            for (auto it = m_it; it != m_matcher->m_quantified.end(); ++it)
              m_matcher->m_substitutions.erase(*it);
            m_matcher->m_quantified.erase(m_it, m_matcher->m_quantified.end());
          }

        private:
          Matcher *m_matcher;
          std::list<Variable>::iterator m_it;
        };

        class RenameEnter {
        public:
          RenameEnter(Matcher *matcher, const Quantifier& quantifier) : m_matcher(matcher), m_quantifier(&quantifier) {
            for (auto it = m_quantifier->variables.begin(); it != m_quantifier.variables.end(); ++it) {
              Variable v = Variable::new_();
              m_matcher->rename_map.insert({*it, v});
              new_quantifier.variables.insert(new_quantifier.variables.end(), v);
            }
          }

          ~RenameEnter() {
            for (auto it = m_quantifier->variables.begin(); it != m_quantifier.variables.end(); ++it)
              m_matcher->rename_map.erase(*it);
          }

          Quantifier new_quantifier;

        private:
          Matcher *m_matcher;
          Quantifier *m_quantifier;
        };

        Maybe<Type> rename(const Variable& variable, const Type& type);

        bool check_one_to_one(const std::unordered_set<Variable>& from,
                              const std::unordered_set<Variable>& to);
      };

      /**
       * Check there is a one to one mapping between variables in \c
       * from and variables in \c to.
       */
      bool Matcher::check_one_to_one(const std::unordered_set<Variable>& from,
                                     const std::unordered_set<Variable>& to) {
        std::unordered_set<Variable> matched = to;
        for (auto it = from.begin(); it != from.end(); ++it) {
          if (!matched.erase(*it))
            return false;
        }

        return matched.empty();
      }

      bool Matcher::forall(const ForAll& pattern, const ForAll& binding, bool exact) {
        if (exact && (pattern.quantifier.variables.size() != binding.quantifier.variables.size()))
          return false;

        if (pattern.term.lhs.size() != binding.term.lhs.size())
          return false;

        StackEnter scope(this, binding.quantifier.variables);

        for (auto it = pattern.term.lhs.begin(), jt = binding.term.lhs.begin();
             it != pattern.term.lhs.end(); ++it, ++jt) {
          if (!forall(*jt, *it, exact))
            return false;
        }

        if (!exists(binding.term.rhs, pattern.term.rhs, exact))
          return false;

        if (exact && !check_one_to_one(binding.quantifier.variables, pattern.quantifier.variables))
          return false;

        return true;
      }

      bool Matcher::exists(const Exists& pattern, const Exists& binding, bool exact) {
        if (exact && (pattern.quantifier.variables.size() != binding.quantifier.variables.size()))
          return false;

        StackEnter scope(this, binding.quantifier.variables);

        bool result = pattern.term.visit2
          (
           [&] (const Variable& pattern_var) {
             return binding.term.visit2
               (
                [&] (const Variable& binding_var) {
                  return this->variable(pattern_var, binding_var);
                },
                [&] (const Apply& binding_apply) {
                  return this->variable_apply(binding_apply, pattern_var);
                });
           },
           [&] (const Apply& pattern_apply) {
             return binding.term.visit2
               (
                [&] (const Variable& binding_var) {
                  return this->variable_apply(pattern_apply, binding_var);
                },
                [&] (const Apply& binding_apply) {
                  return this->apply(pattern_apply, binding_apply);
                });
           });

        if (exact && !check_one_to_one(binding.quantifier.variables, pattern.quantifier.variables))
          return false;

        return result;
      }

      bool Matcher::variable_apply(const Apply& pattern, const Variable& binding, bool exact) {
        auto it = std::find(m_quantified.begin(), m_quantified.end(), binding);
        if (it != m_quantified.end()) {
          auto jt = m_substitutions.find(binding);
          if (jt != m_substitutions.end()) {
            return forall(to_type(pattern), jt->second, true);
          } else {
            auto binding_sub = m_substitutions.find(binding);
            if (binding_sub == m_substitutions.end()) {
              Maybe<Type> t = rename(binding, pattern);
              if (t) {
                m_substitutions.insert({binding, std::move(*t)});
                return true;
              } else {
                return false;
              }
            } else {
              return forall(to_type(pattern), *binding_sub, true);
            }
          }
        } else {
          return false;
        }
      }

      bool Matcher::variable(const Variable& pattern, const Variable& binding, bool exact) {
        assert(pattern != binding);

        auto pattern_sub = m_substitutions.find(pattern);
        auto binding_sub = m_substitutions.find(binding);

        if (pattern_sub == m_substitutions.end()) {
          if (binding_sub == m_substitutions.end()) {
            for (auto it = m_quantified.begin(); it != m_quantified.end(); ++it) {
              if (*it == pattern) {
                substitute(binding, to_type(pattern));
                return true;
              } else if (*it == binding) {
                substitute(pattern, to_type(binding));
                return true;
              }
            }

            // Neither is quantified over, hence they are distinct.
            return false;
          } else {
            return rename(pattern, *binding_sub);
          }
        } else {
          if (binding_sub == m_substitutions.end()) {
            return rename(binding, *pattern_sub);
          } else {
            return forall(*pattern_sub, *binding_sub, exact);
          }
        }
      }

      bool Matcher::apply(const Apply& pattern, const Apply& binding) {
        if (pattern.constructor != binding.constructor)
          return false;

        if (pattern.parameters.size() != binding.parameters.size())
          return false;

        for (auto it = pattern.parameters.begin(), jt = binding.parameters.begin();
             it != pattern.parameters.end(); ++it, ++jt) {
          if (!forall_exact(*it, *jt))
            return false;
        }

        return false;
      }

      Maybe<Type> Matcher::rename(const Variable& var, const ForAll& type) {
        RenameEnter scope_all(this, type.quantifier);

        ForAll result;
        result.quantifier = std::move(scope_all.new_quantifier);

        for (auto it = type.term.lhs.begin(); it != type.term.lhs.end(); ++it) {
          Maybe<Type> t = rename(var, *it);
          if (!t)
            return {};
          result.term.lhs.insert(lhs.end(), std::move(*t));
        }

        RenameEnter scope_exists(this, type.term.rhs.quantifier);
        result.term.rhs.quantifier = std::move(scope_exists.new_quantifier);

        if (!result.term.rhs.term.visit2
            ([] (const Variable& var2) {},
             [] (const Apply& apply) {}))
          return {};

        return {result};
      }

      bool Matcher::rename_variable(const Variable& var, const Variable& to_rename) {
      }
    }

    Maybe<Type> apply(const Type& function, const std::unordered_map<unsigned, Type>& arguments) {
      Matcher matcher(function.quantifier.variables);

      for (auto it = arguments.begin(); it != arguments.end(); ++it) {
        const Type& pattern = function.term.lhs.at(it->first);

        if (!matcher.forall(pattern, it->second, false))
          return {};
      }

      return {};
    }
  }
}
