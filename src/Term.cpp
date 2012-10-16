#include "Term.hpp"
#include "Compiler.hpp"

namespace Psi {
  namespace Compiler {
    Term::Term(const TermVtable *vptr, const TreePtr<Term>& type_, const SourceLocation& location)
    : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), type_.compile_context(), location),
    type(type_) {
    }

    Term::Term(const TermVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
    : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), compile_context, location) {
    }
    
    /**
     * \brief Check whether this tree, which is a pattern, matches a given value.
     *
     * \param value Tree to match to.
     * \param wildcards Substitutions to be identified.
     * \param depth Number of parameter-enclosing terms above this match.
     */
    bool Term::match(const TreePtr<Term>& value, PSI_STD::vector<TreePtr<Term> >& wildcards, unsigned depth) const {
      const Term *self = this;
      const Term *other = value.get();

      if (const Parameter *parameter = dyn_tree_cast<Parameter>(self)) {
        if (parameter->depth == depth) {
          // Check type also matches
          if (!parameter->type->match(other->type, wildcards, depth))
            return false;

          if (parameter->index >= wildcards.size())
            return false;

          TreePtr<Term>& wildcard = wildcards[parameter->index];
          if (wildcard) {
            return wildcard->equivalent(TreePtr<Term>(other));
          } else {
            wildcards[parameter->index].reset(other);
            return true;
          }
        }
      }

      if (si_vptr(self) == si_vptr(other)) {
        // Trees are required to have the same static type to work with pattern matching.
        return derived_vptr(this)->match(this, other, &wildcards, depth);
      } else {
        return false;
      }
    }

    /**
     * \brief Check whether this term is equivalent to another.
     * 
     * This is transitive.
     * 
     * It is possible that transitivity is bugged since this is implemented
     * via match().
     */
    bool Term::equivalent(const TreePtr<Term>& value) const {
      PSI_STD::vector<TreePtr<Term> > empty;
      return match(value, empty, 0);
    }

    bool Term::match_impl(const Term&, const Term&, PSI_STD::vector<TreePtr<Term> >&, unsigned) {
      return false;
    }
    
    TreePtr<Term> Term::parameterize_impl(const Term& self, const SourceLocation&, const PSI_STD::vector<TreePtr<Anonymous> >&, unsigned) {
      return TreePtr<Term>(&self);
    }
    
    TreePtr<Term> Term::specialize_impl(const Term& self, const SourceLocation&, const PSI_STD::vector<TreePtr<Term> >&, unsigned) {
      return TreePtr<Term>(&self);
    }
    
    const SIVtable Term::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Term", Tree);

    Metatype::Metatype(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }

    template<typename V>
    void Metatype::visit(V& v) {
      visit_base<Term>(v);
    }

    const TermVtable Metatype::vtable = PSI_COMPILER_TERM(Metatype, "psi.compiler.Metatype", Term);

    Type::Type(const TermVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
    : Term(vptr, compile_context.builtins().metatype, location) {
    }

    const SIVtable Type::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Type", Term);

    Anonymous::Anonymous(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }

    Anonymous::Anonymous(const TreePtr<Term>& type, const SourceLocation& location)
    : Term(&vtable, type, location) {
    }
    
    template<typename V>
    void Anonymous::visit(V& v) {
      visit_base<Term>(v);
    }

    const TermVtable Anonymous::vtable = PSI_COMPILER_TERM(Anonymous, "psi.compiler.Anonymous", Term);

    Parameter::Parameter(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }

    Parameter::Parameter(const TreePtr<Term>& type, unsigned depth_, unsigned index_, const SourceLocation& location)
    : Term(&vtable, type, location),
    depth(depth_),
    index(index_) {
    }

    template<typename Visitor>
    void Parameter::visit(Visitor& v) {
      visit_base<Term>(v);
      v("depth", &Parameter::depth)
      ("index", &Parameter::index);
    }

    const TermVtable Parameter::vtable = PSI_COMPILER_TERM(Parameter, "psi.compiler.Parameter", Term);
  }
}
