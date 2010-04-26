#include "TypeSystem.hpp"
#include "LazyCopy.hpp"

#include <algorithm>
#include <iostream>
#include <list>

namespace Psi {
  namespace TypeSystem {
    namespace {
      class OccursChecker {
        const std::unordered_set<Variable> *m_variables;

      public:
        OccursChecker(const std::unordered_set<Variable>& variables) : m_variables(&variables) {
        }

        void forall_list(const std::vector<ForAll>& list);
        void forall(const ForAll& type);
        void quantifier(const Quantifier& quantifier);

        std::unordered_set<Variable> result;

      private:
      };

      void OccursChecker::forall(const ForAll& type) {
        quantifier(type.quantifier);
        quantifier(type.term.rhs.quantifier);
        forall_list(type.term.lhs);
        type.term.rhs.term.visit2
          ([this] (const Apply& apply) {this->forall_list(apply.parameters);},
           [this] (const Variable& v) {
             if (m_variables->find(v) != m_variables->end())
               result.insert(v);
           });
      }

      void OccursChecker::forall_list(const std::vector<ForAll>& list) {
        for (auto it = list.begin(); it != list.end(); ++it)
          forall(*it);
      }

      void OccursChecker::quantifier(const Quantifier& quantifier) {
        for (auto it = quantifier.constraints.begin(); it != quantifier.constraints.end(); ++it)
          forall_list(it->parameters);
      }

      class Matcher {
      public:
        Matcher(const std::unordered_set<Variable>& pattern_quantified)
          : m_quantified(pattern_quantified.begin(), pattern_quantified.end()) {
        }

        bool forall(const ForAll& pattern, const ForAll& binding, bool exact);
        bool exists(const Exists& pattern, const Exists& binding, bool exact);
        bool variable(const Variable& pattern, const Variable& binding, bool exact);
        bool apply(const Apply& pattern, const Apply& binding);

        std::unordered_set<Variable> quantified_variables();
        std::unordered_map<Variable, Type> build_substitutions();

      private:
        std::list<Variable> m_quantified;
        std::unordered_map<Variable, Type> m_substitutions;
        std::unordered_map<Variable, Variable> m_rename_map;

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
            for (auto it = m_quantifier->variables.begin(); it != m_quantifier->variables.end(); ++it) {
              Variable v = Variable::new_();
              m_matcher->m_rename_map.insert({*it, v});
              new_quantifier.variables.insert(new_quantifier.variables.end(), v);
            }
          }

          ~RenameEnter() {
            for (auto it = m_quantifier->variables.begin(); it != m_quantifier->variables.end(); ++it)
              m_matcher->m_rename_map.erase(*it);
          }

          Quantifier new_quantifier;

        private:
          Matcher *m_matcher;
          const Quantifier *m_quantifier;
        };

        bool variable_apply(const Apply& pattern, const Variable& binding);
        bool check_one_to_one(const std::unordered_set<Variable>& from,
                              const std::unordered_set<Variable>& to);

        Maybe<Type> rename_forall(const Variable& variable, const ForAll& type);
        Maybe<Type> rename_apply(const Variable& variable, const Apply& type);
        Maybe<Type> rename_variable(const Variable& variable, const Variable& type);
      };

      ForAll to_for_all(Type t) {
	return std::move(t.for_all());
      }

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
		return this->variable(pattern_var, binding_var, exact);
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

      bool Matcher::variable_apply(const Apply& pattern, const Variable& binding) {
        auto it = std::find(m_quantified.begin(), m_quantified.end(), binding);
        if (it != m_quantified.end()) {
          auto jt = m_substitutions.find(binding);
          if (jt != m_substitutions.end()) {
            return forall(to_for_all(pattern), jt->second.for_all(), true);
          } else {
            auto binding_sub = m_substitutions.find(binding);
            if (binding_sub == m_substitutions.end()) {
              Maybe<Type> t = rename_apply(binding, pattern);
              if (t) {
                m_substitutions.insert({binding, std::move(*t)});
                return true;
              } else {
                return false;
              }
            } else {
              return forall(to_for_all(pattern), binding_sub->second.for_all(), true);
            }
          }
        } else {
          return false;
        }
      }

