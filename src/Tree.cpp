#include "Tree.hpp"

#include <boost/checked_delete.hpp>
#include <boost/bind.hpp>

namespace Psi {
  namespace Compiler {
    const SIVtable Tree::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.Tree", NULL);
    const SIVtable Term::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Term", Tree);
    const SIVtable Type::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Type", Term);

    const SIVtable EvaluateContext::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.EvaluateContext", Tree);
    const SIVtable Macro::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Macro", Tree);
    const SIVtable MacroEvaluateCallback::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.MacroEvaluateCallback", Tree);
    
    Tree::Tree(CompileContext& compile_context, const SourceLocation& location)
    : m_compile_context(&compile_context),
    m_location(location) {
    }
    
    void Tree::complete(bool dependency) {
      m_completion_state.complete(compile_context(), m_location, dependency,
                                  boost::bind(derived_vptr()->complete_callback, this));
    }

    Term::Term(const TreePtr<Term>& type, const SourceLocation& location)
    : Tree(type->compile_context(), location),
    m_type(type) {
    }

    /**
     * \brief Rewrite a term, substituting new trees for existing ones.
     *
     * \param location Location to use for error reporting.
     * \param substitutions Substitutions to make.
     */
    TreePtr<Term> Term::rewrite(const SourceLocation& location, const Map<TreePtr<Term>, TreePtr<Term> >& substitutions) {
      if (!this)
        return TreePtr<Term>();

      TreePtr<Term> *replacement = substitutions.get(TreePtr<Term>(this));
      if (replacement)
        return *replacement;
      else
        return TreePtr<Term>(derived_vptr()->rewrite(this, &location, substitutions.vptr(), substitutions.object()), false);
    }

    /**
     * \brief Check whether this term, which is a pattern, matches a given value.
     *
     * \param value Tree to match to.
     * \param wildcards Substitutions to be identified.
     * \param depth Number of parameter-enclosing terms above this match.
     */
    bool Term::match(const TreePtr<Term>& value, const List<TreePtr<Term> >& wildcards, unsigned depth) {
      if (this == value.get())
        return true;
      
      if (!this)
        return false;

      if (Parameter *parameter = dyn_tree_cast<Parameter>(this)) {
        if (parameter->depth == depth) {
          // Check type also matches
          if (!parameter->type()->match(value->type(), wildcards, depth))
            return false;

          TreePtr<Term>& wildcard = wildcards[parameter->index];
          if (wildcard) {
            if (wildcard != value)
              PSI_FAIL("not implemented");
            return false;
          } else {
            wildcards[parameter->index] = value;
            return true;
          }
        }
      }

      if (m_vptr == value->m_vptr) {
        // Trees are required to have the same static type to work with pattern matching.
        return derived_vptr()->match(this, value.get(), wildcards.vptr(), wildcards.object(), depth);
      } else {
        return false;
      }
    }

    /// \copydoc Term::match(const TreePtr<Term>&, const List<TreePtr<Term> >&, const List<TreePtr<Term> >&, unsigned)
    bool Term::match(const TreePtr<Term>& value, const List<TreePtr<Term> >& wildcards) {
      return match(value, wildcards, 0);
    }

    bool Term::match_impl(Term& lhs, Term& rhs, const List<TreePtr<Term> >&, unsigned) {
      return &lhs == &rhs;
    }
    
    TreePtr<Term> Term::rewrite_impl(Term& self, const SourceLocation&, const Map<TreePtr<Term>, TreePtr<Term> >&) {
      return TreePtr<Term>(&self);
    }

    Type::Type(const TreePtr<Term>& type, const SourceLocation& location)
    : Term(type, location) {
    }

    GlobalTree::GlobalTree(const TreePtr<Type>& type, const SourceLocation& location)
    : Term(type, location) {
    }

    ExternalGlobalTree::ExternalGlobalTree(const TreePtr<Type>& type, const SourceLocation& location)
    : GlobalTree(type, location) {
    }

    FunctionTypeArgument::FunctionTypeArgument(const TreePtr<Term>& type, const SourceLocation& location)
    : Term(type, location) {
    }
    
    FunctionType::FunctionType(CompileContext& context, const SourceLocation& location)
    : Type(context.metatype(), location) {
    }

    template<typename Visitor>
    void FunctionType::visit_impl(FunctionType& self, Visitor& visitor) {
      Type::visit_impl(self, visitor);
      visitor
      ("arguments", self.arguments)
      ("result_type", self.result_type);
    }

    template<typename Key, typename Value>
    class ForwardMap {
      Map<Key, Value> m_next;

    public:
      static const MapVtable vtable;

      PSI_STD::map<Key, Value> own;

      ForwardMap(const Map<Key, Value>& next) : m_next(next) {
      }

