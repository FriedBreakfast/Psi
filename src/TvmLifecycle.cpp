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
    Tvm::ValuePtr<> fini_func = builder.builder().load(Tvm::FunctionalBuilder::apply_element_ptr(m_movable, interface_movable_fini, location()), location());
    builder.builder().call2(fini_func, m_movable, m_target, location());
  }
};

/// \brief Generate a default constructor/assignment call
bool TvmFunctionBuilder::object_construct_default(ConstructMode mode, const Tvm::ValuePtr<>& dest, const TreePtr<Term>& type, const SourceLocation& location) {
  TreePtr<Term> unwrapped_type = term_unwrap(type);
  if (TreePtr<ConstantType> const_type = dyn_treeptr_cast<ConstantType>(unwrapped_type)) {
    if ((mode == construct_initialize) || (mode == construct_initialize_destroy)) {
      Tvm::ValuePtr<> tvm_type = Tvm::value_cast<Tvm::PointerType>(dest)->target_type();
      PSI_ASSERT(Tvm::isa<Tvm::ConstantType>(tvm_type));
      builder().store(Tvm::FunctionalBuilder::zero(tvm_type, location), dest, location);
    }
    return true;
  } else if (unwrapped_type->is_primitive_type()) {
    return true;
  } else if (TreePtr<StructType> struct_type = dyn_treeptr_cast<StructType>(unwrapped_type)) {
    const PSI_STD::vector<TreePtr<Term> >& member_types = struct_type->members;
    for (std::size_t ii = 0, ie = member_types.size(); ii != ie; ++ii) {
      Tvm::ValuePtr<> dest_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      if (!object_construct_default(mode, dest_member, member_types[ii], location))
        return false;
    }
    return true;
  } else if (TreePtr<ArrayType> array_type = dyn_treeptr_cast<ArrayType>(unwrapped_type)) {
    // Bloody hell... I'm going to need a for loop here
    PSI_NOT_IMPLEMENTED();
  } else {
    if (TreePtr<TypeInstance> inst_type = dyn_treeptr_cast<TypeInstance>(unwrapped_type))
      if (inst_type->generic->primitive_mode == GenericType::primitive_recurse)
        return object_construct_default(mode, dest, inst_type->unwrap(), location);

    TvmResult movable = get_implementation(compile_context().builtins().movable_interface, vector_of(unwrapped_type), location);
    switch (mode) {
    case construct_initialize:
    case construct_initialize_destroy: {
      Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::apply_element_ptr(movable.value, interface_movable_init, location), location);
      builder().call2(init_func, movable.value, dest, location);
      push_cleanup(boost::make_shared<LifecycleConstructorCleanup>(mode == construct_initialize, dest, movable.value, location));
      return true;
    }
    
    case construct_assign: {
      Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::apply_element_ptr(movable.value, interface_movable_clear, location), location);
      builder().call2(init_func, movable.value, dest, location);
      return true;
    }
    
    case construct_interfaces:
      return true;
      
    default: PSI_FAIL("Unrecognised constructor mode");
    }
  }
}

/// \brief Construct or assign a term from a tree value
bool TvmFunctionBuilder::object_construct_term(ConstructMode mode, const Tvm::ValuePtr<>& dest, const TreePtr<Term>& value, const SourceLocation& location) {
  if (tree_isa<DefaultValue>(value)) {
    return object_construct_default(mode, dest, value->type, location);
  } else if (TreePtr<StructValue> struct_val = dyn_treeptr_cast<StructValue>(value)) {
    const PSI_STD::vector<TreePtr<Term> >& member_values = struct_val->members;
    for (std::size_t ii = 0, ie = member_values.size(); ii != ie; ++ii) {
      Tvm::ValuePtr<> dest_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      if (!object_construct_term(mode, dest_member, member_values[ii], location))
        return false;
    }
    return true;
  } else if (TreePtr<ArrayValue> array_val = dyn_treeptr_cast<ArrayValue>(value)) {
    PSI_NOT_IMPLEMENTED(); // Need a for loop
  } else if (TreePtr<UnionValue> union_val = dyn_treeptr_cast<UnionValue>(value)) {
    PSI_NOT_IMPLEMENTED(); // Sort out element_ptr index
    return object_construct_term(mode, Tvm::FunctionalBuilder::element_ptr(dest, 0, location), union_val->member_value, location);
  } else if (value->type->is_register_type()) {
    if (mode == construct_interfaces)
      return true;
    
    TvmResult r = build(value);
    if (r.is_bottom())
      return false;
    
    if (value->mode == term_mode_value) {
      builder().store(r.value, dest, location);
    } else {
      Tvm::ValuePtr<> val = builder().load(r.value, location);
      builder().store(val, dest, location);
    }

    return true;
  } else if ((value->mode == term_mode_lref) || (value->mode == term_mode_rref)) {
    Tvm::ValuePtr<> ptr;
    if (mode != construct_interfaces) {
      TvmResult r = build(value);
      if (r.is_bottom()) return false;
      ptr = r.value;
    }
    return object_construct_move_copy(mode, value->mode == term_mode_rref, dest, ptr, value->type, location);
  } else {
    PSI_ASSERT(tree_isa<FunctionCall>(value));
    if (mode == construct_interfaces)
      return true;

    Tvm::ValuePtr<> tmp = m_current_result_storage;
    m_current_result_storage = dest;
    TvmResult r = build(value);
    m_current_result_storage = tmp;
    return !r.is_bottom();
  }
}

