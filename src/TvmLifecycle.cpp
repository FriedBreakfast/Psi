#include "TvmFunctionLowering.hpp"
#include "Tvm/FunctionalBuilder.hpp"
#include "Tvm/Aggregate.hpp"

namespace Psi {
namespace Compiler {
class TvmFunctionBuilder::LifecycleConstructorCleanup : public TvmCleanup {
  Tvm::ValuePtr<> m_target;
  Tvm::ValuePtr<> m_movable;
  
public:
  LifecycleConstructorCleanup(bool except_only, const Tvm::ValuePtr<>& target, const Tvm::ValuePtr<>& movable, const SourceLocation& location)
  : TvmCleanup(except_only, location), m_target(target), m_movable(movable) {
  }
  
  virtual void run(TvmFunctionBuilder& builder) const {
    Tvm::ValuePtr<> fini_func = builder.builder().load(Tvm::FunctionalBuilder::element_ptr(m_movable, interface_movable_fini, location()), location());
    builder.builder().call2(fini_func, m_movable, m_target, location());
  }
};

/// \brief Generate default constructor call
bool TvmFunctionBuilder::object_initialize_default(const Tvm::ValuePtr<>& dest, const TreePtr<Term>& type, bool except_only, const SourceLocation& location) {
  if (TreePtr<StructType> struct_type = dyn_treeptr_cast<StructType>(type)) {
    const PSI_STD::vector<TreePtr<Term> >& member_types = struct_type->members;
    for (std::size_t ii = 0, ie = member_types.size(); ii != ie; ++ii) {
      Tvm::ValuePtr<> dest_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      if (!object_initialize_default(dest_member, member_types[ii], except_only, location))
        return false;
    }
    return true;
  } else if (TreePtr<ArrayType> array_type = dyn_treeptr_cast<ArrayType>(type)) {
    // Bloody hell... I'm going to need a for loop here
    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<ConstantType> const_type = dyn_treeptr_cast<ConstantType>(type)) {
    Tvm::ValuePtr<> tvm_type = Tvm::value_cast<Tvm::PointerType>(dest)->target_type();
    PSI_ASSERT(Tvm::isa<Tvm::ConstantType>(tvm_type));
    builder().store(dest, Tvm::FunctionalBuilder::zero(tvm_type, location), location);
    return true;
  } else if (type->result_type.type_mode != type_mode_complex) {
    // This can't come first because we need to recurse looking for ConstantType instances
    return true;
  } else {
    if (TreePtr<TypeInstance> inst_type = dyn_treeptr_cast<TypeInstance>(type))
      if (inst_type->generic->primitive_mode == GenericType::primitive_recurse)
        return object_initialize_default(dest, inst_type->unwrap(), except_only, location);

    TvmResult movable = get_implementation(compile_context().builtins().movable_interface, vector_of(type), location);
    Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::element_ptr(movable.value, interface_movable_init, location), location);
    builder().call2(init_func, movable.value, dest, location);
    push_cleanup(boost::make_shared<LifecycleConstructorCleanup>(except_only, dest, movable.value, location));
    return true;
  }
}

bool TvmFunctionBuilder::object_initialize_term(const Tvm::ValuePtr<>& dest, const TreePtr<Term>& value, bool except_only, const SourceLocation& location) {
  if (tree_isa<DefaultValue>(value)) {
    return object_initialize_default(dest, value->result_type.type, except_only, location);
  } else if (TreePtr<StructValue> struct_val = dyn_treeptr_cast<StructValue>(value)) {
    const PSI_STD::vector<TreePtr<Term> >& member_values = struct_val->members;
    for (std::size_t ii = 0, ie = member_values.size(); ii != ie; ++ii) {
      Tvm::ValuePtr<> dest_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      if (!object_initialize_term(dest_member, member_values[ii], except_only, location))
        return false;
    }
    return true;
  } else if (TreePtr<ArrayValue> array_val = dyn_treeptr_cast<ArrayValue>(value)) {
    PSI_NOT_IMPLEMENTED(); // Need a for loop
  } else if (TreePtr<UnionValue> union_val = dyn_treeptr_cast<UnionValue>(value)) {
    PSI_NOT_IMPLEMENTED(); // Sort out element_ptr index
    return object_initialize_term(Tvm::FunctionalBuilder::element_ptr(dest, 0, location), union_val->member_value, except_only, location);
  } else if (value->result_type.type->result_type.type_mode != type_mode_complex) {
    TvmResult r = build(value);
    if (r.is_bottom())
      return false;
    
    if (value->result_type.mode == term_mode_value) {
      builder().store(dest, r.value, location);
    } else {
      Tvm::ValuePtr<> val = builder().load(r.value, location);
      builder().store(dest, val, location);
    }

    return true;
  } else if (value->result_type.mode == term_mode_lref) {
    TvmResult r = build(value);
    if (!r.is_bottom()) return false;
    return object_initialize_copy(dest, r.value, value->result_type.type, except_only, location);
  } else if (value->result_type.mode == term_mode_rref) {
    TvmResult r = build(value);
    if (!r.is_bottom()) return false;
    return object_initialize_move(dest, r.value, value->result_type.type, except_only, location);
  } else {
    PSI_ASSERT(tree_isa<FunctionCall>(value));
    Tvm::ValuePtr<> tmp = m_current_result_storage;
    m_current_result_storage = dest;
    TvmResult r = build(value);
    m_current_result_storage = tmp;
    return !r.is_bottom();
  }
}

bool TvmFunctionBuilder::object_initialize_move(const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const TreePtr<Term>& type, bool except_only, const SourceLocation& location) {
  if (type->result_type.type_mode != type_mode_complex) {
    Tvm::ValuePtr<> val = builder().load(src, location);
    builder().store(dest, val, location);
    return true;
  } else if (TreePtr<StructType> struct_type = dyn_treeptr_cast<StructType>(type)) {
    const PSI_STD::vector<TreePtr<Term> >& member_types = struct_type->members;
    for (std::size_t ii = 0, ie = member_types.size(); ii != ie; ++ii) {
      Tvm::ValuePtr<> dest_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      Tvm::ValuePtr<> src_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      if (!object_initialize_move(dest_member, src_member, member_types[ii], except_only, location))
        return false;
    }
    return true;
  } else if (TreePtr<ArrayType> array_type = dyn_treeptr_cast<ArrayType>(type)) {
    PSI_NOT_IMPLEMENTED(); // Need a for loop
  } else if (TreePtr<UnionType> union_type = dyn_treeptr_cast<UnionType>(type)) {
    builder().memcpy(dest, src, 1, location);
    return true;
  } else {
    if (TreePtr<TypeInstance> inst_type = dyn_treeptr_cast<TypeInstance>(type))
      if (inst_type->generic->primitive_mode == GenericType::primitive_recurse)
        return object_initialize_move(dest, src, inst_type->unwrap(), except_only, location);
    
    TvmResult movable = get_implementation(compile_context().builtins().movable_interface, vector_of(type), location);
    Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::element_ptr(movable.value, interface_movable_move_init, location), location);
    builder().call3(init_func, movable.value, dest, src, location);
    push_cleanup(boost::make_shared<LifecycleConstructorCleanup>(except_only, dest, movable.value, location));
    return true;
  }
}

