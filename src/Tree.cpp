#include "Tree.hpp"
#include "Class.hpp"
#include "Parser.hpp"

#include <boost/checked_delete.hpp>
#include <boost/bind.hpp>

namespace Psi {
  namespace Compiler {
    Object::Object(const ObjectVtable *vtable, CompileContext& compile_context)
    : m_reference_count(0),
    m_compile_context(&compile_context) {
      PSI_COMPILER_SI_INIT(vtable);
      PSI_ASSERT(!m_vptr->abstract);
      m_compile_context->m_gc_list.push_back(*this);
    }

    Object::~Object() {
      if (is_linked())
        m_compile_context->m_gc_list.erase(m_compile_context->m_gc_list.iterator_to(*this));
    }
    
    const SIVtable Object::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.Object", NULL);

    TreeBase::TreeBase(const TreeBaseVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
    : Object(PSI_COMPILER_VPTR_UP(Object, vptr), compile_context),
    m_location(location) {
    }
    
    const SIVtable TreeBase::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.TreeBase", &Object::vtable);

    Tree::Tree(const TreeVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
    : TreeBase(PSI_COMPILER_VPTR_UP(TreeBase, vptr), compile_context, location) {
    }
    
    /**
     * Recursively evaluate all tree references inside this tree.
     */
    void Tree::complete() {
      VisitQueue<TreePtr<> > queue;
      queue.push(TreePtr<>(this));
      
      while (!queue.empty()) {
        TreePtr<> p = queue.pop();
        const Tree *ptr = p.get();
        derived_vptr(ptr)->complete(const_cast<Tree*>(ptr), &queue);
      }
    }

    /**
     * \brief Check whether this tree, which is a pattern, matches a given value.
     *
     * \param value Tree to match to.
     * \param wildcards Substitutions to be identified.
     * \param depth Number of parameter-enclosing terms above this match.
     */
    bool Tree::match(const TreePtr<Tree>& value, const List<TreePtr<Term> >& wildcards, unsigned depth) {
      // Unwrap any Statements involved
      Tree *self = this;
      while (Statement *stmt = dyn_tree_cast<Statement>(self))
        self = stmt->value.get();
      
      Tree *other = value.get();
      while (Statement *stmt = dyn_tree_cast<Statement>(other))
        other = stmt->value.get();
      
      if (self == other)
        return true;

      if (!self)
        return false;

      if (Parameter *parameter = dyn_tree_cast<Parameter>(self)) {
        Term *tvalue = dyn_tree_cast<Term>(other);
        if (!tvalue)
          return false;
        
        if (parameter->depth == depth) {
          // Check type also matches
          if (!parameter->type->match(tvalue->type, wildcards, depth))
            return false;

          TreePtr<Term>& wildcard = wildcards[parameter->index];
          if (wildcard) {
            return wildcard->match(TreePtr<Term>(tvalue), wildcards, depth);
          } else {
            wildcards[parameter->index].reset(tvalue);
            return true;
          }
        }
      }

      if (self->m_vptr == other->m_vptr) {
        // Trees are required to have the same static type to work with pattern matching.
        return derived_vptr(this)->match(this, other, wildcards.vptr(), wildcards.object(), depth);
      } else {
        return false;
      }
    }
    
    bool Tree::match(const TreePtr<Tree>& value) {
      PSI_STD::vector<TreePtr<Term> > wildcards;
      return match(value, list_from_stl(wildcards));
    }

    const SIVtable Tree::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.Tree", &TreeBase::vtable);

    TreeCallback::TreeCallback(const TreeCallbackVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
    : TreeBase(PSI_COMPILER_VPTR_UP(TreeBase, vptr), compile_context, location), m_state(state_ready) {
    }

    const SIVtable TreeCallback::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.TreeCallback", &TreeBase::vtable);

    Term::Term(const TermVtable *vptr, const TreePtr<Term>& type_, const SourceLocation& location)
    : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), type_.compile_context(), location),
    type(type_) {
    }

    Term::Term(const TermVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
    : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), compile_context, location) {
    }

    TreePtr<> Term::interface_search_impl(const Term& self,
                                          const TreePtr<Interface>& interface,
                                          const List<TreePtr<Term> >& parameters) {
      if (self.type)
        return self.type->interface_search(interface, parameters);
      else
        return TreePtr<>();
    }

    const SIVtable Term::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Term", Tree);

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

    Global::Global(const VtableType *vptr, CompileContext& compile_context, const SourceLocation& location)
    : Term(vptr, compile_context, location) {
    }

