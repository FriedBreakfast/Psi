#include "Tree.hpp"
#include "Parser.hpp"

namespace Psi {
  namespace Compiler {
    const SIVtable EvaluateContext::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.EvaluateContext", Tree);

    Functional::Functional(const VtableType *vptr, CompileContext& compile_context, const SourceLocation& location)
    : Term(vptr, compile_context, location) {
    }
    
    Functional::Functional(const VtableType *vptr, const TreePtr<Term>& type, const SourceLocation& location)
    : Term(vptr, type, location) {
    }
    
    template<typename V>
    void Functional::visit(V& v) {
      visit_base<Term>(v);
    }
    
    const SIVtable Functional::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Functional", Term);

    Constructor::Constructor(const TermVtable* vtable, CompileContext& context, const SourceLocation& location)
    : Functional(vtable, context, location) {
    }

    Constructor::Constructor(const TermVtable* vtable, const TreePtr<Term>& type, const SourceLocation& location)
    : Functional(vtable, type, location) {
    }
    
    const SIVtable Constructor::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Constructor", Functional);

    Constant::Constant(const VtableType *vptr, CompileContext& compile_context, const SourceLocation& location)
    : Constructor(vptr, compile_context, location) {
    }
    
    Constant::Constant(const VtableType *vptr, const TreePtr<Term>& type, const SourceLocation& location)
    : Constructor(vptr, type, location) {
    }
    
    const SIVtable Constant::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Constant", Constructor);

    GlobalDefine::GlobalDefine(CompileContext& compile_context, const SourceLocation& location)
    : Functional(&vtable, compile_context, location) {
    }
    
    GlobalDefine::GlobalDefine(const TreePtr<Term>& value_, bool functional_, const SourceLocation& location)
    : Functional(&vtable, value_->type, location),
    value(value_),
    functional(functional_) {
    }
    
    template<typename V>
    void GlobalDefine::visit(V& v) {
      visit_base<Functional>(v);
      v("value", &GlobalDefine::value)
      ("functional", &GlobalDefine::functional);
    }

    const TermVtable GlobalDefine::vtable = PSI_COMPILER_TERM(GlobalDefine, "psi.compiler.GlobalDefine", Functional);

    Global::Global(const VtableType *vptr, CompileContext& compile_context, const SourceLocation& location)
    : Term(vptr, compile_context, location) {
    }

    Global::Global(const VtableType *vptr, const TreePtr<Term>& type, const SourceLocation& location)
    : Term(vptr, type, location) {
    }

    bool Global::match_impl(const Global& lhs, const Global& rhs, PSI_STD::vector<TreePtr<Term> >&, unsigned) {
      return &lhs == &rhs;
    }

    template<typename V>
    void Global::visit(V& v) {
      visit_base<Term>(v);
    }

    const SIVtable Global::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Global", Term);

    ModuleGlobal::ModuleGlobal(const VtableType *vptr, CompileContext& compile_context, const SourceLocation& location)
    : Global(vptr, compile_context, location) {
    }

    ModuleGlobal::ModuleGlobal(const VtableType *vptr, const TreePtr<Module>& module_, PsiBool local_, const TreePtr<Term>& type, const SourceLocation& location)
    : Global(vptr, type, location),
    module(module_),
    local(local_) {
    }
    
    template<typename V>
    void ModuleGlobal::visit(V& v) {
      visit_base<Global>(v);
      v("module", &ModuleGlobal::module)
      ("local", &ModuleGlobal::local);
    }

    void ModuleGlobal::global_dependencies_impl(const ModuleGlobal& self, PSI_STD::set<TreePtr<ModuleGlobal> >& globals) {
      globals.insert(TreePtr<ModuleGlobal>(&self));
    }

    const SIVtable ModuleGlobal::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.ModuleGlobal", Global);

    ExternalGlobal::ExternalGlobal(CompileContext& compile_context, const SourceLocation& location)
    : ModuleGlobal(&vtable, compile_context, location) {
    }
    
    ExternalGlobal::ExternalGlobal(const TreePtr<Module>& module, const TreePtr<Term>& type, const SourceLocation& location)
    : ModuleGlobal(&vtable, module, false, type, location) {
    }
    
    template<typename V>
    void ExternalGlobal::visit(V& v) {
      visit_base<ModuleGlobal>(v);
    }
    
    const TermVtable ExternalGlobal::vtable = PSI_COMPILER_TERM(ExternalGlobal, "psi.compiler.ExternalGlobal", ModuleGlobal);

    GlobalVariable::GlobalVariable(CompileContext& context, const SourceLocation& location)
    : ModuleGlobal(&vtable, context, location) {
    }

    GlobalVariable::GlobalVariable(const TreePtr<Module>& module, PsiBool local, const TreePtr<Term>& value_,
                                   PsiBool constant_, PsiBool merge_, const SourceLocation& location)
    : ModuleGlobal(&vtable, module, local, tree_attribute(value_, &Term::type), location),
    value(value_),
    constant(constant_),
    merge(merge_) {
    }
    
    template<typename V>
    void GlobalVariable::visit(V& v) {
      visit_base<ModuleGlobal>(v);
      v("value", &GlobalVariable::value)
      ("constant", &GlobalVariable::constant)
      ("merge", &GlobalVariable::merge);
    }
    