bool TvmFunctionBuilder::object_initialize_copy(const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const TreePtr<Term>& type, bool except_only, const SourceLocation& location) {
  if (type->result_type.type_mode != type_mode_complex) {
    Tvm::ValuePtr<> val = builder().load(src, location);
    builder().store(dest, val, location);
    return true;
  } else if (TreePtr<StructType> struct_type = dyn_treeptr_cast<StructType>(type)) {
    const PSI_STD::vector<TreePtr<Term> >& member_types = struct_type->members;
    for (std::size_t ii = 0, ie = member_types.size(); ii != ie; ++ii) {
      Tvm::ValuePtr<> dest_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      Tvm::ValuePtr<> src_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      if (!object_initialize_copy(dest_member, src_member, member_types[ii], except_only, location))
        return false;
    }
    return true;
  } else if (TreePtr<ArrayType> array_type = dyn_treeptr_cast<ArrayType>(type)) {
    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<UnionType> union_type = dyn_treeptr_cast<UnionType>(type)) {
    builder().memcpy(dest, src, 1, location);
    return true;
  } else {
    if (TreePtr<TypeInstance> inst_type = dyn_treeptr_cast<TypeInstance>(type))
      if (inst_type->generic->primitive_mode == GenericType::primitive_recurse)
        return object_initialize_copy(dest, src, inst_type->unwrap(), except_only, location);
    
    TvmResult copyable = get_implementation(compile_context().builtins().copyable_interface, vector_of(type), location);
    Tvm::ValuePtr<> movable = builder().load(Tvm::FunctionalBuilder::element_ptr(copyable.value, interface_copyable_movable, location), location);
    Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::element_ptr(copyable.value, interface_copyable_copy_init, location), location);
    builder().call3(init_func, copyable.value, dest, src, location);
    push_cleanup(boost::make_shared<LifecycleConstructorCleanup>(except_only, dest, movable, location));
    return true;
  }
}