    Global::Global(const VtableType *vptr, const TreePtr<Term>& type, const SourceLocation& location)
    : Term(vptr, type, location) {
    }
    
    template<typename V>
    void Global::visit(V& v) {
      visit_base<Term>(v);
    }

    const SIVtable Global::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Global", Term);

    ExternalGlobal::ExternalGlobal(CompileContext& compile_context, const SourceLocation& location)
    : Global(&vtable, compile_context, location) {
    }

    ExternalGlobal::ExternalGlobal(const TreePtr<Term>& type, const String& symbol, const SourceLocation& location)
    : Global(&vtable, type, location),
    m_symbol(symbol) {
    }

    template<typename Visitor>
    void ExternalGlobal::visit(Visitor& v) {
      visit_base<Global>(v);
      v("symbol", &ExternalGlobal::m_symbol);
    }

    const TermVtable ExternalGlobal::vtable = PSI_COMPILER_TERM(ExternalGlobal, "psi.compiler.ExternalGlobal", Global);

    FunctionType::FunctionType(CompileContext& compile_context, const SourceLocation& location)
    : Type(&vtable, compile_context, location) {
    }

    /**
     * \todo Generate function type lazily.
     */
    FunctionType::FunctionType(const TreePtr<Term>& result_type_, const PSI_STD::vector<TreePtr<Anonymous> >& arguments, const SourceLocation& location)
    : Type(&vtable, result_type_.compile_context(), location) {
      /*
       * I haven't written a method to slice a list yet and since I want an error should the arguments forward
       * reference, I copy the argument list so I can have a "short" version.
       */
      PSI_STD::vector<TreePtr<Anonymous> > arguments_copy;
      arguments_copy.reserve(arguments.size());
      argument_types.reserve(arguments.size());
      for (PSI_STD::vector<TreePtr<Anonymous> >::const_iterator ii = arguments.begin(), ie = arguments.end(); ii != ie; ++ii) {
        argument_types.push_back((*ii)->type->parameterize(location, list_from_stl(arguments_copy)));
        arguments_copy.push_back(*ii);
      }

      result_type = result_type_->parameterize(location, list_from_stl(arguments_copy));
    }
    