      static Value* get_impl(ForwardMap& self, const Key& key) {
        typename PSI_STD::map<Key, Value>::iterator it = self.own.find(key);
        if (it != self.own.end())
          return &it->second;
        return self.m_next.get(key);
      }

      Map<Key, Value> object() {
        return Map<Key, Value>(&vtable, this);
      }
    };

    template<typename Key, typename Value>
    const MapVtable ForwardMap<Key, Value>::vtable = PSI_MAP(ForwardMap, Key, Value);

    TreePtr<Term> FunctionType::rewrite_impl(const SourceLocation& location, const Map<TreePtr<Term>, TreePtr<Term> >& substitutions) {
      PSI_FAIL("need to sort out function type equivalence checking");
      
      for (PSI_STD::vector<TreePtr<FunctionTypeArgument> >::iterator ii = arguments.begin(), ie = arguments.end(); ii != ie; ++ii) {
        TreePtr<Term> rw_type = (*ii)->rewrite(location, substitutions);
        if (rw_type != (*ii))
          goto rewrite_required;
      }
      return TreePtr<Term>(this);

    rewrite_required:
      TreePtr<FunctionType> rw_self(new FunctionType(compile_context(), this->location()));

      ForwardMap<TreePtr<Term>, TreePtr<Term> > child_substitutions(substitutions);
      for (PSI_STD::vector<TreePtr<FunctionTypeArgument> >::iterator ii = arguments.begin(), ie = arguments.end(); ii != ie; ++ii) {
        TreePtr<Term> rw_type = (*ii)->type()->rewrite(location, child_substitutions.object());
        TreePtr<FunctionTypeArgument> rw_arg(new FunctionTypeArgument(rw_type, location));
        child_substitutions.own[*ii] = rw_arg;
        rw_self->arguments.push_back(rw_arg);
      }

      rw_self->result_type = result_type->rewrite(location, child_substitutions.object());

      return rw_self;
    }

    TreePtr<Term> FunctionType::argument_type_after(const SourceLocation& location, const List<TreePtr<Term> >& previous) {
      if (previous.size() >= arguments.size())
        compile_context().error_throw(location, "Too many arguments passed to function");
      
      PSI_STD::map<TreePtr<Term>, TreePtr<Term> > substitutions;
      for (unsigned ii = 0, ie = previous.size(); ii != ie; ++ii)
        substitutions[arguments[ii]] = previous[ii];

      TreePtr<> type = arguments[previous.size()]->type()->rewrite(location, substitutions);
      TreePtr<Type> cast_type = dyn_treeptr_cast<Type>(type);
      if (!cast_type)
        compile_context().error_throw(location, "Rewritten function argument type is not a type");

      return cast_type;
    }
    
    TreePtr<Term> FunctionType::result_type_after(const SourceLocation& location, const List<TreePtr<Term> >& previous) {
      if (previous.size() != arguments.size())
        compile_context().error_throw(location, "Incorrect number of arguments passed to function");

      PSI_STD::map<TreePtr<Term>, TreePtr<Term> > substitutions;
      for (unsigned ii = 0, ie = previous.size(); ii != ie; ++ii)
        substitutions[arguments[ii]] = previous[ii];

      TreePtr<> type = result_type->rewrite(location, substitutions);
      TreePtr<Type> cast_type = dyn_treeptr_cast<Type>(type);
      if (!cast_type)
        compile_context().error_throw(location, "Rewritten function result type is not a type");

      return cast_type;
    }

    const TermVtable Function::vtable = PSI_COMPILER_TERM(Function, "psi.compiler.Function", Term);

    Function::Function(const TreePtr<FunctionType>& type, const SourceLocation& location)
    : Term(type, location) {
    }

    Function::Function(const TreePtr<FunctionType>& type, const SourceLocation& location, DependencyPtr& dependency)
    : Term(type, location) {
      m_dependency.swap(dependency);
    }

    void Function::complete_callback_impl(Function& self) {
      self.m_dependency->run(&self);
      self.m_dependency.clear();
    }

    FunctionArgument::FunctionArgument(const TreePtr<Term>& type, const SourceLocation& location)
    : Term(type, location) {
    }

    TryFinally::TryFinally(const TreePtr<Term>& type, const SourceLocation& location)
    : Term(type, location) {
    }

    Block::Block(const TreePtr<Term>& type, const SourceLocation& location)
    : Term(type, location) {
    }

    Implementation::Implementation(CompileContext& compile_context, const SourceLocation& location)
    : Tree(compile_context, location) {
    }
    
    ImplementationTerm::ImplementationTerm(const TreePtr<Term>& type, const SourceLocation& location)
    : Term(type, location) {
    }
  }
}