bool TvmFunctionBuilder::object_assign_default(const Tvm::ValuePtr<>& dest, const TreePtr<Term>& type, const SourceLocation& location) {
  if (type->result_type.type_mode != type_mode_complex) {
    return true;
  } if (TreePtr<StructType> struct_type = dyn_treeptr_cast<StructType>(type)) {
    const PSI_STD::vector<TreePtr<Term> >& member_types = struct_type->members;
    for (std::size_t ii = 0, ie = member_types.size(); ii != ie; ++ii) {
      Tvm::ValuePtr<> dest_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      if (!object_assign_default(dest_member, member_types[ii], location))
        return false;
    }
    return true;
  } else if (TreePtr<ArrayType> array_type = dyn_treeptr_cast<ArrayType>(type)) {
    // Bloody hell... I'm going to need a for loop here
    PSI_NOT_IMPLEMENTED();
  } else {
    if (TreePtr<TypeInstance> inst_type = dyn_treeptr_cast<TypeInstance>(type))
      if (inst_type->generic->primitive_mode == GenericType::primitive_recurse)
        return object_assign_default(dest, inst_type->unwrap(), location);

    TvmResult movable = get_implementation(compile_context().builtins().movable_interface, vector_of(type), location);
    Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::element_ptr(movable.value, interface_movable_clear, location), location);
    builder().call2(init_func, movable.value, dest, location);
    return true;
  }
}

bool TvmFunctionBuilder::object_assign_term(const Tvm::ValuePtr<>& dest, const TreePtr<Term>& value, const SourceLocation& location) {
  if (tree_isa<DefaultValue>(value)) {
    return object_assign_default(dest, value->result_type.type, location);
  } else if (TreePtr<StructValue> struct_val = dyn_treeptr_cast<StructValue>(value)) {
    const PSI_STD::vector<TreePtr<Term> >& member_values = struct_val->members;
    for (std::size_t ii = 0, ie = member_values.size(); ii != ie; ++ii) {
      Tvm::ValuePtr<> dest_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      if (!object_assign_default(dest_member, member_values[ii], location))
        return false;
    }
    return true;
  } else if (TreePtr<ArrayValue> array_val = dyn_treeptr_cast<ArrayValue>(value)) {
    PSI_NOT_IMPLEMENTED(); // Need a for loop
  } else if (TreePtr<UnionValue> union_val = dyn_treeptr_cast<UnionValue>(value)) {
    PSI_NOT_IMPLEMENTED(); // Sort out element_ptr index
    return object_assign_term(Tvm::FunctionalBuilder::element_ptr(dest, 0, location), union_val->member_value, location);
  } else if (value->result_type.type->result_type.type_mode != type_mode_complex) {
    TvmResult r = build(value);
    if (r.is_bottom())
      return false;
    
    if (value->result_type.mode == term_mode_value) {
      builder().store(dest, r.value, location);
    } else {
      Tvm::ValuePtr<> val = builder().load(r.value, location);
      builder().store(dest, val, location);
    }

    return true;
  } else if (value->result_type.mode == term_mode_lref) {
    TvmResult r = build(value);
    if (!r.is_bottom()) return false;
    return object_assign_copy(dest, r.value, value->result_type.type, location);
  } else if (value->result_type.mode == term_mode_rref) {
    TvmResult r = build(value);
    if (!r.is_bottom()) return false;
    return object_assign_move(dest, r.value, value->result_type.type, location);
  } else {
    PSI_ASSERT(tree_isa<FunctionCall>(value));
    Tvm::ValuePtr<> tmp = m_current_result_storage;
    m_current_result_storage = dest;
    TvmResult r = build(value);
    m_current_result_storage = tmp;
    return !r.is_bottom();
  }
}