    template<typename Visitor>
    void FunctionType::visit(Visitor& v) {
      visit_base<Type>(v);
      v("result_type", &FunctionType::result_type)
      ("argument_types", &FunctionType::argument_types);
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

    TreePtr<Term> FunctionType::argument_type_after(const SourceLocation& location, const List<TreePtr<Term> >& previous) const {
      if (previous.size() >= argument_types.size())
        compile_context().error_throw(location, "Too many arguments passed to function");

      TreePtr<Term> type = argument_types[previous.size()]->type->specialize(location, previous);
      if (!type->is_type())
        compile_context().error_throw(location, "Rewritten function argument type is not a type");

      return type;
    }
    
    TreePtr<Term> FunctionType::result_type_after(const SourceLocation& location, const List<TreePtr<Term> >& previous) const {
      if (previous.size() != argument_types.size())
        compile_context().error_throw(location, "Incorrect number of arguments passed to function");

      TreePtr<Term> type = result_type->specialize(location, previous);
      if (!type->is_type())
        compile_context().error_throw(location, "Rewritten function result type is not a type");

      return type;
    }

    const TermVtable FunctionType::vtable = PSI_COMPILER_TERM(FunctionType, "psi.compiler.FunctionType", Type);

    Function::Function(CompileContext& compile_context, const SourceLocation& location)
    : Global(&vtable, compile_context, location) {
    }
    
    Function::Function(const TreePtr<Term>& result_type_,
                       const PSI_STD::vector<TreePtr<Anonymous> >& arguments_,
                       const TreePtr<Term>& body_,
                       const SourceLocation& location)
    : Global(&vtable, TreePtr<Term>(new FunctionType(result_type_, arguments_, location)), location),
    arguments(arguments_),
    result_type(result_type_),
    body(body_) {
    }

    template<typename Visitor> void Function::visit(Visitor& v) {
      visit_base<Term>(v);
      v("arguments", &Function::arguments)
      ("result_type", &Function::result_type)
      ("body", &Function::body);
    }

    const TermVtable Function::vtable = PSI_COMPILER_TERM(Function, "psi.compiler.Function", Term);

    TryFinally::TryFinally(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }

    TryFinally::TryFinally(const TreePtr<Term>& try_expr_, const TreePtr<Term>& finally_expr_, const SourceLocation& location)
    : Term(&vtable, tree_attribute(try_expr_, &Term::type), location),
    try_expr(try_expr_),
    finally_expr(finally_expr_) {
    }

    template<typename Visitor> void TryFinally::visit(Visitor& v) {
      visit_base<Term>(v);
      v("try_expr", &TryFinally::try_expr)
      ("finally_expr", &TryFinally::finally_expr);
    }

    const TermVtable TryFinally::vtable = PSI_COMPILER_TERM(TryFinally, "psi.compiler.TryFinally", Term);

    Statement::Statement(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }

    Statement::Statement(const TreePtr<Term>& value_, const SourceLocation& location)
    : Term(&vtable, tree_attribute(value_, &Term::type), location),
    value(value_) {
    }

    TreePtr<> Statement::interface_search_impl(const Statement& self, const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters) {
      return self.value->interface_search(interface, parameters);
    }

    template<typename Visitor>
    void Statement::visit(Visitor& v) {
      visit_base<Term>(v);
      v("value", &Statement::value);
    }

    const TermVtable Statement::vtable = PSI_COMPILER_TERM(Statement, "psi.compiler.Statement", Term);
    
    StatementRef::StatementRef(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }

    StatementRef::StatementRef(const TreePtr<Statement>& value_, const SourceLocation& location)
    : Term(&vtable, tree_attribute(TreePtr<Term>(value_), &Statement::type), location),
    value(value_) {
    }

    TreePtr<> StatementRef::interface_search_impl(const StatementRef& self, const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters) {
      return self.value->interface_search(interface, parameters);
    }

    template<typename Visitor>
    void StatementRef::visit(Visitor& v) {
      visit_base<Term>(v);
      v("value", &StatementRef::value);
    }

    const TermVtable StatementRef::vtable = PSI_COMPILER_TERM(StatementRef, "psi.compiler.StatementRef", Term);

    StatementList::StatementList(const TermVtable* vptr, CompileContext& compile_context, const SourceLocation& location)
    : Term(vptr, compile_context, location) {

    }

    StatementList::StatementList(const TermVtable* vptr, const TreePtr<Term>& type, const PSI_STD::vector<TreePtr<Statement> >& statements_, const SourceLocation& location)
    : Term(vptr, type, location),
    statements(statements_) {
    }

    template<typename Visitor>
    void StatementList::visit(Visitor& v) {
      visit_base<Term>(v);
      v("statements", &StatementList::statements);
    }

    const SIVtable StatementList::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.StatementList", Term);

    Block::Block(CompileContext& compile_context, const SourceLocation& location)
    : StatementList(&vtable, compile_context, location) {
    }

    Block::Block(const PSI_STD::vector<TreePtr<Statement> >& statements_, const TreePtr<Term>& value_, const SourceLocation& location)
    : StatementList(&vtable, tree_attribute(value_, &Term::type), statements_, location),
    value(value_) {
    }

    const TermVtable Block::vtable = PSI_COMPILER_TERM(Block, "psi.compiler.Block", StatementList);

    template<typename Visitor>
    void Block::visit(Visitor& v) {
      visit_base<StatementList>(v);
      v("value", &Block::value);
    }
    
    Namespace::Namespace(CompileContext& compile_context, const SourceLocation& location)
    : StatementList(&vtable, compile_context, location) {
    }

    Namespace::Namespace(const PSI_STD::vector<TreePtr<Statement> >& statements, CompileContext& compile_context, const SourceLocation& location)
    : StatementList(&vtable, compile_context.builtins().empty_type, statements, location) {
    }
    
    template<typename V>
    void Namespace::visit(V& v) {
      visit_base<StatementList>(v);
    }

    const TermVtable Namespace::vtable = PSI_COMPILER_TERM(Namespace, "psi.compiler.Namespace", StatementList);

    Interface::Interface(CompileContext& compile_context, const SourceLocation& location)
    : Tree(&vtable, compile_context, location) {
    }
    
    Interface::Interface(CompileContext& compile_context, unsigned n_parameters_, const SIVtable *compile_time_type_, const TreePtr<Term>& run_time_type_, const SourceLocation& location)
    : Tree(&vtable, compile_context, location),
    n_parameters(n_parameters_),
    compile_time_type(compile_time_type_),
    run_time_type(run_time_type_) {
    }
    
    template<typename V>
    void Interface::visit(V& v) {
      visit_base<Tree>(v);
      v("n_parameters", &Interface::n_parameters)
      ("compile_time_type", &Interface::compile_time_type)
      ("run_time_type", &Interface::run_time_type);
    }

    const TreeVtable Interface::vtable = PSI_COMPILER_TREE(Interface, "psi.compiler.Interface", Tree);

    Implementation::Implementation(CompileContext& compile_context, const SourceLocation& location)
    : Tree(&vtable, compile_context, location) {
    }

    Implementation::Implementation(CompileContext& compile_context,
                                   const TreePtr<>& value_,
                                   const TreePtr<Interface>& interface_,
                                   const PSI_STD::vector<TreePtr<Term> >& wildcard_types_,
                                   const PSI_STD::vector<TreePtr<Term> >& interface_parameters_,
                                   const SourceLocation& location)
    : Tree(&vtable, compile_context, location),
    value(value_),
    interface(interface_),
    wildcard_types(wildcard_types_),
    interface_parameters(interface_parameters_) {
    }

    template<typename Visitor>
    void Implementation::visit(Visitor& v) {
      visit_base<Tree>(v);
      v("value", &Implementation::value)
      ("interface", &Implementation::interface)
      ("wildcard_types", &Implementation::wildcard_types)
      ("interface_parameters", &Implementation::interface_parameters);
    }

    bool Implementation::matches(const TreePtr<Interface>& interface_, const List<TreePtr<Term> >& parameters) const {
      if (interface != interface_)
        return false;
      
      PSI_ASSERT(interface_parameters.size() == parameters.size());
      PSI_STD::vector<TreePtr<Term> > wildcards(wildcard_types.size());
      for (std::size_t ji = 0, je = interface_parameters.size(); ji != je; ++ji) {
        if (!interface_parameters[ji]->match(parameters[ji], list_from_stl(wildcards)))
          return false;
      }

      return true;
    }

    const TreeVtable Implementation::vtable = PSI_COMPILER_TREE(Implementation, "psi.compiler.Implementation", Tree);
    
    Metatype::Metatype(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }

    template<typename V>
    void Metatype::visit(V& v) {
      visit_base<Term>(v);
    }

    const TermVtable Metatype::vtable = PSI_COMPILER_TERM(Metatype, "psi.compiler.Metatype", Term);

    BottomType::BottomType(CompileContext& compile_context, const SourceLocation& location)
    : Type(&vtable, compile_context, location) {
    }
    
    template<typename V>
    void BottomType::visit(V& v) {
      visit_base<Type>(v);
    }
    
    const TermVtable BottomType::vtable = PSI_COMPILER_TERM(BottomType, "psi.compiler.BottomType", Type);

    EmptyType::EmptyType(CompileContext& compile_context, const SourceLocation& location)
    : Type(&vtable, compile_context, location) {
    }

    TreePtr<Term> EmptyType::value(CompileContext& compile_context, const SourceLocation& location) {
      return TreePtr<Term>(new NullValue(compile_context.builtins().empty_type, location));
    }
    
    template<typename V>
    void EmptyType::visit(V& v) {
      visit_base<Type>(v);
    }

    const TermVtable EmptyType::vtable = PSI_COMPILER_TERM(EmptyType, "psi.compiler.EmptyType", Type);

    NullValue::NullValue(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }

    NullValue::NullValue(const TreePtr<Term>& type, const SourceLocation& location)
    : Term(&vtable, type, location) {
    }
    
    template<typename V>
    void NullValue::visit(V& v) {
      visit_base<Term>(v);
    }

    const TermVtable NullValue::vtable = PSI_COMPILER_TERM(NullValue, "psi.compiler.NullValue", Term);
    
    PointerType::PointerType(CompileContext& compile_context, const SourceLocation& location)
    : Type(&vtable, compile_context, location) {
    }

    PointerType::PointerType(CompileContext& compile_context, const TreePtr<Term>& target_type_, const SourceLocation& location)
    : Type(&vtable, compile_context, location),
    target_type(target_type_) {
    }
    
    template<typename V>
    void PointerType::visit(V& v) {
      visit_base<Type>(v);
      v("target_type", &PointerType::target_type);
    }
    
    const TermVtable PointerType::vtable = PSI_COMPILER_TERM(PointerType, "psi.compiler.PointerType", Term);
    
    StructType::StructType(CompileContext& compile_context, const SourceLocation& location)
    : Type(&vtable, compile_context, location) {
    }

    StructType::StructType(CompileContext& compile_context, const PSI_STD::vector<TreePtr<Term> >& members_, const SourceLocation& location)
    : Type(&vtable, compile_context, location),
    members(members_) {
    }
    
    template<typename V>
    void StructType::visit(V& v) {
      visit_base<Type>(v);
      v("members", &StructType::members);
    }

    const TermVtable StructType::vtable = PSI_COMPILER_TERM(StructType, "psi.compiler.StructType", Type);

    StructValue::StructValue(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }
    
    StructValue::StructValue(const TreePtr<StructType>& type, const PSI_STD::vector<TreePtr<Term> >& members_, const SourceLocation& location)
    : Term(&vtable, type, location),
    members(members_) {
    }
    
    template<typename V>
    void StructValue::visit(V& v) {
      visit_base<Term>(v);
      v("members", &StructValue::members);
    }
    
    const TermVtable StructValue::vtable = PSI_COMPILER_TERM(StructValue, "psi.compiler.StructValue", Term);

    UnionType::UnionType(CompileContext& compile_context, const SourceLocation& location)
    : Type(&vtable, compile_context, location) {
    }

    UnionType::UnionType(CompileContext& compile_context, const SourceLocation& location, const std::vector<TreePtr<Term> >& members_)
    : Type(&vtable, compile_context, location),
    members(members_) {
    }

    template<typename V>
    void UnionType::visit(V& v) {
      visit_base<Term>(v);
      v("members", &UnionType::members);
    }

    const TermVtable UnionType::vtable = PSI_COMPILER_TERM(UnionType, "psi.compiler.UnionType", Type);

    GenericType::GenericType(const TreePtr<Term>& member_,
                             const PSI_STD::vector<TreePtr<Anonymous> >& parameters_,
                             const PSI_STD::vector<TreePtr<Implementation> >& implementations_,
                             const SourceLocation& location)
    : Tree(&vtable, member_.compile_context(), location),
    member(member_),
    implementations(implementations_) {
    }

    template<typename Visitor>
    void GenericType::visit(Visitor& v) {
      visit_base<Tree>(v);
      v("member", &GenericType::member)
      ("implementations", &GenericType::implementations);
    }

    const TreeVtable GenericType::vtable = PSI_COMPILER_TREE(GenericType, "psi.compiler.GenericType", Tree);

    TypeInstance::TypeInstance(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }
    
    TypeInstance::TypeInstance(const TreePtr<GenericType>& generic_type_,
                               const PSI_STD::vector<TreePtr<Term> >& parameter_values_,
                               const SourceLocation& location)
    : Term(&vtable, generic_type_.compile_context().builtins().metatype, location),
    generic_type(generic_type_),
    parameter_values(parameter_values_) {
    }

    template<typename Visitor>
    void TypeInstance::visit(Visitor& v) {
      visit_base<Term>(v);
      v("generic_type", &TypeInstance::generic_type)
      ("parameter_values", &TypeInstance::parameter_values);
    }
    
    TreePtr<> TypeInstance::interface_search_impl(const TypeInstance& self,
                                                  const TreePtr<Interface>& interface,
                                                  const List<TreePtr<Term> >& parameters) {
      const PSI_STD::vector<TreePtr<Implementation> >& implementations = self.generic_type->implementations;
      for (PSI_STD::vector<TreePtr<Implementation> >::const_iterator ii = implementations.begin(), ie = implementations.end(); ii != ie; ++ii) {
        if ((*ii)->matches(interface, parameters))
          return (*ii)->value;
      }

      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = self.parameter_values.begin(), ie = self.parameter_values.end(); ii != ie; ++ii) {
        if (TreePtr<> result = (*ii)->interface_search(interface, parameters))
          return result;        
      }

      return TreePtr<>();
    }

