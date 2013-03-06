#include "Term.hpp"
#include "Tree.hpp"
#include "Compiler.hpp"
#include "TermBuilder.hpp"

#include <boost/format.hpp>

namespace Psi {
  namespace Compiler {
    const SIVtable TermVisitor::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.TermVisitor", NULL);
    const SIVtable TermRewriter::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.TermRewriter", NULL);
    const SIVtable TermComparator::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.TermComparator", NULL);

    Term::Term(const TermVtable *vptr)
    : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr)),
    m_result_info_computed(false),
    m_pure(-1) {
    }

    Term::Term(const TermVtable *vptr, CompileContext& compile_context, const TreePtr<Term>& type_, const SourceLocation& location)
    : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), compile_context, location),
    m_result_info_computed(false),
    m_pure(-1),
    type(type_) {
    }

    Term::Term(const TermVtable *vptr, const TreePtr<Term>& type_, const SourceLocation& location)
    : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), type_.compile_context(), location),
    m_result_info_computed(false),
    m_pure(-1),
    type(type_) {
    }

    void Term::result_info_compute() const {
      PSI_ASSERT(!m_result_info_computed);
      ResultStorage<TermResultInfo> tri;
      derived_vptr(this)->result_info(tri.ptr(), this);
      m_result_info = tri.done();
      m_result_info_computed = true;
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
              term.compile_context().error_throw(*self.m_location, "Parameter index out of range");
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
    
    class MatchComparator : public TermComparator {
      PSI_STD::vector<TreePtr<Term> > *m_wildcards;
      unsigned m_depth;
      
    public:
      static const TermComparatorVtable vtable;
      
      MatchComparator(PSI_STD::vector<TreePtr<Term> > *wildcards, unsigned depth)
      : TermComparator(&vtable), m_wildcards(wildcards), m_depth(depth) {}
      
      static bool compare_impl(MatchComparator& self, const TreePtr<Term>& lhs, const TreePtr<Term>& rhs) {
        if (const TreePtr<Parameter> parameter = dyn_treeptr_cast<Parameter>(lhs)) {
          if (parameter->depth == self.m_depth) {
            // Check type also matches
            if (!self.compare(parameter->type, rhs->type))
              return false;

            if (parameter->index >= self.m_wildcards->size())
              return false;

            TreePtr<Term>& wildcard = (*self.m_wildcards)[parameter->index];
            if (wildcard) {
              return wildcard == rhs;
            } else {
              wildcard = rhs;
              return true;
            }
          }
        }

        if (si_vptr(lhs.get()) == si_vptr(rhs.get())) {
          if (TreePtr<Functional> func = dyn_treeptr_cast<Functional>(lhs)) {
            if (tree_isa<ParameterizedType>(func)) {
              MatchComparator child(self.m_wildcards, self.m_depth + 1);
              return func->compare(*tree_cast<Functional>(rhs.get()), child);
            } else {
              return func->compare(*tree_cast<Functional>(rhs.get()), self);
            }
          } else if (tree_isa<Statement>(lhs) || tree_isa<Anonymous>(lhs)) {
            return lhs == rhs;
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
    bool Term::match(const TreePtr<Term>& value, PSI_STD::vector<TreePtr<Term> >& wildcards, unsigned depth) const {
      return MatchComparator(&wildcards, depth).compare(tree_from(this), value);
    }

    const SIVtable Term::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Term", Tree);

    Metatype::Metatype()
    : Functional(&vtable) {
    }

    TreePtr<Term> Metatype::check_type_impl(const Metatype&) {
      return TreePtr<Term>();
    }
    
    TermResultInfo Metatype::result_info_impl(const Metatype&) {
      TermResultInfo result;
      result.mode = term_mode_value;
      result.type_mode = type_mode_metatype;
      result.type_fixed_size = true;
      return result;
    }

    bool Metatype::pure_impl(const Metatype&) {
      return true;
    }

    template<typename V>
    void Metatype::visit(V& v) {
      visit_base<Functional>(v);
    }

    const FunctionalVtable Metatype::vtable = PSI_COMPILER_FUNCTIONAL(Metatype, "psi.compiler.Metatype", Functional);

    Type::Type(const VtableType *vptr)
    : Functional(vptr) {
    }

    bool Type::pure_impl(const Type&) {
      return true;
    }

    const SIVtable Type::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Type", Functional);

    Anonymous::Anonymous(const TreePtr<Term>& type, TermMode mode_, const SourceLocation& location)
    : Term(&vtable, type, location),
    mode(mode_) {
      if (type->result_info().type_mode == type_mode_none)
        compile_context().error_throw(location, "Type of anonymous term is not a type");
    }
    
    TermResultInfo Anonymous::result_info_impl(const Anonymous& self) {
      TermResultInfo rt;
      rt.mode = self.mode;
      rt.type_fixed_size = false;
      rt.type_mode = (self.type->result_info().type_mode == type_mode_metatype) ? type_mode_complex : type_mode_none;
      return rt;
    }

    bool Anonymous::pure_impl(const Anonymous&) {
      return true;
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

    TreePtr<Term> Parameter::check_type_impl(const Parameter& self) {
      if (!self.parameter_type || !self.parameter_type->is_type())
        self.compile_context().error_throw(self.location(), "Type of parameter is not a type");
      return self.parameter_type;
    }
    
    TermResultInfo Parameter::result_info_impl(const Parameter& self) {
      TermResultInfo result;
      result.mode = term_mode_value;
      result.type_fixed_size = false;
      result.type_mode = (self.parameter_type->result_info().type_mode == type_mode_metatype) ? type_mode_complex : type_mode_none;
      return result;
    }
    
    bool Parameter::pure_impl(const Parameter&) {
      return true;
    }

    template<typename Visitor>
    void Parameter::visit(Visitor& v) {
      visit_base<Functional>(v);
      v("parameter_type", &Parameter::parameter_type)
      ("depth", &Parameter::depth)
      ("index", &Parameter::index);
    }
    
    const FunctionalVtable Parameter::vtable = PSI_COMPILER_FUNCTIONAL(Parameter, "psi.compiler.Parameter", Term);
    
    /**
     * \brief Get a value "functionally equivalent" to this one.
     * 
     * This unwraps any instances of Statement or GlobalStatement.
     */
    TreePtr<Term> functional_unwrap(const TreePtr<Term>& term) {
      TreePtr<Term> my_term = term;
      while (true) {
        if (TreePtr<PointerType> ptr = dyn_treeptr_cast<PointerType>(my_term))
          my_term = ptr->target_type;
        else if (TreePtr<DerivedType> derived = dyn_treeptr_cast<DerivedType>(my_term))
          my_term = derived->value_type;
        else if (TreePtr<GlobalStatement> def = dyn_treeptr_cast<GlobalStatement>(my_term)) {
          if (def->mode == statement_mode_functional)
            my_term = def->value;
          else
            break;
        } else if (TreePtr<Statement> stmt = dyn_treeptr_cast<Statement>(my_term)) {
          if (stmt->mode == statement_mode_functional)
            my_term = stmt->value;
          else
            break;
        } else
          break;
      }
      
      return my_term;
    }
  }
}
