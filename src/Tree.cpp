#include "Tree.hpp"
#include "Parser.hpp"
#include "TermBuilder.hpp"

#include <boost/format.hpp>

namespace Psi {
  namespace Compiler {
    /**
     * Return the mode that should be used to return a result which
     * could be of mode \c lhs or \c rhs.
     */
    TermMode term_mode_combine(TermMode lhs, TermMode rhs) {
      if (lhs == term_mode_bottom)
        return rhs;
      else if (rhs == term_mode_bottom)
        return lhs;
      
      if ((lhs == term_mode_value) || (rhs == term_mode_value))
        return term_mode_value;
      else if ((lhs == term_mode_lref) || (rhs == term_mode_lref))
        return term_mode_lref;
      else {
        PSI_ASSERT((lhs == term_mode_rref) && (rhs == term_mode_rref));
        return term_mode_rref;
      }
    }
    
    TermResultInfo term_result_combine(const TermResultInfo& lhs, const TermResultInfo& rhs, const SourceLocation& location) {
      TermResultInfo ri;
      ri.mode = term_mode_combine(lhs.mode, rhs.mode);
      ri.pure = lhs.pure && rhs.pure;

      if (tree_isa<BottomType>(lhs.type))
        ri.type = rhs.type;
      else if (tree_isa<BottomType>(rhs.type))
        ri.type = lhs.type;
      else {
        ri.type = rhs.type;
        lhs.type->unify(ri.type, location);
      }
      
      return ri;
    }
    
    TermTypeInfo term_type_info_combine(const TermTypeInfo& lhs, const TermTypeInfo& rhs) {
      TermTypeInfo rt;
      rt.type_fixed_size = false;
      if ((lhs.type_mode == rhs.type_mode) || (rhs.type_mode == type_mode_metatype))
        rt.type_mode = lhs.type_mode;
      else if (lhs.type_mode == type_mode_metatype)
        rt.type_mode = rhs.type_mode;
      return rt;
    }

    /**
     * \brief Get the appropriate result type for terms which have no value.
     */
    TermResultInfo term_result_void(CompileContext& compile_context) {
      TermResultInfo rs;
      rs.type = TermBuilder::empty_type(compile_context);
      rs.mode = term_mode_value;
      rs.pure = false;
      return rs;
    }
    
    TermResultInfo term_result_bottom(CompileContext& compile_context) {
      TermResultInfo rs;
      rs.type = TermBuilder::bottom_type(compile_context);
      rs.mode = term_mode_bottom;
      rs.pure = false;
      return rs;
    }
    
    TermResultInfo term_result_type(CompileContext& compile_context) {
      TermResultInfo rs;
      rs.type = TermBuilder::metatype(compile_context);
      rs.mode = term_mode_value;
      rs.pure = true;
      return rs;
    }
    
    const SIVtable EvaluateContext::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.EvaluateContext", Tree);

    Functional::Functional(const VtableType *vptr)
    : Term(PSI_COMPILER_VPTR_UP(Term, vptr)) {
    }
    
    Functional::~Functional() {
      if (m_set_hook.is_linked()) {
        CompileContext::FunctionalTermSetType& hs = compile_context().m_functional_term_set;
        hs.erase(hs.iterator_to(*this));
      }
    }
    
    const SIVtable Functional::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Functional", Term);

    Constructor::Constructor(const VtableType* vtable)
    : Functional(vtable) {
    }
    
    const SIVtable Constructor::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Constructor", Functional);

    Constant::Constant(const VtableType *vptr)
    : Constructor(vptr) {
    }
    
    const SIVtable Constant::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Constant", Constructor);

    GlobalStatement::GlobalStatement(const TreePtr<Module>& module, const TreePtr<Term>& value_, StatementMode mode_, const SourceLocation& location)
    : ModuleGlobal(&vtable, module,
                   TermResultInfo(TermBuilder::to_global_functional(module, value_->type, location),
                                  (mode_ == statement_mode_functional) ? value_->mode : term_mode_lref, true),
                   mode_ == statement_mode_value ? link_private : link_none,
                   location),
    value(value_),
    mode(mode_) {
      switch (mode) {
      case statement_mode_destroy:
        compile_context().error_throw(location, "Global statements must have a storage type");
        
      case statement_mode_ref:
        value = TermBuilder::to_global_functional(module, value_, location);
        if (value->mode == term_mode_value)
          compile_context().error_throw(location, "Cannot create reference to a temporary");
        break;
      
      case statement_mode_functional:
        value = TermBuilder::to_global_functional(module, value_, location);
        if (value->type && !value->type->is_register_type())
          compile_context().error_throw(location, "Global statement value is not functional");
        break;
        
      case statement_mode_value:
        break;
        
      default: PSI_FAIL("Unknown statement mode");
      }
    }
    
    template<typename V>
    void GlobalStatement::visit(V& v) {
      visit_base<ModuleGlobal>(v);
      v("value", &GlobalStatement::value)
      ("mode", &GlobalStatement::mode);
    }
    
    TermTypeInfo GlobalStatement::type_info_impl(const GlobalStatement& self) {
      return self.value->type_info();
    }
    
    const TermVtable GlobalStatement::vtable = PSI_COMPILER_TERM(GlobalStatement, "psi.compiler.GlobalStatement", ModuleGlobal);
    
    /**
     * \brief General implementation for classes derived from Global.
     */
    TermTypeInfo Global::type_info_impl(const Global& self) {
      return self.type->type_info();
    }

    Global::Global(const VtableType *vptr, const TreePtr<Term>& type, const SourceLocation& location)
    : Term(vptr, TermResultInfo(type, term_mode_lref, true), location) {
    }

    Global::Global(const VtableType *vptr, const TermResultInfo& type, const SourceLocation& location)
    : Term(vptr, type, location) {
    }

    template<typename V>
    void Global::visit(V& v) {
      visit_base<Term>(v);
    }

    const SIVtable Global::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Global", Term);

    ModuleGlobal::ModuleGlobal(const VtableType *vptr, const TreePtr<Module>& module_, const String& symbol_name_, const TreePtr<Term>& type, Linkage linkage_, const SourceLocation& location)
    : Global(vptr, type, location),
    module(module_),
    linkage(linkage_),
    symbol_name(symbol_name_) {
    }

    ModuleGlobal::ModuleGlobal(const VtableType *vptr, const TreePtr<Module>& module_, const TermResultInfo& type, Linkage linkage_, const SourceLocation& location)
    : Global(vptr, type, location),
    module(module_),
    linkage(linkage_) {
    }
    
    template<typename V>
    void ModuleGlobal::visit(V& v) {
      visit_base<Global>(v);
      v("module", &ModuleGlobal::module)
      ("linkage", &ModuleGlobal::linkage);
    }

    const SIVtable ModuleGlobal::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.ModuleGlobal", Global);

    ExternalGlobal::ExternalGlobal(const TreePtr<Module>& module, const String& symbol_name, const TreePtr<Term>& type, const SourceLocation& location)
    : ModuleGlobal(&vtable, module, symbol_name, type, link_public, location) {
    }
    
    template<typename V>
    void ExternalGlobal::visit(V& v) {
      visit_base<ModuleGlobal>(v);
    }
    
    const TermVtable ExternalGlobal::vtable = PSI_COMPILER_TERM(ExternalGlobal, "psi.compiler.ExternalGlobal", ModuleGlobal);

    GlobalVariable::~GlobalVariable() {
    }
    
    template<typename V>
    void GlobalVariable::visit(V& v) {
      visit_base<ModuleGlobal>(v);
      v("value", &GlobalVariable::m_value)
      ("constant", &GlobalVariable::constant)
      ("merge", &GlobalVariable::merge);
    }
    
    void GlobalVariable::local_complete_impl(const GlobalVariable& self) {
      self.value();
    }
    
    const TermVtable GlobalVariable::vtable = PSI_COMPILER_TERM(GlobalVariable, "psi.compiler.GlobalVariable", ModuleGlobal);
    
    ParameterizedType::ParameterizedType(const VtableType *vptr)
    : Type(vptr) {
    }
    
    const SIVtable ParameterizedType::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.ParameterizedType", Type);

    Exists::Exists(const TreePtr<Term>& result_, const PSI_STD::vector<TreePtr<Term> >& parameter_types_, const SourceLocation& location)
    : ParameterizedType(&vtable),
    result(TermBuilder::to_functional(result_, location)),
    parameter_types(parameter_types_) {
      TermBuilder::to_functional(parameter_types, location);
    }
    
    template<typename V>
    void Exists::visit(V& v) {
      visit_base<Type>(v);
      v("result", &Exists::result)
      ("parameter_types", &Exists::parameter_types);
    }
    