    const TermVtable TypeInstance::vtable = PSI_COMPILER_TERM(TypeInstance, "psi.compiler.TypeInstance", Term);

    TypeInstanceValue::TypeInstanceValue(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }

    TypeInstanceValue::TypeInstanceValue(const TreePtr<TypeInstance>& type, const TreePtr<Term>& member_value, const SourceLocation& location)
    : Term(&vtable, type, location),
    m_member_value(member_value) {
    }

    template<typename Visitor>
    void TypeInstanceValue::visit(Visitor& v) {
      visit_base<Term>(v);
      v("member_value", &TypeInstanceValue::m_member_value);
    }

    const TermVtable TypeInstanceValue::vtable = PSI_COMPILER_TERM(TypeInstanceValue, "psi.compiler.TypeInstanceValue", Term);
    
    IfThenElse::IfThenElse(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }
    
    namespace {
      struct IfThenElseType {
        TreePtr<Term> true_value;
        TreePtr<Term> false_value;
        
        IfThenElseType(const TreePtr<Term>& true_value_, const TreePtr<Term>& false_value_)
        : true_value(true_value_), false_value(false_value_) {
        }
        
        TreePtr<Term> evaluate(const TreePtr<Term>&) {
          return type_combine(true_value->type, false_value->type);
        }
        
