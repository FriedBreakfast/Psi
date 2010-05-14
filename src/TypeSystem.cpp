#include "TypeSystem.hpp"
#include "LazyCopy.hpp"

#include <algorithm>
#include <iostream>
#include <list>

namespace Psi {
  namespace TypeSystem {
    namespace {
      class Renamer {
      public:
	Renamer() {
	}

	Renamer(std::unordered_map<Variable, Variable> name_map) : m_name_map(std::move(name_map)) {
	}

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
	  return {std::move(pq.new_quantifier), rename_atom(ex.term)};
	}

	Atom rename_atom(const Atom& at) {
	  Atom result = at.visit2
	    (
	     [&] (const Variable& v) -> Atom {
	       auto it = m_name_map.find(v);
	       return (it == m_name_map.end()) ? v : it->second;
	     },
	     [&] (const Apply& a) -> Atom {
	       return this->rename_apply(a);
	     });

	  assert(!result.empty());

	  return result;
	}

	Apply rename_apply(const Apply& a) {
	  Apply result;
	  result.constructor = a.constructor;

	  for (auto it = a.parameters.begin(); it != a.parameters.end(); ++it)
	    result.parameters.push_back(rename_forall(*it));

	  return result;
	}

	Constraint rename_constraint(const Constraint& c) {
	  Constraint result;
	  result.predicate = c.predicate;
	  for (auto it = c.parameters.begin(); it != c.parameters.end(); ++it)
	    result.parameters.push_back(std::move(rename_forall(*it)));
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

	    for (auto it = m_q->constraints.begin(); it != m_q->constraints.end(); ++it)
	      new_quantifier.constraints.push_back(m_self->rename_constraint(*it));
	  }

	  ~PushQuantifier() {
	  }

	  Quantifier new_quantifier;

	private:
	  Renamer *m_self;
	  const Quantifier *m_q;
	};
      };

      class OccursChecker {
	bool m_check_only;
        const std::unordered_set<Variable> *m_variables;

      public:
        OccursChecker(bool check_only, const std::unordered_set<Variable>& variables) : m_check_only(check_only), m_variables(&variables), found(false) {
        }

	void atom(const Atom& atom);
        void forall(const ForAll& type);
        void forall_list(const std::vector<ForAll>& list);
        void quantifier(const Quantifier& quantifier);
	void constraint(const Constraint& constraint);

	bool found;
        std::unordered_set<Variable> result;
      };

      void OccursChecker::forall(const ForAll& type) {
        quantifier(type.quantifier);
	if (m_check_only && found) return;
        quantifier(type.term.rhs.quantifier);
	if (m_check_only && found) return;
        forall_list(type.term.lhs);
	if (m_check_only && found) return;
	atom(type.term.rhs.term);
      }

      void OccursChecker::forall_list(const std::vector<ForAll>& list) {
	for (auto it = list.begin(); it != list.end(); ++it) {
	  forall(*it);
	  if (m_check_only && found)
	    return;
	}
      }

      void OccursChecker::quantifier(const Quantifier& quantifier) {
        for (auto it = quantifier.constraints.begin(); it != quantifier.constraints.end(); ++it) {
	  constraint(*it);
	  if (m_check_only && found)
	    return;
	}
      }

      void OccursChecker::atom(const Atom& atom) {
        atom.visit2
          (
	   [this] (const Apply& apply) {
	     this->forall_list(apply.parameters);
	   },
           [this] (const Variable& v) {
             if (m_variables->find(v) != m_variables->end()) {
	       if (m_check_only)
		 found = true;
	       else
		 result.insert(v);
	     }
           });
      }

      void OccursChecker::constraint(const Constraint& cons) {
	for (auto it = cons.parameters.begin(); it != cons.parameters.end(); ++it) {
	  forall(*it);
	  if (m_check_only && found)
	    return;
	}
      }

      class Matcher {
      public:
	Matcher() {
	}

        Matcher(const std::unordered_set<Variable>& pattern_quantified)
          : m_quantified(pattern_quantified.begin(), pattern_quantified.end()) {
        }

        bool forall(const ForAll& pattern, const ForAll& binding, bool exact);
        bool exists(const Exists& pattern, const Exists& binding, bool exact);
        bool variable(const Variable& pattern, const Variable& binding, bool exact);
        bool apply(const Apply& pattern, const Apply& binding);
	bool constraint(const Constraint& pattern, const Constraint& binding, bool exact);

