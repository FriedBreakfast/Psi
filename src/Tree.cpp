#include "Tree.hpp"
#include "Parser.hpp"

#include <boost/checked_delete.hpp>
#include <boost/bind.hpp>

namespace Psi {
  namespace Compiler {
    Constructor::Constructor(const TermVtable* vtable, CompileContext& context, const SourceLocation& location)
    : Term(vtable, context, location) {
    }

    Constructor::Constructor(const TermVtable* vtable, const TreePtr<Term>& type, const SourceLocation& location)
    : Term(vtable, type, location) {
    }

    const SIVtable Constructor::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Constructor", Term);

    Global::Global(const VtableType *vptr, CompileContext& compile_context, const SourceLocation& location)
    : Term(vptr, compile_context, location) {
    }

    Global::Global(const VtableType *vptr, const TreePtr<Module>& module_, const TreePtr<Term>& type, const SourceLocation& location)
    : Term(vptr, type, location),
    module(module_) {
    }
    
    template<typename V>
    void Global::visit(V& v) {
      visit_base<Term>(v);
      v("module", &Global::module);
    }

    bool Global::match_impl(const Global& lhs, const Global& rhs, PSI_STD::vector<TreePtr<Term> >&, unsigned) {
      return &lhs == &rhs;
    }

    const SIVtable Global::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Global", Term);
    
    GlobalVariable::GlobalVariable(CompileContext& context, const SourceLocation& location): Global(&vtable, context, location) {
    }

    GlobalVariable::GlobalVariable(const TreePtr<Module>& module, const TreePtr<Term>& value_, const SourceLocation& location)
    : Global(&vtable, module, tree_attribute(value_, &Term::type), location),
    value(value_) {
    }
    
    template<typename V>
    void GlobalVariable::visit(V& v) {
      visit_base<Global>(v);
      v("value", &GlobalVariable::value);
    }

    FunctionType::FunctionType(CompileContext& compile_context, const SourceLocation& location)
    : Type(&vtable, compile_context, location) {
    }

    FunctionType::FunctionType(ResultMode result_mode_, const TreePtr<Term>& result_type_, const PSI_STD::vector<FunctionParameterType>& parameter_types_,
                               const PSI_STD::vector<TreePtr<InterfaceValue> >& interfaces_, const SourceLocation& location)
    : Type(&vtable, result_type_.compile_context(), location),
    result_mode(result_mode_),
    result_type(result_type_),
    parameter_types(parameter_types_),
    interfaces(interfaces_) {
    }
    
