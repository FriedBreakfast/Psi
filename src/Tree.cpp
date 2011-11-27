#include "Tree.hpp"

#include <boost/checked_delete.hpp>
#include <boost/bind.hpp>

namespace Psi {
  namespace Compiler {
    TreeBase::TreeBase(CompileContext& compile_context, const SourceLocation& location)
    : m_reference_count(0),
    m_compile_context(&compile_context),
    m_location(location) {
      m_compile_context->m_gc_list.push_back(*this);
    }

    TreeBase::~TreeBase() {
      if (is_linked())
        m_compile_context->m_gc_list.erase(m_compile_context->m_gc_list.iterator_to(*this));
    }
    
    Tree::Tree(CompileContext& compile_context, const SourceLocation& location)
    : TreeBase(compile_context, location) {
    }

    TreeCallback::TreeCallback(CompileContext& compile_context, const SourceLocation& location)
    : TreeBase(compile_context, location), m_state(state_ready) {
    }

    Term::Term(const TreePtr<Term>& type, const SourceLocation& location)
    : Tree(type->compile_context(), location),
    m_type(type) {
    }

    Term::Term(CompileContext& compile_context, const SourceLocation& location)
      : Tree(compile_context, location) {
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
        return tree_from_base<Term>(derived_vptr(this)->rewrite(this, &location, substitutions.vptr(), substitutions.object()), false);
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
        if (parameter->m_depth == depth) {
          // Check type also matches
          if (!m_type->match(value->type(), wildcards, depth))
            return false;

          TreePtr<Term>& wildcard = wildcards[parameter->m_index];
          if (wildcard) {
            if (wildcard != value)
              PSI_FAIL("not implemented");
            return false;
          } else {
            wildcards[parameter->m_index] = value;
            return true;
          }
        }
      }

      Term *value_term = value.get();
      if (m_vptr == value_term->m_vptr) {
        // Trees are required to have the same static type to work with pattern matching.
        return derived_vptr(this)->match(this, value_term, wildcards.vptr(), wildcards.object(), depth);
      } else {
        return false;
      }
    }

