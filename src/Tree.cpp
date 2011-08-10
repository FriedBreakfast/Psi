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

    const TermVtable Parameter::vtable = PSI_COMPILER_TERM(Parameter, "psi.compiler.Parameter", Term);
    const TreeVtable Interface::vtable = PSI_COMPILER_TREE(Interface, "psi.compiler.Interface", Tree);
    const TreeVtable Implementation::vtable = PSI_COMPILER_TREE(Implementation, "psi.compiler.Implementation", Tree);
    const SIVtable ImplementationTerm::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.ImplementationTerm", Term);

    const TermVtable FunctionType::vtable = PSI_COMPILER_TERM(FunctionType, "psi.compiler.FunctionType", Type);
    const TermVtable FunctionTypeArgument::vtable = PSI_COMPILER_TERM(FunctionTypeArgument, "psi.compiler.FunctionTypeArgument", Term);
    const TermVtable Function::vtable = PSI_COMPILER_TERM(Function, "psi.compiler.Function", Term);
    const TermVtable FunctionArgument::vtable = PSI_COMPILER_TERM(FunctionArgument, "psi.compiler.FunctionArgument", Term);

    const TermVtable Block::vtable = PSI_COMPILER_TERM(Block, "psi.compiler.Block", Term);
    const TermVtable Statement::vtable = PSI_COMPILER_TERM(Statement, "psi.compiler.Statement", Term);
    const TermVtable TryFinally::vtable = PSI_COMPILER_TERM(TryFinally, "psi.compiler.TryFinally", Term);

    const TermVtable Metatype::vtable = PSI_COMPILER_TERM(Metatype, "psi.compiler.Metatype", Term);
    const TermVtable EmptyType::vtable = PSI_COMPILER_TERM(EmptyType, "psi.compiler.EmptyType", Type);
    const TermVtable NullValue::vtable = PSI_COMPILER_TERM(NullValue, "psi.compiler.NullValue", Term);

    const TermVtable RecursiveType::vtable = PSI_COMPILER_TERM(RecursiveType, "psi.compiler.RecursiveType", ImplementationTerm);
    const TermVtable RecursiveValue::vtable = PSI_COMPILER_TERM(RecursiveValue, "psi.compiler.RecursiveValue", Term);

    const SIVtable Global::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Global", Term);
    const TermVtable ExternalGlobal::vtable = PSI_COMPILER_TERM(ExternalGlobal, "psi.compiler.ExternalGlobal", Global);
    
    Tree::Tree(CompileContext& compile_context, const SourceLocation& location)
      : m_reference_count(0),
	m_compile_context(&compile_context),
	m_location(location) {
      m_compile_context->m_gc_list.push_back(*this);
    }

    Tree::~Tree() {
      if (is_linked())
	m_compile_context->m_gc_list.erase(m_compile_context->m_gc_list.iterator_to(*this));
    }
    
    void Tree::complete(bool dependency) {
      m_completion_state.complete(compile_context(), m_location, dependency,
                                  boost::bind(derived_vptr(this)->complete_callback, this));
    }

    Term::Term(const TreePtr<Term>& type, const SourceLocation& location)
    : Tree(type->compile_context(), location),
    m_type(type) {
    }

    Term::Term(CompileContext& compile_context, const SourceLocation& location)
      : Tree(compile_context, location) {
    }

    Term::~Term() {
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
        return TreePtr<Term>(derived_vptr(this)->rewrite(this, &location, substitutions.vptr(), substitutions.object()), false);
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
        return derived_vptr(this)->match(this, value.get(), wildcards.vptr(), wildcards.object(), depth);
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

    Type::Type(CompileContext& compile_context, const SourceLocation& location)
      : Term(compile_context.metatype(), location) {
    }

    Global::Global(const TreePtr<Type>& type, const SourceLocation& location)
    : Term(type, location) {
    }

    ExternalGlobal::ExternalGlobal(const TreePtr<Type>& type, const SourceLocation& location)
    : Global(type, location) {
      PSI_COMPILER_TREE_INIT();
    }

    FunctionTypeArgument::FunctionTypeArgument(const TreePtr<Term>& type, const SourceLocation& location)
    : Term(type, location) {
      PSI_COMPILER_TREE_INIT();
    }
    
    FunctionType::FunctionType(CompileContext& context, const SourceLocation& location)
    : Type(context, location) {
      PSI_COMPILER_TREE_INIT();
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

    TreePtr<Term> FunctionType::rewrite_impl(FunctionType& self, const SourceLocation& location, const Map<TreePtr<Term>, TreePtr<Term> >& substitutions) {
      PSI_FAIL("need to sort out function type equivalence checking");
      
      for (PSI_STD::vector<TreePtr<FunctionTypeArgument> >::iterator ii = self.arguments.begin(), ie = self.arguments.end(); ii != ie; ++ii) {
        TreePtr<Term> rw_type = (*ii)->rewrite(location, substitutions);
        if (rw_type != (*ii))
          goto rewrite_required;
      }
      return TreePtr<Term>(&self);

    rewrite_required:
      TreePtr<FunctionType> rw_self(new FunctionType(self.compile_context(), self.location()));

      ForwardMap<TreePtr<Term>, TreePtr<Term> > child_substitutions(substitutions);
      for (PSI_STD::vector<TreePtr<FunctionTypeArgument> >::iterator ii = self.arguments.begin(), ie = self.arguments.end(); ii != ie; ++ii) {
        TreePtr<Term> rw_type = (*ii)->type()->rewrite(location, child_substitutions.object());
        TreePtr<FunctionTypeArgument> rw_arg(new FunctionTypeArgument(rw_type, location));
        child_substitutions.own[*ii] = rw_arg;
        rw_self->arguments.push_back(rw_arg);
      }

      rw_self->result_type = self.result_type->rewrite(location, child_substitutions.object());

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

    Function::Function(const TreePtr<FunctionType>& type, const SourceLocation& location)
    : Term(type, location) {
      PSI_COMPILER_TREE_INIT();
    }

    Function::Function(const TreePtr<FunctionType>& type, const SourceLocation& location, DependencyPtr& dependency)
    : Term(type, location) {
      PSI_COMPILER_TREE_INIT();
      m_dependency.swap(dependency);
    }

    void Function::complete_callback_impl(Function& self) {
      self.m_dependency->run(&self);
      self.m_dependency.clear();
    }

    FunctionArgument::FunctionArgument(const TreePtr<Term>& type, const SourceLocation& location)
    : Term(type, location) {
      PSI_COMPILER_TREE_INIT();
    }

    TryFinally::TryFinally(const TreePtr<Term>& type, const SourceLocation& location)
    : Term(type, location) {
      PSI_COMPILER_TREE_INIT();
    }

    Statement::Statement(const TreePtr<Term>& value_, const SourceLocation& location)
      : Term(value_->type(), location), value(value_) {
      PSI_COMPILER_TREE_INIT();
    }

    Block::Block(const TreePtr<Term>& type, const SourceLocation& location)
    : Term(type, location) {
      PSI_COMPILER_TREE_INIT();
    }

    Interface::Interface(CompileContext& compile_context, const String& name_, const SourceLocation& location)
      : Tree(compile_context, location),
	name(name_) {
      PSI_COMPILER_TREE_INIT();
    }

    Implementation::Implementation(CompileContext& compile_context, const SourceLocation& location)
    : Tree(compile_context, location) {
      PSI_COMPILER_TREE_INIT();
    }
    
    ImplementationTerm::ImplementationTerm(const TreePtr<Term>& type, const SourceLocation& location)
    : Term(type, location) {
    }

    Metatype::Metatype(CompileContext& compile_context, const SourceLocation& location)
      : Term(compile_context, location) {
      PSI_COMPILER_TREE_INIT();
    }

    EmptyType::EmptyType(CompileContext& compile_context, const SourceLocation& location)
      : Type(compile_context, location) {
      PSI_COMPILER_TREE_INIT();
    }

    TreePtr<Term> EmptyType::value(CompileContext& compile_context, const SourceLocation& location) {
      return NullValue::get(compile_context.empty_type(), location);
    }

    NullValue::NullValue(const TreePtr<Term>& type, const SourceLocation& location)
      : Term(type, location) {
      PSI_COMPILER_TREE_INIT();
    }

    TreePtr<Term> NullValue::get(const TreePtr<Term>& type, const SourceLocation& location) {
      return TreePtr<Term>(new NullValue(type, location));
    }

    RecursiveType::RecursiveType(CompileContext& compile_context, const SourceLocation& location)
    : ImplementationTerm(compile_context.metatype(), location) {
      PSI_COMPILER_TREE_INIT();
    }
    
    RecursiveValue::RecursiveValue(const TreePtr<RecursiveType>& type, const TreePtr<Term>& member, const SourceLocation& location)
    : Term(type, location),
    m_member(member) {
      PSI_COMPILER_TREE_INIT();
    }
    
    TreePtr<Term> RecursiveValue::get(const TreePtr<RecursiveType>&, const TreePtr<Term>&, const SourceLocation&) {
      PSI_FAIL("not implemented");
    }
  }
}