    const TermVtable GlobalVariable::vtable = PSI_COMPILER_TERM(GlobalVariable, "psi.compiler.GlobalVariable", ModuleGlobal);

    Exists::Exists(CompileContext& compile_context, const SourceLocation& location)
    : Type(&vtable, compile_context, location) {
    }
    
    Exists::Exists(const TreePtr<Term>& result_, const PSI_STD::vector<TreePtr<Term> >& parameter_types_, const SourceLocation& location)
    : Type(&vtable, result_.compile_context(), location),
    result(result_),
    parameter_types(parameter_types_) {
    }
    
    template<typename V>
    void Exists::visit(V& v) {
      visit_base<Type>(v);
      v("result", &Exists::result)
      ("parameter_types", &Exists::parameter_types);
    }

    TreePtr<Term> Exists::parameter_type_after(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& previous) const {
      if (previous.size() >= parameter_types.size())
        compile_context().error_throw(location, "Too many arguments passed to function");

      TreePtr<Term> type = parameter_types[previous.size()]->specialize(location, previous);
      if (!type->is_type())
        compile_context().error_throw(location, "Rewritten function argument type is not a type");

      return type;
    }
    
    TreePtr<Term> Exists::result_after(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& previous) const {
      if (previous.size() != parameter_types.size())
        compile_context().error_throw(location, "Incorrect number of arguments passed to function");

      return result->specialize(location, previous);
    }

    const TermVtable Exists::vtable = PSI_COMPILER_TERM(Exists, "psi.compiler.Exists", Type);

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
      v("result_mode", &FunctionType::result_mode)
      ("result_type", &FunctionType::result_type)
      ("parameter_types", &FunctionType::parameter_types)
      ("interfaces", &FunctionType::interfaces);
    }

    TreePtr<Term> FunctionType::parameter_type_after(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& previous) const {
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
    : ModuleGlobal(&vtable, compile_context, location) {
    }
    
    Function::Function(const TreePtr<Module>& module,
                       bool local,
                       const TreePtr<FunctionType>& type,
                       const PSI_STD::vector<TreePtr<Anonymous> >& arguments_,
                       const TreePtr<Term>& body_,
                       const TreePtr<JumpTarget>& return_target_,
                       const SourceLocation& location)
    : ModuleGlobal(&vtable, module, local, type, location),
    arguments(arguments_),
    body(body_),
    return_target(return_target_) {
    }

    template<typename Visitor> void Function::visit(Visitor& v) {
      visit_base<ModuleGlobal>(v);
      v("arguments", &Function::arguments)
      ("body", &Function::body)
      ("return_target", &Function::return_target);
    }

    void Function::global_dependencies_impl(const Function& self, PSI_STD::set<TreePtr<ModuleGlobal> >& globals) {
      PSI_ASSERT(!self.return_target || !self.return_target->value);
      self.body->global_dependencies(globals);
    }
    
    const TermVtable Function::vtable = PSI_COMPILER_TERM(Function, "psi.compiler.Function", ModuleGlobal);

    TryFinally::TryFinally(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }

    TryFinally::TryFinally(const TreePtr<Term>& try_expr_, const TreePtr<Term>& finally_expr_, bool except_only_, const SourceLocation& location)
    : Term(&vtable, tree_attribute(try_expr_, &Term::type), location),
    try_expr(try_expr_),
    finally_expr(finally_expr_),
    except_only(except_only_) {
    }

    template<typename Visitor> void TryFinally::visit(Visitor& v) {
      visit_base<Term>(v);
      v("try_expr", &TryFinally::try_expr)
      ("finally_expr", &TryFinally::finally_expr)
      ("except_only", &TryFinally::except_only);
    }

    void TryFinally::global_dependencies_impl(const TryFinally& self, PSI_STD::set<TreePtr<ModuleGlobal> >& globals) {
      self.try_expr->global_dependencies(globals);
      self.finally_expr->global_dependencies(globals);
    }

    const TermVtable TryFinally::vtable = PSI_COMPILER_TERM(TryFinally, "psi.compiler.TryFinally", Term);

    Statement::Statement(CompileContext& compile_context, const SourceLocation& location)
    : Tree(&vtable, compile_context, location) {
    }

    Statement::Statement(const TreePtr<Term>& value_, StatementMode mode_, const SourceLocation& location)
    : Tree(&vtable, value_.compile_context(), location),
    value(value_),
    mode(mode_) {
    }

    template<typename Visitor>
    void Statement::visit(Visitor& v) {
      visit_base<Tree>(v);
      v("value", &Statement::value)
      ("mode", &Statement::mode);
    }

    const TreeVtable Statement::vtable = PSI_COMPILER_TREE(Statement, "psi.compiler.Statement", Tree);
    
    StatementRef::StatementRef(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }

    StatementRef::StatementRef(const TreePtr<Statement>& value_, const SourceLocation& location)
    : Term(&vtable, tree_attribute(tree_attribute(value_, &Statement::value), &Term::type), location),
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
    
    TreePtr<Term> StatementRef::anonymize_impl(const StatementRef& self, const SourceLocation& location,
                                               PSI_STD::vector<TreePtr<Term> >& parameter_types, PSI_STD::map<TreePtr<Statement>, unsigned>& parameter_map,
                                               const PSI_STD::vector<TreePtr<Statement> >& statements, unsigned depth) {
      if (std::find(statements.begin(), statements.end(), self.value) != statements.end()) {
        unsigned index;
        std::map<TreePtr<Statement>, unsigned>::iterator ii = parameter_map.find(self.value);
        if (ii == parameter_map.end()) {
          index = ii->second;
        } else {
          // Need to visit the type twice because this visit is at 0 depth since it will go into the parameter list of the final result.
          parameter_types.push_back(self.type->anonymize(location, parameter_types, parameter_map, statements, 0));
          index = parameter_types.size() - 1;
        }
        
        return TreePtr<Term>(new Parameter(self.type->anonymize(location, parameter_types, parameter_map, statements, depth), index, depth, location));
      } else {
        PSI_ASSERT(parameter_map.find(self.value) == parameter_map.end());
        return TreePtr<Term>(&self);
      }
    }

    const TermVtable StatementRef::vtable = PSI_COMPILER_TERM(StatementRef, "psi.compiler.StatementRef", Term);

    Block::Block(CompileContext& compile_context, const SourceLocation& location)
    : Term(&vtable, compile_context, location) {
    }

    Block::Block(const PSI_STD::vector<TreePtr<Statement> >& statements_, const TreePtr<Term>& value_, const SourceLocation& location)
    : Term(&vtable, anonymize_type_delayed(value_, location, statements_), location),
    statements(statements_),
    value(value_) {
    }

    TreePtr<Term> Block::make(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& values, const TreePtr<Term>& result) {
      PSI_STD::vector<TreePtr<Statement> > statements;
      statements.reserve(values.size());
      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = values.begin(), ie = values.end(); ii != ie; ++ii)
        statements.push_back(TreePtr<Statement>(new Statement(*ii, statement_mode_destroy, location)));
      TreePtr<Term> my_result;
      if (result) {
        my_result = result;
      } else {
        PSI_ASSERT(!values.empty());
        my_result = values.front().compile_context().builtins().empty_value;
      }
      return TreePtr<Term>(new Block(statements, my_result, location));
    }