      bool Matcher::variable(const Variable& pattern, const Variable& binding, bool exact) {
	if (pattern == binding)
	  return true;

	bool pattern_q, binding_q, pattern_first;
	for (auto it = m_quantified.begin(); it != m_quantified.end(); ++it) {
	  if (*it == pattern) {
	    pattern_q = true;
	    pattern_first = true;
	    binding_q = std::find(it, m_quantified.end(), binding) != m_quantified.end();
	    break;
	  } else if (*it == binding) {
	    binding_q = true;
	    pattern_first = false;
	    pattern_q = std::find(it, m_quantified.end(), pattern) != m_quantified.end();
	    break;
	  }
	}

	if (pattern_q) {
	  if (binding_q) {
	    auto pattern_sub = m_substitutions.find(pattern);

	    if (pattern_sub == m_substitutions.end()) {
	      if (pattern_first) {
		auto binding_sub = m_substitutions.find(binding);
		if (binding_sub == m_substitutions.end()) {
		  m_substitutions.insert({binding, to_for_all(pattern)});
		  return true;
		} else {
		  auto sub = rename_forall(pattern, binding_sub->second.for_all());
		  if (sub) {
		    m_substitutions.insert({pattern, std::move(*sub)});
		    return true;
		  } else {
		    return false;
		  }
		}
	      } else {
		m_substitutions.insert({pattern, to_for_all(binding)});
		return true;
	      }
	    } else {
	      auto binding_sub = m_substitutions.find(binding);
	      if (binding_sub == m_substitutions.end()) {
		if (pattern_first) {
		  m_substitutions.insert({binding, to_for_all(pattern)});
		  return true;
		} else {
		  auto sub = rename_forall(binding, pattern_sub->second.for_all());
		  if (sub) {
		    m_substitutions.insert({binding, std::move(*sub)});
		    return true;
		  } else {
		    return false;
		  }
		}
	      } else {
		return forall(pattern_sub->second.for_all(), binding_sub->second.for_all(), exact);
	      }
	    }
	  } else {
	    auto pattern_sub = m_substitutions.find(pattern);
	    if (pattern_sub != m_substitutions.end()) {
	      auto pattern_sub_var = pattern_sub->second.as_variable();
	      return pattern_sub_var && (*pattern_sub_var == binding);
	    } else {
	      m_substitutions.insert({pattern, binding});
	      return true;
	    }
	  }
	} else {
	  if (binding_q) {
	    auto binding_sub = m_substitutions.find(binding);
	    if (binding_sub != m_substitutions.end()) {
	      auto binding_sub_var = binding_sub->second.as_variable();
	      return binding_sub_var && (*binding_sub_var == binding);
	    } else {
	      m_substitutions.insert({binding, pattern});
	      return true;
	    }
	  } else {
	    // Neither quantified - since pattern==binding already
	    // checked, this is a mismatch.
	    return false;
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
          if (!forall(*it, *jt, true))
            return false;
        }

        return false;
      }

      Maybe<Type> Matcher::rename_forall(const Variable& var, const ForAll& type) {
        RenameEnter scope_all(this, type.quantifier);

        ForAll result;
        result.quantifier = std::move(scope_all.new_quantifier);

        for (auto it = type.term.lhs.begin(); it != type.term.lhs.end(); ++it) {
          Maybe<Type> t = rename_forall(var, *it);
          if (!t)
            return {};
          result.term.lhs.insert(result.term.lhs.end(), std::move(t->for_all()));
        }

        RenameEnter scope_exists(this, type.term.rhs.quantifier);
        result.term.rhs.quantifier = std::move(scope_exists.new_quantifier);

	auto rhs = result.term.rhs.term.visit2
	  ([&] (const Variable& type_var) {return this->rename_variable(var, type_var);},
	   [&] (const Apply& apply) {return this->rename_apply(var, apply);});

	if (!rhs)
	  return {};

	auto& rhs_type = rhs->for_all();

	result.quantifier.variables.insert(rhs_type.quantifier.variables.begin(),
					   rhs_type.quantifier.variables.end());

#if 0
	result.quantifier.constraints.insert(rhs_type.quantifier.constraints.begin(),
					     rhs_type.quantifier.constraints.end());
#endif

	result.term.lhs.insert(result.term.lhs.end(), rhs_type.term.lhs.begin(), rhs_type.term.lhs.end());

	result.term.rhs = std::move(rhs_type.term.rhs);

        return {result};
      }

      Maybe<Type> Matcher::rename_variable(const Variable& var, const Variable& type) {
	auto rename_type = m_rename_map.find(type);
	if (rename_type != m_rename_map.end())
	  return {to_for_all(rename_type->second)};

        // Prolog-style occurs check
        if (var == type)
          return {};

	for (auto it = m_quantified.begin(); it != m_quantified.end(); ++it) {
	  if (*it == var) {
	    auto type_sub = m_substitutions.find(type);
	    if (type_sub == m_substitutions.end()) {
	      Variable new_type = Variable::new_();
	      m_quantified.insert(it, new_type);
	      ForAll new_type_to_type = to_for_all(new_type);
	      m_substitutions.insert({type, new_type_to_type});
	      return {std::move(new_type_to_type)};
	    } else {
	      return rename_forall(var, type_sub->second.for_all());
	    }
	  } else if (*it == type) {
	    return {to_for_all(type)};
	  }
	}

        // Should never reach here since var should be in m_quantified
        assert(false);
      }

      Maybe<Type> Matcher::rename_apply(const Variable& var, const Apply& type) {
	Apply result;
	result.constructor = type.constructor;

	for (auto it = type.parameters.begin(); it != type.parameters.end(); ++it) {
	  Maybe<Type> term = rename_forall(var, *it);
	  if (!term)
	    return {};
	  result.parameters.push_back(std::move(term->for_all()));
	}

	return {to_for_all(std::move(result))};
      }

      std::unordered_set<Variable> Matcher::quantified_variables() {
        std::unordered_set<Variable> result;
        for (auto it = m_quantified.begin(); it != m_quantified.end(); ++it) {
          if (m_substitutions.find(*it) == m_substitutions.end())
            result.insert(*it);
        }
        return result;
      }

      std::unordered_map<Variable, Type> Matcher::build_substitutions() {
        std::unordered_map<Variable, Type> result;
        for (auto it = m_quantified.begin(); it != m_quantified.end(); ++it) {
          auto jt = m_substitutions.find(*it);
          if (jt != m_substitutions.end())
            result.insert({*it, substitute(jt->second, result)});
        }
        return result;
      }

      class Renamer {
      public:
	Type rename(const Type& t) {
	  return rename_forall(t.for_all());
	}

	ForAll rename_forall(const ForAll& fa) {
	  PushQuantifier pq(this, fa.quantifier);

	  ForAll result;
	  result.quantifier = std::move(pq.new_quantifier);

	  for (auto it = fa.term.lhs.begin(); it != fa.term.lhs.end(); ++it)
	    result.term.lhs.push_back(rename_forall(*it));

	  result.term.rhs = rename_exists(fa.term.rhs);

	  return result;
	}

	Exists rename_exists(const Exists& ex) {
	  PushQuantifier pq(this, ex.quantifier);

	  Exists result;
	  result.quantifier = std::move(pq.new_quantifier);

	  ex.term.visit2
	    (
	     [&] (const Variable& v) {
	       auto it = m_name_map.find(v);
	       result.term = (it == m_name_map.end()) ? v : it->second;
	     },
	     [&] (const Apply& a) {
	       result.term = this->rename_apply(a);
	     });

          assert(!result.term.empty());

	  return result;
	}

	Apply rename_apply(const Apply& a) {
	  Apply result;
	  result.constructor = a.constructor;

	  for (auto it = a.parameters.begin(); it != a.parameters.end(); ++it)
	    result.parameters.push_back(rename_forall(*it));

	  return result;
	}

      private:
	std::unordered_map<Variable, Variable> m_name_map;

	class PushQuantifier {
	public:
	  PushQuantifier(Renamer *self, const Quantifier& q) : m_self(self), m_q(&q) {
	    for (auto it = m_q->variables.begin(); it != m_q->variables.end(); ++it) {
	      Variable v = Variable::new_();
	      m_self->m_name_map.insert({*it, v});
	      new_quantifier.variables.insert(v);
	    }

#if 0
	    for (auto it = m_q->constraints.begin(); it != m_q->constraints.end(); ++it) {
	      Constraint c;
	      c.predicate = it->predicate;
	      for (auto jt = it->parameters.begin(); jt != it->parameters.end(); ++jt) {
		c.parameters.push_back(m_self->rename(*jt));
	      }
	      new_quantifier.constraints.insert(std::move(c));
	    }
#endif
	  }

	  ~PushQuantifier() {
	  }

	  Quantifier new_quantifier;

	private:
	  Renamer *m_self;
	  const Quantifier *m_q;
	};
      };

      void merge_quantifier(Quantifier& to, const Quantifier& from) {
	to.variables.insert(from.variables.begin(), from.variables.end());
#if 0
	to.constraints.insert(from.constraints.begin(), from.constraints.end());
#endif
      }
    }