bool TvmFunctionBuilder::object_assign_move(const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const TreePtr<Term>& type, const SourceLocation& location) {
  if (type->result_type.type_mode != type_mode_complex) {
    Tvm::ValuePtr<> val = builder().load(src, location);
    builder().store(dest, val, location);
    return true;
  } else if (TreePtr<StructType> struct_type = dyn_treeptr_cast<StructType>(type)) {
    const PSI_STD::vector<TreePtr<Term> >& member_types = struct_type->members;
    for (std::size_t ii = 0, ie = member_types.size(); ii != ie; ++ii) {
      Tvm::ValuePtr<> dest_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      Tvm::ValuePtr<> src_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      if (!object_assign_move(dest_member, src_member, member_types[ii], location))
        return false;
    }
    return true;
  } else if (TreePtr<ArrayType> array_type = dyn_treeptr_cast<ArrayType>(type)) {
    PSI_NOT_IMPLEMENTED(); // Need a for loop
  } else if (TreePtr<UnionType> union_type = dyn_treeptr_cast<UnionType>(type)) {
    builder().memcpy(dest, src, 1, location);
    return true;
  } else {
    if (TreePtr<TypeInstance> inst_type = dyn_treeptr_cast<TypeInstance>(type))
      if (inst_type->generic->primitive_mode == GenericType::primitive_recurse)
        return object_assign_move(dest, src, inst_type->unwrap(), location);
    
    TvmResult movable = get_implementation(compile_context().builtins().movable_interface, vector_of(type), location);
    Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::element_ptr(movable.value, interface_movable_move, location), location);
    builder().call3(init_func, movable.value, dest, src, location);
    return true;
  }
}

bool TvmFunctionBuilder::object_assign_copy(const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const TreePtr<Term>& type, const SourceLocation& location) {
  if (type->result_type.type_mode != type_mode_complex) {
    Tvm::ValuePtr<> val = builder().load(src, location);
    builder().store(dest, val, location);
    return true;
  } else if (TreePtr<StructType> struct_type = dyn_treeptr_cast<StructType>(type)) {
    const PSI_STD::vector<TreePtr<Term> >& member_types = struct_type->members;
    for (std::size_t ii = 0, ie = member_types.size(); ii != ie; ++ii) {
      Tvm::ValuePtr<> dest_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      Tvm::ValuePtr<> src_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      if (!object_assign_copy(dest_member, src_member, member_types[ii], location))
        return false;
    }
    return true;
  } else if (TreePtr<ArrayType> array_type = dyn_treeptr_cast<ArrayType>(type)) {
    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<UnionType> union_type = dyn_treeptr_cast<UnionType>(type)) {
    builder().memcpy(dest, src, 1, location);
    return true;
  } else {
    if (TreePtr<TypeInstance> inst_type = dyn_treeptr_cast<TypeInstance>(type))
      if (inst_type->generic->primitive_mode == GenericType::primitive_recurse)
        return object_assign_copy(dest, src, inst_type->unwrap(), location);
    
    TvmResult copyable = get_implementation(compile_context().builtins().copyable_interface, vector_of(type), location);
    Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::element_ptr(copyable.value, interface_copyable_copy, location), location);
    builder().call3(init_func, copyable.value, dest, src, location);
    return true;
  }
}

void TvmFunctionBuilder::object_destroy(const Tvm::ValuePtr<>& dest, const TreePtr<Term>& type, const SourceLocation& location) {
  if (type->result_type.type_mode != type_mode_complex) {
    return;
  } else if (TreePtr<StructType> struct_type = dyn_treeptr_cast<StructType>(type)) {
    PSI_STD::vector<TreePtr<Statement> > statements;
    for (std::size_t ii = 0, ie = struct_type->members.size(); ii != ie; ++ii) {
      std::size_t idx = ie - ii - 1;
      object_destroy(Tvm::FunctionalBuilder::element_ptr(dest, idx, location), struct_type->members[idx], location);
    }
  } else if (TreePtr<ArrayType> array_type = dyn_treeptr_cast<ArrayType>(type)) {
    PSI_NOT_IMPLEMENTED();
  } else {
    // Use Movable interface
    TvmResult movable = get_implementation(compile_context().builtins().movable_interface, vector_of(type), location);
    Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::element_ptr(movable.value, interface_movable_fini, location), location);
    builder().call2(init_func, movable.value, dest, location);
  }
}

/// \brief Generate copy constructor call
void TvmFunctionBuilder::copy_construct(const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location) {
  object_initialize_copy(dest, src, type, true, location);
}

/// \brief Generate move constructor call
void TvmFunctionBuilder::move_construct(const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location) {
  object_initialize_move(dest, src, type, true, location);
}

/**
 * \brief Generate a move constructor call followed by a destructor call on the source.
 * 
 * It is expected that this can be optimised by merging the two calls. However, currently
 * this is not done and this funtion simply calls move_construct() followed by destroy().
 */
void TvmFunctionBuilder::move_construct_destroy(const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location) {
  move_construct(type, dest, src, location);
  object_destroy(src, type, location);
}
}
}