        template<typename Visitor>
        static void visit(Visitor& v) {
          v("true_value", &IfThenElseType::true_value)
          ("false_value", &IfThenElseType::false_value);
        }
      };
    }

    IfThenElse::IfThenElse(const TreePtr<Term>& condition_, const TreePtr<Term>& true_value_, const TreePtr<Term>& false_value_, const SourceLocation& location)
    : Term(&vtable, tree_callback<Term>(condition_.compile_context(), location, IfThenElseType(true_value_, false_value_)), location),
    condition(condition_),
    true_value(true_value_),
    false_value(false_value_) {
    }
    
    template<typename V>
    void IfThenElse::visit(V& v) {
      visit_base<Term>(v);
      v("condition", &IfThenElse::condition)
      ("true_value", &IfThenElse::true_value)
      ("false_value", &IfThenElse::false_value);
    }

    const TermVtable IfThenElse::vtable = PSI_COMPILER_TERM(IfThenElse, "psi.compiler.IfThenElse", Term);

    JumpTarget::JumpTarget(CompileContext& compile_context, const SourceLocation& location)
    : Tree(&vtable, compile_context, location) {
    }
    
    JumpTarget::JumpTarget(const TreePtr<Term>& value_, const TreePtr<Anonymous>& argument_, const SourceLocation& location)
    : Tree(&vtable, value.compile_context(), location),
    value(value_),
    argument(argument_) {
    }
    