    TermResultInfo Exists::check_type_impl(const Exists& self) {
      if (!self.result->is_type())
        self.compile_context().error_throw(self.location(), "Result of exists is not a type");
      
      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = self.parameter_types.begin(), ie = self.parameter_types.end(); ii != ie; ++ii) {
        if (!(*ii)->is_primitive_type())
          self.compile_context().error_throw(self.location(), "Parameter type of exists term is not a primitive type");
      }
      
      return term_result_type(self.compile_context());
    }
    
    TermTypeInfo Exists::type_info_impl(const Exists& self) {
      return self.result->type_info();
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

    const FunctionalVtable Exists::vtable = PSI_COMPILER_FUNCTIONAL(Exists, "psi.compiler.Exists", ParameterizedType);

    FunctionType::FunctionType(ResultMode result_mode_, const TreePtr<Term>& result_type_, const PSI_STD::vector<FunctionParameterType>& parameter_types_,
                               const PSI_STD::vector<TreePtr<InterfaceValue> >& interfaces_, const SourceLocation& location)
    : ParameterizedType(&vtable),
    result_mode(result_mode_),
    result_type(TermBuilder::to_functional(result_type_, location)),
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
    
    /// \brief Create an anonymous term which has the right type to for a function parameter
    TreePtr<Anonymous> FunctionType::parameter_after(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& previous) const {
      TreePtr<Term> ty = parameter_type_after(location, previous);
      return TermBuilder::anonymous(ty, parameter_to_term_mode(parameter_types[previous.size()].mode), location);
    }
    
    TreePtr<Term> FunctionType::result_type_after(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& previous) const {
      if (previous.size() != parameter_types.size())
        compile_context().error_throw(location, "Incorrect number of arguments passed to function");

      TreePtr<Term> type = result_type->specialize(location, previous);
      if (!type->is_type())
        compile_context().error_throw(location, "Rewritten function result type is not a type");

      return type;
    }
    
    TermResultInfo FunctionType::check_type_impl(const FunctionType& self) {
      // Doesn't currently check that parameters are correctly ordered
      for (PSI_STD::vector<FunctionParameterType>::const_iterator ii = self.parameter_types.begin(), ie = self.parameter_types.end(); ii != ie; ++ii) {
        if (!ii->type->is_type() || !ii->type->pure)
          self.compile_context().error_throw(self.location(), "Function parameter types must be pure types");
        
        if ((ii->mode == parameter_mode_functional) || (ii->mode == parameter_mode_phantom)) {
          if (!ii->type->is_register_type())
            self.compile_context().error_throw(self.location(), "Cannot pass complex types in functional (or phantom) arguments");
        }
      }
      
      if (!self.result_type->is_type() || !self.result_type->pure)
        self.compile_context().error_throw(self.location(), "Function result types must be pure types");
      if ((self.result_mode == result_mode_functional) && !self.result_type->is_register_type())
        self.compile_context().error_throw(self.location(), "Cannot return complex types functionally");
      
      return term_result_type(self.compile_context());
    }
    
    TermTypeInfo FunctionType::type_info_impl(const FunctionType&) {
      TermTypeInfo result;
      result.type_fixed_size = false;
      // Function types are effectively complex because function values cannot be dynamically constructed at all!
      result.type_mode = type_mode_complex;
      return result;
    }
    
    const FunctionalVtable FunctionType::vtable = PSI_COMPILER_FUNCTIONAL(FunctionType, "psi.compiler.FunctionType", ParameterizedType);

    Function::~Function() {
    }
    
    void Function::check_type() {
      TreePtr<FunctionType> ftype = treeptr_cast<FunctionType>(type);
      
      if (arguments.size() != ftype->parameter_types.size())
        compile_context().error_throw(location(), "Number of arguments to function does not match the function signature");
      
      PSI_STD::vector<TreePtr<Term> > my_arguments;
      my_arguments.reserve(arguments.size());
      for (std::size_t ii = 0, ie = arguments.size(); ii != ie; ++ii) {
        TreePtr<Term> type = ftype->parameter_type_after(arguments[ii]->location(), my_arguments);
        if (arguments[ii]->type != type)
          compile_context().error_throw(arguments[ii]->location(), "Parameter type to function does not match the function signature");
        my_arguments.push_back(arguments[ii]);
      }
      
      if (return_target) {
        if (return_target->argument_mode != ftype->result_mode)
          compile_context().error_throw(location(), "Return target mode does not match result mode of function");
        if (return_target->argument->type != ftype->result_type_after(location(), my_arguments))
          compile_context().error_throw(location(), "Return target mode does not match result mode of function");
      }
      
      if (TreePtr<Term> *body_ptr = m_body.get_maybe())
        body_check_type(*body_ptr);
    }
    
    void Function::body_check_type(TreePtr<Term>& body) const {
      TreePtr<FunctionType> ftype = treeptr_cast<FunctionType>(type);
      TreePtr<Term> result_type = ftype->result_type_after(location(), vector_from<TreePtr<Term> >(arguments));
      if (!result_type->convert_match(body->type))
        compile_context().error_throw(location(), "Function result has the wrong type");
    }

    template<typename Visitor> void Function::visit(Visitor& v) {
      visit_base<ModuleGlobal>(v);
      v("arguments", &Function::arguments)
      ("body", &Function::m_body)
      ("return_target", &Function::return_target);
    }

    void Function::local_complete_impl(const Function& self) {
      self.body();
    }
    
    const TermVtable Function::vtable = PSI_COMPILER_TERM(Function, "psi.compiler.Function", ModuleGlobal);

    TryFinally::TryFinally(const TreePtr<Term>& try_expr_, const TreePtr<Term>& finally_expr_, bool except_only_, const SourceLocation& location)
    : Term(&vtable, TermResultInfo(try_expr_->type, try_expr_->mode, false), location),
    try_expr(try_expr_),
    finally_expr(finally_expr_),
    except_only(except_only_) {
    }

    TermTypeInfo TryFinally::type_info_impl(const TryFinally& self) {
      return self.try_expr->type_info();
    }

    template<typename Visitor> void TryFinally::visit(Visitor& v) {
      visit_base<Term>(v);
      v("try_expr", &TryFinally::try_expr)
      ("finally_expr", &TryFinally::finally_expr)
      ("except_only", &TryFinally::except_only);
    }

    const TermVtable TryFinally::vtable = PSI_COMPILER_TERM(TryFinally, "psi.compiler.TryFinally", Term);

    Statement::Statement(const TreePtr<Term>& value_, StatementMode mode_, const SourceLocation& location)
    : Term(&vtable, value_.compile_context(),
           TermResultInfo(value_->type,
                          mode_ == statement_mode_functional ? term_mode_value
                          : mode_ == statement_mode_destroy ? term_mode_bottom
                          : term_mode_lref, true),
           location),
    value(value_),
    mode(mode_) {
      switch (mode) {
      case statement_mode_value:
      case statement_mode_destroy:
        break;
        
      case statement_mode_functional:
        if (type && !type->is_register_type()) {
          CompileError err(value.compile_context().error_context(), location);
          err.info(location, "Only primitive types can be used as functional values");
          err.info(type->location(), "Type is not primitive");
          err.end();
          throw CompileException();
        }
        break;
        
      case statement_mode_ref: {
        if ((value->mode != term_mode_lref) && (value->mode != term_mode_rref))
          value.compile_context().error_throw(location, "Cannot bind temporary to reference");
        break;
      }
        
      default: PSI_FAIL("Unknown statement mode");
      }
    }
    
    TermTypeInfo Statement::type_info_impl(const Statement& self) {
      return self.value->type_info();
    }

    template<typename Visitor>
    void Statement::visit(Visitor& v) {
      visit_base<Term>(v);
      v("value", &Statement::value)
      ("mode", &Statement::mode);
    }

    const TermVtable Statement::vtable = PSI_COMPILER_TERM(Statement, "psi.compiler.Statement", Term);
    
    Block::Block(const PSI_STD::vector<TreePtr<Statement> >& statements_, const TreePtr<Term>& value_, const SourceLocation& location)
    : Term(&vtable, TermResultInfo(value_->type, value_->mode, false), location),
    statements(statements_),
    value(value_) {
    }

    template<typename Visitor>
    void Block::visit(Visitor& v) {
      visit_base<Term>(v);
      v("statements", &Block::statements)
      ("value", &Block::value);
    }
    
    TermTypeInfo Block::type_info_impl(const Block& self) {
      return self.value->type_info();
    }

    const TermVtable Block::vtable = PSI_COMPILER_TERM(Block, "psi.compiler.Block", Term);

    BottomType::BottomType()
    : Type(&vtable) {
    }
    
    template<typename V>
    void BottomType::visit(V& v) {
      visit_base<Type>(v);
    }
    
    TermResultInfo BottomType::check_type_impl(const BottomType& self) {
      return term_result_type(self.compile_context());
    }
    
    TermTypeInfo BottomType::type_info_impl(const BottomType&) {
      TermTypeInfo rt;
      rt.type_mode = type_mode_bottom;
      rt.type_fixed_size = false;
      return rt;
    }
    
    const FunctionalVtable BottomType::vtable = PSI_COMPILER_FUNCTIONAL(BottomType, "psi.compiler.BottomType", Type);

    ConstantType::ConstantType(const TreePtr<Term>& value_, const SourceLocation& location)
    : Type(&vtable),
    value(TermBuilder::to_functional(value_, location)) {
    }
    
    template<typename V>
    void ConstantType::visit(V& v) {
      visit_base<Type>(v);
      v("value", &ConstantType::value);
    }
    
    TermResultInfo ConstantType::check_type_impl(const ConstantType& self) {
      if (self.value->type && !self.value->type->is_register_type())
        self.compile_context().error_throw(self.location(), "Type of value of constant type is not a register type");
      return term_result_type(self.compile_context());
    }
    
    TermTypeInfo ConstantType::type_info_impl(const ConstantType&) {
      TermTypeInfo rt;
      rt.type_mode = type_mode_primitive;
      rt.type_fixed_size = true;
      return rt;
    }
    
    const FunctionalVtable ConstantType::vtable = PSI_COMPILER_FUNCTIONAL(ConstantType, "psi.compiler.ConstantType", Type);

    EmptyType::EmptyType()
    : Type(&vtable) {
    }

    template<typename V>
    void EmptyType::visit(V& v) {
      visit_base<Type>(v);
    }
    
    TermResultInfo EmptyType::check_type_impl(const EmptyType& self) {
      return term_result_type(self.compile_context());
    }
    
    TermTypeInfo EmptyType::type_info_impl(const EmptyType&) {
      TermTypeInfo rt;
      rt.type_mode = type_mode_primitive;
      rt.type_fixed_size = true;
      return rt;
    }

    const FunctionalVtable EmptyType::vtable = PSI_COMPILER_FUNCTIONAL(EmptyType, "psi.compiler.EmptyType", Type);

    DefaultValue::DefaultValue(const TreePtr<Term>& type, const SourceLocation& location)
    : Constructor(&vtable),
    value_type(TermBuilder::to_functional(type, location)) {
    }
    
    TermResultInfo DefaultValue::check_type_impl(const DefaultValue& self) {
      const TermTypeInfo& value_info = self.value_type->type_info();
      PSI_ASSERT(self.value_type->pure || tree_isa<FunctionalEvaluate>(self.value_type));
      if (value_info.type_mode == type_mode_none)
        self.compile_context().error_throw(self.location(), "Type for default value is not a type");
      if (value_info.type_mode == type_mode_bottom)
        self.compile_context().error_throw(self.location(), "Cannot create default value of bottom type");
      return TermResultInfo(self.value_type, term_mode_value, self.value_type->is_register_type());
    }
    
    TermTypeInfo DefaultValue::type_info_impl(const DefaultValue& self) {
      const TermTypeInfo& value_info = self.value_type->type_info();
      TermTypeInfo rt;
      rt.type_mode = (value_info.type_mode == type_mode_metatype) ? type_mode_complex : type_mode_none;
      rt.type_fixed_size = false;
      return rt;
    }
    
    template<typename V>
    void DefaultValue::visit(V& v) {
      visit_base<Constructor>(v);
      v("value_type", &DefaultValue::value_type);
    }

    const FunctionalVtable DefaultValue::vtable = PSI_COMPILER_FUNCTIONAL(DefaultValue, "psi.compiler.DefaultValue", Constructor);

    PointerType::PointerType(const TreePtr<Term>& target_type_, const TreePtr<Term>& upref_, const SourceLocation& location)
    : Type(&vtable),
    target_type(TermBuilder::to_functional(target_type_, location)),
    upref(upref_) {
    }
    
    template<typename V>
    void PointerType::visit(V& v) {
      visit_base<Type>(v);
      v("target_type", &PointerType::target_type)
      ("upref", &PointerType::upref);
    }
    
    TermResultInfo PointerType::check_type_impl(const PointerType& self) {
      if (!self.target_type->is_type())
        self.compile_context().error_throw(self.location(), "Pointer target type is not a type");
      
      if (!term_unwrap_isa<UpwardReferenceType>(self.upref->type))
        self.compile_context().error_throw(self.location(), "Upward reference parameter to pointer type is not an upward reference");

      return term_result_type(self.compile_context());
    }
    
    TermTypeInfo PointerType::type_info_impl(const PointerType&) {
      TermTypeInfo rt;
      rt.type_fixed_size = true;
      rt.type_mode = type_mode_primitive;
      return rt;
    }
    
    const FunctionalVtable PointerType::vtable = PSI_COMPILER_FUNCTIONAL(PointerType, "psi.compiler.PointerType", Type);
    
    PointerTo::PointerTo(const TreePtr<Term>& value_)
    : Functional(&vtable),
    value(value_) {
    }

    template<typename V>
    void PointerTo::visit(V& v) {
      visit_base<Functional>(v);
      v("value", &PointerTo::value);
    }
    
    TermResultInfo PointerTo::check_type_impl(const PointerTo& self) {
      PSI_ASSERT(!self.value->type || self.value->type->pure);

      if ((self.value->mode != term_mode_lref) && (self.value->mode != term_mode_rref))
        self.compile_context().error_throw(self.location(), "Cannot take address of temporary variable");
      
      return TermResultInfo(TermBuilder::pointer(self.value->type, self.location()), term_mode_value, self.value->pure);
    }
    
    TermTypeInfo PointerTo::type_info_impl(const PointerTo&) {
      TermTypeInfo rt;
      rt.type_mode = type_mode_none;
      return rt;
    }

    const FunctionalVtable PointerTo::vtable = PSI_COMPILER_FUNCTIONAL(PointerTo, "psi.compiler.PointerTo", Functional);
    
    PointerTarget::PointerTarget(const TreePtr<Term>& value_)
    : Functional(&vtable),
    value(value_) {
    }

    template<typename V>
    void PointerTarget::visit(V& v) {
      visit_base<Functional>(v);
      v("value", &PointerTarget::value);
    }
    
    TermResultInfo PointerTarget::check_type_impl(const PointerTarget& self) {
      TreePtr<PointerType> ptr_ty = term_unwrap_dyn_cast<PointerType>(self.value->type);
      if (!ptr_ty)
        self.compile_context().error_throw(self.location(), "Argument to PointerTarget is not a pointer");
      return TermResultInfo(ptr_ty->target_type, term_mode_lref, self.value->pure);
    }
    
    TermTypeInfo PointerTarget::type_info_impl(const PointerTarget& self) {
      TermTypeInfo rt;
      rt.type_mode = (self.type->type_info().type_mode == type_mode_metatype) ? type_mode_complex : type_mode_none;
      return rt;
    }

    const FunctionalVtable PointerTarget::vtable = PSI_COMPILER_FUNCTIONAL(PointerTarget, "psi.compiler.PointerTarget", Functional);
    
    PointerCast::PointerCast(const TreePtr<Term>& value_, const TreePtr<Term>& target_type_)
    : Functional(&vtable),
    value(value_),
    target_type(target_type_) {
    }

    template<typename V>
    void PointerCast::visit(V& v) {
      visit_base<Functional>(v);
      v("value", &PointerCast::value)
      ("target_type", &PointerCast::target_type);
    }
    
    TermResultInfo PointerCast::check_type_impl(const PointerCast& self) {
      if (!tree_isa<PointerType>(self.value->type))
        self.compile_context().error_throw(self.location(), "Argument to PointerCast is not a pointer");
      return TermResultInfo(TermBuilder::ptr_to(self.target_type, self.location()), term_mode_value, self.value->pure);
    }
    
    TermTypeInfo PointerCast::type_info_impl(const PointerCast&) {
      TermTypeInfo rt;
      rt.type_mode = type_mode_none;
      return rt;
    }
    
    const FunctionalVtable PointerCast::vtable = PSI_COMPILER_FUNCTIONAL(PointerCast, "psi.compiler.PointerCast", Functional);
    
    ElementValue::ElementValue(const TreePtr<Term>& value_, const TreePtr<Term>& index_)
    : Functional(&vtable),
    value(value_),
    index(index_) {
    }

    template<typename V>
    void ElementValue::visit(V& v) {
      visit_base<Functional>(v);
      v("value", &ElementValue::value)
      ("index", &ElementValue::index);
    }
    
    /**
     * \brief Get the type of an aggregate element.
     */
    TreePtr<Term> ElementValue::element_type(const TreePtr<Term>& aggregate, const TreePtr<Term>& index, const SourceLocation& location) {
      CompileContext& compile_context = aggregate.compile_context();
      
      TreePtr<Term> unwrapped = term_unwrap(aggregate);
      if (TreePtr<StructType> st = dyn_treeptr_cast<StructType>(unwrapped)) {
        unsigned index_int = TermBuilder::size_from(index, location);
        if (index_int >= st->members.size())
          compile_context.error_throw(location, "Structure member index out of range");
        return st->members[index_int];
      } else if (TreePtr<UnionType> un = dyn_treeptr_cast<UnionType>(unwrapped)) {
        unsigned index_int = TermBuilder::size_from(index, location);
        if (index_int >= un->members.size())
          compile_context.error_throw(location, "Union member index out of range");
        return un->members[index_int];
      } else if (TreePtr<ArrayType> ar = dyn_treeptr_cast<ArrayType>(unwrapped)) {
        return ar->element_type;
      } else if (TreePtr<TypeInstance> inst = dyn_treeptr_cast<TypeInstance>(unwrapped)) {
        unsigned index_int = TermBuilder::size_from(index, location);
        if (index_int != 0)
          compile_context.error_throw(location, "Generic instance member index must be zero");
        return inst->unwrap();
      } else {
        CompileError err(compile_context.error_context(), location);
        err.info("Element lookup argument is not an aggregate type");
        err.info(unwrapped.location(), "Type of aggregate");
        err.end();
        throw CompileException();
      }
    }
    
    TermResultInfo ElementValue::check_type_impl(const ElementValue& self) {
      return TermResultInfo(element_type(self.value->type, self.index, self.location()),
                            term_mode_lref, self.value->pure && self.index->pure);
    }

    TermTypeInfo ElementValue::type_info_impl(const ElementValue& self) {
      TermTypeInfo rt;
      rt.type_mode = (self.type->type_info().type_mode == type_mode_metatype) ? type_mode_complex : type_mode_none;
      rt.type_fixed_size = false;
      return rt;
    }
    
    const FunctionalVtable ElementValue::vtable = PSI_COMPILER_FUNCTIONAL(ElementValue, "psi.compiler.ElementValue", Functional);
    
    ElementPointer::ElementPointer(const TreePtr<Term>& pointer_, const TreePtr<Term>& index_)
    : Functional(&vtable),
    pointer(pointer_),
    index(index_) {
    }

    template<typename V>
    void ElementPointer::visit(V& v) {
      visit_base<Functional>(v);
      v("pointer", &ElementPointer::pointer)
      ("index", &ElementPointer::index);
    }
    
    TermResultInfo ElementPointer::check_type_impl(const ElementPointer& self) {
      TreePtr<PointerType> ptr_ty = term_unwrap_dyn_cast<PointerType>(self.pointer->type);
      if (!ptr_ty)
        self.compile_context().error_throw(self.location(), "Argument to ElementPointer is not a pointer");
      
      TreePtr<Term> upref = TermBuilder::upref(ptr_ty->target_type, self.index, ptr_ty->upref, self.location());
      TreePtr<Term> inner = TermBuilder::pointer(ElementValue::element_type(ptr_ty->target_type, self.index, self.location()), upref, self.location());
      return TermResultInfo(inner, term_mode_value, self.pointer->pure && self.index->pure);
    }

    TermTypeInfo ElementPointer::type_info_impl(const ElementPointer& PSI_UNUSED(self)) {
      TermTypeInfo rt;
      rt.type_mode = type_mode_none;
      return rt;
    }
    
    const FunctionalVtable ElementPointer::vtable = PSI_COMPILER_FUNCTIONAL(ElementPointer, "psi.compiler.ElementPointer", Functional);

    OuterPointer::OuterPointer(const TreePtr<Term>& pointer_)
    : Functional(&vtable),
    pointer(pointer_) {
    }
    
    template<typename V>
    void OuterPointer::visit(V& v) {
      visit_base<Functional>(v);
      v("pointer", &OuterPointer::pointer);
    }
    
    TermResultInfo OuterPointer::check_type_impl(const OuterPointer& self) {
      TreePtr<PointerType> pointer_ty = term_unwrap_dyn_cast<PointerType>(self.pointer->type);
      if (!pointer_ty)
        self.compile_context().error_throw(self.location(), "Outer value operation called on value which is not a pointer");
      
      TreePtr<UpwardReference> upref = term_unwrap_dyn_cast<UpwardReference>(pointer_ty->upref);
      if (!upref)
        self.compile_context().error_throw(self.location(), "Outer value operation called on value with unknown upward reference");
      
      return TermResultInfo(TermBuilder::pointer(upref->outer_type(), upref->next, self.location()),
                            term_mode_value, self.pointer->pure);
    }
    
    TermTypeInfo OuterPointer::type_info_impl(const OuterPointer&) {
      TermTypeInfo rt;
      rt.type_mode = type_mode_none;
      return rt;
    }
    
    const FunctionalVtable OuterPointer::vtable = PSI_COMPILER_FUNCTIONAL(OuterPointer, "psi.compiler.OuterPointer", Functional);
    
    StructType::StructType(const PSI_STD::vector<TreePtr<Term> >& members_, const SourceLocation& location)
    : Type(&vtable),
    members(members_) {
      TermBuilder::to_functional(members, location);
    }
    
    template<typename V>
    void StructType::visit(V& v) {
      visit_base<Type>(v);
      v("members", &StructType::members);
    }
    
    TermResultInfo StructType::check_type_impl(const StructType& self) {
      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = self.members.begin(), ie = self.members.end(); ii != ie; ++ii) {
        PSI_ASSERT((*ii)->pure);
        if (!(*ii)->is_type())
          self.compile_context().error_throw(self.location(), "Struct member is not a type");
      }
      
      return term_result_type(self.compile_context());
    }
    