    template<typename Visitor>
    void Block::visit(Visitor& v) {
      visit_base<Term>(v);
      v("statements", &Block::statements)
      ("value", &Block::value);
    }

    void Block::global_dependencies_impl(const Block& self, PSI_STD::set<TreePtr<ModuleGlobal> >& globals) {
      for (PSI_STD::vector<TreePtr<Statement> >::const_iterator ii = self.statements.begin(), ie = self.statements.end(); ii != ie; ++ii)
        (*ii)->value->global_dependencies(globals);
      self.value->global_dependencies(globals);
    }

    const TermVtable Block::vtable = PSI_COMPILER_TERM(Block, "psi.compiler.Block", Term);

    BottomType::BottomType(CompileContext& compile_context, const SourceLocation& location)
    : Type(&vtable, compile_context, location) {
    }
    
    template<typename V>
    void BottomType::visit(V& v) {
      visit_base<Type>(v);
    }
    
    const TermVtable BottomType::vtable = PSI_COMPILER_TERM(BottomType, "psi.compiler.BottomType", Type);

    ConstantType::ConstantType(CompileContext& compile_context, const SourceLocation& location)
    : Type(&vtable, compile_context, location) {
    }

    ConstantType::ConstantType(const TreePtr<Term>& value_, const SourceLocation& location)
    : Type(&vtable, value_.compile_context(), location),
    value(value_) {
    }
    
    template<typename V>
    void ConstantType::visit(V& v) {
      visit_base<Type>(v);
      v("value", &ConstantType::value);
    }
    
    const TermVtable ConstantType::vtable = PSI_COMPILER_TERM(ConstantType, "psi.compiler.ConstantType", Type);

