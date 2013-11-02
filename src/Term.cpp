#include "Term.hpp"
#include "Tree.hpp"
#include "Compiler.hpp"
#include "TermBuilder.hpp"

#include <boost/format.hpp>

namespace Psi {
  namespace Compiler {
    const SIVtable TermVisitor::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.TermVisitor", NULL);
    const SIVtable TermRewriter::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.TermRewriter", NULL);
    const SIVtable TermBinaryRewriter::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.TermBinaryRewriter", NULL);
    const SIVtable TermComparator::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.TermComparator", NULL);

    Term::Term(const TermVtable *vptr)
    : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr)),
    m_type_info_computed(false) {
    }

    Term::Term(const TermVtable *vptr, CompileContext& compile_context, const TermResultInfo& ri, const SourceLocation& location)
    : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), compile_context, location),
    type(ri.type), mode(ri.mode), pure(ri.pure),
    m_type_info_computed(false) {
    }

    Term::Term(const TermVtable *vptr, const TermResultInfo& ri, const SourceLocation& location)
    : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), ri.type->compile_context(), location),
    type(ri.type), mode(ri.mode), pure(ri.pure),
    m_type_info_computed(false) {
    }

    void Term::type_info_compute() const {
      PSI_ASSERT(!m_type_info_computed);
      ResultStorage<TermTypeInfo> tri;
      derived_vptr(this)->type_info(tri.ptr(), this);
      m_type_info = tri.done();
      m_type_info_computed = true;
    }

    TermTypeInfo Term::type_info_impl(const Term&) {
      TermTypeInfo tti;
      tti.type_fixed_size = false;
      tti.type_mode = type_mode_none;
      return tti;
    }
    
    class ParameterizeRewriter : public TermRewriter {
      const SourceLocation *m_location;
      const PSI_STD::vector<TreePtr<Anonymous> > *m_elements;
      unsigned m_depth;
      
    public:
      static const TermRewriterVtable vtable;
      
      ParameterizeRewriter(const SourceLocation *location, const PSI_STD::vector<TreePtr<Anonymous> > *elements, unsigned depth)
      : TermRewriter(&vtable), m_location(location), m_elements(elements), m_depth(depth) {}
      
      static TreePtr<Term> rewrite_impl(ParameterizeRewriter& self, const TreePtr<Term>& term) {
        if (tree_isa<Anonymous>(term)) {
          PSI_STD::vector<TreePtr<Anonymous> >::const_iterator it = std::find(self.m_elements->begin(), self.m_elements->end(), term);
          if (it != self.m_elements->end()) {
            // Depth of parameter types is defined relative to the parameter's own depth
            ParameterizeRewriter child(self.m_location, self.m_elements, 0);
            return TermBuilder::parameter(child.rewrite(term->type), self.m_depth, it - self.m_elements->begin(), *self.m_location);
          } else {
            return term;
          }
        } else if (TreePtr<Functional> func = dyn_treeptr_cast<Functional>(term)) {
          if (tree_isa<ParameterizedType>(func)) {
            ParameterizeRewriter child(self.m_location, self.m_elements, self.m_depth + 1);
            return func->rewrite(child, *self.m_location);
          } else {
            return func->rewrite(self, *self.m_location);
          }
        } else {
          return term;
        }
      }
    };
    
    const TermRewriterVtable ParameterizeRewriter::vtable = PSI_COMPILER_TERM_REWRITER(ParameterizeRewriter, "psi.compiler.ParameterizeRewriter", TermRewriter);

    /**
     * \brief Parameterize a term.
     * 
     * \param elements Anonymous terms to turn into parameters.
     */
    TreePtr<Term> Term::parameterize(const SourceLocation& location, const PSI_STD::vector<TreePtr<Anonymous> >& elements) const {
      return ParameterizeRewriter(&location, &elements, 0).rewrite(tree_from(this));
    }
    
    class DepthIncreaseRewriter : public TermRewriter {
      const SourceLocation *m_location;
      unsigned m_depth;
      
    public:
      static const TermRewriterVtable vtable;
      
      DepthIncreaseRewriter(const SourceLocation *location, unsigned depth)
      : TermRewriter(&vtable), m_location(location), m_depth(depth) {
      }
      
      static TreePtr<Term> rewrite_impl(DepthIncreaseRewriter& self, const TreePtr<Term>& term) {
        if (TreePtr<Parameter> param = dyn_treeptr_cast<Parameter>(term)) {
          if (param->depth == 0) {
            return TermBuilder::parameter(param->type, self.m_depth, param->index, *self.m_location);
          } else {
            term->compile_context().error_throw(*self.m_location, "Term specialization parameters can only have depth 0");
          }
        } else if (TreePtr<Functional> func = dyn_treeptr_cast<Functional>(term)) {
          if (tree_isa<ParameterizedType>(func)) {
            DepthIncreaseRewriter child(self.m_location, self.m_depth + 1);
            return func->rewrite(child, *self.m_location);
          } else {
            return func->rewrite(self, *self.m_location);
          }
        } else {
          return term;
        }
      }
    };

    const TermRewriterVtable DepthIncreaseRewriter::vtable = PSI_COMPILER_TERM_REWRITER(DepthIncreaseRewriter, "psi.compiler.DepthIncreaseRewriter", TermRewriter);
    
    class SpecializeRewriter : public TermRewriter {
      const SourceLocation *m_location;
      const PSI_STD::vector<TreePtr<Term> > *m_elements;
      unsigned m_depth;
      
    public:
      static const TermRewriterVtable vtable;
      
      SpecializeRewriter(const SourceLocation *location, const PSI_STD::vector<TreePtr<Term> > *elements, unsigned depth)
      : TermRewriter(&vtable), m_location(location), m_elements(elements), m_depth(depth) {}
      
      static TreePtr<Term> rewrite_impl(SpecializeRewriter& self, const TreePtr<Term>& term) {
        if (TreePtr<Parameter> param = dyn_treeptr_cast<Parameter>(term)) {
          if (param->depth == self.m_depth) {
            if (param->index >= self.m_elements->size())
              term->compile_context().error_throw(*self.m_location, "Parameter index out of range");
            const TreePtr<Term>& result = (*self.m_elements)[param->index];

            SpecializeRewriter child(self.m_location, self.m_elements, 0);
            TreePtr<Term> type = child.rewrite(param->type);
            if (!type->convert_match(result->type))
              term->compile_context().error_throw(*self.m_location, "Type mismatch in term substitution");

            return DepthIncreaseRewriter(self.m_location, self.m_depth).rewrite(result);
          } else {
            return param;
          }
        } else if (TreePtr<Functional> func = dyn_treeptr_cast<Functional>(term)) {
          if (tree_isa<ParameterizedType>(func)) {
            SpecializeRewriter child(self.m_location, self.m_elements, self.m_depth + 1);
            return func->rewrite(child, *self.m_location);
          } else {
            return func->rewrite(self, *self.m_location);
          }
        } else {
          return term;
        }
      }
    };
    
    const TermRewriterVtable SpecializeRewriter::vtable = PSI_COMPILER_TERM_REWRITER(SpecializeRewriter, "psi.compiler.SpecializeRewriter", TermRewriter);
    
    TreePtr<Term> Term::specialize(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& values) const {
      return SpecializeRewriter(&location, &values, 0).rewrite(tree_from(this));
    }
    
    class AnonymizeRewriter : public TermRewriter {
      const SourceLocation *m_location;
      PSI_STD::vector<TreePtr<Term> > *m_parameter_types;
      PSI_STD::map<TreePtr<Statement>, unsigned> *m_parameter_map;
      const PSI_STD::vector<TreePtr<Statement> > *m_statements;
      unsigned m_depth;
      
    public:
      static const VtableType vtable;
      
      AnonymizeRewriter(const SourceLocation *location, PSI_STD::vector<TreePtr<Term> > *parameter_types,
                        PSI_STD::map<TreePtr<Statement>, unsigned> *parameter_map, const PSI_STD::vector<TreePtr<Statement> > *statements, unsigned depth)
      : TermRewriter(&vtable), m_location(location), m_parameter_types(parameter_types),
      m_parameter_map(parameter_map), m_statements(statements), m_depth(depth) {}
      
      static TreePtr<Term> rewrite_impl(AnonymizeRewriter& self, const TreePtr<Term>& term) {
        if (tree_isa<Statement>(term)) {
          PSI_STD::vector<TreePtr<Statement> >::const_iterator it = std::find(self.m_statements->begin(), self.m_statements->end(), term);
          if (it != self.m_statements->end()) {
            AnonymizeRewriter child(self.m_location, self.m_parameter_types, self.m_parameter_map, self.m_statements, 0);
            TreePtr<Term> type = child.rewrite((*it)->type);

            unsigned index;
            PSI_STD::map<TreePtr<Statement>, unsigned>::iterator jt = self.m_parameter_map->find(*it);
            if (jt == self.m_parameter_map->end()) {
              index = self.m_parameter_types->size();
              self.m_parameter_map->insert(std::make_pair(*it, index));
              self.m_parameter_types->push_back(type);
            } else {
              index = jt->second;
            }
            
            return TermBuilder::parameter(type, self.m_depth, index, *self.m_location);
          } else {
            return term;
          }
        } else if (TreePtr<Functional> func = dyn_treeptr_cast<Functional>(term)) {
          if (tree_isa<ParameterizedType>(func)) {
            AnonymizeRewriter child(self.m_location, self.m_parameter_types, self.m_parameter_map, self.m_statements, self.m_depth + 1);
            return func->rewrite(child, *self.m_location);
          } else {
            return func->rewrite(self, *self.m_location);
          }
        } else {
          // Anything not functional is replaced by an anonymous value
          AnonymizeRewriter child(self.m_location, self.m_parameter_types, self.m_parameter_map, self.m_statements, 0);
          TreePtr<Term> type = child.rewrite(term->type);
          unsigned index = self.m_parameter_types->size();
          self.m_parameter_types->push_back(type);
          return TermBuilder::parameter(type, self.m_depth, index, *self.m_location);
        }
      }
    };
    
    const TermRewriterVtable AnonymizeRewriter::vtable = PSI_COMPILER_TERM_REWRITER(AnonymizeRewriter, "psi.compiler.AnonymizeRewriter", TermRewriter);
    
    /**
     * \brief Anonymise a term.
     * 
     * If the result type of an operation depends on a parameter which requires stateful evaluation,
     * the actual result type must have those stateful values replaced by wildcards since repeat
     * evaluation may not necessarily produce the same values.
     * 
     * Only statements listed in \c statements are anonymized, otherwise they are assumed to remain
     * in scope.
     * 
     * \param statements Statements of block whose scope is ending: this allows block result types
     * to be computed correctly.
     */
    TreePtr<Term> Term::anonymize(const SourceLocation& location, const PSI_STD::vector<TreePtr<Statement> >& statements) const {
      PSI_STD::vector<TreePtr<Term> > parameter_types;
      PSI_STD::map<TreePtr<Statement>, unsigned> parameter_map;
      AnonymizeRewriter rw(&location, &parameter_types, &parameter_map, &statements, 0);
      
      TreePtr<Term> result = rw.rewrite(tree_from(this));
      if (parameter_types.empty())
        return result; // No parameterisation required
        
      return TermBuilder::exists(result, parameter_types, location);
    }

    /// \copydoc Term::anonymize
    TreePtr<Term> Term::anonymize(const SourceLocation& location) const {
      return anonymize(location, default_);
    }
    
    class UnifyRewriter : public TermBinaryRewriter {
      Term::UprefUnifyMode m_upref_mode;
      
    public:
      static const TermBinaryRewriterVtable vtable;
      UnifyRewriter(Term::UprefUnifyMode upref_mode) : TermBinaryRewriter(&vtable), m_upref_mode(upref_mode) {}
      
      static Maybe<TreePtr<Term> > binary_rewrite_impl(UnifyRewriter& self, const TreePtr<Term>& lhs, const TreePtr<Term>& rhs, const SourceLocation& location) {
        TreePtr<Term> lhs_unwrapped = term_unwrap(lhs), rhs_unwrapped = term_unwrap(rhs);

        if (!lhs_unwrapped)
          return rhs_unwrapped ? Maybe<TreePtr<Term> >() : TreePtr<Term>();
        else if (!rhs_unwrapped)
          return maybe_none;

        if (!lhs_unwrapped->pure || !rhs_unwrapped->pure)
          return maybe_none;

        if (lhs_unwrapped == rhs_unwrapped)
          return lhs_unwrapped;
        
        // Note upref_match_exact cast is handled implicitly by lhs_unwrapped == rhs_unwrapped
        if (tree_isa<UpwardReferenceNull>(lhs_unwrapped)) {
          switch (self.m_upref_mode) {
          case Term::upref_unify_ignore:
          case Term::upref_unify_short: return lhs_unwrapped;
          case Term::upref_unify_long: return rhs_unwrapped;
          case Term::upref_unify_exact: return tree_isa<UpwardReferenceNull>(rhs_unwrapped) ? lhs_unwrapped : Maybe<TreePtr<Term> >();
          default: PSI_FAIL("Unrecognised enumeration value");
          }
        } else if (tree_isa<UpwardReferenceNull>(rhs_unwrapped)) {
          switch (self.m_upref_mode) {
          case Term::upref_unify_ignore:
          case Term::upref_unify_short: return rhs_unwrapped;
          case Term::upref_unify_long: return lhs_unwrapped;
          case Term::upref_unify_exact: return tree_isa<UpwardReferenceNull>(lhs_unwrapped) ? rhs_unwrapped : Maybe<TreePtr<Term> >();
          default: PSI_FAIL("Unrecognised enumeration value");
          }
        }

        if (si_vptr(lhs_unwrapped.get()) == si_vptr(rhs_unwrapped.get())) {
          if (TreePtr<UpwardReference> lhs_upref = dyn_treeptr_cast<UpwardReference>(lhs_unwrapped)) {
            TreePtr<UpwardReference> rhs_upref = treeptr_cast<UpwardReference>(rhs_unwrapped);
            
            Maybe<TreePtr<Term> > outer_index = self.binary_rewrite(lhs_upref->outer_index, rhs_upref->outer_index, location);
            if (!outer_index)
              return maybe_none;
            
            TreePtr<Term> outer_type;
            if (!term_unwrap_isa<UpwardReference>(lhs_upref->next) || !term_unwrap_isa<UpwardReference>(rhs_upref->next)) {
              Maybe<TreePtr<Term> > outer_type_m = self.binary_rewrite(lhs_upref->outer_type(), rhs_upref->outer_type(), location);
              if (!outer_type_m)
                return maybe_none;
              outer_type = *outer_type_m;
            }
            
            Maybe<TreePtr<Term> > next = self.binary_rewrite(lhs_upref->next, rhs_upref->next, location);
            if (!next)
              return maybe_none;
            
            return TermBuilder::upref(outer_type, *outer_index, *next, location);
          } else if (TreePtr<FunctionType> lhs_ftype = dyn_treeptr_cast<FunctionType>(lhs_unwrapped)) {
            // Need to reverse the upward reference mode for the result
            TreePtr<FunctionType> rhs_ftype = treeptr_cast<FunctionType>(rhs_unwrapped);
            PSI_ASSERT(lhs_ftype->interfaces.empty() && rhs_ftype->interfaces.empty());

            if (lhs_ftype->result_mode != rhs_ftype->result_mode)
              return maybe_none;

            if (lhs_ftype->parameter_types.size() != rhs_ftype->parameter_types.size())
              return maybe_none;
            
            PSI_STD::vector<FunctionParameterType> arg_types;
            for (std::size_t ii = 0, ie = lhs_ftype->parameter_types.size(); ii != ie; ++ii) {
              FunctionParameterType arg;
              if (lhs_ftype->parameter_types[ii].mode != rhs_ftype->parameter_types[ii].mode)
                return maybe_none;
              arg.mode = lhs_ftype->parameter_types[ii].mode;
              Maybe<TreePtr<Term> > arg_type = self.binary_rewrite(lhs_ftype->parameter_types[ii].type, rhs_ftype->parameter_types[ii].type, location);
              if (!arg_type)
                return maybe_none;
              arg.type = *arg_type;
            }

            Term::UprefUnifyMode reverse_upref_mode;
            switch (self.m_upref_mode) {
            case Term::upref_unify_short: reverse_upref_mode = Term::upref_unify_long; break;
            case Term::upref_unify_long: reverse_upref_mode = Term::upref_unify_short; break;
            case Term::upref_unify_exact: reverse_upref_mode = Term::upref_unify_exact; break;
            case Term::upref_unify_ignore: reverse_upref_mode = Term::upref_unify_ignore; break;
            default: PSI_FAIL("Unrecognised enumeration value");
            }
            Maybe<TreePtr<Term> > result_type = UnifyRewriter(reverse_upref_mode).binary_rewrite(lhs_ftype->result_type, rhs_ftype->result_type, location);
            if (!result_type)
              return maybe_none;
            
            return TermBuilder::function_type(lhs_ftype->result_mode, *result_type, arg_types, default_, location);
          } else if (TreePtr<PointerType> lhs_ptr = dyn_treeptr_cast<PointerType>(lhs_unwrapped)) {
            TreePtr<PointerType> rhs_ptr = treeptr_cast<PointerType>(rhs_unwrapped);
            Maybe<TreePtr<Term> > target = UnifyRewriter(Term::upref_unify_exact).binary_rewrite(lhs_ptr->target_type, rhs_ptr->target_type, location);
            if (!target)
              return maybe_none;
            TreePtr<Term> upref;
            if (self.m_upref_mode != Term::upref_unify_ignore) {
              Maybe<TreePtr<Term> > upref_m = self.binary_rewrite(lhs_ptr->upref, rhs_ptr->upref, location);
              if (!upref_m)
                return maybe_none;
              upref = *upref_m;
            } else {
              upref = TermBuilder::upref_null(lhs_ptr->compile_context());
            }
            return TermBuilder::pointer(*target, upref, location);
          } else if (TreePtr<Functional> lhs_func = dyn_treeptr_cast<Functional>(lhs_unwrapped)) {
            return lhs_func->binary_rewrite(*tree_cast<Functional>(rhs_unwrapped.get()), self, location);
          }
        }
        
        return maybe_none;
      }
    };

    const TermBinaryRewriterVtable UnifyRewriter::vtable = PSI_COMPILER_TERM_BINARY_REWRITER(UnifyRewriter, "psi.compiler.UnifyRewriter", TermBinaryRewriter);
    
    /**
     * \brief Attempt to create a term which will match both this and src.
     */
    Maybe<TreePtr<Term> > Term::unify(const TreePtr<Term>& other, UprefUnifyMode upref_mode, const SourceLocation& location) const {
      return UnifyRewriter(upref_mode).binary_rewrite(tree_from(this), other, location);
    }
    
    class MatchComparator : public TermComparator {
      unsigned m_depth;
      Term::UprefMatchMode m_upref_mode;
      PSI_STD::vector<TreePtr<Term> > *m_wildcards_0, *m_wildcards_1;
      
    public:
      static const TermComparatorVtable vtable;
      
      MatchComparator(unsigned depth, Term::UprefMatchMode upref_mode,
                      PSI_STD::vector<TreePtr<Term> > *wildcards_0, PSI_STD::vector<TreePtr<Term> > *wildcards_1)
      : TermComparator(&vtable), m_depth(depth), m_upref_mode(upref_mode),
      m_wildcards_0(wildcards_0), m_wildcards_1(wildcards_1) {}
      
      MatchComparator make_child(unsigned depth, Term::UprefMatchMode upref_mode) {
        return MatchComparator(depth, upref_mode, m_wildcards_0, m_wildcards_1);
      }

      MatchComparator make_child(unsigned depth) {
        return make_child(depth, m_upref_mode);
      }
      
      static bool compare_impl(MatchComparator& self, const TreePtr<Term>& lhs, const TreePtr<Term>& rhs) {
        TreePtr<Term> lhs_unwrapped = term_unwrap(lhs), rhs_unwrapped = term_unwrap(rhs);
        
        if (!lhs_unwrapped)
          return !rhs_unwrapped;
        else if (!rhs_unwrapped)
          return false;
        
        if (TreePtr<Parameter> parameter = dyn_treeptr_cast<Parameter>(lhs_unwrapped)) {
          PSI_STD::vector<TreePtr<Term> > *level;
          if (parameter->depth == self.m_depth)
            level = self.m_wildcards_0;
          else if (parameter->depth == self.m_depth+1)
            level = self.m_wildcards_1;
          else
            level = NULL;
          
          if (level) {
            // Check type also matches
            MatchComparator child = self.make_child(0);
            if (!child.compare(parameter->type, rhs_unwrapped->type))
              return false;

            if (parameter->index >= level->size())
              return false;

            TreePtr<Term>& wildcard = (*level)[parameter->index];
            if (wildcard) {
              // This probably isn't the right location to use...
              // The unification mode also won't do exactly what is required,
              // because in principle there can be multiple constraints
              // on upref chains (i.e. shortest and longest) and the current
              // match algorithm doesn't allow for that.
              return rhs_unwrapped->unify(wildcard, Term::upref_unify_exact, wildcard->location());
            } else {
              wildcard = rhs_unwrapped;
              return true;
            }
          }
        }
        
        if (!lhs_unwrapped->pure || !rhs_unwrapped->pure)
          return false;

        if (lhs_unwrapped == rhs_unwrapped)
          return true;
        
        // Note upref_match_exact cast is handled implicitly by lhs_unwrapped == rhs_unwrapped
        if (tree_isa<UpwardReferenceNull>(lhs_unwrapped))
          return self.m_upref_mode == Term::upref_match_read;
        else if (tree_isa<UpwardReferenceNull>(rhs_unwrapped))
          return self.m_upref_mode == Term::upref_match_write;
        
        if (si_vptr(lhs_unwrapped.get()) == si_vptr(rhs_unwrapped.get())) {
          if (TreePtr<UpwardReference> lhs_upref = dyn_treeptr_cast<UpwardReference>(lhs_unwrapped)) {
            TreePtr<UpwardReference> rhs_upref = treeptr_cast<UpwardReference>(rhs_unwrapped);
            if (!self.compare(lhs_upref->outer_index, rhs_upref->outer_index))
              return false;
            
            if (!term_unwrap_isa<UpwardReference>(lhs_upref->next) || !term_unwrap_isa<UpwardReference>(rhs_upref->next)) {
              if (!self.compare(lhs_upref->outer_type(), rhs_upref->outer_type()))
                return false;
            }
            
            return self.compare(lhs_upref->next, rhs_upref->next);
          } else if (TreePtr<FunctionType> lhs_ftype = dyn_treeptr_cast<FunctionType>(lhs_unwrapped)) {
            // Need to reverse the upward reference mode for the result
            TreePtr<FunctionType> rhs_ftype = treeptr_cast<FunctionType>(rhs_unwrapped);
            PSI_ASSERT(lhs_ftype->interfaces.empty() && rhs_ftype->interfaces.empty());
            if (lhs_ftype->result_mode != rhs_ftype->result_mode)
              return false;
            if (lhs_ftype->parameter_types.size() != rhs_ftype->parameter_types.size())
              return false;
            
            // Reverse constraint mode applies in arguments
            Term::UprefMatchMode reverse_upref_mode;
            switch (self.m_upref_mode) {
            case Term::upref_match_read: reverse_upref_mode = Term::upref_match_write; break;
            case Term::upref_match_write: reverse_upref_mode = Term::upref_match_read; break;
            case Term::upref_match_exact: reverse_upref_mode = Term::upref_match_exact; break;
            case Term::upref_match_ignore: reverse_upref_mode = Term::upref_match_ignore; break;
            default: PSI_FAIL("Unrecognised enumeration value");
            }
            MatchComparator arg_child = self.make_child(self.m_depth + 1, reverse_upref_mode);
            for (std::size_t ii = 0, ie = lhs_ftype->parameter_types.size(); ii != ie; ++ii) {
              if (lhs_ftype->parameter_types[ii].mode != rhs_ftype->parameter_types[ii].mode)
                return false;
              if (!arg_child.compare(lhs_ftype->parameter_types[ii].type, rhs_ftype->parameter_types[ii].type))
                return false;
            }
            
            MatchComparator arg_result = self.make_child(self.m_depth + 1);
            if (!arg_result.compare(lhs_ftype->result_type, rhs_ftype->result_type))
              return false;
            
            return true;
          } else if (TreePtr<PointerType> lhs_ptr = dyn_treeptr_cast<PointerType>(lhs_unwrapped)) {
            TreePtr<PointerType> rhs_ptr = treeptr_cast<PointerType>(rhs_unwrapped);
            MatchComparator child = self.make_child(self.m_depth, Term::upref_match_exact);
            if (!child.compare(lhs_ptr->target_type, rhs_ptr->target_type))
              return false;
            if (self.m_upref_mode != Term::upref_match_ignore) {
              if (!self.compare(lhs_ptr->upref, rhs_ptr->upref))
                return false;
            }
            return true;
          } else if (TreePtr<Functional> lhs_func = dyn_treeptr_cast<Functional>(lhs_unwrapped)) {
            if (tree_isa<ParameterizedType>(lhs_func)) {
              MatchComparator child = self.make_child(self.m_depth + 1);
              return lhs_func->compare(*tree_cast<Functional>(rhs_unwrapped.get()), child);
            } else {
              return lhs_func->compare(*tree_cast<Functional>(rhs_unwrapped.get()), self);
            }
          }
        }
        
        return false;
      }
    };
    
    const TermComparatorVtable MatchComparator::vtable = PSI_COMPILER_TERM_COMPARATOR(MatchComparator, "psi.compiler.MatchComparator", TermComparator);
    
    /**
     * \brief Check whether this tree, which is a pattern, matches a given value.
     *
     * \param value Tree to match to.
     * \param wildcards Substitutions to be identified.
     * 
     * Note that it is important that when \c wildcards is empty, this function simply
     * checks that this tree and \c value are the same.
     */
    bool Term::match(const TreePtr<Term>& value, UprefMatchMode upref_mode, PSI_STD::vector<TreePtr<Term> >& wildcards) const {
      return MatchComparator(0, upref_mode, &wildcards, NULL).compare(tree_from(this), value);
    }

    /**
     * \brief Check whether this tree, which is a pattern, matches a given value.
     *
     * \param value Tree to match to.
     * \param wildcards_0 Substitutions to be identified at depth 0.
     * \param wildcards_1 Substitutions to be identified at depth 1.
     */
    bool Term::match2(const TreePtr<Term>& value, UprefMatchMode upref_mode, PSI_STD::vector<TreePtr<Term> >& wildcards_0, PSI_STD::vector<TreePtr<Term> >& wildcards_1) const {
      return MatchComparator(0, upref_mode, &wildcards_0, &wildcards_1).compare(tree_from(this), value);
    }
    
    /**
     * \brief Check whether \c value matches this tree, which is a pattern.
     * 
     * A no-wildcard match is useful because a few cases of implicit equivalence exist, specifically:
     * 
     * <ul>
     * <li>NULL values in upward reference chains can match non-NULL values</li>
     * <li>Types can be matched by DerivedType wrapping that type</li>
     * <li>If the top level pattern is Exists, non-exists terms can match</li>
     * </ul>
     */
    bool Term::convert_match(const TreePtr<Term>& value) const {
      if (tree_isa<Exists>(this)) {
        PSI_NOT_IMPLEMENTED();
      } else {
        PSI_STD::vector<TreePtr<Term> > wildcards;
        return match(value, upref_match_read, wildcards);
      }
    }

    const SIVtable Term::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Term", Tree);

    Metatype::Metatype()
    : Functional(&vtable) {
    }

    TermResultInfo Metatype::check_type_impl(const Metatype&) {
      TermResultInfo tri;
      tri.mode = term_mode_value;
      tri.pure = true;
      return tri;
    }
    
    TermTypeInfo Metatype::type_info_impl(const Metatype&) {
      TermTypeInfo result;
      result.type_mode = type_mode_metatype;
      result.type_fixed_size = true;
      return result;
    }

    template<typename V>
    void Metatype::visit(V& v) {
      visit_base<Functional>(v);
    }

    const FunctionalVtable Metatype::vtable = PSI_COMPILER_FUNCTIONAL(Metatype, "psi.compiler.Metatype", Functional);

    Type::Type(const VtableType *vptr)
    : Functional(vptr) {
    }

    const SIVtable Type::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Type", Functional);

    Anonymous::Anonymous(const TreePtr<Term>& type, TermMode mode_, const SourceLocation& location)
    : Term(&vtable, TermResultInfo(type, mode_, true), location),
    mode(mode_) {
      if (!type->is_type())
        compile_context().error_throw(location, "Type of anonymous term is not a type");
    }
    
    TermTypeInfo Anonymous::type_info_impl(const Anonymous& self) {
      TermTypeInfo rt;
      rt.type_fixed_size = false;
      rt.type_mode = (self.type->type_info().type_mode == type_mode_metatype) ? type_mode_complex : type_mode_none;
      return rt;
    }
    
    template<typename V>
    void Anonymous::visit(V& v) {
      visit_base<Term>(v);
      v("mode", &Anonymous::mode);
    }

    const TermVtable Anonymous::vtable = PSI_COMPILER_TERM(Anonymous, "psi.compiler.Anonymous", Term);

    Parameter::Parameter(const TreePtr<Term>& type, unsigned depth_, unsigned index_)
    : Functional(&vtable),
    parameter_type(type),
    depth(depth_),
    index(index_) {
    }

    TermResultInfo Parameter::check_type_impl(const Parameter& self) {
      if (!self.parameter_type || !self.parameter_type->is_type())
        self.compile_context().error_throw(self.location(), "Type of parameter is not a type");
      return TermResultInfo(self.parameter_type, term_mode_value, true);
    }
    
    TermTypeInfo Parameter::type_info_impl(const Parameter& self) {
      TermTypeInfo result;
      result.type_fixed_size = false;
      result.type_mode = (self.parameter_type->type_info().type_mode == type_mode_metatype) ? type_mode_complex : type_mode_none;
      return result;
    }

    template<typename Visitor>
    void Parameter::visit(Visitor& v) {
      visit_base<Functional>(v);
      v("parameter_type", &Parameter::parameter_type)
      ("depth", &Parameter::depth)
      ("index", &Parameter::index);
    }
    
    const FunctionalVtable Parameter::vtable = PSI_COMPILER_FUNCTIONAL(Parameter, "psi.compiler.Parameter", Functional);
    
    /**
     * \brief Find the underlying type of a term.
     * 
     * This unwraps \c GlobalStatement, \c Statement and \c DerivedType.
     */
    TreePtr<Term> term_unwrap(const TreePtr<Term>& term) {
      if (!term)
        return term;
      
      TreePtr<Term> my_term = term;
      while (true) {
        if (TreePtr<GlobalStatement> def = dyn_treeptr_cast<GlobalStatement>(my_term)) {
          if ((def->statement_mode == statement_mode_functional) && def->value->pure)
            my_term = def->value;
          else
            break;
        } else if (TreePtr<Statement> stmt = dyn_treeptr_cast<Statement>(my_term)) {
          if ((stmt->statement_mode == statement_mode_functional) && stmt->value->pure)
            my_term = stmt->value;
          else
            break;
        }
        
        break;
      }
      
      return my_term;
    }
  }
}