        std::unordered_set<Variable> quantified_variables();
        std::unordered_map<Variable, Type> build_substitutions();

      private:
        std::list<Variable> m_quantified;
        std::unordered_map<Variable, Type> m_substitutions;
        std::unordered_map<Variable, Variable> m_rename_map;
	std::unordered_map<Variable, Type> m_predicate_binding_map;

        class StackEnter {
        public:
          StackEnter(Matcher *matcher, const Quantifier& quantifier) : m_matcher(matcher) {
            m_it = matcher->m_quantified.end();
            matcher->m_quantified.insert(matcher->m_quantified.end(), quantifier.variables.begin(), quantifier.variables.end());
          }

          ~StackEnter() {
            try {
              for (auto it = m_it; it != m_matcher->m_quantified.end(); ++it)
                m_matcher->m_substitutions.erase(*it);
              m_matcher->m_quantified.erase(m_it, m_matcher->m_quantified.end());
            } catch (...) {
            }
          }

        private:
          Matcher *m_matcher;
          std::list<Variable>::iterator m_it;
        };

        bool variable_apply(const Apply& pattern, const Variable& binding);
        bool check_one_to_one(const std::unordered_set<Variable>& from,
                              const std::unordered_set<Variable>& to);

	bool quantifier(const Quantifier& pattern, const Quantifier& binding, bool exact);

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
        assert(from.size() == to.size());

        std::unordered_set<Variable> matched = to;
        for (auto it = from.begin(); it != from.end(); ++it) {
          auto jt = m_substitutions.find(*it);
          if (jt == m_substitutions.end())
            return false;

          const Variable *var = jt->second.as_variable();
          if (!var)
            return false;

          if (!matched.erase(*var))
            return false;
        }

        assert(matched.empty());
        return true;
      }

      bool Matcher::forall(const ForAll& pattern, const ForAll& binding, bool exact) {
        if (exact && (pattern.quantifier.variables.size() != binding.quantifier.variables.size()))
          return false;

        if (pattern.term.lhs.size() != binding.term.lhs.size())
          return false;

        StackEnter scope(this, binding.quantifier);

        for (auto it = pattern.term.lhs.begin(), jt = binding.term.lhs.begin();
             it != pattern.term.lhs.end(); ++it, ++jt) {
          if (!forall(*jt, *it, exact))
            return false;
        }

        if (!exists(binding.term.rhs, pattern.term.rhs, exact))
          return false;

	// Need to check this last so that all inner bindings have
	// been generated, since constraint checks cannot generate
	// bindings themselves.
        if (!quantifier(pattern.quantifier, binding.quantifier, exact))
          return false;

        if (exact && !check_one_to_one(binding.quantifier.variables, pattern.quantifier.variables))
          return false;

        return true;
      }