    EmptyType::EmptyType(CompileContext& compile_context, const SourceLocation& location)
    : Type(&vtable, compile_context, location) {
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

    PointerType::PointerType(const TreePtr<Term>& target_type_, const SourceLocation& location)
    : Type(&vtable, target_type_.compile_context(), location),
    target_type(target_type_) {
    }
    
    template<typename V>
    void PointerType::visit(V& v) {
      visit_base<Type>(v);
      v("target_type", &PointerType::target_type);
    }
    
    const TermVtable PointerType::vtable = PSI_COMPILER_TERM(PointerType, "psi.compiler.PointerType", Type);
    
    PointerTo::PointerTo(CompileContext& compile_context, const SourceLocation& location)
    : Functional(&vtable, compile_context, location) {
    }

    PointerTo::PointerTo(const TreePtr<Term>& value_, const SourceLocation& location)
    : Functional(&vtable, TreePtr<Term>(new PointerType(tree_attribute(value_, &Term::type), location)), location),
    value(value_) {
    }

    template<typename V>
    void PointerTo::visit(V& v) {
      visit_base<Functional>(v);
      v("value", &PointerTo::value);
    }

    const TermVtable PointerTo::vtable = PSI_COMPILER_TERM(PointerTo, "psi.compiler.PointerTo", Functional);
    
    PointerTarget::PointerTarget(CompileContext& compile_context, const SourceLocation& location)
    : Functional(&vtable, compile_context, location) {
    }

    PointerTarget::PointerTarget(const TreePtr<Term>& value_, const SourceLocation& location)
    : Functional(&vtable, TreePtr<Term>(new PointerType(tree_attribute(value_, &Term::type), location)), location),
    value(value_) {
    }

    template<typename V>
    void PointerTarget::visit(V& v) {
      visit_base<Functional>(v);
      v("value", &PointerTarget::value);
    }

    const TermVtable PointerTarget::vtable = PSI_COMPILER_TERM(PointerTarget, "psi.compiler.PointerTarget", Functional);
    
    PointerCast::PointerCast(CompileContext& compile_context, const SourceLocation& location)
    : Functional(&vtable, compile_context, location) {
    }

    PointerCast::PointerCast(const TreePtr<Term>& value_, const TreePtr<Term>& target_type_, const SourceLocation& location)
    : Functional(&vtable, TreePtr<Term>(new PointerType(target_type_, location)), location),
    value(value_),
    target_type(target_type_) {
    }

    template<typename V>
    void PointerCast::visit(V& v) {
      visit_base<Functional>(v);
      v("value", &PointerCast::value)
      ("target_type", &PointerCast::target_type);
    }

    const TermVtable PointerCast::vtable = PSI_COMPILER_TERM(PointerCast, "psi.compiler.PointerCast", Functional);
    
    namespace {
      TreePtr<Term> element_type(const TreePtr<Term>& aggregate_type, const TreePtr<Term>& index, const SourceLocation& location) {
        CompileContext& compile_context = aggregate_type.compile_context();
        
        TreePtr<DerivedType> derived = dyn_treeptr_cast<DerivedType>(aggregate_type);
        TreePtr<Term> my_aggregate_type, next_upref;
        if (derived) {
          my_aggregate_type = derived->value_type;
          next_upref = derived->upref;
        } else {
          my_aggregate_type = aggregate_type;
        }
        
        TreePtr<Term> upref(new UpwardReference(my_aggregate_type, index, next_upref, location));
        if (TreePtr<StructType> st = dyn_treeptr_cast<StructType>(my_aggregate_type)) {
          int index_int = index_to_int(index, location);
          if ((index_int < 0) || (unsigned(index_int) >= st->members.size()))
            compile_context.error_throw(location, "Structure member index out of range");
          return TreePtr<Term>(new DerivedType(st->members[index_int], upref, location));
        } else if (TreePtr<UnionType> un = dyn_treeptr_cast<UnionType>(my_aggregate_type)) {
          int index_int = index_to_int(index, location);
          if ((index_int < 0) || (unsigned(index_int) >= un->members.size()))
            compile_context.error_throw(location, "Union member index out of range");
          return TreePtr<Term>(new DerivedType(un->members[index_int], upref, location));
        } else if (TreePtr<ArrayType> ar = dyn_treeptr_cast<ArrayType>(my_aggregate_type)) {
          return TreePtr<Term>(new DerivedType(ar->element_type, upref, location));
        } else if (TreePtr<TypeInstance> inst = dyn_treeptr_cast<TypeInstance>(my_aggregate_type)) {
          int index_int = index_to_int(index, location);
          if (index_int != 0)
            compile_context.error_throw(location, "Generic instance member index must be zero");
          return TreePtr<Term>(new DerivedType(inst->unwrap(), upref, location));
        } else {
          CompileError err(compile_context, location);
          err.info("Element lookup argument is not an aggregate type");
          err.info(aggregate_type.location(), "Type of element");
          err.end();
          throw CompileException();
        }
      }
      
      class ElementValueType {
        TreePtr<Term> m_aggregate_value;
        TreePtr<Term> m_index;
        
      public:
        typedef Term TreeResultType;

        ElementValueType(const TreePtr<Term>& aggregate_value, const TreePtr<Term>& index)
        : m_aggregate_value(aggregate_value), m_index(index) {}
        
        TreePtr<Term> evaluate(const TreePtr<Term>& self) {
          return element_type(m_aggregate_value->type, m_index, self.location());
        }
        
        template<typename V>
        static void visit(V& v) {
          v("aggregate_value", &ElementValueType::m_aggregate_value)
          ("index", &ElementValueType::m_index);
        }
      };
    }
    
    ElementValue::ElementValue(CompileContext& compile_context, const SourceLocation& location)
    : Functional(&vtable, compile_context, location) {
    }

    ElementValue::ElementValue(const TreePtr<Term>& value_, const TreePtr<Term>& index_, const SourceLocation& location)
    : Functional(&vtable, tree_callback(value_.compile_context(), location, ElementValueType(value_, index_)), location),
    value(value_),
    index(index_) {
    }

    ElementValue::ElementValue(const TreePtr<Term>& value_, int index_, const SourceLocation& location)
    : Functional(&vtable,
                 tree_callback(value_.compile_context(), location,
                               ElementValueType(value_, int_to_index(index_, value_.compile_context(), location))),
                 location),
    value(value_),
    index(int_to_index(index_, value_.compile_context(), location)) {
    }

    template<typename V>
    void ElementValue::visit(V& v) {
      visit_base<Functional>(v);
      v("value", &ElementValue::value)
      ("index", &ElementValue::index);
    }

    const TermVtable ElementValue::vtable = PSI_COMPILER_TERM(ElementValue, "psi.compiler.ElementValue", Functional);
    
    namespace {
      TreePtr<Term> outer_type(const TreePtr<Term>& inner_type, const SourceLocation& location) {
        CompileContext& compile_context = inner_type.compile_context();
        
        TreePtr<DerivedType> derived = dyn_treeptr_cast<DerivedType>(inner_type);
        if (!derived)
          compile_context.error_throw(location, "Outer value operation called on value with no upward reference");
        
        TreePtr<UpwardReference> upref = dyn_treeptr_cast<UpwardReference>(derived->upref);
        if (!upref)
          compile_context.error_throw(location, "Outer value operation called on value with unknown upward reference");
        
        return TreePtr<Term>(new DerivedType(upref->outer_type, upref->next, location));
      }
      
      class OuterValueType {
        TreePtr<Term> m_value;
        
      public:
        typedef Term TreeResultType;

        OuterValueType(const TreePtr<Term>& value) : m_value(value) {}
        
        TreePtr<Term> evaluate(const TreePtr<Term>& self) {
          return outer_type(m_value->type, self.location());
        }
        
        template<typename V>
        static void visit(V& v) {
          v("value", &OuterValueType::m_value);
        }
      };
    }
    
    OuterValue::OuterValue(CompileContext& compile_context, const SourceLocation& location)
    : Functional(&vtable, compile_context, location) {
    }
    
    OuterValue::OuterValue(const TreePtr<Term>& value_, const SourceLocation& location)
    : Functional(&vtable, tree_callback(value_.compile_context(), location, OuterValueType(value_)), location),
    value(value_) {
    }
    
    template<typename V>
    void OuterValue::visit(V& v) {
      visit_base<Functional>(v);
      v("value", &OuterValue::value);
    }
    
    const TermVtable OuterValue::vtable = PSI_COMPILER_TERM(OuterValue, "psi.compiler.OuterValue", Functional);
    
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
    
    namespace {
      TreePtr<Term> make_struct_type(CompileContext& compile_context, const PSI_STD::vector<TreePtr<Term> >& members, const SourceLocation& location) {
        PSI_STD::vector<TreePtr<Term> > member_types;
        member_types.reserve(members.size());
        for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = members.begin(), ie = members.end(); ii != ie; ++ii)
          member_types.push_back(*ii);
        TreePtr<Term> ty(new StructType(compile_context, members, location));
        return ty;
      }
    }
    
