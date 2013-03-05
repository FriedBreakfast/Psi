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
    
    TermResultType result_type_combine(const TermResultType& lhs, const TermResultType& rhs, CompileContext& compile_context, const SourceLocation& location) {
      if (lhs.mode == term_mode_bottom)
        return rhs;
      else if (rhs.mode == term_mode_bottom)
        return lhs;
      
      TermResultType rs;
      if (lhs.type != rhs.type)
        compile_context.error_throw(location, "Cannot merge distinct result types");
      PSI_ASSERT(lhs.type_mode == rhs.type_mode);
      rs.type = lhs.type;
      rs.type_mode = lhs.type_mode;
      rs.mode = term_mode_combine(lhs.mode, rhs.mode);
      rs.pure = lhs.pure && rhs.pure;
      return rs;
    }
    
    /**
     * \brief Get the appropriate result type for terms which have no value.
     */
    TermResultType result_type_void(CompileContext& compile_context) {
      TermResultType rs;
      rs.type = TermBuilder::empty_type(compile_context);
      rs.type_mode = type_mode_none;
      rs.mode = term_mode_value;
      rs.pure = false;
      return rs;
    }
    
    TermResultType result_type_bottom(CompileContext& compile_context, bool pure) {
      TermResultType rs;
      rs.type = TermBuilder::bottom_type(compile_context);
      rs.type_mode = type_mode_bottom;
      rs.mode = term_mode_bottom;
      rs.pure = pure;
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
    
    template<typename V>
    void Functional::visit(V& v) {
      visit_base<Term>(v);
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

    TermResultType GlobalStatement::make_result_type(const TreePtr<Term>& value, StatementMode mode) {
      TermResultType rt;
      rt.type = value->result_type.type;
      PSI_NOT_IMPLEMENTED();
      return rt;
    }
    
    GlobalStatement::GlobalStatement(const TreePtr<Module>& module, const TreePtr<Term>& value_, StatementMode mode_, const SourceLocation& location)
    : ModuleGlobal(&vtable, module, make_result_type(value_, mode_), false, location),
    value(value_),
    mode(mode_) {
    }

    template<typename V>
    void GlobalStatement::visit(V& v) {
      visit_base<ModuleGlobal>(v);
      v("value", &GlobalStatement::value)
      ("mode", &GlobalStatement::mode);
    }

    const TermVtable GlobalStatement::vtable = PSI_COMPILER_TERM(GlobalStatement, "psi.compiler.GlobalStatement", ModuleGlobal);
    
    /**
     * \brief Utility function to construct result type in a way common to most global types.
     */
    TermResultType Global::global_result_type(const TreePtr<Term>& type) {
      TermResultType rt;
      rt.type = type;
      rt.pure = true;
      rt.mode = term_mode_lref;
      rt.type_mode = type->result_type.type_mode;
      return rt;
    }

    Global::Global(const VtableType *vptr, const TermResultType& type, const SourceLocation& location)
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

    ModuleGlobal::ModuleGlobal(const VtableType *vptr, const TreePtr<Module>& module_, const TermResultType& type, PsiBool local_, const SourceLocation& location)
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

    const SIVtable ModuleGlobal::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.ModuleGlobal", Global);

    ExternalGlobal::ExternalGlobal(const TreePtr<Module>& module, const TreePtr<Term>& type, const SourceLocation& location)
    : ModuleGlobal(&vtable, module, global_result_type(type), false, location) {
    }
    
    template<typename V>
    void ExternalGlobal::visit(V& v) {
      visit_base<ModuleGlobal>(v);
    }
    
    const TermVtable ExternalGlobal::vtable = PSI_COMPILER_TERM(ExternalGlobal, "psi.compiler.ExternalGlobal", ModuleGlobal);
    
    template<typename V>
    void GlobalVariable::visit(V& v) {
      visit_base<ModuleGlobal>(v);
      v("value", &GlobalVariable::m_value)
      ("constant", &GlobalVariable::constant)
      ("merge", &GlobalVariable::merge);
    }
    
    const TermVtable GlobalVariable::vtable = PSI_COMPILER_TERM(GlobalVariable, "psi.compiler.GlobalVariable", ModuleGlobal);
    
    ParameterizedType::ParameterizedType(const VtableType  *vptr)
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
    
    TermResultType Exists::check_type_impl(const Exists& self) {
      if (!self.result->is_type())
        self.compile_context().error_throw(self.location(), "Result of exists is not a type");
      
      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = self.parameter_types.begin(), ie = self.parameter_types.end(); ii != ie; ++ii) {
        if (!(*ii)->is_primitive_type())
          self.compile_context().error_throw(self.location(), "Parameter type of exists term is not a primitive type");
      }
      
      TermResultType rt;
      rt.pure = true;
      rt.mode = term_mode_value;
      rt.type = TermBuilder::metatype(self.compile_context());
      rt.type_fixed_size = self.result->result_type.type_fixed_size;
      rt.type_mode = self.result->result_type.type_mode;
      return rt;
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
                               const PSI_STD::vector<TreePtr<InterfaceValue> >& interfaces_)
    : ParameterizedType(&vtable),
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
    
    TermResultType FunctionType::check_type_impl(const FunctionType& self) {
      // Doesn't currently check that parameters are correctly ordered
      for (PSI_STD::vector<FunctionParameterType>::const_iterator ii = self.parameter_types.begin(), ie = self.parameter_types.end(); ii != ie; ++ii) {
        const TermResultType& rt = ii->type->result_type;
        if ((rt.type_mode == type_mode_none) || !rt.pure) {
          self.compile_context().error_throw(self.location(), "Function parameter types must be pure types");
        } else if (rt.type_mode == type_mode_complex) {
          if (ii->mode == parameter_mode_functional)
            self.compile_context().error_throw(self.location(), "Cannot pass complex types in functional argument");
          else if (ii->mode == parameter_mode_phantom)
            self.compile_context().error_throw(self.location(), "It does not make sense to pass complex types in phantom arguments");
        }
      }
      
      const TermResultType& rrt = self.result_type->result_type;
      if ((rrt.type_mode == type_mode_none) || !rrt.pure)
        self.compile_context().error_throw(self.location(), "Function result types must be pure types");
      else if ((rrt.type_mode == type_mode_complex) && (self.result_mode == result_mode_functional))
        self.compile_context().error_throw(self.location(), "Cannot return complex types functionally");

      TermResultType result;
      result.type = TermBuilder::metatype(self.compile_context());
      result.pure = true;
      result.mode = term_mode_value;
      // Function types are effectively complex because function values cannot be dynamically constructed at all!
      result.type_mode = type_mode_complex;
      return result;
    }

    const FunctionalVtable FunctionType::vtable = PSI_COMPILER_FUNCTIONAL(FunctionType, "psi.compiler.FunctionType", ParameterizedType);
    
    void Function::check_type() {
      TreePtr<FunctionType> ftype = treeptr_cast<FunctionType>(result_type.type);
      
      if (arguments.size() != ftype->parameter_types.size())
        compile_context().error_throw(location(), "Number of arguments to function does not match the function signature");
      
      PSI_STD::vector<TreePtr<Term> > my_arguments;
      my_arguments.reserve(arguments.size());
      for (std::size_t ii = 0, ie = arguments.size(); ii != ie; ++ii) {
        TreePtr<Term> type = ftype->parameter_type_after(arguments[ii]->location(), my_arguments);
        if (arguments[ii]->result_type.type != type)
          compile_context().error_throw(arguments[ii]->location(), "Parameter type to function does not match the function signature");
        my_arguments.push_back(arguments[ii]);
      }
      
      if (return_target) {
        if (return_target->argument_mode != ftype->result_mode)
          compile_context().error_throw(location(), "Return target mode does not match result mode of function");
        if (return_target->argument->result_type.type != ftype->result_type_after(location(), my_arguments))
          compile_context().error_throw(location(), "Return target mode does not match result mode of function");
      }
    }

    template<typename Visitor> void Function::visit(Visitor& v) {
      visit_base<ModuleGlobal>(v);
      v("arguments", &Function::arguments)
      ("body", &Function::m_body)
      ("return_target", &Function::return_target);
    }
    
    const TermVtable Function::vtable = PSI_COMPILER_TERM(Function, "psi.compiler.Function", ModuleGlobal);

    TryFinally::TryFinally(const TreePtr<Term>& try_expr_, const TreePtr<Term>& finally_expr_, bool except_only_, const SourceLocation& location)
    : Term(&vtable, try_expr_->result_type, location),
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

    const TermVtable TryFinally::vtable = PSI_COMPILER_TERM(TryFinally, "psi.compiler.TryFinally", Term);
    
    TermResultType Statement::make_result_type(StatementMode mode, const TreePtr<Term>& value, const SourceLocation& location) {
      const TermResultType& value_rt = value->result_type;
      
      TermResultType rt;
      rt.pure = true;
      rt.mode = term_mode_lref;
      rt.type_mode = value_rt.type_mode;
      rt.type_fixed_size = value_rt.type_fixed_size;
      rt.type = value_rt.type;
      
      switch (mode) {
      case statement_mode_value:
        break;
        
      case statement_mode_functional:
        if (value_rt.type) {
          const TermResultType& type_rt = value_rt.type->result_type;
          PSI_ASSERT(type_rt.type_mode != type_mode_none);
          if (type_rt.type_mode == type_mode_complex) {
            CompileError err(value.compile_context(), location);
            err.info(location, "Only primitive types can be used as functional values");
            err.info(value_rt.type.location(), "Type is not primitive");
            err.end();
            throw CompileException();
          }
        }
        
        rt.mode = term_mode_value;
        rt.type_mode = value_rt.type_mode;
        break;
        
      case statement_mode_ref:
        if ((value_rt.mode != term_mode_lref) && (value_rt.mode != term_mode_rref))
          value.compile_context().error_throw(location, "Cannot bind temporary to reference");
        break;
        
      case statement_mode_destroy:
        // Result cannot be re-used
        rt.mode = term_mode_bottom;
        rt.type = TermBuilder::bottom_type(value.compile_context());
        break;
        
      default: PSI_FAIL("Unknown statement mode");
      }
      
      return rt;
    }

    Statement::Statement(const TreePtr<Term>& value_, StatementMode mode_, const SourceLocation& location)
    : Term(&vtable, value_.compile_context(), make_result_type(mode_, value_, location), location),
    value(value_),
    mode(mode_) {
    }

    template<typename Visitor>
    void Statement::visit(Visitor& v) {
      visit_base<Tree>(v);
      v("value", &Statement::value)
      ("mode", &Statement::mode);
    }

    const TermVtable Statement::vtable = PSI_COMPILER_TERM(Statement, "psi.compiler.Statement", Term);
    
    Block::Block(const PSI_STD::vector<TreePtr<Statement> >& statements_, const TreePtr<Term>& value_, const SourceLocation& location)
    : Term(&vtable, value_->result_type, location),
    statements(statements_),
    value(value_) {
    }

    template<typename Visitor>
    void Block::visit(Visitor& v) {
      visit_base<Term>(v);
      v("statements", &Block::statements)
      ("value", &Block::value);
    }

    const TermVtable Block::vtable = PSI_COMPILER_TERM(Block, "psi.compiler.Block", Term);

    BottomType::BottomType()
    : Type(&vtable) {
    }
    
    template<typename V>
    void BottomType::visit(V& v) {
      visit_base<Type>(v);
    }
    
    TermResultType BottomType::check_type_impl(const BottomType& self) {
      TermResultType rt;
      rt.mode = term_mode_value;
      rt.type_mode = type_mode_bottom;
      rt.type = TermBuilder::metatype(self.compile_context());
      rt.pure = true;
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
    
    TermResultType ConstantType::check_type_impl(const ConstantType& self) {
      TermResultType rt;
      rt.pure = true;
      rt.type_mode = type_mode_primitive;
      rt.mode = term_mode_value;
      rt.type = TermBuilder::metatype(self.compile_context());
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
    
    TermResultType EmptyType::check_type_impl(const EmptyType& self) {
      TermResultType rt;
      rt.type = TermBuilder::metatype(self.compile_context());
      rt.mode = term_mode_value;
      rt.type_mode = type_mode_primitive;
      rt.pure = true;
      rt.type_fixed_size = true;
      return rt;
    }

    const FunctionalVtable EmptyType::vtable = PSI_COMPILER_FUNCTIONAL(EmptyType, "psi.compiler.EmptyType", Type);

    DefaultValue::DefaultValue(const TreePtr<Term>& type, const SourceLocation& location)
    : Constructor(&vtable),
    value_type(type) {
      if (!value_type->result_type.pure)
        value_type = TermBuilder::functional_eval(value_type, location);
    }
    
    TermResultType DefaultValue::check_type_impl(const DefaultValue& self) {
      PSI_ASSERT(self.value_type->result_type.pure || tree_isa<FunctionalEvaluate>(self.value_type));
      if (self.value_type->result_type.type_mode == type_mode_none)
        self.compile_context().error_throw(self.location(), "Type for default value is not a type");
      if (self.value_type->result_type.type_mode == type_mode_bottom)
        self.compile_context().error_throw(self.location(), "Cannot create default value of bottom type");
      TermResultType rt;
      rt.mode = term_mode_value;
      rt.pure = (self.value_type->result_type.type_mode != type_mode_complex) && self.value_type->result_type.pure;
      rt.type_mode = (self.value_type->result_type.type_mode == type_mode_metatype) ? type_mode_complex : type_mode_none;
      rt.type = self.value_type;
      return rt;
    }
    
    template<typename V>
    void DefaultValue::visit(V& v) {
      visit_base<Constructor>(v);
      v("value_type", &DefaultValue::value_type);
    }

    const FunctionalVtable DefaultValue::vtable = PSI_COMPILER_FUNCTIONAL(DefaultValue, "psi.compiler.DefaultValue", Constructor);

    PointerType::PointerType(const TreePtr<Term>& target_type_, const SourceLocation& location)
    : Type(&vtable),
    target_type(TermBuilder::to_functional(target_type_, location)) {
    }
    
    template<typename V>
    void PointerType::visit(V& v) {
      visit_base<Type>(v);
      v("target_type", &PointerType::target_type);
    }
    
    TermResultType PointerType::check_type_impl(const PointerType& self) {
      TermResultType rt;
      rt.type_fixed_size = true;
      rt.pure = true;
      rt.mode = term_mode_value;
      rt.type_mode = type_mode_primitive;
      rt.type = TermBuilder::metatype(self.compile_context());
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
    
    TermResultType PointerTo::check_type_impl(const PointerTo& self) {
      PSI_ASSERT(!self.value->result_type.type || self.value->result_type.type->result_type.pure);
      
      if ((self.value->result_type.mode != term_mode_lref) && (self.value->result_type.mode != term_mode_rref))
        self.compile_context().error_throw(self.location(), "Cannot take address of temporary variable");
      
      TermResultType rt;
      rt.type = TermBuilder::ptr_to(self.value->result_type.type, self.location());
      rt.pure = self.value->result_type.pure;
      rt.type_mode = type_mode_none;
      rt.mode = term_mode_value;
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
    
    TermResultType PointerTarget::check_type_impl(const PointerTarget& self) {
      TreePtr<PointerType> ptr_ty = dyn_treeptr_cast<PointerType>(self.value->result_type.type);
      if (!ptr_ty)
        self.compile_context().error_throw(self.location(), "Argument to PointerTarget is not a pointer");
      
      TermResultType rt;
      rt.pure = self.value->result_type.pure;
      rt.mode = term_mode_lref;
      rt.type = ptr_ty->target_type;
      rt.type_mode = (ptr_ty->target_type->result_type.type_mode == type_mode_metatype) ? type_mode_complex : type_mode_none;
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
    
    TermResultType PointerCast::check_type_impl(const PointerCast& self) {
      if (!tree_isa<PointerType>(self.value->result_type.type))
        self.compile_context().error_throw(self.location(), "Argument to PointerCast is not a pointer");
      
      TermResultType rt;
      rt.pure = self.value->result_type.pure;
      rt.mode = term_mode_value;
      rt.type = TermBuilder::ptr_to(self.target_type, self.location());
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
      
      if (TreePtr<StructType> st = dyn_treeptr_cast<StructType>(aggregate)) {
        unsigned index_int = TermBuilder::size_from(index, location);
        if (index_int >= st->members.size())
          compile_context.error_throw(location, "Structure member index out of range");
        return st->members[index_int];
      } else if (TreePtr<UnionType> un = dyn_treeptr_cast<UnionType>(aggregate)) {
        unsigned index_int = TermBuilder::size_from(index, location);
        if (index_int >= un->members.size())
          compile_context.error_throw(location, "Union member index out of range");
        return un->members[index_int];
      } else if (TreePtr<ArrayType> ar = dyn_treeptr_cast<ArrayType>(aggregate)) {
        return ar->element_type;
      } else if (TreePtr<TypeInstance> inst = dyn_treeptr_cast<TypeInstance>(aggregate)) {
        unsigned index_int = TermBuilder::size_from(index, location);
        if (index_int != 0)
          compile_context.error_throw(location, "Generic instance member index must be zero");
        return inst->unwrap();
      } else {
        CompileError err(compile_context, location);
        err.info("Element lookup argument is not an aggregate type");
        err.info(aggregate.location(), "Type of aggregate");
        err.end();
        throw CompileException();
      }
    }
    
    TermResultType ElementValue::check_type_impl(const ElementValue& self) {
      TreePtr<DerivedType> derived = dyn_treeptr_cast<DerivedType>(self.value->result_type.type);
      TreePtr<Term> my_aggregate_type, next_upref;
      if (derived) {
        my_aggregate_type = derived->value_type;
        next_upref = derived->upref;
      } else {
        my_aggregate_type = self.value->result_type.type;
      }
      
      TreePtr<Term> upref = TermBuilder::upref(my_aggregate_type, self.index, next_upref, self.location());

      TermResultType rt;
      rt.type = TermBuilder::derived(element_type(my_aggregate_type, self.index, self.location()), upref, self.location());
      rt.mode = term_mode_lref;
      rt.pure = self.value->result_type.pure && self.index->result_type.pure;
      rt.type_mode = type_mode_none;
      rt.type_fixed_size = rt.type->result_type.type_fixed_size;
      
      return rt;
    }

    const FunctionalVtable ElementValue::vtable = PSI_COMPILER_FUNCTIONAL(ElementValue, "psi.compiler.ElementValue", Functional);
    
    OuterValue::OuterValue(const TreePtr<Term>& value_)
    : Functional(&vtable),
    value(value_) {
    }
    
    template<typename V>
    void OuterValue::visit(V& v) {
      visit_base<Functional>(v);
      v("value", &OuterValue::value);
    }
    
    TermResultType OuterValue::check_type_impl(const OuterValue& self) {
      TreePtr<DerivedType> derived = dyn_treeptr_cast<DerivedType>(self.value->result_type.type);
      if (!derived)
        self.compile_context().error_throw(self.location(), "Outer value operation called on value with no upward reference");
      
      TreePtr<UpwardReference> upref = dyn_treeptr_cast<UpwardReference>(derived->upref);
      if (!upref)
        self.compile_context().error_throw(self.location(), "Outer value operation called on value with unknown upward reference");
      
      PSI_ASSERT((self.value->result_type.mode == term_mode_lref) && (self.value->result_type.mode == term_mode_rref));
      
      TermResultType rt;
      rt.type = TermBuilder::derived(upref->outer_type, upref->next, self.location());
      rt.type_mode = type_mode_none;
      rt.mode = term_mode_lref;
      rt.pure = self.value->result_type.pure;
      return rt;
    }
    
    const FunctionalVtable OuterValue::vtable = PSI_COMPILER_FUNCTIONAL(OuterValue, "psi.compiler.OuterValue", Functional);
    
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
    
    TermResultType StructType::check_type_impl(const StructType& self) {
      TermResultType rt;
      rt.type = TermBuilder::metatype(self.compile_context());
      rt.mode = term_mode_value;
      rt.type_mode = type_mode_primitive;
      rt.type_fixed_size = true;
      rt.pure = true;
      
      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = self.members.begin(), ie = self.members.end(); ii != ie; ++ii) {
        PSI_ASSERT((*ii)->result_type.pure);
        if (!(*ii)->result_type.type_fixed_size)
          rt.type_fixed_size = false;
        if (!(*ii)->is_type())
          self.compile_context().error_throw(self.location(), "Struct member is not a type");
        if (!(*ii)->is_primitive_type())
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
    
    TermResultType StructValue::check_type_impl(const StructValue& self) {
      if (self.members.size() != self.struct_type->members.size())
        self.compile_context().error_throw(self.location(), "Struct value has the wrong number of members according to its type");
      
      for (std::size_t ii = 0, ie = self.members.size(); ii != ie; ++ii) {
        if (self.members[ii]->result_type.type != self.struct_type->members[ii])
          self.compile_context().error_throw(self.location(), boost::format("Struct member %d has the wrong type") % ii);
      }
      
      TermResultType rt;
      rt.type = self.struct_type;
      rt.pure = true;
      rt.type_mode = type_mode_none;
      rt.mode = term_mode_value;
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
    TermResultType ArrayType::check_type_impl(const ArrayType& self) {
      PSI_ASSERT(self.element_type->result_type.pure && self.length->result_type.pure);
      
      if (!self.element_type->is_type())
        self.compile_context().error_throw(self.location(), "Array element type is not a type");
      if (self.length->result_type.type != TermBuilder::size_type(self.compile_context()))
        self.compile_context().error_throw(self.location(), "Array length is not a size");
      
      TermResultType rt;
      rt.type = TermBuilder::metatype(self.compile_context());
      rt.pure = true;
      rt.type_fixed_size = self.element_type->result_type.type_fixed_size && !tree_isa<IntegerValue>(self.length);
      rt.mode = term_mode_value;
      rt.type_mode = self.element_type->result_type.type_mode;
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
    
    TermResultType ArrayValue::check_type_impl(const ArrayValue& self) {
      TreePtr<Term> element_type = self.array_type->element_type;
      if (TermBuilder::size_equals(self.array_type->length, self.element_values.size()))
        self.compile_context().error_throw(self.location(), "Array literal length does not match its type");
      
      TermResultType rt;
      rt.type = self.array_type;
      rt.type_mode = type_mode_none;
      rt.pure = true;
      rt.mode = term_mode_value;

      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = self.element_values.begin(), ie = self.element_values.end(); ii != ie; ++ii) {
        rt.pure = rt.pure && (*ii)->result_type.pure;
        if ((*ii)->result_type.type != element_type)
          self.compile_context().error_throw(self.location(), "Array literal element has incorrect type");
      }
      
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
    
    TermResultType UnionType::check_type_impl(const UnionType& self) {
      TermResultType rt;
      rt.pure = true;
      rt.type_fixed_size = true;
      
      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = self.members.begin(), ie = self.members.end(); ii != ie; ++ii) {
        PSI_ASSERT((*ii)->result_type.pure);
        rt.type_fixed_size = rt.type_fixed_size && (*ii)->result_type.type_fixed_size;
        if (!(*ii)->is_type())
          self.compile_context().error_throw(self.location(), "Union element type is not a type");
      }
      
      rt.type = TermBuilder::metatype(self.compile_context());
      rt.mode = term_mode_value;
      // Unions are always primitive because there's no sensible default way to handle members
      rt.type_mode = type_mode_primitive;
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
    
    TermResultType UnionValue::check_type_impl(const UnionValue& self) {
      bool found = false;
      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = self.union_type->members.begin(), ie = self.union_type->members.end(); ii != ie; ++ii) {
        if (self.member_value->result_type.type == *ii) {
          found = true;
          break;
        }
      }
      
      if (!found)
        self.compile_context().error_throw(self.location(), "Union constructor member value is not a member of the union");
      
      TermResultType rt;
      rt.type = self.union_type;
      rt.mode = term_mode_value;
      rt.pure = self.member_value->result_type.pure;
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
    
    TermResultType UpwardReferenceType::check_type_impl(const UpwardReferenceType& self) {
      TermResultType rt;
      rt.type_mode = type_mode_complex;
      rt.type_fixed_size = false;
      rt.mode = term_mode_value;
      rt.pure = true;
      rt.type = TermBuilder::metatype(self.compile_context());
      return rt;
    }
    
    const FunctionalVtable UpwardReferenceType::vtable = PSI_COMPILER_FUNCTIONAL(UpwardReferenceType, "psi.compiler.UpwardReferenceType", Type);
    
    UpwardReference::UpwardReference(const TreePtr<Term>& outer_type_, const TreePtr<Term>& outer_index_, const TreePtr<Term>& next_, const SourceLocation& location)
    : Constructor(&vtable),
    outer_type(TermBuilder::to_functional(outer_type_, location)),
    outer_index(TermBuilder::to_functional(outer_index_, location)),
    next(TermBuilder::to_functional(next_, location)) {
    }
    
    /**
     * \brief Get the inner type implied by UpwardReference
     */
    TreePtr<Term> UpwardReference::inner_type() const {
      return ElementValue::element_type(outer_type, outer_index, location());
    }
    
    template<typename V>
    void UpwardReference::visit(V& v) {
      visit_base<Constructor>(v);
      v("outer_type", &UpwardReference::outer_type)
      ("outer_index", &UpwardReference::outer_index)
      ("next", &UpwardReference::next);
    }
    
    TermResultType UpwardReference::check_type_impl(const UpwardReference& self) {
      if (self.outer_index->result_type.type != TermBuilder::size_type(self.compile_context()))
        self.compile_context().error_throw(self.location(), "Upward reference index is not a size");
      if (self.next) {
        if (TreePtr<UpwardReference> next_upref = dyn_treeptr_cast<UpwardReference>(self.next)) {
          if (next_upref->inner_type() != self.outer_type)
            self.compile_context().error_throw(self.location(), "Inner type of next upward reference does not match outer type of this one");
        } else if (self.next->result_type.type != TermBuilder::upref_type(self.compile_context()))
          self.compile_context().error_throw(self.location(), "Next reference of upward reference is not itself an upward reference");
      }

      // This checks that the arguments are correct
      self.inner_type();
      
      TermResultType rt;
      rt.type_mode = type_mode_none;
      rt.mode = term_mode_value;
      rt.pure = true;
      rt.type = TermBuilder::upref_type(self.compile_context());
      return rt;
    }
    
    const FunctionalVtable UpwardReference::vtable = PSI_COMPILER_FUNCTIONAL(UpwardReference, "psi.compiler.UpwardReference", Constructor);
    
    DerivedType::DerivedType(const TreePtr<Term>& value_type_, const TreePtr<Term>& upref_, const SourceLocation& location)
    : Type(&vtable),
    value_type(TermBuilder::to_functional(value_type_, location)),
    upref(upref_) {
    }
    
    template<typename V>
    void DerivedType::visit(V& v) {
      visit_base<Type>(v);
      v("value_type", &DerivedType::value_type)
      ("upref", &DerivedType::upref);
    }
    
    TermResultType DerivedType::check_type_impl(const DerivedType& self) {
      if (self.upref) {
        if (TreePtr<UpwardReference> upref = dyn_treeptr_cast<UpwardReference>(self.upref)) {
          if (upref->inner_type() != self.value_type)
            self.compile_context().error_throw(self.location(), "Value type of DerivedType does not match type implied by upward reference");
        } else if (self.upref->result_type.type != TermBuilder::upref_type(self.compile_context()))
          self.compile_context().error_throw(self.location(), "Upward reference parameter to DerivedType is not an upward reference");
      }

      TermResultType rt = self.value_type->result_type;
      rt.type_mode = type_mode_complex;
      rt.type_fixed_size = false;
      return rt;
    }
    
    const FunctionalVtable DerivedType::vtable = PSI_COMPILER_FUNCTIONAL(DerivedType, "psi.compiler.DerivedType", Type);

    template<typename Visitor>
    void GenericType::visit(Visitor& v) {
      visit_base<Tree>(v);
      v("pattern", &GenericType::pattern)
      ("member", &GenericType::m_member)
      ("overloads", &GenericType::m_overloads)
      ("primitive_mode", &GenericType::primitive_mode);
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
    
    TermResultType TypeInstance::check_type_impl(const TypeInstance& self) {
      if (self.parameters.size() != self.generic->pattern.size())
        self.compile_context().error_throw(self.location(), "Wrong number of parameters to generic");
      
      for (std::size_t ii = 0, ie = self.parameters.size(); ii != ie; ++ii) {
        if (self.parameters[ii]->result_type.type != self.generic->pattern[ii]->specialize(self.location(), self.parameters))
          self.compile_context().error_throw(self.location(), "Incorrect parameter type to generic instance");
      }
      
      return self.unwrap()->result_type;
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
    
    TermResultType TypeInstanceValue::check_type_impl(const TypeInstanceValue& self) {
      if (self.type_instance->generic->primitive_mode == GenericType::primitive_never)
        self.compile_context().error_throw(self.location(), "Cannot construct complex generic type with non-default value");
      
      if (self.member_value->result_type.type != self.type_instance->unwrap())
        self.compile_context().error_throw(self.location(), "Generic instance value has the wrong type");
      
      TermResultType rt = self.member_value->result_type;
      rt.type = self.type_instance;
      return rt;
    }

    const FunctionalVtable TypeInstanceValue::vtable = PSI_COMPILER_FUNCTIONAL(TypeInstanceValue, "psi.compiler.TypeInstanceValue", Constructor);

    IfThenElse::IfThenElse(const TreePtr<Term>& condition_, const TreePtr<Term>& true_value_, const TreePtr<Term>& false_value_)
    : Functional(&vtable),
    condition(condition_),
    true_value(true_value_),
    false_value(false_value_) {
    }
    
    TermResultType IfThenElse::check_type_impl(const IfThenElse& self) {
      if (self.condition->result_type.type != TermBuilder::boolean_type(self.compile_context()))
        self.compile_context().error_throw(self.location(), "Conditional value is not boolean");
      
      return result_type_combine(self.true_value->result_type, self.false_value->result_type, self.compile_context(), self.location());
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
        if ((argument_mode == result_mode_functional) && (argument->result_type.type->result_type.type_mode == type_mode_complex))
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
    
    TermResultType JumpGroup::make_result_type(const TreePtr<Term>& initial, const PSI_STD::vector<TreePtr<JumpTarget> >& values, const SourceLocation& location) {
      TermResultType rt = initial->result_type;
      for (PSI_STD::vector<TreePtr<JumpTarget> >::const_iterator ii = values.begin(), ie = values.end(); ii != ie; ++ii)
        rt = result_type_combine(rt, (*ii)->value->result_type, initial.compile_context(), location);
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
    
    TermResultType JumpTo::make_result_type(const TreePtr<JumpTarget>& target, const TreePtr<Term>& argument, const SourceLocation& location) {
      if (target->argument->result_type.type != argument->result_type.type)
        target.compile_context().error_throw(location, "Jump argument has the wrong type");
      
      if (target->argument_mode == result_mode_lvalue) {
        if ((argument->result_type.mode != term_mode_lref) && (argument->result_type.mode != term_mode_rref))
          target.compile_context().error_throw(location, "Cannot make reference to temporary in a jump");
      } else if (target->argument_mode == result_mode_rvalue) {
        if (argument->result_type.mode != term_mode_rref)
          target.compile_context().error_throw(location, "Cannot make rvalue reference to temporary or lvalue in a jump");
      }
      
      return result_type_bottom(target.compile_context(), false);
    }
    
    JumpTo::JumpTo(const TreePtr<JumpTarget>& target_, const TreePtr<Term>& argument_, const SourceLocation& location)
    : Term(&vtable, make_result_type(target_, argument_, location), location),
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

    /**
     * \param arguments_ Arguments to the function call. Note that this variable is destroyed by this constructor.
     */
    FunctionCall::FunctionCall(const TreePtr<Term>& target_, PSI_STD::vector<TreePtr<Term> >& arguments_, const SourceLocation& location)
    : Term(&vtable, get_result_type(target_, arguments_, location), location),
    target(target_) {
      arguments.swap(arguments_);
    }

    TermResultType FunctionCall::get_result_type(const TreePtr<Term>& target, PSI_STD::vector<TreePtr<Term> >& arguments, const SourceLocation& location) {
      TreePtr<FunctionType> ft = dyn_treeptr_cast<FunctionType>(target->result_type.type);
      if (!ft)
        target.compile_context().error_throw(location, "Target of function call does not have function type");
      
      if (target->result_type.mode != term_mode_lref)
        target.compile_context().error_throw(location, "Function call target is a function but not a reference", CompileError::error_internal);
      
      if (ft->parameter_types.size() != arguments.size())
        target.compile_context().error_throw(location, "Function call has the wrong number of parameters");
      
      for (std::size_t ii = 0, ie = arguments.size(); ii != ie; ++ii) {
        if ((ft->parameter_types[ii].mode == parameter_mode_functional) || (ft->parameter_types[ii].mode == parameter_mode_phantom))
          arguments[ii] = TermBuilder::to_functional(arguments[ii], location);
      }

      TermResultType rt;
      rt.type = ft->result_type_after(location, arguments)->anonymize(location);
      
      if (rt.type->result_type.type_mode != type_mode_bottom) {
        switch (ft->result_mode) {
        case result_mode_by_value:
        case result_mode_functional: rt.mode = term_mode_value; break;
        case result_mode_lvalue: rt.mode = term_mode_lref; break;
        case result_mode_rvalue: rt.mode = term_mode_rref; break;
        default: PSI_FAIL("Unrecognised result mode");
        }
      } else {
        rt.mode = term_mode_bottom;
      }
      
      rt.type_mode = rt.type->is_type() ? type_mode_complex : type_mode_none;
      rt.pure = false;
      return rt;
    }
    
    template<typename Visitor>
    void FunctionCall::visit(Visitor& v) {
      visit_base<Term>(v);
      v("target", &FunctionCall::target)
      ("arguments", &FunctionCall::arguments);
    }
    
    const TermVtable FunctionCall::vtable = PSI_COMPILER_TERM(FunctionCall, "psi.compiler.FunctionCall", Term);
    
    SolidifyDuring::SolidifyDuring(const PSI_STD::vector<TreePtr<Term> >& value_, const TreePtr<Term>& body_, const SourceLocation& location)
    : Term(&vtable, body_->result_type, location),
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
    
    PrimitiveType::PrimitiveType(const String& name_)
    : Type(&vtable),
    name(name_) {
    }
    
    template<typename Visitor> void PrimitiveType::visit(Visitor& v) {
      visit_base<Type>(v);
      v("name", &PrimitiveType::name);
    }
    
    TermResultType PrimitiveType::check_type_impl(const PrimitiveType& self) {
      TermResultType rt;
      rt.pure = true;
      rt.type_fixed_size = true;
      rt.type = TermBuilder::metatype(self.compile_context());
      rt.mode = term_mode_value;
      rt.type_mode = type_mode_primitive;
      return rt;
    }

    const FunctionalVtable PrimitiveType::vtable = PSI_COMPILER_FUNCTIONAL(PrimitiveType, "psi.compiler.PrimitiveType", Type);
    
    BuiltinValue::BuiltinValue(const String& constructor_, const String& data_, const TreePtr<Term>& type)
    : Constant(&vtable),
    builtin_type(type),
    constructor(constructor_),
    data(data_) {
    }
    
    template<typename Visitor>
    void BuiltinValue::visit(Visitor& v) {
      visit_base<Constant>(v);
      v("builtin_type", &BuiltinValue::builtin_type)
      ("constructor", &BuiltinValue::constructor)
      ("data", &BuiltinValue::data);
    }
    
    TermResultType BuiltinValue::check_type_impl(const BuiltinValue& self) {
      if (!tree_isa<PrimitiveType>(self.builtin_type))
        self.compile_context().error_throw(self.location(), "Type of builtin value is not a primitive type");
      
      TermResultType rt;
      rt.type = self.builtin_type;
      rt.pure = true;
      rt.mode = term_mode_value;
      rt.type_mode = type_mode_none;
      return rt;
    }

    const FunctionalVtable BuiltinValue::vtable = PSI_COMPILER_FUNCTIONAL(BuiltinValue, "psi.compiler.BuiltinValue", Constant);

    IntegerValue::IntegerValue(const TreePtr<Term>& type, int value_, const SourceLocation& location)
    : Constant(&vtable),
    integer_type(TermBuilder::to_functional(type, location)),
    value(value_) {
    }
    
    template<typename V>
    void IntegerValue::visit(V& v) {
      visit_base<Constant>(v);
      v("integer_type", &IntegerValue::integer_type)
      ("value", &IntegerValue::value);
    }
    
    TermResultType IntegerValue::check_type_impl(const IntegerValue& self) {
      TermResultType rt;
      rt.pure = true;
      rt.type = self.integer_type;
      rt.type_mode = type_mode_none;
      rt.mode = term_mode_value;
      return rt;
    }
    
    const FunctionalVtable IntegerValue::vtable = PSI_COMPILER_FUNCTIONAL(IntegerValue, "psi.compiler.IntegerValue", Constant);

    StringValue::StringValue(const String& value_)
    : Constant(&vtable),
    value(value_) {
    }
    
    template<typename V>
    void StringValue::visit(V& v) {
      visit_base<Constant>(v);
      v("value", &StringValue::value);
    }
    
    TermResultType StringValue::check_type_impl(const StringValue& self) {
      TermResultType rt;
      rt.pure = true;
      rt.mode = term_mode_value;
      rt.type = TermBuilder::string_type(self.value.length()+1, self.compile_context(), self.location());
      rt.type_mode = type_mode_none;
      return rt;
    }
    
    const FunctionalVtable StringValue::vtable = PSI_COMPILER_FUNCTIONAL(StringValue, "psi.compiler.StringValue", Constant);
    
    BuiltinFunction::BuiltinFunction(const String& name_, bool pure_, const TreePtr<FunctionType>& type, const SourceLocation& location)
    : Global(&vtable, global_result_type(type), location),
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
    : Global(&vtable, global_result_type(type), location),
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
    
    TermResultType InterfaceValue::check_type_impl(const InterfaceValue& self) {
      TermResultType rt;
      rt.mode = term_mode_lref;
      rt.type_mode = type_mode_none;
      rt.pure = true;
      rt.type = self.interface->type_after(self.parameters, self.location());
      return rt;
    }
    
    const FunctionalVtable InterfaceValue::vtable = PSI_COMPILER_FUNCTIONAL(InterfaceValue, "psi.compiler.InterfaceValue", Functional);
    
    MovableValue::MovableValue(const TreePtr<Term>& value_)
    : Functional(&vtable),
    value(value_) {
    }
    
    TermResultType MovableValue::check_type_impl(const MovableValue& self) {
      TermResultType rs = self.value->result_type;
      if (rs.mode == term_mode_lref)
        rs.mode = term_mode_rref;
      return rs;
    }
    
    template<typename V>
    void MovableValue::visit(V& v) {
      visit_base<Term>(v);
      v("value", &MovableValue::value);
    }
    
    const FunctionalVtable MovableValue::vtable = PSI_COMPILER_FUNCTIONAL(MovableValue, "psi.compiler.MovableValue", Functional);
    
    InitializePointer::InitializePointer(const TreePtr<Term>& target_ptr_, const TreePtr<Term>& assign_value_, const TreePtr<Term>& inner_, const SourceLocation& location)
    : Term(&vtable, inner_->result_type, location),
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
    
    AssignPointer::AssignPointer(const TreePtr<Term>& target_ptr_, const TreePtr<Term>& assign_value_, const SourceLocation& location)
    : Term(&vtable, result_type_void(target_ptr_.compile_context()), location),
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
    
    FinalizePointer::FinalizePointer(const TreePtr<Term>& target_ptr_, const SourceLocation& location)
    : Term(&vtable, result_type_void(target_ptr_.compile_context()), location) {
    }
    
    template<typename V>
    void FinalizePointer::visit(V& v) {
      visit_base<Term>(v);
      v("target_ptr", &FinalizePointer::target_ptr);
    }
    
    const TermVtable FinalizePointer::vtable = PSI_COMPILER_TERM(FinalizePointer, "psi.compiler.FinalizePointer", Term);
    
    IntroduceImplementation::IntroduceImplementation(const PSI_STD::vector<TreePtr<Implementation> >& implementations_, const TreePtr<Term>& value_, const SourceLocation& location)
    : Term(&vtable, value_->result_type, location),
    implementations(implementations_),
    value(value_) {
    }
    
    template<typename V>
    void IntroduceImplementation::visit(V& v) {
      visit_base<Term>(v);
      v("implementations", &IntroduceImplementation::implementations)
      ("value", &IntroduceImplementation::value);
    }
    
    const TermVtable IntroduceImplementation::vtable = PSI_COMPILER_TERM(IntroduceImplementation, "psi.compiler.IntroduceImplementation", Term);
    
    TermResultType FunctionalEvaluate::make_result_type(const TreePtr<Term>& value, const SourceLocation& location) {
      if (value->result_type.pure)
        value.compile_context().error_throw(location, "Already pure terms should not be wrapped in FunctionalEvaluate", CompileError::error_internal);
      TermResultType rt;
      rt.type = value->result_type.type;
      rt.pure = true;
      rt.mode = term_mode_value;
      rt.type_mode = value->result_type.type_mode;
      return rt;
    }

    FunctionalEvaluate::FunctionalEvaluate(const TreePtr<Term>& value, const SourceLocation& location)
    : Term(&vtable, make_result_type(value, location), location) {
    }
    
    template<typename V>
    void FunctionalEvaluate::visit(V& v) {
      visit_base<Term>(v);
      v("value", &FunctionalEvaluate::value);
    }
    
    const TermVtable FunctionalEvaluate::vtable = PSI_COMPILER_TERM(FunctionalEvaluate, "psi.compiler.FunctionalEvaluate", Term);
  }
}
