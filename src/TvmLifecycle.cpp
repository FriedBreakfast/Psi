#include "TvmFunctionLowering.hpp"
#include "Tvm/FunctionalBuilder.hpp"
#include "Tvm/Aggregate.hpp"

namespace Psi {
namespace Compiler {
class TvmFunctionLowering::ConstructorCleanup : public TvmFunctionLowering::CleanupCallback {
  Tvm::ValuePtr<> m_target;
  Tvm::ValuePtr<> m_movable;
  
public:
  ConstructorCleanup(const Tvm::ValuePtr<>& target, const Tvm::ValuePtr<>& movable)
  : m_target(target), m_movable(movable) {
  }
  
  virtual void run(Scope& scope) {
    Tvm::InstructionBuilder& builder = scope.shared().builder();
    Tvm::ValuePtr<> fini_func = builder.load(Tvm::FunctionalBuilder::element_ptr(m_movable, interface_movable_fini, scope.location()), scope.location());
    builder.call2(fini_func, m_movable, m_target, scope.location());
  }
};

/// \brief Generate default constructor call
void TvmFunctionLowering::object_initialize_default(ScopeList& scope_list, const Tvm::ValuePtr<>& dest, const TreePtr<Term>& type, const SourceLocation& location) {
  if (TreePtr<StructType> struct_type = dyn_treeptr_cast<StructType>(type)) {
    const PSI_STD::vector<TreePtr<Term> >& member_types = struct_type->members;
    for (std::size_t ii = 0, ie = member_types.size(); ii != ie; ++ii) {
      Tvm::ValuePtr<> dest_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      object_initialize_default(scope_list, dest_member, member_types[ii], location);
    }
  } else if (TreePtr<ArrayType> array_type = dyn_treeptr_cast<ArrayType>(type)) {
    // Bloody hell... I'm going to need a for loop here
    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<ConstantType> const_type = dyn_treeptr_cast<ConstantType>(type)) {
    Tvm::ValuePtr<> tvm_type = Tvm::value_cast<Tvm::PointerType>(dest)->target_type();
    PSI_ASSERT(Tvm::isa<Tvm::ConstantType>(tvm_type));
    builder().store(dest, Tvm::FunctionalBuilder::zero(tvm_type, location), location);
  } else if (TreePtr<TypeInstance> inst_type = dyn_treeptr_cast<TypeInstance>(type)) {
    if (!is_primitive(scope_list.current(), inst_type)) {
      Tvm::ValuePtr<> movable = get_implementation(scope_list.current(), compile_context().builtins().movable_interface, vector_of(type), location);
      Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::element_ptr(movable, interface_movable_init, location), location);
      builder().call2(init_func, movable, dest, location);
      std::auto_ptr<CleanupCallback> cleanup(new ConstructorCleanup(dest, movable));
      scope_list.push(new Scope(scope_list.current(), location, cleanup, true));
    } else {
      object_initialize_default(scope_list, dest, inst_type->unwrap(), location);
    }
  }
}

void TvmFunctionLowering::object_initialize_term(ScopeList& scope_list, const Tvm::ValuePtr<>& dest, const TreePtr<Term>& value, const SourceLocation& location) {
  if (tree_isa<DefaultValue>(value)) {
    object_initialize_default(scope_list, dest, value->type, location);
  } else if (TreePtr<StructValue> struct_val = dyn_treeptr_cast<StructValue>(value)) {
    const PSI_STD::vector<TreePtr<Term> >& member_values = struct_val->members;
    for (std::size_t ii = 0, ie = member_values.size(); ii != ie; ++ii) {
      Tvm::ValuePtr<> dest_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      object_initialize_term(scope_list, dest_member, member_values[ii], location);
    }
  } else if (TreePtr<ArrayValue> array_val = dyn_treeptr_cast<ArrayValue>(value)) {
    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<UnionValue> union_val = dyn_treeptr_cast<UnionValue>(value)) {
    PSI_NOT_IMPLEMENTED(); // Sort out element_ptr index
    object_initialize_term(scope_list, Tvm::FunctionalBuilder::element_ptr(dest, 0, location), union_val->member_value, location);
  } else {
    // Complex type - may require a constructor call
    VariableSlot r_vs(scope_list.current(), value->type);
    TvmResult r = run(scope_list.current(), value, r_vs, scope_list.current());
    Scope r_scope(scope_list.current(), value.location(), r, r_vs);
    switch (r.storage()) {
    case tvm_storage_functional:
      builder().store(dest, r.value(), location);
      break;
      
    case tvm_storage_lvalue_ref:
      object_initialize_copy(scope_list, dest, r.value(), value->type, location);
      break;
      
    case tvm_storage_stack:
    case tvm_storage_rvalue_ref:
      object_initialize_move(scope_list, dest, r.value(), value->type, location);
      break;
      
    default: PSI_FAIL("unexpected storage type");
    }
    r_scope.cleanup(false);
  }
}

void TvmFunctionLowering::object_initialize_move(ScopeList& scope_list, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const TreePtr<Term>& type, const SourceLocation& location) {
  if (is_register(scope_list.current(), type)) {
    Tvm::ValuePtr<> val = builder().load(src, location);
    builder().store(dest, val, location);
  } else if (TreePtr<StructType> struct_type = dyn_treeptr_cast<StructType>(type)) {
    const PSI_STD::vector<TreePtr<Term> >& member_types = struct_type->members;
    for (std::size_t ii = 0, ie = member_types.size(); ii != ie; ++ii) {
      Tvm::ValuePtr<> dest_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      Tvm::ValuePtr<> src_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      object_initialize_move(scope_list, dest_member, src_member, member_types[ii], location);
    }
  } else if (TreePtr<ArrayType> array_type = dyn_treeptr_cast<ArrayType>(type)) {
    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<UnionType> union_type = dyn_treeptr_cast<UnionType>(type)) {
    builder().memcpy(dest, src, 1, location);
  } else if (!is_primitive(scope_list.current(), type)) {
    Tvm::ValuePtr<> movable = get_implementation(scope_list.current(), compile_context().builtins().movable_interface, vector_of(type), location);
    Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::element_ptr(movable, interface_movable_move_init, location), location);
    builder().call3(init_func, movable, dest, src, location);
    std::auto_ptr<CleanupCallback> cleanup(new ConstructorCleanup(dest, movable));
    scope_list.push(new Scope(scope_list.current(), location, cleanup, true));
  } else {
    // This must be a TypeInstance because Anonymous types should be non-primitive
    object_initialize_move(scope_list, dest, src, treeptr_cast<TypeInstance>(type)->unwrap(), location);
  }
}