    StructValue::StructValue(CompileContext& compile_context, const PSI_STD::vector<TreePtr<Term> >& members_, const SourceLocation& location)
    : Constructor(&vtable, make_struct_type(compile_context, members_, location), location),
    members(members_) {
    }

    template<typename V>
    void StructValue::visit(V& v) {
      visit_base<Constructor>(v);
      v("members", &StructValue::members);
    }
    
    const TermVtable StructValue::vtable = PSI_COMPILER_TERM(StructValue, "psi.compiler.StructValue", Constructor);

    ArrayType::ArrayType(CompileContext& compile_context, const SourceLocation& location)
    : Type(&vtable, compile_context, location) {
    }

    ArrayType::ArrayType(const TreePtr<Term>& element_type_, const TreePtr<Term>& length_, const SourceLocation& location)
    : Type(&vtable, element_type_.compile_context(), location),
    element_type(element_type_),
    length(length_) {
    }

    template<typename V>
    void ArrayType::visit(V& v) {
      visit_base<Type>(v);
      v("element_type", &ArrayType::element_type)
      ("length", &ArrayType::length);
    }

    const TermVtable ArrayType::vtable = PSI_COMPILER_TERM(ArrayType, "psi.compiler.ArrayType", Type);
    
    ArrayValue::ArrayValue(CompileContext& compile_context, const SourceLocation& location)
    : Constructor(&vtable, compile_context, location) {
    }

    ArrayValue::ArrayValue(const TreePtr<ArrayType>& type, const PSI_STD::vector<TreePtr<Term> >& element_values_, const SourceLocation& location)
    : Constructor(&vtable, type, location),
    element_values(element_values_) {
    }
    
    template<typename V>
    void ArrayValue::visit(V& v) {
      visit_base<Constructor>(v);
      v("element_values", &ArrayValue::element_values);
    }

    const TermVtable ArrayValue::vtable = PSI_COMPILER_TERM(ArrayValue, "psi.compiler.ArrayValue", Constructor);

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
    
    UnionValue::UnionValue(CompileContext& compile_context, const SourceLocation& location)
    : Constructor(&vtable, compile_context, location) {
    }
    
    UnionValue::UnionValue(const TreePtr<UnionType>& type, const TreePtr<Term>& member_value_, const SourceLocation& location)
    : Constructor(&vtable, type, location),
    member_value(member_value_) {
    }
    
    template<typename V>
    void UnionValue::visit(V& v) {
      visit_base<Constructor>(v);
      v("member_value", &UnionValue::member_value);
    }
    
    const TermVtable UnionValue::vtable = PSI_COMPILER_TERM(UnionValue, "psi.compiler.UnionValue", Constructor);
    