    template<typename Visitor>
    void FunctionType::visit(Visitor& v) {
      visit_base<Type>(v);
      v("result_type", &FunctionType::result_type)
      ("parameter_types", &FunctionType::parameter_types);
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

    TreePtr<Term> FunctionType::argument_type_after(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& previous) const {
      if (previous.size() >= parameter_types.size())
        compile_context().error_throw(location, "Too many arguments passed to function");

      TreePtr<Term> type = parameter_types[previous.size()].type->specialize(location, previous);
      if (!type->is_type())
        compile_context().error_throw(location, "Rewritten function argument type is not a type");

      return type;
    }
    
    TreePtr<Term> FunctionType::result_type_after(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& previous) const {
      if (previous.size() != parameter_types.size())
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
    
    TreePtr<FunctionType> Function::make_type() {
    }
    
    Function::Function(const TreePtr<Module>& module,
                       ResultMode result_mode_,
                       const TreePtr<Term>& result_type_,
                       const std::vector<std::pair<ParameterMode, TreePtr<Anonymous> > >& arguments_,
                       const TreePtr<Term>& body_,
                       const SourceLocation& location)
    : Global(&vtable, module, TreePtr<Term>(new FunctionType(result_mode_, result_type_, arguments_, location)), location),
    result_type(result_type_),
    body(body_) {
      for (std::vector<std::pair<ParameterMode, TreePtr<Anonymous> > >::const_iterator ii = arguments_.begin(), ie = arguments_.end(); ii != ie; ++ii)
        arguments.push_back(ii->second);
    }

    template<typename Visitor> void Function::visit(Visitor& v) {
      visit_base<Global>(v);
      v("arguments", &Function::arguments)
      ("result_type", &Function::result_type)
      ("body", &Function::body)
      ("return_target", &Function::return_target);
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
    : Tree(&vtable, compile_context, location) {
    }

    Statement::Statement(const TreePtr<Term>& value_, const SourceLocation& location)
    : Tree(&vtable, value_.compile_context(), location),
    value(value_) {
    }

    template<typename Visitor>
    void Statement::visit(Visitor& v) {
      visit_base<Tree>(v);
      v("value", &Statement::value);
    }

    const TreeVtable Statement::vtable = PSI_COMPILER_TREE(Statement, "psi.compiler.Statement", Tree);
    
    StatementRef::StatementRef(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }

    StatementRef::StatementRef(const TreePtr<Statement>& value_, const SourceLocation& location)
    : Term(&vtable, tree_attribute(tree_attribute(value, &Statement::value), &Term::type), location),
    value(value_) {
    }

    template<typename Visitor>
    void StatementRef::visit(Visitor& v) {
      visit_base<Term>(v);
      v("value", &StatementRef::value);
    }

    bool StatementRef::match_impl(const StatementRef& lhs, const StatementRef& rhs, PSI_STD::vector<TreePtr<Term> >&, unsigned) {
      return lhs.value == rhs.value;
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
      return TreePtr<Term>(new DefaultValue(compile_context.builtins().empty_type, location));
    }
    
    template<typename V>
    void EmptyType::visit(V& v) {
      visit_base<Type>(v);
    }

    const TermVtable EmptyType::vtable = PSI_COMPILER_TERM(EmptyType, "psi.compiler.EmptyType", Type);

    DefaultValue::DefaultValue(CompileContext& compile_context, const SourceLocation& location)
    : Constructor(&vtable, compile_context, location) {
    }

    DefaultValue::DefaultValue(const TreePtr<Term>& type, const SourceLocation& location)
    : Constructor(&vtable, type, location) {
    }
    
    template<typename V>
    void DefaultValue::visit(V& v) {
      visit_base<Constructor>(v);
    }

    const TermVtable DefaultValue::vtable = PSI_COMPILER_TERM(DefaultValue, "psi.compiler.DefaultValue", Constructor);
    
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
    : Constructor(&vtable, compile_context, location) {
    }
    
    StructValue::StructValue(const TreePtr<StructType>& type, const PSI_STD::vector<TreePtr<Term> >& members_, const SourceLocation& location)
    : Constructor(&vtable, type, location),
    members(members_) {
    }
    
    template<typename V>
    void StructValue::visit(V& v) {
      visit_base<Constructor>(v);
      v("members", &StructValue::members);
    }
    
    const TermVtable StructValue::vtable = PSI_COMPILER_TERM(StructValue, "psi.compiler.StructValue", Constructor);

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

    GenericType::GenericType(const PSI_STD::vector<TreePtr<Term> >& pattern_,
                             const TreePtr<Term>& member_type_,
                             const PSI_STD::vector<TreePtr<OverloadValue> >& overloads_,
                             const SourceLocation& location)
    : Tree(&vtable, member_type_.compile_context(), location),
    pattern(pattern_),
    member_type(member_type_),
    overloads(overloads_) {
    }

    template<typename Visitor>
    void GenericType::visit(Visitor& v) {
      visit_base<Tree>(v);
      v("pattern", &GenericType::pattern)
      ("member_type", &GenericType::member_type)
      ("overloads", &GenericType::overloads);
    }

    const TreeVtable GenericType::vtable = PSI_COMPILER_TREE(GenericType, "psi.compiler.GenericType", Tree);

    TypeInstance::TypeInstance(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }
    
    TypeInstance::TypeInstance(const TreePtr<GenericType>& generic_,
                               const PSI_STD::vector<TreePtr<Term> >& parameters_,
                               const SourceLocation& location)
    : Term(&vtable, generic_.compile_context().builtins().metatype, location),
    generic(generic_),
    parameters(parameters_) {
    }

    template<typename Visitor>
    void TypeInstance::visit(Visitor& v) {
      visit_base<Term>(v);
      v("generic", &TypeInstance::generic)
      ("parameters", &TypeInstance::parameters);
    }

    bool TypeInstance::match_impl(const TypeInstance& lhs, const TypeInstance& rhs, PSI_STD::vector<TreePtr<Term> >& wildcards, unsigned depth) {
      if (lhs.generic != rhs.generic)
        return false;
      
      PSI_ASSERT(lhs.parameters.size() == rhs.parameters.size());
      
      for (unsigned ii = 0, ie = lhs.parameters.size(); ii != ie; ++ii) {
        if (!lhs.parameters[ii]->match(rhs.parameters[ii], wildcards, depth))
          return false;
      }
      
      return true;
    }

    const TermVtable TypeInstance::vtable = PSI_COMPILER_TERM(TypeInstance, "psi.compiler.TypeInstance", Term);

    TypeInstanceValue::TypeInstanceValue(CompileContext& compile_context, const SourceLocation& location)
    : Constructor(&vtable, compile_context, location) {
    }

    TypeInstanceValue::TypeInstanceValue(const TreePtr<TypeInstance>& type, const TreePtr<Term>& member_value_, const SourceLocation& location)
    : Constructor(&vtable, type, location),
    member_value(member_value_) {
    }

    template<typename Visitor>
    void TypeInstanceValue::visit(Visitor& v) {
      visit_base<Constructor>(v);
      v("member_value", &TypeInstanceValue::member_value);
    }

    const TermVtable TypeInstanceValue::vtable = PSI_COMPILER_TERM(TypeInstanceValue, "psi.compiler.TypeInstanceValue", Constructor);
    
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
#if 0
          return type_combine(true_value->type, false_value->type);
#else
          PSI_NOT_IMPLEMENTED();
#endif
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
    
    JumpTarget::JumpTarget(const TreePtr<Term>& value_, ResultMode argument_mode_, const TreePtr<Anonymous>& argument_, const SourceLocation& location)
    : Tree(&vtable, value.compile_context(), location),
    value(value_),
    argument_mode(argument_mode_),
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
#if 0
          TreePtr<Term> result = initial->type;
          for (PSI_STD::vector<TreePtr<JumpTarget> >::iterator ii = entries.begin(), ie = entries.end(); ii != ie; ++ii)
            result = type_combine(result, (*ii)->value->type);
          return result;
#else
          PSI_NOT_IMPLEMENTED();
#endif
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
      return ft->result_type_after(location, nc_arguments);
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

    PrimitiveType::PrimitiveType(CompileContext& compile_context, const SourceLocation& location)
    : Type(&vtable, compile_context, location) {
    }
    
    PrimitiveType::PrimitiveType(CompileContext& compile_context, const String& name_, const SourceLocation& location)
    : Type(&vtable, compile_context, location),
    name(name_) {
    }
    
    template<typename Visitor> void PrimitiveType::visit(Visitor& v) {
      visit_base<Type>(v);
      v("name", &PrimitiveType::name);
    }

    const TermVtable PrimitiveType::vtable = PSI_COMPILER_TERM(PrimitiveType, "psi.compiler.BuiltinType", Term);
    
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
    
    BuiltinFunction::BuiltinFunction(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }
    
    BuiltinFunction::BuiltinFunction(const String& name_, bool pure_, const TreePtr<FunctionType>& type, const SourceLocation& location)
    : Term(&vtable, type, location),
    name(name_),
    pure(pure_) {
    }

    template<typename Visitor>
    void BuiltinFunction::visit(Visitor& v) {
      visit_base<Term>(v);
      v("name", &BuiltinFunction::name)
      ("pure", &BuiltinFunction::pure);
    }

    const TermVtable BuiltinFunction::vtable = PSI_COMPILER_TERM(BuiltinFunction, "psi.compiler.BuiltinFunction", Term);

    Module::Module(CompileContext& compile_context, const SourceLocation& location)
    : Tree(&vtable, compile_context, location) {
    }
    
    Module::Module(CompileContext& compile_context, const String& name_, const SourceLocation& location)
    : Tree(&vtable, compile_context, location),
    name(name_) {
    }
    
    template<typename V>
    void Module::visit(V& v) {
      visit_base<Tree>(v);
    }
    
    const TreeVtable Module::vtable = PSI_COMPILER_TREE(Module, "psi.compiler.Module", Tree);
    
    Library::Library(CompileContext& compile_context, const SourceLocation& location)
    : Tree(&vtable, compile_context, location) {
    }
    
    Library::Library(const TreePtr<TargetCallback>& callback_, const SourceLocation& location)
    : Tree(&vtable, callback_.compile_context(), location),
    callback(callback_) {
    }
    
    template<typename V>
    void Library::visit(V& v) {
      visit_base<Tree>(v);
      v("callback", &Library::callback);
    }
    
    const TreeVtable Library::vtable = PSI_COMPILER_TREE(Library, "psi.compiler.Library", Tree);
    
    LibrarySymbol::LibrarySymbol(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }
    
    LibrarySymbol::LibrarySymbol(const TreePtr<Library>& library_, const TreePtr<TargetCallback>& callback_, const SourceLocation& location)
    : Term(&vtable, library_.compile_context(), location),
    library(library_),
    callback(callback_) {
    }
    
    template<typename V>
    void LibrarySymbol::visit(V& v) {
      visit_base<Term>(v);
      v("library", &LibrarySymbol::library)
      ("callback", &LibrarySymbol::callback);
    }

    const TermVtable LibrarySymbol::vtable = PSI_COMPILER_TERM(LibrarySymbol, "psi.compiler.LibrarySymbol", Term);
    
    TargetCallback::TargetCallback(const TargetCallbackVtable *vtable, CompileContext& compile_context, const SourceLocation& location)
    : Tree(PSI_COMPILER_VPTR_UP(Tree, vtable), compile_context, location) {
    }
    
    const SIVtable TargetCallback::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.TargetCallback", Tree);
    const SIVtable EvaluateContext::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.EvaluateContext", Tree);
  }
}