bool TvmFunctionBuilder::object_construct_move_copy(ConstructMode mode, bool move, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const TreePtr<Term>& type, const SourceLocation& location) {
  TreePtr<Term> unwrapped_type = term_unwrap(type);
  if (unwrapped_type->is_register_type()) {
    if (mode != construct_interfaces) {
      Tvm::ValuePtr<> val = builder().load(src, location);
      builder().store(val, dest, location);
    }
    return true;
  } else if (TreePtr<StructType> struct_type = dyn_treeptr_cast<StructType>(unwrapped_type)) {
    const PSI_STD::vector<TreePtr<Term> >& member_types = struct_type->members;
    for (std::size_t ii = 0, ie = member_types.size(); ii != ie; ++ii) {
      Tvm::ValuePtr<> dest_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      Tvm::ValuePtr<> src_member = Tvm::FunctionalBuilder::element_ptr(dest, ii, location);
      if (!object_construct_move_copy(mode, move, dest_member, src_member, member_types[ii], location))
        return false;
    }
    return true;
  } else if (TreePtr<ArrayType> array_type = dyn_treeptr_cast<ArrayType>(unwrapped_type)) {
    PSI_NOT_IMPLEMENTED(); // Need a for loop
  } else if (TreePtr<UnionType> union_type = dyn_treeptr_cast<UnionType>(unwrapped_type)) {
    if (mode != construct_interfaces)
      builder().memcpy(dest, src, 1, location);
    return true;
  } else {
    if (TreePtr<TypeInstance> inst_type = dyn_treeptr_cast<TypeInstance>(unwrapped_type))
      if (inst_type->generic->primitive_mode == GenericType::primitive_recurse)
        return object_construct_move_copy(mode, move, dest, src, inst_type->unwrap(), location);

    if (move) {
      TvmResult movable = get_implementation(compile_context().builtins().movable_interface, vector_of(unwrapped_type), location);
      switch (mode) {
      case construct_initialize:
      case construct_initialize_destroy: {
        Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::apply_element_ptr(movable.value, interface_movable_move_init, location), location);
        builder().call3(init_func, movable.value, dest, src, location);
        push_cleanup(boost::make_shared<LifecycleConstructorCleanup>(mode == construct_initialize, dest, movable.value, location));
        return true;
      }
      
      case construct_assign: {
        Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::apply_element_ptr(movable.value, interface_movable_move, location), location);
        builder().call3(init_func, movable.value, dest, src, location);
        return true;
      }
      
      case construct_interfaces:
        return true;
        
      default: PSI_FAIL("Unrecognised constructor mode");
      }
    } else {
      TvmResult copyable = get_implementation(compile_context().builtins().copyable_interface, vector_of(unwrapped_type), location);
      switch (mode) {
      case construct_initialize:
      case construct_initialize_destroy: {
        Tvm::ValuePtr<> movable = builder().load(Tvm::FunctionalBuilder::element_ptr(copyable.value, interface_copyable_movable, location), location);
        Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::element_ptr(copyable.value, interface_copyable_copy_init, location), location);
        builder().call3(init_func, copyable.value, dest, src, location);
        push_cleanup(boost::make_shared<LifecycleConstructorCleanup>(mode == construct_initialize, dest, movable, location));
        return true;
      }
      
      case construct_assign: {
        Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::element_ptr(copyable.value, interface_copyable_copy, location), location);
        builder().call3(init_func, copyable.value, dest, src, location);
        return true;
      }
      
      case construct_interfaces:
        return true;

      default: PSI_FAIL("Unrecognised constructor mode");
      }
    }
  }
}

void TvmFunctionBuilder::object_destroy(const Tvm::ValuePtr<>& dest, const TreePtr<Term>& type, const SourceLocation& location) {
  TreePtr<Term> unwrapped_type = term_unwrap(type);
  if (unwrapped_type->is_primitive_type()) {
    return;
  } else if (TreePtr<StructType> struct_type = dyn_treeptr_cast<StructType>(unwrapped_type)) {
    PSI_STD::vector<TreePtr<Statement> > statements;
    for (std::size_t ii = 0, ie = struct_type->members.size(); ii != ie; ++ii) {
      std::size_t idx = ie - ii - 1;
      object_destroy(Tvm::FunctionalBuilder::element_ptr(dest, idx, location), struct_type->members[idx], location);
    }
  } else if (TreePtr<ArrayType> array_type = dyn_treeptr_cast<ArrayType>(unwrapped_type)) {
    PSI_NOT_IMPLEMENTED();
  } else {
    // Use Movable interface
    TvmResult movable = get_implementation(compile_context().builtins().movable_interface, vector_of(unwrapped_type), location);
    Tvm::ValuePtr<> init_func = builder().load(Tvm::FunctionalBuilder::apply_element_ptr(movable.value, interface_movable_fini, location), location);
    builder().call2(init_func, movable.value, dest, location);
  }
}

/// \brief Generate copy constructor call
void TvmFunctionBuilder::copy_construct(const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location) {
  object_construct_move_copy(construct_initialize, false, dest, src, type, location);
}

/// \brief Generate move constructor call
void TvmFunctionBuilder::move_construct(const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location) {
  object_construct_move_copy(construct_initialize, true, dest, src, type, location);
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