    template<typename V>
    void JumpTarget::visit(V& v) {
      visit_base<Tree>(v);
      v("value", &JumpTarget::value)
      ("argument", &JumpTarget::argument);
    }
    
    const TreeVtable JumpTarget::vtable = PSI_COMPILER_TREE(JumpTarget, "psi.compiler.JumpTarget", Tree);

    JumpGroup::JumpGroup(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }
    
    namespace {
      struct JumpGroupType {
        TreePtr<Term> initial;
        PSI_STD::vector<TreePtr<JumpTarget> > entries;
        
        JumpGroupType(const TreePtr<Term>& initial_, const PSI_STD::vector<TreePtr<JumpTarget> >& entries_)
        : initial(initial_), entries(entries_) {
        }
        
        TreePtr<Term> evaluate(const TreePtr<Term>&) {
          TreePtr<Term> result = initial->type;
          for (PSI_STD::vector<TreePtr<JumpTarget> >::iterator ii = entries.begin(), ie = entries.end(); ii != ie; ++ii)
            result = type_combine(result, (*ii)->value->type);
          return result;
        }

        template<typename Visitor>
        static void visit(Visitor& v) {
          v("initial", &JumpGroupType::initial)
          ("entries", &JumpGroupType::entries);
        }
      };
    }

    JumpGroup::JumpGroup(const TreePtr<Term>& initial_, const PSI_STD::vector<TreePtr<JumpTarget> >& entries_, const SourceLocation& location)
    : Term(&vtable, tree_callback<Term>(initial_.compile_context(), location, JumpGroupType(initial_, entries_)), location),
    initial(initial_),
    entries(entries_) {
    }
    
    template<typename V>
    void JumpGroup::visit(V& v) {
      visit_base<Term>(v);
      v("initial", &JumpGroup::initial)
      ("entries", &JumpGroup::entries);
    }

    const TermVtable JumpGroup::vtable = PSI_COMPILER_TERM(JumpGroup, "psi.compiler.JumpGroup", Term);
    
    JumpTo::JumpTo(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable , compile_context, location) {
    }
    
    JumpTo::JumpTo(const TreePtr<JumpTarget>& target_, const TreePtr<Term>& argument_, const SourceLocation& location)
    : Term(&vtable, target_.compile_context().builtins().bottom_type, location),
    target(target_),
    argument(argument_) {
    }
    
    template<typename V>
    void JumpTo::visit(V& v) {
      visit_base<Term>(v);
      v("target", &JumpTo::target)
      ("argument", &JumpTo::argument);
    }

    const TermVtable JumpTo::vtable = PSI_COMPILER_TERM(JumpTo, "psi.compiler.JumpTo", Term);

    FunctionCall::FunctionCall(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }

    FunctionCall::FunctionCall(const TreePtr<Term>& target_, const PSI_STD::vector<TreePtr<Term> >& arguments_, const SourceLocation& location)
    : Term(&vtable, get_type(target_, arguments_, location), location),
    target(target_),
    arguments(arguments_) {
    }