void TvmFunctionLowering::object_initialize_copy(ScopeList& scope_list, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const TreePtr<Term>& type, const SourceLocation& location) {
  if (is_register(scope_list.current(), type)) {
    Tvm::ValuePtr<> val = builder().load(src, location);
    builder().store(dest, val, location);
  } else if (TreePtr<StructType> struct_type = dyn_treeptr_cast<StructType>(type)) {
    const PSI_STD::vector<TreePtr<Term> >& member_types = struct_type->members;
    for (std::size_t ii = 0, ie = member_types.size(); ii != ie; ++ii) {
      Tvm::ValuePtr<> dest_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      Tvm::ValuePtr<> src_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      object_initialize_copy(scope_list, dest_member, src_member, member_types[ii], location);
    }
  } else if (TreePtr<ArrayType> array_type = dyn_treeptr_cast<ArrayType>(type)) {
    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<UnionType> union_type = dyn_treeptr_cast<UnionType>(type)) {
    builder().memcpy(dest, src, 1, location);
  } else if (!is_primitive(scope_list.current(), type)) {
    Tvm::ValuePtr<> copyable = get_implementation(scope_list.current(), compile_context().builtins().copyable_interface, vector_of(type), location);
    Tvm::ValuePtr<> movable = builder().load(Tvm::FunctionalBuilder::element_ptr(copyable, interface_copyable_movable, location), location);
    Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::element_ptr(copyable, interface_copyable_copy_init, location), location);
    builder().call3(init_func, copyable, dest, src, location);
    std::auto_ptr<CleanupCallback> cleanup(new ConstructorCleanup(dest, movable));
    scope_list.push(new Scope(scope_list.current(), location, cleanup, true));
  } else {
    // This must be a TypeInstance because Anonymous types should be non-primitive
    object_initialize_copy(scope_list, dest, src, treeptr_cast<TypeInstance>(type)->unwrap(), location);
  }
}