    TreePtr<> Term::interface_search_impl(Term& self,
                                          const TreePtr<Interface>& interface,
                                          const List<TreePtr<Term> >& parameters) {
      return self.m_type->interface_search(interface, parameters);
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

    Parameter::Parameter(const TreePtr<Term>& type, unsigned depth, unsigned index, const SourceLocation& location)
    : Term(type, location),
    m_depth(depth),
    m_index(index) {
    }

    template<typename Visitor>
    void Parameter::visit_impl(Parameter& self, Visitor& visitor) {
      Term::visit_impl(self, visitor);
      visitor
        ("depth", self.m_depth)
        ("index", self.m_index);
    }

    ExternalGlobal::ExternalGlobal(const TreePtr<Type>& type, const SourceLocation& location)
    : Global(type, location) {
      PSI_COMPILER_TREE_INIT();
    }

    FunctionTypeArgument::FunctionTypeArgument(const TreePtr<Term>& type, const SourceLocation& location)
    : Term(type, location) {
      PSI_COMPILER_TREE_INIT();
    }
    
    FunctionType::FunctionType(const TreePtr<Term>& result_type, const PSI_STD::vector<TreePtr<FunctionTypeArgument> >& arguments, const SourceLocation& location)
    : Type(result_type->compile_context(), location),
    m_result_type(result_type),
    m_arguments(arguments) {
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
      
      for (PSI_STD::vector<TreePtr<FunctionTypeArgument> >::iterator ii = self.m_arguments.begin(), ie = self.m_arguments.end(); ii != ie; ++ii) {
        TreePtr<Term> rw_type = (*ii)->rewrite(location, substitutions);
        if (rw_type != (*ii))
          goto rewrite_required;
      }
      return TreePtr<Term>(&self);

    rewrite_required:
      PSI_STD::vector<TreePtr<FunctionTypeArgument> > arguments;

      ForwardMap<TreePtr<Term>, TreePtr<Term> > child_substitutions(substitutions);
      for (PSI_STD::vector<TreePtr<FunctionTypeArgument> >::iterator ii = self.m_arguments.begin(), ie = self.m_arguments.end(); ii != ie; ++ii) {
        TreePtr<Term> rw_type = (*ii)->type()->rewrite(location, child_substitutions.object());
        TreePtr<FunctionTypeArgument> rw_arg(new FunctionTypeArgument(rw_type, location));
        child_substitutions.own[*ii] = rw_arg;
        arguments.push_back(rw_arg);
      }

      TreePtr<Term> result_type = self.m_result_type->rewrite(location, child_substitutions.object());

      return TreePtr<FunctionType>(new FunctionType(result_type, arguments, location));
    }

    TreePtr<Term> FunctionType::argument_type_after(const SourceLocation& location, const List<TreePtr<Term> >& previous) {
      if (previous.size() >= m_arguments.size())
        compile_context().error_throw(location, "Too many arguments passed to function");
      
      PSI_STD::map<TreePtr<Term>, TreePtr<Term> > substitutions;
      for (unsigned ii = 0, ie = previous.size(); ii != ie; ++ii)
        substitutions[m_arguments[ii]] = previous[ii];

      TreePtr<> type = m_arguments[previous.size()]->type()->rewrite(location, substitutions);
      TreePtr<Type> cast_type = dyn_treeptr_cast<Type>(type);
      if (!cast_type)
        compile_context().error_throw(location, "Rewritten function argument type is not a type");

      return cast_type;
    }
    
    TreePtr<Term> FunctionType::result_type_after(const SourceLocation& location, const List<TreePtr<Term> >& previous) {
      if (previous.size() != m_arguments.size())
        compile_context().error_throw(location, "Incorrect number of arguments passed to function");

      PSI_STD::map<TreePtr<Term>, TreePtr<Term> > substitutions;
      for (unsigned ii = 0, ie = previous.size(); ii != ie; ++ii)
        substitutions[m_arguments[ii]] = previous[ii];

      TreePtr<> type = m_result_type->rewrite(location, substitutions);
      TreePtr<Type> cast_type = dyn_treeptr_cast<Type>(type);
      if (!cast_type)
        compile_context().error_throw(location, "Rewritten function result type is not a type");

      return cast_type;
    }

    Function::Function(const TreePtr<Term>& result_type,
                       const PSI_STD::vector<TreePtr<FunctionArgument> >& arguments,
                       const TreePtr<Term>& body,
                       const SourceLocation& location)
    : Term(get_type(result_type, arguments), location),
    m_arguments(arguments),
    m_result_type(result_type),
    m_body(body) {
      PSI_COMPILER_TREE_INIT();
    }

    TreePtr<Term> Function::get_type(const TreePtr<Term>& result_type,
                                     const PSI_STD::vector<TreePtr<FunctionArgument> >& arguments) {
      return default_;
    }

    template<typename Visitor> void Function::visit_impl(Function& self, Visitor& visitor) {
      Term::visit_impl(self, visitor);
      visitor
        ("arguments", self.m_arguments)
        ("result_type", self.m_result_type)
        ("body", self.m_body);
    }

    FunctionArgument::FunctionArgument(const TreePtr<Term>& type, const SourceLocation& location)
    : Term(type, location) {
      PSI_COMPILER_TREE_INIT();
    }

    TryFinally::TryFinally(const TreePtr<Term>& try_expr, const TreePtr<Term>& finally_expr, const SourceLocation& location)
    : Term(try_expr->type(), location),
    m_try_expr(try_expr),
    m_finally_expr(finally_expr) {
      PSI_COMPILER_TREE_INIT();
    }

    template<typename Visitor> void TryFinally::visit_impl(TryFinally& self, Visitor& visitor) {
      Term::visit_impl(self, visitor);
      visitor
        ("try_expr", self.m_try_expr)
        ("finally_expr", self.m_finally_expr);
    }

    Statement::Statement(const TreePtr<Term>& value, const SourceLocation& location)
    : Term(value->type(), location),
    m_value(value) {
      PSI_COMPILER_TREE_INIT();
    }

    template<typename Visitor>
    void Statement::visit_impl(Statement& self, Visitor& visitor) {
      Term::visit_impl(self, visitor);
      visitor("value", self.m_value);
    }

    Block::Block(const PSI_STD::vector<TreePtr<Statement> >& statements, const TreePtr<Term>& value, const SourceLocation& location)
    : Term(value->type(), location),
    m_statements(statements),
    m_value(value) {
      PSI_COMPILER_TREE_INIT();
    }

    template<typename Visitor>
    void Block::visit_impl(Block& self, Visitor& visitor) {
      Term::visit_impl(self, visitor);
      visitor
        ("statements", self.m_statements)
        ("result", self.m_value);
    }

    Interface::Interface(CompileContext& compile_context, const SourceLocation& location)
      : Tree(compile_context, location) {
      PSI_COMPILER_TREE_INIT();
    }

    Implementation::Implementation(CompileContext& compile_context,
                                   const TreePtr<>& value,
                                   const TreePtr<Interface>& interface,
                                   const PSI_STD::vector<TreePtr<Term> >& wildcard_types,
                                   const PSI_STD::vector<TreePtr<Term> >& interface_parameters,
                                   const SourceLocation& location)
    : Tree(compile_context, location),
    m_value(value),
    m_interface(interface),
    m_wildcard_types(wildcard_types),
    m_interface_parameters(interface_parameters) {
      PSI_COMPILER_TREE_INIT();
    }

    template<typename Visitor>
    void Implementation::visit_impl(Implementation& self, Visitor& visitor) {
      Tree::visit_impl(self, visitor);
      visitor
        ("value", self.m_value)
        ("interface", self.m_interface)
        ("wildcard_types", self.m_wildcard_types)
        ("interface_parameters", self.m_interface_parameters);
    }

    bool Implementation::matches(const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters) {
      if (interface != m_interface)
        return false;
      
      PSI_ASSERT(m_interface_parameters.size() == parameters.size());
      PSI_STD::vector<TreePtr<Term> > wildcards(m_wildcard_types.size());
      for (std::size_t ji = 0, je = m_interface_parameters.size(); ji != je; ++ji) {
        if (!m_interface_parameters[ji]->match(parameters[ji], list_from_stl(wildcards)))
          return false;
      }

      return true;
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
      return TreePtr<Term>(new NullValue(compile_context.empty_type(), location));
    }

    NullValue::NullValue(const TreePtr<Term>& type, const SourceLocation& location)
      : Term(type, location) {
      PSI_COMPILER_TREE_INIT();
    }

    GenericType::GenericType(const TreePtr<Term>& member,
                             const PSI_STD::vector<TreePtr<Parameter> >& parameters,
                             const PSI_STD::vector<TreePtr<Implementation> >& implementations,
                             const SourceLocation& location)
    : Tree(member->compile_context(), location),
    m_member(member),
    m_parameters(parameters),
    m_implementations(implementations) {
      PSI_COMPILER_TREE_INIT();
    }

    template<typename Visitor>
    void GenericType::visit_impl(GenericType& self, Visitor& visitor) {
      Tree::visit_impl(self, visitor);
      visitor
        ("parameters", self.m_parameters)
        ("member", self.m_member)
        ("implementations", self.m_implementations);
    }
    
    TypeInstance::TypeInstance(const TreePtr<GenericType>& generic_type,
                               const PSI_STD::vector<TreePtr<Term> >& parameter_values,
                               const SourceLocation& location)
    : Term(generic_type->compile_context().metatype(), location),
    m_generic_type(generic_type),
    m_parameter_values(parameter_values) {
      PSI_COMPILER_TREE_INIT();
    }

    template<typename Visitor>
    void TypeInstance::visit_impl(TypeInstance& self, Visitor& visitor) {
      Term::visit_impl(self, visitor);
      visitor
        ("generic_type", self.m_generic_type)
        ("parameter_values", self.m_parameter_values);
    }
    
    TreePtr<> TypeInstance::interface_search_impl(TypeInstance& self,
                                                  const TreePtr<Interface>& interface,
                                                  const List<TreePtr<Term> >& parameters) {
      const PSI_STD::vector<TreePtr<Implementation> >& implementations = self.m_generic_type->implementations();
	    for (PSI_STD::vector<TreePtr<Implementation> >::const_iterator ii = implementations.begin(), ie = implementations.end(); ii != ie; ++ii) {
        if ((*ii)->matches(interface, parameters))
          return (*ii)->value();
      }

      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = self.m_parameter_values.begin(), ie = self.m_parameter_values.end(); ii != ie; ++ii) {
        if (TreePtr<> result = (*ii)->interface_search(interface, parameters))
          return result;        
      }

      return TreePtr<>();
    }