    TreePtr<Term> FunctionCall::get_type(const TreePtr<Term>& target, const PSI_STD::vector<TreePtr<Term> >& arguments, const SourceLocation& location) {
      TreePtr<FunctionType> ft = dyn_treeptr_cast<FunctionType>(target->type);
      if (!ft)
        target.compile_context().error_throw(location, "Target of function call does not have function type");

      PSI_STD::vector<TreePtr<Term> >& nc_arguments = const_cast<PSI_STD::vector<TreePtr<Term> >&>(arguments);
      return ft->result_type_after(location, list_from_stl(nc_arguments));
    }
    
    TreePtr<FunctionType> FunctionCall::target_type() {
      return treeptr_cast<FunctionType>(treeptr_cast<PointerType>(target->type)->target_type);
    }
    
    template<typename Visitor>
    void FunctionCall::visit(Visitor& v) {
      visit_base<Term>(v);
      v("target", &FunctionCall::target);
    }

    const TermVtable FunctionCall::vtable = PSI_COMPILER_TERM(FunctionCall, "psi.compiler.FunctionCall", Term);

    BuiltinType::BuiltinType(CompileContext& compile_context, const SourceLocation& location)
    : Type(&vtable, compile_context, location) {
    }
    
    BuiltinType::BuiltinType(CompileContext& compile_context, const String& name_, const SourceLocation& location)
    : Type(&vtable, compile_context, location),
    name(name_) {
    }
    
    template<typename Visitor> void BuiltinType::visit(Visitor& v) {
      visit_base<Type>(v);
      v("name", &BuiltinType::name);
    }

    const TermVtable BuiltinType::vtable = PSI_COMPILER_TERM(BuiltinType, "psi.compiler.BuiltinType", Term);
    
    class BuiltinTypeClassMember : public ClassMemberInfoCallback {
    public:
      static const ClassMemberInfoCallbackVtable vtable;
      
      TreePtr<Term> type;
      
      BuiltinTypeClassMember(const TreePtr<Term>& type_)
      : ClassMemberInfoCallback(&vtable, type_.compile_context(), type_.location()),
      type(type_) {
      }
      
      static ClassMemberInfo class_member_info_impl(const BuiltinTypeClassMember& self) {
        ClassMemberInfo cmi;
        cmi.member_type = self.type;
        return cmi;
      }
      
      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<ClassMemberInfoCallback>(v);
        v("type", &BuiltinTypeClassMember::type);
      }
    };
    
    const ClassMemberInfoCallbackVtable BuiltinTypeClassMember::vtable =
    PSI_COMPILER_CLASS_MEMBER_INFO_CALLBACK(BuiltinTypeClassMember, "psi.compiler.BuiltinTypeClassMember", ClassMemberInfoCallback);

    TreePtr<> BuiltinType::interface_search_impl(BuiltinType& self, const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters) {
      if (interface == self.compile_context().builtins().class_member_info_interface) {
        PSI_ASSERT(parameters.size() == 1);
        if (self.match(parameters[0]))
          return TreePtr<>(new BuiltinTypeClassMember(TreePtr<Term>(&self)));
      }
      
      return TreePtr<>();
    }

    BuiltinValue::BuiltinValue(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }
    
    BuiltinValue::BuiltinValue(const String& constructor_, const String& data_, const TreePtr<Term>& type, const SourceLocation& location)
    : Term(&vtable, type, location),
    constructor(constructor_),
    data(data_) {
    }
    
    template<typename Visitor>
    void BuiltinValue::visit(Visitor& v) {
      visit_base<Term>(v);
      v("constructor", &BuiltinValue::constructor)
      ("data", &BuiltinValue::data);
    }

    const TermVtable BuiltinValue::vtable = PSI_COMPILER_TERM(BuiltinValue, "psi.compiler.BuiltinValue", Term);

    ExternalFunction::ExternalFunction(const TermVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
    : Term(vptr, compile_context, location) {
    }
    
    ExternalFunction::ExternalFunction(const TermVtable *vptr, const TreePtr<Term>& result_type, const PSI_STD::vector<TreePtr<Term> >& argument_types, const SourceLocation& location)
    : Term(vptr, get_type(result_type, argument_types, location), location) {
    }
    
    TreePtr<Term> ExternalFunction::get_type(const TreePtr<Term>& result_type, const PSI_STD::vector<TreePtr<Term> >& argument_types, const SourceLocation& location) {
      PSI_STD::vector<TreePtr<Anonymous> > arguments;
      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = argument_types.begin(), ie = argument_types.end(); ii != ie; ++ii)
        arguments.push_back(TreePtr<Anonymous>(new Anonymous(*ii, location)));
      return TreePtr<Term>(new FunctionType(result_type, arguments, location));
    }
    