void TvmFunctionLowering::object_assign_default(Scope& scope, const Tvm::ValuePtr<>& dest, const TreePtr<Term>& type, const SourceLocation& location) {
  if (is_primitive(scope, type)) {
    return;
  } else if (TreePtr<StructType> struct_type = dyn_treeptr_cast<StructType>(type)) {
    const PSI_STD::vector<TreePtr<Term> >& member_types = struct_type->members;
    for (std::size_t ii = 0, ie = member_types.size(); ii != ie; ++ii) {
      Tvm::ValuePtr<> dest_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      object_assign_default(scope, dest_member, member_types[ii], location);
    }
  } else if (TreePtr<ArrayType> array_type = dyn_treeptr_cast<ArrayType>(type)) {
    // Bloody hell... I'm going to need a for loop here
    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<ConstantType> const_type = dyn_treeptr_cast<ConstantType>(type)) {
    Tvm::ValuePtr<> tvm_type = Tvm::value_cast<Tvm::PointerType>(dest)->target_type();
    PSI_ASSERT(Tvm::isa<Tvm::ConstantType>(tvm_type));
    builder().store(dest, Tvm::FunctionalBuilder::zero(tvm_type, location), location);
  } else {
    // Use movable interface
    Tvm::ValuePtr<> movable = get_implementation(scope, compile_context().builtins().movable_interface, vector_of(type), location);
    Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::element_ptr(movable, interface_movable_clear, location), location);
    builder().call2(init_func, movable, dest, location);
  }
}

void TvmFunctionLowering::object_assign_term(Scope& scope, const Tvm::ValuePtr<>& dest, const TreePtr<Term>& value, const SourceLocation& location) {
  if (tree_isa<DefaultValue>(value)) {
    object_assign_default(scope, dest, value->type, location);
  } else if (TreePtr<StructValue> struct_val = dyn_treeptr_cast<StructValue>(value)) {
    const PSI_STD::vector<TreePtr<Term> >& member_values = struct_val->members;
    for (std::size_t ii = 0, ie = member_values.size(); ii != ie; ++ii) {
      Tvm::ValuePtr<> dest_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      object_assign_term(scope, dest_member, member_values[ii], location);
    }
  } else if (TreePtr<ArrayValue> array_val = dyn_treeptr_cast<ArrayValue>(value)) {
    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<UnionValue> union_val = dyn_treeptr_cast<UnionValue>(value)) {
    PSI_NOT_IMPLEMENTED();
  } else {
    VariableSlot r_vs(scope, value->type);
    TvmResult r = run(scope, value, r_vs, scope);
    Scope r_scope(scope, value.location(), r, r_vs);
    switch (r.storage()) {
    case tvm_storage_functional:
      builder().store(dest, r.value(), location);
      break;
      
    case tvm_storage_lvalue_ref:
      object_assign_copy(scope, dest, r.value(), value->type, location);
      break;
      
    case tvm_storage_stack:
    case tvm_storage_rvalue_ref:
      object_assign_move(scope, dest, r.value(), value->type, location);
      break;
      
    default: PSI_FAIL("unknown storage type");
    }
    r_scope.cleanup(false);
  }
}

void TvmFunctionLowering::object_assign_move(Scope& scope, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const TreePtr<Term>& type, const SourceLocation& location) {
  if (is_register(scope, type)) {
    Tvm::ValuePtr<> val = builder().load(src, location);
    builder().store(dest, val, location);
  } else if (TreePtr<StructType> struct_type = dyn_treeptr_cast<StructType>(type)) {
    const PSI_STD::vector<TreePtr<Term> >& member_types = struct_type->members;
    for (std::size_t ii = 0, ie = member_types.size(); ii != ie; ++ii) {
      Tvm::ValuePtr<> dest_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      Tvm::ValuePtr<> src_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      object_assign_move(scope, dest_member, src_member, member_types[ii], location);
    }
  } else if (TreePtr<ArrayType> array_type = dyn_treeptr_cast<ArrayType>(type)) {
    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<UnionType> union_type = dyn_treeptr_cast<UnionType>(type)) {
    builder().memcpy(dest, src, 1, location);
  } else {
    // Use Movable interface
    Tvm::ValuePtr<> movable = get_implementation(scope, compile_context().builtins().movable_interface, vector_of(type), location);
    Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::element_ptr(movable, interface_movable_move, location), location);
    builder().call3(init_func, movable, dest, src, location);
  }
}

void TvmFunctionLowering::object_assign_copy(Scope& scope, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const TreePtr<Term>& type, const SourceLocation& location) {
  if (is_register(scope, type)) {
    Tvm::ValuePtr<> val = builder().load(src, location);
    builder().store(dest, val, location);
  } else if (TreePtr<StructType> struct_type = dyn_treeptr_cast<StructType>(type)) {
    const PSI_STD::vector<TreePtr<Term> >& member_types = struct_type->members;
    for (std::size_t ii = 0, ie = member_types.size(); ii != ie; ++ii) {
      Tvm::ValuePtr<> dest_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      Tvm::ValuePtr<> src_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      object_assign_copy(scope, dest_member, src_member, member_types[ii], location);
    }
  } else if (TreePtr<ArrayType> array_type = dyn_treeptr_cast<ArrayType>(type)) {
    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<UnionType> union_type = dyn_treeptr_cast<UnionType>(type)) {
    builder().memcpy(dest, src, 1, location);
  } else {
    // Use Copyable interface
    Tvm::ValuePtr<> copyable = get_implementation(scope, compile_context().builtins().copyable_interface, vector_of(type), location);
    Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::element_ptr(copyable, interface_copyable_copy, location), location);
    builder().call3(init_func, copyable, dest, src, location);
  }
}

