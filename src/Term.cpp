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
          if (it != self.m_elements->end())
            return TermBuilder::parameter(self.rewrite(term->type), self.m_depth, it - self.m_elements->begin(), *self.m_location);
          else
            return term;
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
            return (*self.m_elements)[param->index];
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
            TreePtr<Term> type = self.rewrite((*it)->type);

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
          TreePtr<Term> type = self.rewrite(term->type);
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
    public:
      static const TermBinaryRewriterVtable vtable;
      UnifyRewriter() : TermBinaryRewriter(&vtable) {}
      
      static bool binary_rewrite_impl(UnifyRewriter& self, TreePtr<Term>& lhs, const TreePtr<Term>& rhs, const SourceLocation& location) {
        PSI_NOT_IMPLEMENTED();
      }
    };

    const TermBinaryRewriterVtable UnifyRewriter::vtable = PSI_COMPILER_TERM_BINARY_REWRITER(UnifyRewriter, "psi.compiler.UnifyRewriter", TermBinaryRewriter);
    
    /**
     * \brief Attempt to create a term which will match both this and src.
     */
    bool Term::unify(TreePtr<Term>& other, const SourceLocation& location) const {
      return UnifyRewriter().binary_rewrite(other, tree_from(this), location);
    }
    
    class MatchComparator : public TermComparator {
      PSI_STD::vector<TreePtr<Term> > *m_wildcards;
      unsigned m_depth;
      Term::UprefMatchMode m_upref_mode;
      
    public:
      static const TermComparatorVtable vtable;
      
      MatchComparator(PSI_STD::vector<TreePtr<Term> > *wildcards, unsigned depth, Term::UprefMatchMode upref_mode)
      : TermComparator(&vtable), m_wildcards(wildcards), m_depth(depth), m_upref_mode(upref_mode) {}
      
      static bool compare_impl(MatchComparator& self, const TreePtr<Term>& lhs, const TreePtr<Term>& rhs) {
        TreePtr<Term> lhs_unwrapped = term_unwrap(lhs), rhs_unwrapped = term_unwrap(rhs);
        
        if (!lhs_unwrapped)
          return !rhs_unwrapped;
        else if (!rhs_unwrapped)
          return false;
        
        if (TreePtr<Parameter> parameter = dyn_treeptr_cast<Parameter>(lhs_unwrapped)) {
          if (parameter->depth == self.m_depth) {
            // Check type also matches
            if (!self.compare(parameter->type, rhs_unwrapped->type))
              return false;

            if (parameter->index >= self.m_wildcards->size())
              return false;

            TreePtr<Term>& wildcard = (*self.m_wildcards)[parameter->index];
            if (wildcard) {
              // This probably isn't the right location to use...
              return rhs_unwrapped->unify(wildcard, wildcard->location());
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
            
            MatchComparator arg_child(self.m_wildcards, self.m_depth + 1, self.m_upref_mode);
            for (std::size_t ii = 0, ie = lhs_ftype->parameter_types.size(); ii != ie; ++ii) {
              if (lhs_ftype->parameter_types[ii].mode != rhs_ftype->parameter_types[ii].mode)
                return false;
              if (!arg_child.compare(lhs_ftype->parameter_types[ii].type, rhs_ftype->parameter_types[ii].type))
                return false;
            }
            
            Term::UprefMatchMode reverse_upref_mode;
            switch (self.m_upref_mode) {
            case Term::upref_match_read: reverse_upref_mode = Term::upref_match_write; break;
            case Term::upref_match_write: reverse_upref_mode = Term::upref_match_read; break;
            case Term::upref_match_exact: reverse_upref_mode = Term::upref_match_exact; break;
            case Term::upref_match_ignore: reverse_upref_mode = Term::upref_match_ignore; break;
            default: PSI_FAIL("Unrecognised enumeration value");
            }
            MatchComparator arg_result(self.m_wildcards, self.m_depth + 1, reverse_upref_mode);
          } else if (TreePtr<PointerType> lhs_ptr = dyn_treeptr_cast<PointerType>(lhs_unwrapped)) {
            TreePtr<PointerType> rhs_ptr = dyn_treeptr_cast<PointerType>(rhs_unwrapped);
            MatchComparator child(self.m_wildcards, self.m_depth, Term::upref_match_exact);
            if (!child.compare(lhs_ptr->target_type, rhs_ptr->target_type))
              return false;
            if (self.m_upref_mode != Term::upref_match_ignore) {
              if (!self.compare(lhs_ptr->upref, rhs_ptr->upref))
                return false;
            }
            return true;
          } else if (TreePtr<Functional> lhs_func = dyn_treeptr_cast<Functional>(lhs_unwrapped)) {
            if (tree_isa<ParameterizedType>(lhs_func)) {
              MatchComparator child(self.m_wildcards, self.m_depth + 1, self.m_upref_mode);
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
     * \param depth Number of parameter-enclosing terms above this match.
     * 
     * Note that it is important that when \c wildcards is empty, this function simply
     * checks that this tree and \c value are the same.
     */
    bool Term::match(const TreePtr<Term>& value, PSI_STD::vector<TreePtr<Term> >& wildcards, unsigned depth, UprefMatchMode upref_mode) const {
      return MatchComparator(&wildcards, depth, upref_mode).compare(tree_from(this), value);
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
        return match(value, wildcards, 0, upref_match_read);
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
          if ((def->mode == statement_mode_functional) && def->value->pure)
            my_term = def->value;
          else
            break;
        } else if (TreePtr<Statement> stmt = dyn_treeptr_cast<Statement>(my_term)) {
          if ((stmt->mode == statement_mode_functional) && stmt->value->pure)
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