    class ExternalFunctionInvokeMacro : public Macro {
    public:
      static const MacroVtable vtable;
      
      TreePtr<Term> function;
      
      ExternalFunctionInvokeMacro(const TreePtr<Term>& function_)
      : Macro(&vtable, function_.compile_context(), function_.location()),
      function(function_) {
      }

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<Macro>(v);
        v("function", &ExternalFunctionInvokeMacro::function);
      }

      static TreePtr<Term> evaluate_impl(const ExternalFunctionInvokeMacro& self,
                                         const TreePtr<Term>&,
                                         const List<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        if (parameters.size() != 1)
          self.compile_context().error_throw(location, "Wrong number of parameters to builtin function invocation macro (expected 1)");
        
        SharedPtr<Parser::TokenExpression> arguments;
        if (!(arguments = expression_as_token_type(parameters[0], Parser::TokenExpression::bracket)))
          self.compile_context().error_throw(location, "Parameter to external function invocation macro is not a (...)");
        
        PSI_STD::vector<TreePtr<Term> > argument_values;
        PSI_STD::vector<SharedPtr<Parser::Expression> > argument_expressions = Parser::parse_positional_list(arguments->text);
        for (PSI_STD::vector<SharedPtr<Parser::Expression> >::iterator ii = argument_expressions.begin(), ie = argument_expressions.end(); ii != ie; ++ii)
          argument_values.push_back(compile_expression(*ii, evaluate_context, location.logical));

        return TreePtr<Term>(new FunctionCall(self.function, argument_values, location));
      }

      static TreePtr<Term> dot_impl(const ExternalFunctionInvokeMacro& self,
                                    const TreePtr<Term>&,
                                    const SharedPtr<Parser::Expression>&,
                                    const TreePtr<EvaluateContext>&,
                                    const SourceLocation& location) {
        self.compile_context().error_throw(location, "External functions do not support the dot operator");
      }
    };

    const MacroVtable ExternalFunctionInvokeMacro::vtable = PSI_COMPILER_MACRO(ExternalFunctionInvokeMacro, "psi.compiler.ExternalFunctionInvokeMacro", Macro);

    TreePtr<> ExternalFunction::interface_search_impl(ExternalFunction& self, const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters) {
      if (interface == self.compile_context().builtins().macro_interface) {
        PSI_ASSERT(parameters.size() == 1);
        if (self.match(parameters[0]))
          return TreePtr<>(new ExternalFunctionInvokeMacro(TreePtr<Term>(&self)));
      }
      
      return TreePtr<>();
    }
    
    template<typename V>
    void ExternalFunction::visit(V& v) {
      visit_base<Term>(v);
    }
    
    const SIVtable ExternalFunction::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.ExternalFunction", Term);
    
    BuiltinFunction::BuiltinFunction(CompileContext& compile_context, const SourceLocation& location)
    : ExternalFunction(&vtable, compile_context, location) {
    }
    
    BuiltinFunction::BuiltinFunction(const String& name_, const TreePtr<Term>& result_type, const PSI_STD::vector<TreePtr<Term> >& argument_types, const SourceLocation& location)
    : ExternalFunction(&vtable, result_type, argument_types, location),
    name(name_) {
    }

    template<typename Visitor>
    void BuiltinFunction::visit(Visitor& v) {
      visit_base<ExternalFunction>(v);
      v("name", &BuiltinFunction::name);
    }

    const TermVtable BuiltinFunction::vtable = PSI_COMPILER_TERM(BuiltinFunction, "psi.compiler.BuiltinFunction", ExternalFunction);

    CFunction::CFunction(CompileContext& compile_context, const SourceLocation& location)
    : ExternalFunction(&vtable, compile_context, location) {
    }
    
    CFunction::CFunction(const String& name_, const TreePtr<Term>& result_type, const PSI_STD::vector<TreePtr<Term> >& argument_types, const SourceLocation& location)
    : ExternalFunction(&vtable, result_type, argument_types, location),
    name(name_) {
    }

    template<typename Visitor>
    void CFunction::visit(Visitor& v) {
      visit_base<ExternalFunction>(v);
      v("name", &CFunction::name);
    }

    const TermVtable CFunction::vtable = PSI_COMPILER_TERM(CFunction, "psi.compiler.CFunction", ExternalFunction);

    const SIVtable EvaluateContext::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.EvaluateContext", Tree);
    const SIVtable Macro::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Macro", Tree);
  }
}