    TypeInstanceValue::TypeInstanceValue(const TreePtr<TypeInstance>& type, const TreePtr<Term>& member_value, const SourceLocation& location)
    : Term(type, location),
    m_member_value(member_value) {
      PSI_COMPILER_TREE_INIT();
    }

    template<typename Visitor>
    void TypeInstanceValue::visit_impl(TypeInstanceValue& self, Visitor& visitor) {
      Term::visit_impl(self, visitor);
      visitor("member_value", self.m_member_value);
    }

    const SIVtable TreeBase::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.TreeBase", NULL);
    const SIVtable TreeCallback::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.TreeCallback", &TreeBase::vtable);
    const SIVtable Tree::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.Tree", &TreeBase::vtable);

    const SIVtable Term::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Term", Tree);
    const SIVtable Type::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Type", Term);

    const SIVtable EvaluateContext::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.EvaluateContext", Tree);
    const SIVtable Macro::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Macro", Tree);

    const TermVtable Parameter::vtable = PSI_COMPILER_TERM(Parameter, "psi.compiler.Parameter", Term);
    const TreeVtable Interface::vtable = PSI_COMPILER_TREE(Interface, "psi.compiler.Interface", Tree);
    const TreeVtable Implementation::vtable = PSI_COMPILER_TREE(Implementation, "psi.compiler.Implementation", Tree);

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

    const TreeVtable GenericType::vtable = PSI_COMPILER_TREE(GenericType, "psi.compiler.GenericType", Tree);
    const TermVtable TypeInstance::vtable = PSI_COMPILER_TERM(TypeInstance, "psi.compiler.TypeInstance", Term);
    const TermVtable TypeInstanceValue::vtable = PSI_COMPILER_TERM(TypeInstanceValue, "psi.compiler.TypeInstanceValue", Term);

    const SIVtable Global::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Global", Term);
    const TermVtable ExternalGlobal::vtable = PSI_COMPILER_TERM(ExternalGlobal, "psi.compiler.ExternalGlobal", Global);
  }
}