void TvmFunctionLowering::object_destroy(Scope& scope, const Tvm::ValuePtr<>& dest, const TreePtr<Term>& type, const SourceLocation& location) {
  if (is_primitive(scope, type)) {
    return;
  } if (TreePtr<StructType> struct_type = dyn_treeptr_cast<StructType>(type)) {
    PSI_STD::vector<TreePtr<Statement> > statements;
    for (std::size_t ii = 0, ie = struct_type->members.size(); ii != ie; ++ii) {
      std::size_t idx = ie - ii - 1;
      object_destroy(scope, Tvm::FunctionalBuilder::element_ptr(dest, idx, location), struct_type->members[idx], location);
    }
  } else if (TreePtr<ArrayType> array_type = dyn_treeptr_cast<ArrayType>(type)) {
    PSI_NOT_IMPLEMENTED();
  } else {
    // Use Movable interface
    Tvm::ValuePtr<> movable = get_implementation(scope, compile_context().builtins().movable_interface, vector_of(type), location);
    Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::element_ptr(movable, interface_movable_fini, location), location);
    builder().call2(init_func, movable, dest, location);
  }
}

/// \brief Generate copy constructor call
void TvmFunctionLowering::copy_construct(Scope& scope, const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location) {
  ScopeList sl(scope);
  object_initialize_copy(sl, dest, src, type, location);
}

/// \brief Generate move constructor call
void TvmFunctionLowering::move_construct(Scope& scope, const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location) {
  ScopeList sl(scope);
  object_initialize_move(sl, dest, src, type, location);
}

/**
 * \brief Generate a move constructor call followed by a destructor call on the source.
 * 
 * It is expected that this can be optimised by merging the two calls. However, currently
 * this is not done and this funtion simply calls move_construct() followed by destroy().
 */
void TvmFunctionLowering::move_construct_destroy(Scope& scope, const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location) {
  move_construct(scope, type, dest, src, location);
  object_destroy(scope, src, type, location);
}

TvmResult TvmFunctionLowering::run_initialize(Scope& scope, const TreePtr<InitializePointer>& initialize, const VariableSlot& slot, Scope& following_scope) {
  Tvm::ValuePtr<> dest_ptr = run_functional(scope, initialize->target_ptr);
  ScopeList sl(scope);
  object_initialize_term(sl, dest_ptr, initialize->assign_value, initialize.location());
  return run(sl.current(), initialize->inner, slot, following_scope);
}

TvmResult TvmFunctionLowering::run_assign(Scope& scope, const TreePtr<AssignPointer>& assign, const VariableSlot&, Scope&) {
  Tvm::ValuePtr<> dest_ptr = run_functional(scope, assign->target_ptr);
  object_assign_term(scope, dest_ptr, assign->assign_value, assign.location());
  return TvmResult::in_register(assign->type, tvm_storage_functional, Tvm::FunctionalBuilder::empty_value(tvm_context(), assign.location()));
}

TvmResult TvmFunctionLowering::run_finalize(Scope& scope, const TreePtr<FinalizePointer>& finalize, const VariableSlot&, Scope&) {
  TreePtr<PointerType> ptr_type = dyn_treeptr_cast<PointerType>(finalize->target_ptr);
  if (!ptr_type)
    compile_context().error_throw(finalize.location(), "Argument to finalize operation is not a pointer");
  Tvm::ValuePtr<> target = run_functional(scope, finalize->target_ptr);
  object_destroy(scope, target, ptr_type->target_type, finalize.location());
  return TvmResult::in_register(finalize->type, tvm_storage_functional, Tvm::FunctionalBuilder::empty_value(tvm_context(), finalize.location()));
}

TvmResult TvmFunctionLowering::run_constructor(Scope& scope, const TreePtr<Term>& value, const VariableSlot& slot, Scope&) {
  ScopeList sl(scope);
  object_initialize_term(sl, slot.slot(), value, value.location());
  sl.cleanup(false);
  return TvmResult::on_stack(value->type, slot.slot());
}
}
}