    UpwardReferenceType::UpwardReferenceType(CompileContext& compile_context, const SourceLocation& location)
    : Type(&vtable, compile_context, location) {
    }
    
    template<typename V>
    void UpwardReferenceType::visit(V& v) {
      visit_base<Type>(v);
    }
    
    const TermVtable UpwardReferenceType::vtable = PSI_COMPILER_TERM(UpwardReferenceType, "psi.compiler.UpwardReferenceType", Type);
    
    UpwardReference::UpwardReference(const TreePtr<Term>& outer_type_, const TreePtr<Term>& outer_index_, const TreePtr<Term>& next_, const SourceLocation& location)
    : Constructor(&vtable, outer_type_.compile_context().builtins().upref_type, location),
    outer_type(outer_type_),
    outer_index(outer_index_),
    next(next_) {
    }
    
    UpwardReference::UpwardReference(CompileContext& context, const SourceLocation& location)
    : Constructor(&vtable, context, location) {
    }
    
    template<typename V>
    void UpwardReference::visit(V& v) {
      visit_base<Constructor>(v);
      v("outer_type", &UpwardReference::outer_type)
      ("outer_index", &UpwardReference::outer_index)
      ("next", &UpwardReference::next);
    }
    
    const TermVtable UpwardReference::vtable = PSI_COMPILER_TERM(UpwardReference, "psi.compiler.UpwardReference", Constructor);
    
    DerivedType::DerivedType(CompileContext& compile_context, const SourceLocation& location)
    : Type(&vtable, compile_context, location) {
    }
    
    DerivedType::DerivedType(const TreePtr<Term>& value_type_, const TreePtr<Term>& upref_, const SourceLocation& location)
    : Type(&vtable, value_type_->compile_context(), location),
    value_type(value_type_),
    upref(upref_) {
    }
    
    template<typename V>
    void DerivedType::visit(V& v) {
      visit_base<Type>(v);
      v("value_type", &DerivedType::value_type)
      ("upref", &DerivedType::upref);
    }
    
    const TermVtable DerivedType::vtable = PSI_COMPILER_TERM(DerivedType, "psi.compiler.DerivedType", Type);