      bool Matcher::exists(const Exists& pattern, const Exists& binding, bool exact) {
        if (exact && (pattern.quantifier.variables.size() != binding.quantifier.variables.size()))
          return false;

        StackEnter scope(this, binding.quantifier);

	if (!pattern.term.visit2
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
           }))
          return false;

        if (!quantifier(pattern.quantifier, binding.quantifier, exact))
          return false;

        if (exact && !check_one_to_one(binding.quantifier.variables, pattern.quantifier.variables))
          return false;

        return true;
      }

      bool Matcher::quantifier(const Quantifier& pattern, const Quantifier& binding, bool exact) {
        if (exact && (pattern.constraints.size() != binding.constraints.size()))
          return false;

	class HideQuantified {
	public:
	  HideQuantified(Matcher *matcher) : m_matcher(matcher) {
	    std::swap(m_quantified, m_matcher->m_quantified);
	  }
	
	  ~HideQuantified() {
	    try {
	      std::swap(m_quantified, m_matcher->m_quantified);
	    } catch(...) {
	    }
	  }

	private:
	  Matcher *m_matcher;
	  std::list<Variable> m_quantified;
	};
	// Need to hide quantified variables to prevent bindings to these being generated
	HideQuantified hq(this);

        for (auto it = pattern.constraints.begin(); it != pattern.constraints.end(); ++it) {
          bool found = false;
          for (auto jt = binding.constraints.begin(); jt != binding.constraints.end(); ++jt) {
	    if (constraint(*it, *jt, exact)) {
	      found = true;
	      break;
	    }
	  }

	  if (!found)
	    return false;
        }

        return true;
      }

      bool Matcher::constraint(const Constraint& pattern, const Constraint& binding, bool exact) {
	if (pattern.predicate == binding.predicate) {
	  assert(pattern.parameters.size() == binding.parameters.size());
	  bool good = true;
	  for (auto it = pattern.parameters.begin(), jt = binding.parameters.begin(); it != pattern.parameters.end(); ++it, ++jt) {
	    if (!forall(*it, *jt, true)) {
	      good = false;
	      break;
	    }
	  }

	  if (good)
	    return true;
	} else if (!exact) {
	  assert(binding.parameters.size() == binding.predicate.parameters().size());
	  std::unordered_map<Variable, Type> substitutions;
	  {
	    auto it = binding.parameters.begin();
	    auto jt = binding.predicate.parameters().begin();
	    for (; it != binding.parameters.end(); ++it)
	      substitutions.insert({*jt, *it});
	  }

	  for (auto it = binding.predicate.constraints().begin(); it != binding.predicate.constraints().end(); ++it) {
	    Constraint child_binding;
	    child_binding.predicate = it->predicate;

	    for (auto jt = it->parameters.begin(); jt != it->parameters.end(); ++jt)
	      child_binding.parameters.push_back(std::move(substitute(*jt, substitutions).for_all()));

	    if (constraint(pattern, child_binding, exact))
	      return true;
	  }
	}

	return false;
      }

      bool Matcher::variable_apply(const Apply& pattern, const Variable& binding) {
        auto it = m_substitutions.find(binding);
        if (it != m_substitutions.end()) {
          return forall(to_for_all(pattern), it->second.for_all(), true);
        }

        auto jt = std::find(m_quantified.begin(), m_quantified.end(), binding);
        if (jt != m_quantified.end()) {
          auto t = rename_apply(binding, pattern);
          if (t) {
            m_substitutions.insert({binding, std::move(*t)});
            return true;
          }
        }

        return false;
      }

      bool Matcher::variable(const Variable& pattern, const Variable& binding, bool exact) {
	if (pattern == binding)
	  return true;

	bool pattern_q = false, binding_q = false, pattern_first = false;
        for (auto it = m_quantified.begin(); it != m_quantified.end(); ++it) {
          if (*it == pattern) {
            pattern_q = true;
            pattern_first = true;
            binding_q = std::find(it, m_quantified.end(), binding) != m_quantified.end();
          } else if (*it == binding) {
            binding_q = true;
            pattern_first = false;
            pattern_q = std::find(it, m_quantified.end(), pattern) != m_quantified.end();
          }
        }

        auto pattern_sub = m_substitutions.find(pattern);
        auto binding_sub = m_substitutions.find(binding);

        if (pattern_sub != m_substitutions.end()) {
          if (binding_sub != m_substitutions.end()) {
            return forall(pattern_sub->second.for_all(), binding_sub->second.for_all(), exact);
          } else if (binding_q) {
            if (pattern_first) {
              m_substitutions.insert({binding, to_for_all(pattern)});
              return true;
            } else {
              auto sub = rename_forall(binding, pattern_sub->second.for_all());
              if (sub) {
                m_substitutions.insert({binding, std::move(*sub)});
                return true;
              }
            }
          } else {
            return forall(pattern_sub->second.for_all(), to_for_all(binding), exact);
          }
        } else if (pattern_q) {
          if (!pattern_first || !binding_q) {
            m_substitutions.insert({pattern, to_for_all(binding)});
            return true;
          } else {
            if (binding_sub != m_substitutions.end()) {
              auto sub = rename_forall(pattern, binding_sub->second.for_all());
              if (sub) {
                m_substitutions.insert({pattern, std::move(*sub)});
                return true;
              }
            } else /*(pattern_first && binding_q)*/ {
              m_substitutions.insert({binding, to_for_all(pattern)});
              return true;
            }
          }
        } else {
          if (binding_sub != m_substitutions.end()) {
            return forall(to_for_all(pattern), binding_sub->second.for_all(), exact);
          } else if (binding_q) {
            m_substitutions.insert({binding, to_for_all(pattern)});
            return true;
          }
        }

        return false;
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
            try {
              for (auto it = m_quantifier->variables.begin(); it != m_quantifier->variables.end(); ++it)
                m_matcher->m_rename_map.erase(*it);
            } catch (...) {
            }
          }

          Quantifier new_quantifier;

        private:
          Matcher *m_matcher;
          const Quantifier *m_quantifier;
        };

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

	auto rhs = type.term.rhs.term.visit2
	  ([&] (const Variable& type_var) {return this->rename_variable(var, type_var);},
	   [&] (const Apply& apply) {return this->rename_apply(var, apply);});

	if (!rhs)
	  return {};

	auto& rhs_type = rhs->for_all();

	result.quantifier.variables.insert(rhs_type.quantifier.variables.begin(),
					   rhs_type.quantifier.variables.end());

	result.quantifier.constraints.insert(result.quantifier.constraints.end(),
					     rhs_type.quantifier.constraints.begin(),
					     rhs_type.quantifier.constraints.end());

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
        std::abort();
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

      void merge_quantifier(Quantifier& to, const Quantifier& from) {
	to.variables.insert(from.variables.begin(), from.variables.end());
	to.constraints.insert(to.constraints.end(), from.constraints.begin(), from.constraints.end());
      }
    }

    Type::Type(Variable var) : m_for_all({{{}, {}}, {{}, {{{}, {}}, std::move(var)}}}) {
    }

    Type::Type(Apply apply) : m_for_all({{{}, {}}, {{}, {{{}, {}}, std::move(apply)}}}) {
    }

    Type::Type(Exists exists) : m_for_all({{{}, {}}, {{}, std::move(exists)}}) {
    }

    Type::Type(Implies implies) : m_for_all({{{}, {}}, std::move(implies)}) {
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

    const Constructor* Type::as_primitive() const {
      if (m_for_all.quantifier.variables.empty() &&
          m_for_all.term.lhs.empty() &&
          m_for_all.term.rhs.quantifier.variables.empty()) {
        const Apply *ap = m_for_all.term.rhs.term.get<Apply>();
        if (ap) {
          if (ap->parameters.empty())
            return &ap->constructor;
        }
      }

      return 0;
    }

    bool Type::operator == (const Type& rhs) const {
      return Matcher().forall(m_for_all, rhs.m_for_all, true);
    }

    bool Type::operator != (const Type& rhs) const {
      return !(*this == rhs);
    }

    std::unordered_set<Variable> Type::occurs(const std::unordered_set<Variable>& variables) const {
      OccursChecker checker(false, variables);
      checker.forall(m_for_all);
      return std::move(checker.result);
    }

    void TypeContext::add(Constraint c) {
      m_implementations[c.predicate].push_back(c);
    }

    Maybe<Type> function_apply(const Type& function, const std::unordered_map<unsigned, Type>& arguments, const TypeContext& context) {
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

      std::vector<Constraint> constraints;
      Matcher matcher2;
      for (auto it = function.for_all().quantifier.constraints.begin();
	   it != function.for_all().quantifier.constraints.end(); ++it) {
	Constraint c = constraint_substitute(*it, substitutions);
	if (!constraint_occurs_check(c, quantified_used)) {
	  // Need to ensure this constraint is satisfied now since it
	  // is not dependent on any remaining quantified variables
	  auto jt = context.m_implementations.find(c.predicate);
	  bool found = false;
	  if (jt != context.m_implementations.end()) {
	    for (auto kt = jt->second.begin(); kt != jt->second.end(); ++kt) {
	      if (matcher2.constraint(c, *kt, false)) {
		found = true;
		break;
	      }
	    }
	  }

	  if (!found)
	    return {}; // Not satisfied
	} else {
	  constraints.push_back(std::move(c));
	}
      }

      return for_all(std::move(quantified_used), implies(std::move(lhs), std::move(rhs)), std::move(constraints));
    }

    Type for_all(const std::unordered_set<Variable>& variables, const Type& term) {
      return for_all(variables, term, std::vector<Constraint>());
    }

    Type exists(const std::unordered_set<Variable>& variables, const Type& term) {
      return exists(variables, term, std::vector<Constraint>());
    }

    Type for_all(const std::unordered_set<Variable>& variables, const Type& term, const std::vector<Constraint>& constraints) {
      ForAll result = term.for_all();
      result.quantifier.variables.insert(variables.begin(), variables.end());
      result.quantifier.constraints.insert(result.quantifier.constraints.end(), constraints.begin(), constraints.end());
      return Renamer().rename_forall(result);
    }

    Type exists(const std::unordered_set<Variable>& variables, const Type& term, const std::vector<Constraint>& constraints) {
      const ForAll& fa = term.for_all();
      if (!fa.quantifier.variables.empty() ||
	  !fa.term.lhs.empty()) {
	throw std::runtime_error("invalid use of exists()");
      }

      Exists result = fa.term.rhs;
      result.quantifier.variables.insert(variables.begin(), variables.end());
      result.quantifier.constraints.insert(result.quantifier.constraints.end(), constraints.begin(), constraints.end());
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
	  result.term.lhs.push_back(ForAll({{{}, {}}, {{}, {{{}, {}}, std::move(ex2.term)}}}));
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

    Predicate predicate(const std::vector<Variable>& parameters, std::vector<Constraint> constraints) {
      Predicate::Data d;

      std::unordered_map<Variable, Variable> rename_map;
      for (auto it = parameters.begin(); it != parameters.end(); ++it) {
	auto v = Variable::new_();
	d.parameters.push_back(v);
	rename_map.insert({*it, v});
      }

      Renamer renamer(std::move(rename_map));

      // Work out which constraints we actually need
      Matcher matcher;
      for (auto it = constraints.begin(); it != constraints.end();) {
	bool found = false;
	for (auto jt = constraints.begin(); jt != constraints.end(); ++jt) {
	  if ((it != jt) && matcher.constraint(*it, *jt, false)) {
	    found = true;
	    break;
	  }
	}

	if (!found)
	  ++it;
	else
	  it = constraints.erase(it);
      }

      for (auto it = constraints.begin(); it != constraints.end(); ++it)
	d.constraints.push_back(renamer.rename_constraint(*it));

      return {std::make_shared<Predicate::Data>(std::move(d))};
    }

    Constraint constraint(const Predicate& pred, std::vector<Type> parameters) {
      if (parameters.size() != pred.parameters().size())
	throw std::runtime_error("incorrect number of arguments to predicate when constructing constraint");
      std::vector<ForAll> new_parameters;
      for (auto it = parameters.begin(); it != parameters.end(); ++it)
	new_parameters.push_back(std::move(it->for_all()));
      return {pred, std::move(new_parameters)};
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

	Constraint sub_constraint(const Constraint& cons) const {
	  Constraint result;
	  result.predicate = cons.predicate;
	  for (auto it = cons.parameters.begin(); it != cons.parameters.end(); ++it)
	    result.parameters.push_back(std::move(sub_forall(*it).for_all()));
	  return result;
	}

        std::vector<Constraint> sub_constraints(const std::vector<Constraint>& cons) const {
          std::vector<Constraint> result;
          for (auto it = cons.begin(); it != cons.end(); ++it)
	    result.push_back(sub_constraint(*it));
          return result;
        }

      private:
        const std::unordered_map<Variable, Type> *m_substitutions;
      };
    }

    Type substitute(const Type& type, const std::unordered_map<Variable, Type>& substitutions) {
      return Substituter(substitutions).sub_forall(type.for_all());
    }

    Constraint constraint_substitute(const Constraint& cons, const std::unordered_map<Variable, Type>& substitutions) {
      return Substituter(substitutions).sub_constraint(cons);
    }

    bool constraint_occurs_check(const Constraint& cons, const std::unordered_set<Variable>& variables) {
      OccursChecker oc(true, variables);
      oc.constraint(cons);
      return oc.found;
    }

    namespace {
      void hash_combine(std::size_t& h, std::size_t x) {
        h ^= x + 0x9e3779b9 + (h<<6) + (h>>2);
      }

      template<typename T>
      std::size_t adl_hash(const T& v) {
	return hash(v);
      }

      class TypeHasher {
      public:
	std::size_t hash(const ForAll& x) {
	  ScopeEnter scope(this, x.quantifier);
	  std::size_t result = 0;
	  hash_combine(result, hash(x.quantifier));
	  hash_combine(result, hash(x.term));
	  return result;
	}

	std::size_t hash(const Exists& x) {
	  ScopeEnter scope(this, x.quantifier);
	  std::size_t result = 0;
	  hash_combine(result, hash(x.quantifier));
	  hash_combine(result, x.term.visit2
		       (
			[&] (const Variable& y) {return m_quantified.find(y) == m_quantified.end() ? adl_hash(y) : 0;},
			[&] (const Apply& y) {return this->hash(y);}));
	  return result;
	}

	std::size_t hash(const Apply& x) {
	  std::size_t result = 0;
	  hash_combine(result, adl_hash(x.constructor));
	  for (auto it = x.parameters.begin(); it != x.parameters.end(); ++it)
	    hash_combine(result, hash(*it));
	  return result;
	}

	std::size_t hash(const Constraint& x) {
	  std::size_t result = 0;
	  hash_combine(result, adl_hash(x.predicate));
	  for (auto it = x.parameters.begin(); it != x.parameters.end(); ++it)
	    hash_combine(result, hash(*it));
	  return result;
	}

	std::size_t hash(const Quantifier& x) {
	  std::size_t result = 0;
	  for (auto it = x.constraints.begin(); it != x.constraints.end(); ++it)
	    hash_combine(result, hash(*it));
	  return result;
	}

	std::size_t hash(const Implies& x) {
	  std::size_t result = 0;
	  for (auto it = x.lhs.begin(); it != x.lhs.end(); ++it)
	    hash_combine(result, hash(*it));
	  hash_combine(result, hash(x.rhs));
	  return result;
	}

      private:
	std::unordered_set<Variable> m_quantified;

	class ScopeEnter {
	public:
	  ScopeEnter(TypeHasher *hasher, const Quantifier& quantifier) : m_hasher(hasher), m_quantifier(&quantifier) {
	    m_hasher->m_quantified.insert(m_quantifier->variables.begin(), m_quantifier->variables.end());
	  }

	  ~ScopeEnter() {
	    for (auto it = m_quantifier->variables.begin(); it != m_quantifier->variables.end(); ++it)
	      m_hasher->m_quantified.erase(*it);
	  }

	private:
	  TypeHasher *m_hasher;
	  const Quantifier *m_quantifier;
	};
      };
    }

    bool operator == (const Apply& lhs, const Apply& rhs) {return Matcher().apply(lhs, rhs);}
    bool operator == (const ForAll& lhs, const ForAll& rhs) {return Matcher().forall(lhs, rhs, true);}
    bool operator == (const Exists& lhs, const Exists& rhs) {return Matcher().exists(lhs, rhs, true);}

    bool operator == (const Constraint& lhs, const Constraint& rhs) {
      if (lhs.predicate != rhs.predicate)
	return false;

      if (lhs.parameters.size() != rhs.parameters.size())
	return false;

      Matcher m;
      for (auto it = lhs.parameters.begin(), jt = rhs.parameters.begin(); it != lhs.parameters.end(); ++it, ++jt) {
	if (!m.forall(*it, *jt, true))
	  return false;
      }

      return true;
    }

    bool operator == (const Implies& lhs, const Implies& rhs) {
      if (lhs.lhs.size() != rhs.lhs.size())
	return false;

      Matcher m;
      for (auto it = lhs.lhs.begin(), jt = rhs.lhs.begin(); it != lhs.lhs.end(); ++it, ++jt) {
	if (!m.forall(*it, *jt, true))
	  return false;
      }

      if (!m.exists(lhs.rhs, rhs.rhs, true))
	return false;

      return true;
    }

    std::size_t hash(const Apply& x) {return TypeHasher().hash(x);}
    std::size_t hash(const Constraint& x) {return TypeHasher().hash(x);}
    std::size_t hash(const Quantifier& x) {return TypeHasher().hash(x);}
    std::size_t hash(const Exists& x) {return TypeHasher().hash(x);}
    std::size_t hash(const Implies& x) {return TypeHasher().hash(x);}
    std::size_t hash(const ForAll& x) {return TypeHasher().hash(x);}

    namespace {
      const char *for_all_str = u8"\u2200";
      const char *exists_str = u8"\u2203";
      const char *right_arrow_str = u8"\u2192";
      const char *double_arrow_str = u8"\u21d2";
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

      if (!q.constraints.empty()) {
	bool first = true;
	for (auto it = q.constraints.begin(); it != q.constraints.end(); ++it) {
	  if (first) {
	    first = false;
	  } else {
	    os << ", ";
	  }
	  os << m_term_namer.predicate_namer(it->predicate);
	  for (auto jt = it->parameters.begin(); jt != it->parameters.end(); ++jt) {
	    os << " ";
	    print_forall(os, *jt, true);
	  }
	}

	os << " " << double_arrow_str << " ";
      }
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