    Type::Type(Variable var) : m_for_all({{}, {{}, {{}, std::move(var)}}}) {
    }

    Type::Type(Apply apply) : m_for_all({{}, {{}, {{}, std::move(apply)}}}) {
    }

    Type::Type(Exists exists) : m_for_all({{}, {{}, std::move(exists)}}) {
    }

    Type::Type(Implies implies) : m_for_all({{}, std::move(implies)}) {
    }

    Type::Type(ForAll for_all) : m_for_all(std::move(for_all)) {
    }

    Type::~Type() {
    }

    bool Type::is_variable() {
      return m_for_all.quantifier.variables.empty() &&
        m_for_all.term.lhs.empty() &&
        m_for_all.term.rhs.quantifier.variables.empty() &&
        m_for_all.term.rhs.term.contains<Variable>();
    }

    const Variable* Type::as_variable() const {
      if (m_for_all.quantifier.variables.empty() &&
          m_for_all.term.lhs.empty() &&
          m_for_all.term.rhs.quantifier.variables.empty()) {
        return m_for_all.term.rhs.term.get<Variable>();
      }

      return 0;
    }

    bool Type::operator == (const Type& rhs) const {
      Matcher matcher({});
      return matcher.forall(m_for_all, rhs.m_for_all, true);
    }