    TermTypeInfo StructType::type_info_impl(const StructType& self) {
      TermTypeInfo rt;
      rt.type_mode = type_mode_primitive;
      rt.type_fixed_size = true;
      
      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = self.members.begin(), ie = self.members.end(); ii != ie; ++ii) {
        const TermTypeInfo& tri = (*ii)->type_info();
        if (!tri.type_fixed_size)
          rt.type_fixed_size = false;
        if (tri.type_mode == type_mode_complex)
          rt.type_mode = type_mode_complex;
      }
      
      return rt;
    }

    const FunctionalVtable StructType::vtable = PSI_COMPILER_FUNCTIONAL(StructType, "psi.compiler.StructType", Type);
    
    StructValue::StructValue(const TreePtr<StructType>& type, const PSI_STD::vector<TreePtr<Term> >& members_)
    : Constructor(&vtable),
    struct_type(type),
    members(members_) {
    }

    template<typename V>
    void StructValue::visit(V& v) {
      visit_base<Constructor>(v);
      v("struct_type", &StructValue::struct_type)
      ("members", &StructValue::members);
    }
    
    TermResultInfo StructValue::check_type_impl(const StructValue& self) {
      if (self.members.size() != self.struct_type->members.size())
        self.compile_context().error_throw(self.location(), "Struct value has the wrong number of members according to its type");
      
      bool pure = true;
      for (std::size_t ii = 0, ie = self.members.size(); ii != ie; ++ii) {
        pure = pure && self.members[ii]->pure;
        if (self.members[ii]->type != self.struct_type->members[ii])
          self.compile_context().error_throw(self.location(), boost::format("Struct member %d has the wrong type") % ii);
      }
      
      return TermResultInfo(self.struct_type, term_mode_value, pure);
    }
    
    TermTypeInfo StructValue::type_info_impl(const StructValue&) {
      TermTypeInfo rt;
      rt.type_mode = type_mode_none;
      return rt;
    }
    
    const FunctionalVtable StructValue::vtable = PSI_COMPILER_FUNCTIONAL(StructValue, "psi.compiler.StructValue", Constructor);

    ArrayType::ArrayType(const TreePtr<Term>& element_type_, const TreePtr<Term>& length_, const SourceLocation& location)
    : Type(&vtable),
    element_type(TermBuilder::to_functional(element_type_, location)),
    length(TermBuilder::to_functional(length_, location)) {
    }
    
    /**
     * \internal Arrays with a size not known at compile time are complex types
     * because they cannot be loaded onto the stack.
     */
    TermResultInfo ArrayType::check_type_impl(const ArrayType& self) {
      PSI_ASSERT(self.element_type->pure && self.length->pure);
      
      if (!self.element_type->is_type())
        self.compile_context().error_throw(self.location(), "Array element type is not a type");
      if (self.length->type != TermBuilder::size_type(self.compile_context()))
        self.compile_context().error_throw(self.location(), "Array length is not a size");
      
      return term_result_type(self.compile_context());
    }
    
    TermTypeInfo ArrayType::type_info_impl(const ArrayType& self) {
      const TermTypeInfo& elem_info = self.element_type->type_info();
      TermTypeInfo rt;
      rt.type_fixed_size = elem_info.type_fixed_size && !tree_isa<IntegerConstant>(self.length);
      rt.type_mode = elem_info.type_mode;
      return rt;
    }

    template<typename V>
    void ArrayType::visit(V& v) {
      visit_base<Type>(v);
      v("element_type", &ArrayType::element_type)
      ("length", &ArrayType::length);
    }

    const FunctionalVtable ArrayType::vtable = PSI_COMPILER_FUNCTIONAL(ArrayType, "psi.compiler.ArrayType", Type);
    
    ArrayValue::ArrayValue(const TreePtr<ArrayType>& type, const PSI_STD::vector<TreePtr<Term> >& element_values_)
    : Constructor(&vtable),
    array_type(type),
    element_values(element_values_) {
    }
    
    template<typename V>
    void ArrayValue::visit(V& v) {
      visit_base<Constructor>(v);
      v("array_type", &ArrayValue::array_type)
      ("element_values", &ArrayValue::element_values);
    }
    
    TermResultInfo ArrayValue::check_type_impl(const ArrayValue& self) {
      TreePtr<Term> element_type = self.array_type->element_type;
      if (TermBuilder::size_equals(self.array_type->length, self.element_values.size()))
        self.compile_context().error_throw(self.location(), "Array literal length does not match its type");
      
      bool pure = true;
      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = self.element_values.begin(), ie = self.element_values.end(); ii != ie; ++ii) {
        pure = pure && (*ii)->pure;
        if ((*ii)->type != element_type)
          self.compile_context().error_throw(self.location(), "Array literal element has incorrect type");
      }
      
      return TermResultInfo(self.array_type, term_mode_value, pure);
    }
    
    TermTypeInfo ArrayValue::type_info_impl(const ArrayValue&) {
      TermTypeInfo rt;
      rt.type_mode = type_mode_none;
      return rt;
    }

    const FunctionalVtable ArrayValue::vtable = PSI_COMPILER_FUNCTIONAL(ArrayValue, "psi.compiler.ArrayValue", Constructor);

    UnionType::UnionType(const std::vector<TreePtr<Term> >& members_, const SourceLocation& location)
    : Type(&vtable),
    members(members_) {
      TermBuilder::to_functional(members, location);
    }

    template<typename V>
    void UnionType::visit(V& v) {
      visit_base<Term>(v);
      v("members", &UnionType::members);
    }
    
    TermResultInfo UnionType::check_type_impl(const UnionType& self) {
      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = self.members.begin(), ie = self.members.end(); ii != ie; ++ii) {
        PSI_ASSERT((*ii)->pure);
        if (!(*ii)->is_type())
          self.compile_context().error_throw(self.location(), "Union element type is not a type");
      }
      
      return term_result_type(self.compile_context());
    }
    
    TermTypeInfo UnionType::type_info_impl(const UnionType& self) {
      TermTypeInfo rt;
      rt.type_fixed_size = true;
      // Unions are always primitive because there's no sensible default way to handle members
      rt.type_mode = type_mode_primitive;
      
      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = self.members.begin(), ie = self.members.end(); ii != ie; ++ii)
        rt.type_fixed_size = rt.type_fixed_size && (*ii)->type_info().type_fixed_size;
      
      return rt;
    }

    const FunctionalVtable UnionType::vtable = PSI_COMPILER_FUNCTIONAL(UnionType, "psi.compiler.UnionType", Type);
    
    UnionValue::UnionValue(const TreePtr<UnionType>& type, const TreePtr<Term>& member_value_)
    : Constructor(&vtable),
    union_type(type),
    member_value(member_value_) {
    }
    
    template<typename V>
    void UnionValue::visit(V& v) {
      visit_base<Constructor>(v);
      v("union_type", &UnionValue::union_type)
      ("member_value", &UnionValue::member_value);
    }
    
    TermResultInfo UnionValue::check_type_impl(const UnionValue& self) {
      bool found = false;
      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = self.union_type->members.begin(), ie = self.union_type->members.end(); ii != ie; ++ii) {
        if (self.member_value->type == *ii) {
          found = true;
          break;
        }
      }
      
      if (!found)
        self.compile_context().error_throw(self.location(), "Union constructor member value is not a member of the union");
      
      return TermResultInfo(self.union_type, term_mode_value, self.member_value->pure);
    }
    
    TermTypeInfo UnionValue::type_info_impl(const UnionValue&) {
      TermTypeInfo rt;
      rt.type_mode = type_mode_none;
      return rt;
    }
    
    const FunctionalVtable UnionValue::vtable = PSI_COMPILER_FUNCTIONAL(UnionValue, "psi.compiler.UnionValue", Constructor);
    
    UpwardReferenceType::UpwardReferenceType()
    : Type(&vtable) {
    }
    
    template<typename V>
    void UpwardReferenceType::visit(V& v) {
      visit_base<Type>(v);
    }
    
    TermResultInfo UpwardReferenceType::check_type_impl(const UpwardReferenceType& self) {
      return term_result_type(self.compile_context());
    }
    
    TermTypeInfo UpwardReferenceType::type_info_impl(const UpwardReferenceType&) {
      TermTypeInfo rt;
      rt.type_mode = type_mode_primitive;
      rt.type_fixed_size = true;
      return rt;
    }
    
    const FunctionalVtable UpwardReferenceType::vtable = PSI_COMPILER_FUNCTIONAL(UpwardReferenceType, "psi.compiler.UpwardReferenceType", Type);
    
    UpwardReference::UpwardReference(const TreePtr<Term>& outer_type_, const TreePtr<Term>& outer_index_, const TreePtr<Term>& next_, const SourceLocation& location)
    : Constructor(&vtable),
    maybe_outer_type(TermBuilder::to_functional(outer_type_, location)),
    outer_index(TermBuilder::to_functional(outer_index_, location)),
    next(TermBuilder::to_functional(next_, location)) {
    }
    
    /**
     * \brief Get the inner type implied by UpwardReference
     */
    TreePtr<Term> UpwardReference::inner_type() const {
      return ElementValue::element_type(outer_type(), outer_index, location());
    }
    
    TreePtr<Term> UpwardReference::outer_type() const {
      PSI_STD::vector<const UpwardReference*> upref_list;
      const UpwardReference *upref = this;
      while (true) {
        if (!upref)
          compile_context().error_throw(location(), "Outer type of upward reference not available");

        if (upref->maybe_outer_type)
          break;
        
        upref = dyn_tree_cast<UpwardReference>(upref->next.get());
        upref_list.push_back(upref);
      }
      
      TreePtr<Term> ty = upref->maybe_outer_type;
      while (!upref_list.empty()) {
        ty = ElementValue::element_type(ty, upref_list.back()->outer_index, location());
        upref_list.pop_back();
      }
      
      return ty;
    }
    
    template<typename V>
    void UpwardReference::visit(V& v) {
      visit_base<Constructor>(v);
      v("maybe_outer_type", &UpwardReference::maybe_outer_type)
      ("outer_index", &UpwardReference::outer_index)
      ("next", &UpwardReference::next);
    }
    
    TermResultInfo UpwardReference::check_type_impl(const UpwardReference& self) {
      if (self.outer_index->type != TermBuilder::size_type(self.compile_context()))
        self.compile_context().error_throw(self.location(), "Upward reference index is not a size");
      
      if (!term_unwrap_isa<UpwardReferenceType>(self.next->type))
        self.compile_context().error_throw(self.location(), "Next reference of upward reference is not itself an upward reference");

      if (!term_unwrap_isa<UpwardReference>(self.next) && !self.maybe_outer_type)
        self.compile_context().error_throw(self.location(), "One of outer_type and next of an upref must be non-NULL");
      
      return TermResultInfo(TermBuilder::upref_type(self.compile_context()), term_mode_value, true);
    }
    
    TermTypeInfo UpwardReference::type_info_impl(const UpwardReference&) {
      TermTypeInfo rt;
      rt.type_mode = type_mode_none;
      return rt;
    }
    
    TreePtr<Term> UpwardReference::rewrite_impl(const UpwardReference& self, TermRewriter& rewriter, const SourceLocation& location) {
      TreePtr<Term> next, outer_type, outer_index;
      outer_index = rewriter.rewrite(self.outer_index);
      if (self.next)
        next = rewriter.rewrite(self.next);
      if (!tree_isa<UpwardReference>(next))
        outer_type = rewriter.rewrite(self.outer_type());
      return TermBuilder::upref(outer_type, outer_index, next, location);
    }
    
    const FunctionalVtable UpwardReference::vtable = PSI_COMPILER_FUNCTIONAL(UpwardReference, "psi.compiler.UpwardReference", Constructor);
    
    UpwardReferenceNull::UpwardReferenceNull()
    : Constant(&vtable) {
    }
    
    template<typename V>
    void UpwardReferenceNull::visit(V& v) {
      visit_base<Constant>(v);
    }
    
    TermResultInfo UpwardReferenceNull::check_type_impl(const UpwardReferenceNull& self) {
      return TermResultInfo(TermBuilder::upref_type(self.compile_context()), term_mode_value, true);
    }
    
    TermTypeInfo UpwardReferenceNull::type_info_impl(const UpwardReferenceNull&) {
      TermTypeInfo rt;
      rt.type_mode = type_mode_none;
      return rt;
    }
    
    const FunctionalVtable UpwardReferenceNull::vtable = PSI_COMPILER_FUNCTIONAL(UpwardReferenceNull, "psi.compiler.UpwardReferenceNull", Constant);
    
    template<typename Visitor>
    void GenericType::visit(Visitor& v) {
      visit_base<Tree>(v);
      v("pattern", &GenericType::pattern)
      ("member", &GenericType::m_member)
      ("overloads", &GenericType::m_overloads)
      ("primitive_mode", &GenericType::primitive_mode);
    }

    void GenericType::local_complete_impl(const GenericType& self) {
      self.member_type();
      self.overloads();
    }

    const TreeVtable GenericType::vtable = PSI_COMPILER_TREE(GenericType, "psi.compiler.GenericType", Tree);
    
    TypeInstance::TypeInstance(const TreePtr<GenericType>& generic_,
                               const PSI_STD::vector<TreePtr<Term> >& parameters_)
    : Type(&vtable),
    generic(generic_),
    parameters(parameters_) {
    }

    /**
     * \brief Get the inner type of this instance.
     */
    TreePtr<Term> TypeInstance::unwrap() const {
      PSI_ASSERT(generic->pattern.size() == parameters.size());
      return generic->member_type()->specialize(location(), parameters);
    }

    template<typename Visitor>
    void TypeInstance::visit(Visitor& v) {
      visit_base<Functional>(v);
      v("generic", &TypeInstance::generic)
      ("parameters", &TypeInstance::parameters);
    }
    
    TermResultInfo TypeInstance::check_type_impl(const TypeInstance& self) {
      if (self.parameters.size() != self.generic->pattern.size())
        self.compile_context().error_throw(self.location(), "Wrong number of parameters to generic");
      
      for (std::size_t ii = 0, ie = self.parameters.size(); ii != ie; ++ii) {
        if (self.parameters[ii]->type != self.generic->pattern[ii]->specialize(self.location(), self.parameters))
          self.compile_context().error_throw(self.location(), "Incorrect parameter type to generic instance");
      }
      
      return term_result_type(self.compile_context());
    }
    
    TermTypeInfo TypeInstance::type_info_impl(const TypeInstance& self) {
      TermTypeInfo ti = self.unwrap()->type_info();
      if (self.generic->primitive_mode == GenericType::primitive_never)
        ti.type_mode = type_mode_complex;
      return ti;
    }

    const FunctionalVtable TypeInstance::vtable = PSI_COMPILER_FUNCTIONAL(TypeInstance, "psi.compiler.TypeInstance", Type);

    TypeInstanceValue::TypeInstanceValue(const TreePtr<TypeInstance>& type, const TreePtr<Term>& member_value_)
    : Constructor(&vtable),
    type_instance(type),
    member_value(member_value_) {
    }

    template<typename Visitor>
    void TypeInstanceValue::visit(Visitor& v) {
      visit_base<Constructor>(v);
      v("type_instance", &TypeInstanceValue::type_instance)
      ("member_value", &TypeInstanceValue::member_value);
    }
    
    TermResultInfo TypeInstanceValue::check_type_impl(const TypeInstanceValue& self) {
      if (self.type_instance->generic->primitive_mode == GenericType::primitive_never)
        self.compile_context().error_throw(self.location(), "Cannot construct complex generic type with non-default value");
      
      if (!self.type_instance->unwrap()->convert_match(self.member_value->type))
        self.compile_context().error_throw(self.location(), "Generic instance value has the wrong type");
      
      return TermResultInfo(self.type_instance, term_mode_value, self.member_value->pure);
    }
    
    TermTypeInfo TypeInstanceValue::type_info_impl(const TypeInstanceValue& self) {
      return self.member_value->type_info();
    }

    const FunctionalVtable TypeInstanceValue::vtable = PSI_COMPILER_FUNCTIONAL(TypeInstanceValue, "psi.compiler.TypeInstanceValue", Constructor);

    IfThenElse::IfThenElse(const TreePtr<Term>& condition_, const TreePtr<Term>& true_value_, const TreePtr<Term>& false_value_)
    : Functional(&vtable),
    condition(condition_),
    true_value(true_value_),
    false_value(false_value_) {
    }
    
    TermResultInfo IfThenElse::check_type_impl(const IfThenElse& self) {
      if (self.condition->type != TermBuilder::boolean_type(self.compile_context()))
        self.compile_context().error_throw(self.location(), "Conditional value is not boolean");
      if (self.true_value->type != self.false_value->type)
        self.compile_context().error_throw(self.location(), "True and false values of conditional expression have different types");
      
      TermResultInfo tri = term_result_combine(self.true_value->result_info(), self.false_value->result_info(), self.location());
      tri.pure = tri.pure && self.condition->pure;
      return tri;
    }
    
    TermTypeInfo IfThenElse::type_info_impl(const IfThenElse& self) {
      return term_type_info_combine(self.true_value->type_info(), self.false_value->type_info());
    }
    
    template<typename V>
    void IfThenElse::visit(V& v) {
      visit_base<Functional>(v);
      v("condition", &IfThenElse::condition)
      ("true_value", &IfThenElse::true_value)
      ("false_value", &IfThenElse::false_value);
    }

    const FunctionalVtable IfThenElse::vtable = PSI_COMPILER_FUNCTIONAL(IfThenElse, "psi.compiler.IfThenElse", Functional);
    
    JumpTarget::JumpTarget(const TreePtr<Term>& value_, ResultMode argument_mode_, const TreePtr<Anonymous>& argument_, const SourceLocation& location)
    : Tree(&vtable, argument_->compile_context(), location),
    value(value_),
    argument_mode(argument_mode_),
    argument(argument_) {
      if (argument) {
        if ((argument_mode == result_mode_functional) && (argument->type_info().type_mode == type_mode_complex))
          compile_context().error_throw(location, "Cannot pass complex type in functional argument");
      }
    }
    
    template<typename V>
    void JumpTarget::visit(V& v) {
      visit_base<Tree>(v);
      v("value", &JumpTarget::value)
      ("argument", &JumpTarget::argument)
      ("argument_mode", &JumpTarget::argument_mode);
    }
    
    const TreeVtable JumpTarget::vtable = PSI_COMPILER_TREE(JumpTarget, "psi.compiler.JumpTarget", Tree);
    
    TermResultInfo JumpGroup::make_result_type(const TreePtr<Term>& initial, const PSI_STD::vector<TreePtr<JumpTarget> >& values, const SourceLocation& location) {
      TermResultInfo rt = initial->result_info();
      for (PSI_STD::vector<TreePtr<JumpTarget> >::const_iterator ii = values.begin(), ie = values.end(); ii != ie; ++ii)
        rt = term_result_combine(rt, (*ii)->value->result_info(), location);
      return rt;
    }
    
    TermTypeInfo JumpGroup::type_info_impl(const JumpGroup& self) {
      TermTypeInfo rt = self.initial->type_info();
      for (PSI_STD::vector<TreePtr<JumpTarget> >::const_iterator ii = self.entries.begin(), ie = self.entries.end(); ii != ie; ++ii)
        rt = term_type_info_combine(rt, (*ii)->value->type_info());
      return rt;
    }

    JumpGroup::JumpGroup(const TreePtr<Term>& initial_, const PSI_STD::vector<TreePtr<JumpTarget> >& entries_, const SourceLocation& location)
    : Term(&vtable, make_result_type(initial_, entries_, location), location),
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
    
    JumpTo::JumpTo(const TreePtr<JumpTarget>& target_, const TreePtr<Term>& argument_, const SourceLocation& location)
    : Term(&vtable, term_result_bottom(target_.compile_context()), location),
    target(target_),
    argument(argument_) {
      if (target->argument->type != argument->type)
        target.compile_context().error_throw(location, "Jump argument has the wrong type");
      
      if (target->argument_mode == result_mode_lvalue) {
        if ((argument->mode != term_mode_lref) && (argument->mode != term_mode_rref))
          target.compile_context().error_throw(location, "Cannot make reference to temporary in a jump");
      } else if (target->argument_mode == result_mode_rvalue) {
        if (argument->mode != term_mode_rref)
          target.compile_context().error_throw(location, "Cannot make rvalue reference to temporary or lvalue in a jump");
      }
    }
    
    template<typename V>
    void JumpTo::visit(V& v) {
      visit_base<Term>(v);
      v("target", &JumpTo::target)
      ("argument", &JumpTo::argument);
    }

    const TermVtable JumpTo::vtable = PSI_COMPILER_TERM(JumpTo, "psi.compiler.JumpTo", Term);

    /**
     * \param arguments_ Arguments to the function call. Note that this variable is destroyed by this constructor.
     */
    FunctionCall::FunctionCall(const TreePtr<Term>& target_, PSI_STD::vector<TreePtr<Term> >& arguments_, const SourceLocation& location)
    : Term(&vtable, get_result_type(target_, arguments_, location), location),
    target(target_) {
      arguments.swap(arguments_);
    }

    TermResultInfo FunctionCall::get_result_type(const TreePtr<Term>& target, PSI_STD::vector<TreePtr<Term> >& arguments, const SourceLocation& location) {
      TreePtr<FunctionType> ft = term_unwrap_dyn_cast<FunctionType>(target->type);
      if (!ft)
        target.compile_context().error_throw(location, "Target of function call does not have function type");
      
      if (target->mode != term_mode_lref)
        target.compile_context().error_throw(location, "Function call target is a function but not a reference", CompileError::error_internal);
      
      if (ft->parameter_types.size() != arguments.size())
        target.compile_context().error_throw(location, "Function call has the wrong number of parameters");
      
      for (std::size_t ii = 0, ie = arguments.size(); ii != ie; ++ii) {
        if ((ft->parameter_types[ii].mode == parameter_mode_functional) || (ft->parameter_types[ii].mode == parameter_mode_phantom))
          arguments[ii] = TermBuilder::to_functional(arguments[ii], location);
      }
      
      TreePtr<Term> type = ft->result_type_after(location, arguments);

      TermMode mode;
      if (type->type_info().type_mode != type_mode_bottom) {
        switch (ft->result_mode) {
        case result_mode_by_value:
        case result_mode_functional: mode = term_mode_value; break;
        case result_mode_lvalue: mode = term_mode_lref; break;
        case result_mode_rvalue: mode = term_mode_rref; break;
        default: PSI_FAIL("Unrecognised result mode");
        }
      } else {
        mode = term_mode_bottom;
      }

      return TermResultInfo(type, mode, false);
    }
    
    template<typename Visitor>
    void FunctionCall::visit(Visitor& v) {
      visit_base<Term>(v);
      v("target", &FunctionCall::target)
      ("arguments", &FunctionCall::arguments);
    }
    
    TermTypeInfo FunctionCall::type_info_impl(const FunctionCall& self) {
      TermTypeInfo rt;
      rt.type_mode = self.type->is_type() ? type_mode_complex : type_mode_none;
      return rt;
    }
    
    const TermVtable FunctionCall::vtable = PSI_COMPILER_TERM(FunctionCall, "psi.compiler.FunctionCall", Term);
    
    SolidifyDuring::SolidifyDuring(const PSI_STD::vector<TreePtr<Term> >& value_, const TreePtr<Term>& body_, const SourceLocation& location)
    : Term(&vtable, body_->result_info(), location),
    value(value_),
    body(body_) {
    }
    
    template<typename V>
    void SolidifyDuring::visit(V& v) {
      visit_base<Term>(v);
      v("value", &SolidifyDuring::value)
      ("body", &SolidifyDuring::body);
    }
    
    TermTypeInfo SolidifyDuring::type_info_impl(const SolidifyDuring& self) {
      return self.body->type_info();
    }
    
    const TermVtable SolidifyDuring::vtable = PSI_COMPILER_TERM(SolidifyDuring, "psi.compiler.SolidifyDuring", Term);
    
    /// \brief Returns true if the key is a valid number type
    bool NumberType::is_number(unsigned key) {
      switch (key) {
      case n_bool:
      case n_i8: case n_i16: case n_i32: case n_i64: case n_iptr:
      case n_u8: case n_u16: case n_u32: case n_u64: case n_uptr:
      case n_f32: case n_f64:
        return true;
        
      default:
        return false;
      }
    }
    
    /// \brief Returns true if the key is an integer type
    bool NumberType::is_integer(unsigned key) {
      switch (key) {
      case n_i8: case n_i16: case n_i32: case n_i64: case n_iptr:
      case n_u8: case n_u16: case n_u32: case n_u64: case n_uptr:
        return true;
        
      default:
        return false;
      }
    }
    
    /// \brief Returns true if the key is a signed integer type
    bool NumberType::is_signed(unsigned key) {
      switch (key) {
      case n_i8: case n_i16: case n_i32: case n_i64: case n_iptr:
        return true;
        
      default:
        return false;
      }
    }
    
    NumberType::NumberType(ScalarType scalar_type_, unsigned vector_size_)
    : Type(&vtable),
    scalar_type(scalar_type_),
    vector_size(vector_size_) {
    }
    
    template<typename Visitor> void NumberType::visit(Visitor& v) {
      visit_base<Type>(v);
      v("scalar_type", &NumberType::scalar_type)
      ("vector_size", &NumberType::vector_size);
    }
    
    TermResultInfo NumberType::check_type_impl(const NumberType& self) {
      if (!is_number(self.scalar_type))
        self.compile_context().error_throw(self.location(), boost::format("%d is not a valid number type") % self.scalar_type);
      return term_result_type(self.compile_context());
    }
    
    TermTypeInfo NumberType::type_info_impl(const NumberType&) {
      TermTypeInfo rt;
      rt.type_fixed_size = true;
      rt.type_mode = type_mode_primitive;
      return rt;
    }

    const FunctionalVtable NumberType::vtable = PSI_COMPILER_FUNCTIONAL(NumberType, "psi.compiler.NumberType", Type);

    IntegerConstant::IntegerConstant(NumberType::ScalarType number_type_, uint64_t value_)
    : Constant(&vtable),
    number_type(number_type_),
    value(value_) {
    }
    
    template<typename V>
    void IntegerConstant::visit(V& v) {
      visit_base<Constant>(v);
      v("number_type", &IntegerConstant::number_type)
      ("value", &IntegerConstant::value);
    }
    
    TermResultInfo IntegerConstant::check_type_impl(const IntegerConstant& self) {
      if (!NumberType::is_integer(self.number_type) && (self.number_type != NumberType::n_bool))
        self.compile_context().error_throw(self.location(), boost::format("Number type %d is not an integer type") % self.number_type);

      return TermResultInfo(TermBuilder::number_type(self.compile_context(), (NumberType::ScalarType)self.number_type), term_mode_value, true);
    }
    
    TermTypeInfo IntegerConstant::type_info_impl(const IntegerConstant&) {
      TermTypeInfo rt;
      rt.type_mode = type_mode_none;
      return rt;
    }
    
    const FunctionalVtable IntegerConstant::vtable = PSI_COMPILER_FUNCTIONAL(IntegerConstant, "psi.compiler.IntegerConstant", Constant);

    StringValue::StringValue(const String& value_)
    : Constant(&vtable),
    value(value_) {
    }
    
    template<typename V>
    void StringValue::visit(V& v) {
      visit_base<Constant>(v);
      v("value", &StringValue::value);
    }
    
    TermResultInfo StringValue::check_type_impl(const StringValue& self) {
      return TermResultInfo(TermBuilder::string_type(self.value.length()+1, self.compile_context(), self.location()), term_mode_value, true);
    }
    
    TermTypeInfo StringValue::type_info_impl(const StringValue&) {
      TermTypeInfo rt;
      rt.type_mode = type_mode_none;
      return rt;
    }
    
    const FunctionalVtable StringValue::vtable = PSI_COMPILER_FUNCTIONAL(StringValue, "psi.compiler.StringValue", Constant);
    
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
    
    Module::Module(CompileContext& compile_context, const String& name_, const SourceLocation& location)
    : Tree(&vtable, compile_context, location),
    name(name_) {
    }

    TreePtr<Module> Module::new_(CompileContext& compile_context, const String& name, const SourceLocation& location) {
      return tree_from(::new Module(compile_context, name, location));
    }
    
    template<typename V>
    void Module::visit(V& v) {
      visit_base<Tree>(v);
      v("name", &Module::name);
    }
    
    const TreeVtable Module::vtable = PSI_COMPILER_TREE(Module, "psi.compiler.Module", Tree);
    
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
    
    Namespace::Namespace(CompileContext& compile_context, const PSI_STD::map<String, TreePtr<Term> >& members_, const SourceLocation& location)
    : Tree(&vtable, compile_context, location),
    members(members_) {
    }

    TreePtr<Namespace> Namespace::new_(CompileContext& compile_context, const NameMapType& members, const SourceLocation& location) {
      return tree_from(::new Namespace(compile_context, members, location));
    }
    
    template<typename V>
    void Namespace::visit(V& v) {
      visit_base<Tree>(v);
      v("members", &Namespace::members);
    }
    
    const TreeVtable Namespace::vtable = PSI_COMPILER_TREE(Namespace, "psi.compiler.Namespace", Tree);
    
    InterfaceValue::InterfaceValue(const TreePtr<Interface>& interface_, const PSI_STD::vector<TreePtr<Term> >& parameters_,
                                   const TreePtr<Implementation>& implementation_)
    : Functional(&vtable),
    interface(interface_),
    parameters(parameters_),
    implementation(implementation_) {
    }
    
    template<typename V>
    void InterfaceValue::visit(V& v) {
      visit_base<Term>(v);
      v("interface", &InterfaceValue::interface)
      ("parameters", &InterfaceValue::parameters)
      ("implementation", &InterfaceValue::implementation);
    }
    
    TermResultInfo InterfaceValue::check_type_impl(const InterfaceValue& self) {
      return TermResultInfo(self.interface->type_after(self.parameters, self.location()), term_mode_lref, true);
    }
    
    TermTypeInfo InterfaceValue::type_info_impl(const InterfaceValue&) {
      TermTypeInfo rt;
      rt.type_mode = type_mode_none;
      return rt;
    }
    
    const FunctionalVtable InterfaceValue::vtable = PSI_COMPILER_FUNCTIONAL(InterfaceValue, "psi.compiler.InterfaceValue", Functional);
    
    MovableValue::MovableValue(const TreePtr<Term>& value_)
    : Functional(&vtable),
    value(value_) {
    }
    
    TermResultInfo MovableValue::check_type_impl(const MovableValue& self) {
      TermResultInfo tri = self.value->result_info();
      if (tri.mode != term_mode_lref)
        self.compile_context().error_throw(self.location(), "MovableValue should only be used on lvalue references");
      tri.mode = term_mode_rref;
      return tri;
    }
    
    TermTypeInfo MovableValue::type_info_impl(const MovableValue& self) {
      return self.value->type_info();
    }
    
    template<typename V>
    void MovableValue::visit(V& v) {
      visit_base<Term>(v);
      v("value", &MovableValue::value);
    }
    
    const FunctionalVtable MovableValue::vtable = PSI_COMPILER_FUNCTIONAL(MovableValue, "psi.compiler.MovableValue", Functional);
    
    InitializeValue::InitializeValue(const TreePtr<Term>& target_ref_, const TreePtr<Term>& assign_value_, const TreePtr<Term>& inner_, const SourceLocation& location)
    : Term(&vtable, TermResultInfo(inner_->type, inner_->mode, false), location),
    target_ref(target_ref_),
    assign_value(assign_value_),
    inner(inner_) {
      if ((target_ref->mode != term_mode_lref) && (target_ref->mode != term_mode_rref))
        compile_context().error_throw(location, "Initialization target is not a reference");
      if (!target_ref->type->convert_match(assign_value_->type))
        compile_context().error_throw(location, "Initialization value type does not match target storage");
    }
    
    template<typename V>
    void InitializeValue::visit(V& v) {
      visit_base<Term>(v);
      v("target_ref", &InitializeValue::target_ref)
      ("assign_value", &InitializeValue::assign_value)
      ("inner", &InitializeValue::inner);
    }
    
    TermTypeInfo InitializeValue::type_info_impl(const InitializeValue& self) {
      return self.inner->type_info();
    }
    
    const TermVtable InitializeValue::vtable = PSI_COMPILER_TERM(InitializeValue, "psi.compiler.InitializeValue", Term);
    
    AssignValue::AssignValue(const TreePtr<Term>& target_ref_, const TreePtr<Term>& assign_value_, const SourceLocation& location)
    : Term(&vtable, term_result_void(target_ref_.compile_context()), location),
    target_ref(target_ref_),
    assign_value(assign_value_) {
      if ((target_ref->mode != term_mode_lref) && (target_ref->mode != term_mode_rref))
        compile_context().error_throw(location, "Finalize target is not a reference");
      if (!target_ref->type->convert_match(assign_value_->type))
        compile_context().error_throw(location, "Assignment value type does not match target storage");
    }
    
    template<typename V>
    void AssignValue::visit(V& v) {
      visit_base<Term>(v);
      v("target_ref", &AssignValue::target_ref)
      ("assign_value", &AssignValue::assign_value);
    }

    const TermVtable AssignValue::vtable = PSI_COMPILER_TERM(AssignValue, "psi.compiler.AssignValue", Term);
    
    FinalizeValue::FinalizeValue(const TreePtr<Term>& target_ref_, const SourceLocation& location)
    : Term(&vtable, term_result_void(target_ref_.compile_context()), location),
    target_ref(target_ref_) {
      if ((target_ref->mode != term_mode_lref) && (target_ref->mode != term_mode_rref))
        compile_context().error_throw(location, "Finalize target is not a reference");
    }
    
    template<typename V>
    void FinalizeValue::visit(V& v) {
      visit_base<Term>(v);
      v("target_ref", &FinalizeValue::target_ref);
    }
    
    const TermVtable FinalizeValue::vtable = PSI_COMPILER_TERM(FinalizeValue, "psi.compiler.FinalizeValue", Term);
    
    IntroduceImplementation::IntroduceImplementation(const PSI_STD::vector<TreePtr<Implementation> >& implementations_, const TreePtr<Term>& value_, const SourceLocation& location)
    : Term(&vtable, TermResultInfo(value_->type, value_->mode, false), location),
    implementations(implementations_),
    value(value_) {
    }
    
    template<typename V>
    void IntroduceImplementation::visit(V& v) {
      visit_base<Term>(v);
      v("implementations", &IntroduceImplementation::implementations)
      ("value", &IntroduceImplementation::value);
    }
    
    TermTypeInfo IntroduceImplementation::type_info_impl(const IntroduceImplementation& self) {
      return self.value->type_info();
    }
    
    const TermVtable IntroduceImplementation::vtable = PSI_COMPILER_TERM(IntroduceImplementation, "psi.compiler.IntroduceImplementation", Term);
    
    FunctionalEvaluate::FunctionalEvaluate(const TreePtr<Term>& value_, const SourceLocation& location)
    : Term(&vtable, TermResultInfo(value_->type, term_mode_value, true), location),
    value(value_) {
      if (value->type && !value->type->is_functional())
        compile_context().error_throw(location, "Argument to FunctionalEvaluate does not have functional type", CompileError::error_internal);
      if (value->pure && (value->mode == term_mode_value))
        compile_context().error_throw(location, "Already functional terms should not be wrapped in FunctionalEvaluate", CompileError::error_internal);
    }
    
    template<typename V>
    void FunctionalEvaluate::visit(V& v) {
      visit_base<Term>(v);
      v("value", &FunctionalEvaluate::value);
    }
    
    TermTypeInfo FunctionalEvaluate::type_info_impl(const FunctionalEvaluate& self) {
      TermTypeInfo rt = self.value->type_info();
      PSI_ASSERT(!rt.type_fixed_size);
      return rt;
    }
    
    const TermVtable FunctionalEvaluate::vtable = PSI_COMPILER_TERM(FunctionalEvaluate, "psi.compiler.FunctionalEvaluate", Term);
    
    GlobalEvaluate::GlobalEvaluate(const TreePtr<Module>& module, const TreePtr<Term>& value_, const SourceLocation& location)
    : ModuleGlobal(&vtable, module, TermResultInfo(value->type, term_mode_value, true), link_none, location), value(value_) {
      if (value->type && !value->type->is_functional())
        compile_context().error_throw(location, "Argument to GlobalEvaluate does not have functional type", CompileError::error_internal);
      if (value->pure && (value->mode == term_mode_value))
        compile_context().error_throw(location, "Already functional terms should not be wrapped in GlobalEvaluate", CompileError::error_internal);
    }
    
    template<typename V>
    void GlobalEvaluate::visit(V& v) {
      visit_base<Term>(v);
      v("value", &GlobalEvaluate::value);
    }
    
    TermTypeInfo GlobalEvaluate::type_info_impl(const GlobalEvaluate& self) {
      TermTypeInfo rt = self.value->type_info();
      PSI_ASSERT(!rt.type_fixed_size);
      return rt;
    }
    
    const TermVtable GlobalEvaluate::vtable = PSI_COMPILER_TERM(GlobalEvaluate, "psi.compiler.GlobalEvaluate", Term);
  }
}
