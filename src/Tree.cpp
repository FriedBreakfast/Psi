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
    
    Type::Type(CompileContext& compile_context, const SourceLocation& location)
      : Term(compile_context.metatype(), location) {
    }

    Global::Global(const TreePtr<Term>& type, const SourceLocation& location)
    : Term(type, location) {
    }

    Anonymous::Anonymous(const TreePtr<Term>& type, const SourceLocation& location)
    : Term(type, location) {
      PSI_COMPILER_TREE_INIT();
    }

    Parameter::Parameter(const TreePtr<Term>& type, unsigned depth, unsigned index, const SourceLocation& location)
    : Term(type, location),
    m_depth(depth),
    m_index(index) {
      PSI_COMPILER_TREE_INIT();
    }

    template<typename Visitor>
    void Parameter::visit(Visitor& v) {
      visit_base<Term>(v);
      v("depth", &Parameter::m_depth)
      ("index", &Parameter::m_index);
    }

    ExternalGlobal::ExternalGlobal(const TreePtr<Term>& type, const String& symbol, const SourceLocation& location)
    : Global(type, location),
    m_symbol(symbol) {
      PSI_COMPILER_TREE_INIT();
    }

    FunctionType::FunctionType(const TreePtr<Term>& result_type, const PSI_STD::vector<TreePtr<Anonymous> >& arguments, const SourceLocation& location)
    : Type(result_type->compile_context(), location) {
      PSI_COMPILER_TREE_INIT();

      /*
       * I haven't written a method to slice a list yet and since I want an error should the arguments forward
       * reference, I copy the argument list so I can have a "short" version.
       */
      PSI_STD::vector<TreePtr<Anonymous> > arguments_copy;
      arguments_copy.reserve(arguments.size());
      m_argument_types.reserve(arguments.size());
      for (PSI_STD::vector<TreePtr<Anonymous> >::const_iterator ii = arguments.begin(), ie = arguments.end(); ii != ie; ++ii) {
        m_argument_types.push_back((*ii)->type()->parameterize(location, list_from_stl(arguments_copy)));
        arguments_copy.push_back(*ii);
      }

      m_result_type = result_type->parameterize(location, list_from_stl(arguments_copy));
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

    TreePtr<Term> FunctionType::argument_type_after(const SourceLocation& location, const List<TreePtr<Term> >& previous) {
      if (previous.size() >= m_argument_types.size())
        compile_context().error_throw(location, "Too many arguments passed to function");

      TreePtr<Term> type = m_argument_types[previous.size()]->type()->specialize(location, previous);
      if (!type->is_type())
        compile_context().error_throw(location, "Rewritten function argument type is not a type");

      return type;
    }
    
    TreePtr<Term> FunctionType::result_type_after(const SourceLocation& location, const List<TreePtr<Term> >& previous) {
      if (previous.size() != m_argument_types.size())
        compile_context().error_throw(location, "Incorrect number of arguments passed to function");

      TreePtr<Term> type = m_result_type->specialize(location, previous);
      if (!type->is_type())
        compile_context().error_throw(location, "Rewritten function result type is not a type");

      return type;
    }

    Function::Function(const TreePtr<Term>& result_type,
                       const PSI_STD::vector<TreePtr<Anonymous> >& arguments,
                       const TreePtr<Term>& body,
                       const SourceLocation& location)
    : Term(TreePtr<Term>(new FunctionType(result_type, arguments, location)), location),
    m_arguments(arguments),
    m_result_type(result_type),
    m_body(body) {
      PSI_COMPILER_TREE_INIT();
    }

    template<typename Visitor> void Function::visit(Visitor& v) {
      visit_base<Term>(v);
      v("arguments", &Function::m_arguments)
      ("result_type", &Function::m_result_type)
      ("body", &Function::m_body);
    }

    TryFinally::TryFinally(const TreePtr<Term>& try_expr, const TreePtr<Term>& finally_expr, const SourceLocation& location)
    : Term(try_expr->type(), location),
    m_try_expr(try_expr),
    m_finally_expr(finally_expr) {
      PSI_COMPILER_TREE_INIT();
    }

    template<typename Visitor> void TryFinally::visit(Visitor& v) {
      visit_base<Term>(v);
      v("try_expr", &TryFinally::m_try_expr)
      ("finally_expr", &TryFinally::m_finally_expr);
    }

    Statement::Statement(const TreePtr<Term>& value, const SourceLocation& location)
    : Term(value->type(), location),
    m_value(value) {
      PSI_COMPILER_TREE_INIT();
    }

    template<typename Visitor>
    void Statement::visit(Visitor& v) {
      visit_base<Term>(v);
      v("value", &Statement::m_value);
    }

    Block::Block(const PSI_STD::vector<TreePtr<Statement> >& statements, const TreePtr<Term>& value, const SourceLocation& location)
    : Term(value->type(), location),
    m_statements(statements),
    m_value(value) {
      PSI_COMPILER_TREE_INIT();
    }

    template<typename Visitor>
    void Block::visit(Visitor& v) {
      visit_base<Term>(v);
      v("statements", &Block::m_statements)
      ("result", &Block::m_value);
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
    void Implementation::visit(Visitor& v) {
      visit_base<Tree>(v);
      v("value", &Implementation::m_value)
      ("interface", &Implementation::m_interface)
      ("wildcard_types", &Implementation::m_wildcard_types)
      ("interface_parameters", &Implementation::m_interface_parameters);
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
    void GenericType::visit(Visitor& v) {
      visit_base<Tree>(v);
      v("parameters", &GenericType::m_parameters)
      ("member", &GenericType::m_member)
      ("implementations", &GenericType::m_implementations);
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
    void TypeInstance::visit(Visitor& v) {
      visit_base<Term>(v);
      v("generic_type", &TypeInstance::m_generic_type)
      ("parameter_values", &TypeInstance::m_parameter_values);
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
    void TypeInstanceValue::visit(Visitor& v) {
      visit_base<Term>(v);
      v("member_value", &TypeInstanceValue::m_member_value);
    }

    const SIVtable TreeBase::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.TreeBase", NULL);
    const SIVtable TreeCallback::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.TreeCallback", &TreeBase::vtable);
    const SIVtable Tree::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.Tree", &TreeBase::vtable);

    const SIVtable Term::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Term", Tree);
    const SIVtable Type::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Type", Term);

    const SIVtable EvaluateContext::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.EvaluateContext", Tree);
    const SIVtable Macro::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Macro", Tree);

    const TermVtable Anonymous::vtable = PSI_COMPILER_TERM(Anonymous, "psi.compiler.Anonymous", Term);
    const TermVtable Parameter::vtable = PSI_COMPILER_TERM(Parameter, "psi.compiler.Parameter", Term);
    const TreeVtable Interface::vtable = PSI_COMPILER_TREE(Interface, "psi.compiler.Interface", Tree);
    const TreeVtable Implementation::vtable = PSI_COMPILER_TREE(Implementation, "psi.compiler.Implementation", Tree);

    const TermVtable FunctionType::vtable = PSI_COMPILER_TERM(FunctionType, "psi.compiler.FunctionType", Type);
    const TermVtable Function::vtable = PSI_COMPILER_TERM(Function, "psi.compiler.Function", Term);

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