    bool Type::operator != (const Type& rhs) const {
      return !(*this == rhs);
    }

    std::unordered_set<Variable> Type::occurs(const std::unordered_set<Variable>& variables) const {
      OccursChecker checker(variables);
      checker.forall(m_for_all);
      return std::move(checker.result);
    }

    Maybe<Type> function_apply(const Type& function, const std::unordered_map<unsigned, Type>& arguments) {
      Matcher matcher(function.for_all().quantifier.variables);

      for (auto it = arguments.begin(); it != arguments.end(); ++it) {
        auto& pattern = function.for_all().term.lhs.at(it->first);

        if (!matcher.forall(pattern, it->second.for_all(), false))
          return {};
      }

      auto quantified = matcher.quantified_variables();
      auto substitutions = matcher.build_substitutions();

      auto rhs = substitute(function.for_all().term.rhs, substitutions);
      auto quantified_used = rhs.occurs(quantified);

      std::vector<Type> lhs;
      unsigned index = 0;
      for (auto it = function.for_all().term.lhs.begin();
           it != function.for_all().term.lhs.end(); ++it, ++index) {
        if (arguments.find(index) == arguments.end()) {
          auto term = substitute(*it, substitutions);
          auto used = term.occurs(quantified);
          quantified_used.insert(used.begin(), used.end());
          lhs.push_back(std::move(term));
        }
      }

#if 0
      // Need to substitute constraints and check which ones are still
      // necessary
#endif

      return for_all(quantified_used, implies(lhs, rhs));
    }

    Type for_all(const std::unordered_set<Variable>& variables, const Type& term) {
      return for_all(variables, term, std::unordered_set<Constraint>());
    }

    Type exists(const std::unordered_set<Variable>& variables, const Type& term) {
      return exists(variables, term, std::unordered_set<Constraint>());
    }

    Type for_all(const std::unordered_set<Variable>& variables, const Type& term, const std::unordered_set<Constraint>& constraints) {
      ForAll result = term.for_all();
      result.quantifier.variables.insert(variables.begin(), variables.end());
#if 0
      result.quantifier.constraints.insert(constraints.begin(), constraints.end());
#endif
      return Renamer().rename_forall(result);
    }