    GenericType::GenericType(const PSI_STD::vector<TreePtr<Term> >& pattern_,
                             const TreePtr<Term>& member_type_,
                             const PSI_STD::vector<TreePtr<OverloadValue> >& overloads_,
                             GenericTypePrimitive primitive_mode_,
                             const SourceLocation& location)
    : Tree(&vtable, member_type_.compile_context(), location),
    pattern(pattern_),
    member_type(member_type_),
    overloads(overloads_),
    primitive_mode(primitive_mode_) {
      PSI_ASSERT((primitive_mode == primitive_always) || (primitive_mode == primitive_never) || (primitive_mode == primitive_recurse));
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
    : Functional(&vtable, compile_context, location) {
    }
    
    TypeInstance::TypeInstance(const TreePtr<GenericType>& generic_,
                               const PSI_STD::vector<TreePtr<Term> >& parameters_,
                               const SourceLocation& location)
    : Functional(&vtable, generic_.compile_context().builtins().metatype, location),
    generic(generic_),
    parameters(parameters_) {
    }

    /**
     * \brief Get the inner type of this instance.
     */
    TreePtr<Term> TypeInstance::unwrap() const {
      return generic->member_type->specialize(location(), parameters);
    }

    template<typename Visitor>
    void TypeInstance::visit(Visitor& v) {
      visit_base<Functional>(v);
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

    const TermVtable TypeInstance::vtable = PSI_COMPILER_TERM(TypeInstance, "psi.compiler.TypeInstance", Functional);

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
    
    JumpTarget::JumpTarget(CompileContext& compile_context, const TreePtr<Term>& value_, ResultMode argument_mode_, const TreePtr<Anonymous>& argument_, const SourceLocation& location)
    : Tree(&vtable, compile_context, location),
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

    void JumpGroup::global_dependencies_impl(const JumpGroup& self, PSI_STD::set<TreePtr<ModuleGlobal> >& globals) {
      self.initial->global_dependencies(globals);
      for (PSI_STD::vector<TreePtr<JumpTarget> >::const_iterator ii = self.entries.begin(), ie = self.entries.end(); ii != ie; ++ii)
        (*ii)->value->global_dependencies(globals);
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

    void JumpTo::global_dependencies_impl(const JumpTo& self, PSI_STD::set<TreePtr<ModuleGlobal> >& globals) {
      if (self.argument)
        self.argument->global_dependencies(globals);
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
      return anonymize_term(ft->result_type_after(location, nc_arguments), location);
    }
    
    TreePtr<FunctionType> FunctionCall::target_type() {
      return treeptr_cast<FunctionType>(treeptr_cast<PointerType>(target->type)->target_type);
    }
    
    template<typename Visitor>
    void FunctionCall::visit(Visitor& v) {
      visit_base<Term>(v);
      v("target", &FunctionCall::target)
      ("arguments", &FunctionCall::arguments);
    }
    
    void FunctionCall::global_dependencies_impl(const FunctionCall& self, PSI_STD::set<TreePtr<ModuleGlobal> >& globals) {
      self.target->global_dependencies(globals);
      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = self.arguments.begin(), ie = self.arguments.end(); ii != ie; ++ii)
        (*ii)->global_dependencies(globals);
    }

    const TermVtable FunctionCall::vtable = PSI_COMPILER_TERM(FunctionCall, "psi.compiler.FunctionCall", Term);

    SolidifyDuring::SolidifyDuring(CompileContext& context, const SourceLocation& location)
    : Term(&vtable, context, location) {
    }
    
    SolidifyDuring::SolidifyDuring(const TreePtr<Term>& value_, const TreePtr<Term>& body_, const SourceLocation& location)
    : Term(&vtable, value_.compile_context(), location),
    value(value_),
    body(body_) {
    }
    
    template<typename V>
    void SolidifyDuring::visit(V& v) {
      visit_base<Term>(v);
      v("value", &SolidifyDuring::value)
      ("body", &SolidifyDuring::body);
    }
    
    const TermVtable SolidifyDuring::vtable = PSI_COMPILER_TERM(SolidifyDuring, "psi.compiler.SolidifyDuring", Term);
    
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

    const TermVtable PrimitiveType::vtable = PSI_COMPILER_TERM(PrimitiveType, "psi.compiler.PrimitiveType", Type);
    
    BuiltinValue::BuiltinValue(CompileContext& compile_context, const SourceLocation& location)
    : Constant(&vtable, compile_context, location) {
    }
    
    BuiltinValue::BuiltinValue(const String& constructor_, const String& data_, const TreePtr<Term>& type, const SourceLocation& location)
    : Constant(&vtable, type, location),
    constructor(constructor_),
    data(data_) {
    }
    
    template<typename Visitor>
    void BuiltinValue::visit(Visitor& v) {
      visit_base<Constant>(v);
      v("constructor", &BuiltinValue::constructor)
      ("data", &BuiltinValue::data);
    }

    const TermVtable BuiltinValue::vtable = PSI_COMPILER_TERM(BuiltinValue, "psi.compiler.BuiltinValue", Constant);
    
    IntegerValue::IntegerValue(CompileContext& compile_context, const SourceLocation& location)
    : Constant(&vtable, compile_context, location) {
    }

    IntegerValue::IntegerValue(const TreePtr<Term>& type, int value_, const SourceLocation& location)
    : Constant(&vtable, type, location),
    value(value_) {
    }
    
    template<typename V>
    void IntegerValue::visit(V& v) {
      visit_base<Constant>(v);
      v("value", &IntegerValue::value);
    }
    
    const TermVtable IntegerValue::vtable = PSI_COMPILER_TERM(IntegerValue, "psi.compiler.IntegerValue", Constant);
    
    TreePtr<Term> StringValue::string_element_type(CompileContext& compile_context, const SourceLocation& location) {
      return TreePtr<Term>(new PrimitiveType(compile_context, "core.uint.8", location));
    }
    
    TreePtr<Term> StringValue::string_type(CompileContext& compile_context, const TreePtr<Term>& length, const SourceLocation& location) {
      return TreePtr<ArrayType>(new ArrayType(string_element_type(compile_context, location), length, location));
    }
    
    TreePtr<Term> StringValue::string_type(CompileContext& compile_context, unsigned length, const SourceLocation& location) {
      TreePtr<Term> length_type(new PrimitiveType(compile_context, "core.uint.ptr", location));
      TreePtr<Term> length_term(new IntegerValue(length_type, length, location));
      return string_type(compile_context, length_term, location);
    }
    
    StringValue::StringValue(CompileContext& compile_context, const SourceLocation& location)
    : Constant(&vtable, compile_context, location) {
    }

    StringValue::StringValue(CompileContext& compile_context, const String& value_, const SourceLocation& location)
    : Constant(&vtable, string_type(compile_context, value_.length()+1, location), location),
    value(value_) {
    }
    
    template<typename V>
    void StringValue::visit(V& v) {
      visit_base<Constant>(v);
      v("value", &StringValue::value);
    }
    
    const TermVtable StringValue::vtable = PSI_COMPILER_TERM(StringValue, "psi.compiler.StringValue", Constant);
    
    BuiltinFunction::BuiltinFunction(CompileContext& compile_context, const SourceLocation& location)
    : Global(&vtable, compile_context, location) {
    }
    
    BuiltinFunction::BuiltinFunction(const String& name_, bool pure_, const TreePtr<FunctionType>& type, const SourceLocation& location)
    : Global(&vtable, type, location),
    name(name_),
    pure(pure_) {
    }

    template<typename Visitor>
    void BuiltinFunction::visit(Visitor& v) {
      visit_base<Global>(v);
      v("name", &BuiltinFunction::name)
      ("pure", &BuiltinFunction::pure);
    }

    const TermVtable BuiltinFunction::vtable = PSI_COMPILER_TERM(BuiltinFunction, "psi.compiler.BuiltinFunction", Global);

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
      v("name", &Module::name);
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
    : Global(&vtable, compile_context, location) {
    }
    
    LibrarySymbol::LibrarySymbol(const TreePtr<Library>& library_, const TreePtr<TargetCallback>& callback_, const TreePtr<Term>& type, const SourceLocation& location)
    : Global(&vtable, type, location),
    library(library_),
    callback(callback_) {
    }
    
    template<typename V>
    void LibrarySymbol::visit(V& v) {
      visit_base<Global>(v);
      v("library", &LibrarySymbol::library)
      ("callback", &LibrarySymbol::callback);
    }

    const TermVtable LibrarySymbol::vtable = PSI_COMPILER_TERM(LibrarySymbol, "psi.compiler.LibrarySymbol", Global);
    
    TargetCallback::TargetCallback(const TargetCallbackVtable *vtable, CompileContext& compile_context, const SourceLocation& location)
    : Tree(PSI_COMPILER_VPTR_UP(Tree, vtable), compile_context, location) {
    }
    
    const SIVtable TargetCallback::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.TargetCallback", Tree);
    
    Namespace::Namespace(CompileContext& compile_context, const SourceLocation& location)
    : Tree(&vtable, compile_context, location) {
    }
    
    Namespace::Namespace(CompileContext& compile_context, const PSI_STD::map<String, TreePtr<Term> >& members_, const SourceLocation& location)
    : Tree(&vtable, compile_context, location),
    members(members_) {
    }
    
    template<typename V>
    void Namespace::visit(V& v) {
      visit_base<Tree>(v);
      v("members", &Namespace::members);
    }
    
    const TreeVtable Namespace::vtable = PSI_COMPILER_TREE(Namespace, "psi.compiler.Namespace", Tree);
    
    InterfaceValue::InterfaceValue(CompileContext& context, const SourceLocation& location)
    : Term(&vtable, context, location) {
    }
    
    InterfaceValue::InterfaceValue(const TreePtr<Interface>& interface_, const PSI_STD::vector<TreePtr<Term> >& parameters_, const SourceLocation& location)
    : Term(&vtable, interface_.compile_context(), location),
    interface(interface_),
    parameters(parameters_) {
    }
    
    template<typename V>
    void InterfaceValue::visit(V& v) {
      visit_base<Term>(v);
      v("interface", &InterfaceValue::interface)
      ("parameters", &InterfaceValue::parameters);
    }
    
    const TermVtable InterfaceValue::vtable = PSI_COMPILER_TERM(InterfaceValue, "psi.compiler.InterfaceValue", Term);

    MovableValue::MovableValue(CompileContext& context, const SourceLocation& location)
    : Term(&vtable, context, location) {
    }
    
    MovableValue::MovableValue(const TreePtr<Term>& value_, const SourceLocation& location)
    : Term(&vtable, tree_attribute(value_, &Term::type), location),
    value(value_) {
    }
    
    template<typename V>
    void MovableValue::visit(V& v) {
      visit_base<Term>(v);
      v("value", &MovableValue::value);
    }
    
    const TermVtable MovableValue::vtable = PSI_COMPILER_TERM(MovableValue, "psi.compiler.MovableValue", Term);
    
    InitializePointer::InitializePointer(CompileContext& context, const SourceLocation& location)
    : Term(&vtable, context, location) {
    }
    
    InitializePointer::InitializePointer(const TreePtr<Term>& target_ptr_, const TreePtr<Term>& assign_value_, const TreePtr<Term>& inner_, const SourceLocation& location)
    : Term(&vtable, tree_attribute(inner_, &Term::type), location),
    target_ptr(target_ptr_),
    assign_value(assign_value_),
    inner(inner_) {
    }
    
    template<typename V>
    void InitializePointer::visit(V& v) {
      visit_base<Term>(v);
      v("target_ptr", &InitializePointer::target_ptr)
      ("assign_value", &InitializePointer::assign_value)
      ("inner", &InitializePointer::inner);
    }
    
    const TermVtable InitializePointer::vtable = PSI_COMPILER_TERM(InitializePointer, "psi.compiler.InitializePointer", Term);
    
    AssignPointer::AssignPointer(CompileContext& context, const SourceLocation& location)
    : Term(&vtable, context, location) {
    }
    
    AssignPointer::AssignPointer(const TreePtr<Term>& target_ptr_, const TreePtr<Term>& assign_value_, const SourceLocation& location)
    : Term(&vtable, target_ptr_.compile_context().builtins().empty_type, location),
    target_ptr(target_ptr_),
    assign_value(assign_value_) {
    }
    
    template<typename V>
    void AssignPointer::visit(V& v) {
      visit_base<Term>(v);
      v("target_ptr", &AssignPointer::target_ptr)
      ("assign_value", &AssignPointer::assign_value);
    }

    const TermVtable AssignPointer::vtable = PSI_COMPILER_TERM(AssignPointer, "psi.compiler.AssignPointer", Term);
    
    FinalizePointer::FinalizePointer(CompileContext& context, const SourceLocation& location)
    : Term(&vtable, context, location) {
    }
    
    FinalizePointer::FinalizePointer(const TreePtr<Term>& target_ptr_, const SourceLocation& location)
    : Term(&vtable, target_ptr_.compile_context().builtins().empty_type, location) {
    }
    
    template<typename V>
    void FinalizePointer::visit(V& v) {
      visit_base<Term>(v);
      v("target_ptr", &FinalizePointer::target_ptr);
    }
    
    const TermVtable FinalizePointer::vtable = PSI_COMPILER_TERM(FinalizePointer, "psi.compiler.FinalizePointer", Term);
  }
}