    Type exists(const std::unordered_set<Variable>& variables, const Type& term, const std::unordered_set<Constraint>& constraints) {
      const ForAll& fa = term.for_all();
      if (!fa.quantifier.variables.empty() ||
	  !fa.term.lhs.empty()) {
	throw std::runtime_error("invalid use of exists()");
      }

      Exists result = fa.term.rhs;
      result.quantifier.variables.insert(variables.begin(), variables.end());
#if 0
      result.quantifier.constraints.insert(constraints.begin(), constraints.end());
#endif
      return Renamer().rename_exists(result);
    }

    Type implies(const std::vector<Type>& lhs, const Type& rhs) {
      ForAll result;

      Renamer renamer;
      for (auto it = lhs.begin(); it != lhs.end(); ++it) {
	const ForAll& fa = it->for_all();
	if (fa.quantifier.variables.empty() && fa.term.lhs.empty()) {
	  Exists ex2 = renamer.rename_exists(fa.term.rhs);
	  merge_quantifier(result.quantifier, ex2.quantifier);
	  result.term.lhs.push_back(ForAll({{}, {{}, {{}, std::move(ex2.term)}}}));
	} else {
	  result.term.lhs.push_back(renamer.rename_forall(fa));
	}
      }

      const ForAll& fa = rhs.for_all();
      merge_quantifier(result.quantifier, fa.quantifier);
      result.term.rhs = renamer.rename_exists(fa.term.rhs);

      return result;
    }

    Type apply(Constructor constructor, std::vector<Type> parameters) {
      std::vector<ForAll> my_parameters;
      for (auto it = parameters.begin(); it != parameters.end(); ++it)
	my_parameters.push_back(std::move(it->for_all()));
      return Type(Apply({std::move(constructor), std::move(my_parameters)}));
    }

    namespace {
      class Substituter {
      public:
        Substituter(const std::unordered_map<Variable, Type>& substitutions) : m_substitutions(&substitutions) {
        }

        Type sub_forall(const ForAll& fa) const {
          return for_all(fa.quantifier.variables, sub_implies(fa.term),
                         sub_constraints(fa.quantifier.constraints));
        }

        Type sub_implies(const Implies& im) const {
          std::vector<Type> lhs;
          for (auto it = im.lhs.begin(); it != im.lhs.end(); ++it)
            lhs.push_back(sub_forall(*it));
          return implies(std::move(lhs), sub_exists(im.rhs));
        }

        Type sub_exists(const Exists& ex) const {
          Type rhs = ex.term.visit2
            ([this] (const Variable& var) {return this->sub_variable(var);},
             [this] (const Apply& apply) {return this->sub_apply(apply);});

          return exists(ex.quantifier.variables, rhs,
                        sub_constraints(ex.quantifier.constraints));
        }

        Type sub_variable(const Variable& var) const {
          auto it = m_substitutions->find(var);
          if (it != m_substitutions->end())
            return it->second;
          return var;
        }

        Type sub_apply(const Apply& ap) const {
          std::vector<Type> parameters;
          for (auto it = ap.parameters.begin(); it != ap.parameters.end(); ++it)
            parameters.push_back(sub_forall(*it));
          return apply(ap.constructor, std::move(parameters));
        }

        std::unordered_set<Constraint> sub_constraints(const std::unordered_set<Constraint>& cons) const {
          std::unordered_set<Constraint> new_cons;
#if 0
          for (auto it = cons.begin(); it != cons.end(); ++it) {
            Constraint current;
            current.predicate = it->predicate;
            for (auto jt = it->parameters.begin(); jt != it->parameters.end(); ++jt)
              current.parameters.push_back(std::move(sub_forall(*jt)));
            new_cons.insert(std::move(current));
          }
#endif
          return new_cons;
        }

      private:
        const std::unordered_map<Variable, Type> *m_substitutions;
      };
    }

    Type substitute(const Type& type, const std::unordered_map<Variable, Type>& substitutions) {
      return Substituter(substitutions).sub_forall(type.for_all());
    }

    namespace {
      const char *for_all_str = u8"\u2200";
      const char *exists_str = u8"\u2203";
      const char *right_arrow_str = u8"\u2192";
    }

    class TypePrinter::ScopeEnter {
    public:
      ScopeEnter(TypePrinter *printer, const Quantifier& quantifier) : m_printer(printer), m_quantifier(&quantifier) {
	for (auto it = m_quantifier->variables.begin(); it != m_quantifier->variables.end(); ++it)
	  m_printer->m_generated_names.insert({*it, m_printer->generate_name()});
      }

      ~ScopeEnter() {
	for (auto it = m_quantifier->variables.begin(); it != m_quantifier->variables.end(); ++it)
	  m_printer->m_generated_names.erase(*it);
      }

    private:
      TypePrinter *m_printer;
      const Quantifier *m_quantifier;
    };

    class TypePrinter::BracketHandler {
    public:
      BracketHandler(std::ostream& os, bool bracket, bool needed) : m_os(&os) {
	if (bracket && needed) {
	  *m_os << "(";
	  m_printed_bracket = true;
	  m_child_bracket = false;
	} else {
	  m_printed_bracket = false;
	  m_child_bracket = bracket;
	}
      }

      ~BracketHandler() {
	if (m_printed_bracket)
	  *m_os << ")";
      }

      bool child_bracket() const {return m_child_bracket;}

    private:
      bool m_child_bracket, m_printed_bracket;
      std::ostream *m_os;
    };

    TypePrinter::TypePrinter(TermNamer term_namer) : m_term_namer(std::move(term_namer)) {
    }

    TypePrinter::~TypePrinter() {
    }

    void TypePrinter::reset() {
      m_last_name.clear();
    }

    std::string TypePrinter::generate_name() {
      bool hit = false;
      for (auto it = m_last_name.rbegin(); it != m_last_name.rend(); ++it) {
	if (*it != 'z') {
	  ++*it;
	  hit = true;
	  break;
	} else {
	  *it = 'a';
	}
      }
      if (!hit)
	m_last_name.push_back('a');

      return std::string(m_last_name.begin(), m_last_name.end());
    }

    void TypePrinter::print_forall(std::ostream& os, const ForAll& fa, bool bracket) {
      ScopeEnter scope(this, fa.quantifier);
      BracketHandler bh(os, bracket, !fa.quantifier.variables.empty());

      if (!fa.quantifier.variables.empty()) {
	os << for_all_str;
	print_quantifier(os, fa.quantifier);
      }

      print_implies(os, fa.term, bh.child_bracket());
    }

    void TypePrinter::print_implies(std::ostream& os, const Implies& im, bool bracket) {
      BracketHandler bh(os, bracket, !im.lhs.empty());

      for (auto it = im.lhs.begin(); it != im.lhs.end(); ++it) {
	print_forall(os, *it, true);
	os << " " << right_arrow_str << " ";
      }

      print_exists(os, im.rhs, bh.child_bracket());
    }

    void TypePrinter::print_exists(std::ostream& os, const Exists& ex, bool bracket) {
      ScopeEnter scope(this, ex.quantifier);
      BracketHandler bh(os, bracket, !ex.quantifier.variables.empty());

      if (!ex.quantifier.variables.empty()) {
	os << exists_str;
	print_quantifier(os, ex.quantifier);
      }

      ex.term.visit2([&] (const Variable& v) {this->print_variable(os, v);},
		     [&] (const Apply& a) {this->print_apply(os, a, bracket);});
    }

    void TypePrinter::print_quantifier(std::ostream& os, const Quantifier& q) {
      for (auto it = q.variables.begin(); it != q.variables.end(); ++it) {
	os << " ";
	print_variable(os, *it);
      }
      os << ".";
    }

    void TypePrinter::print_variable(std::ostream& os, const Variable& v) {
      auto it = m_generated_names.find(v);
      if (it != m_generated_names.end()) {
	os << it->second;
      } else {
	os << m_term_namer.variable_namer(v);
      }
    }

    void TypePrinter::print_apply(std::ostream& os, const Apply& apply, bool bracket) {
      BracketHandler bh(os, bracket, !apply.parameters.empty());

      os << m_term_namer.constructor_namer(apply.constructor);
      for (auto it = apply.parameters.begin(); it != apply.parameters.end(); ++it) {
	os << " ";
	print_forall(os, *it, true);
      }
    }
  }
}
