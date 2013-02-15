#include "Compiler.hpp"
#include "Tree.hpp"
#include "Interface.hpp"

namespace Psi {
namespace Compiler {
/**
 * \brief Check whether a type is primitive.
 */
bool lifecycle_primitive(const TreePtr<TypeInstance>& type) {
  return type->generic->primitive_mode != GenericType::primitive_never;
}

/**
 * \brief Generate code to initialize a data structure.
 * 
 * This will default initialize all primitive types (which is a no-op except
 * for ConstantType), and initialize generic types using their MoveConstructible
 * or CopyConstructible implementations.
 * 
 * \param extra A term to evaluate inside the initialization code such that if this
 * term raises an error, destructors will be run.
 * 
 * \param which 0=init, 1=move_init, 2=copy_init.
 * 
 * \return NULL if no code was generated, otherwise a tree in which the effects of
 * \c inner are included.
 */
TreePtr<Term> lifecycle_init_common(const TreePtr<Term>& dest, const TreePtr<Term>& src,
                                    const SourceLocation& location, const TreePtr<Term>& inner,
                                    int which) {
  CompileContext& compile_context = dest.compile_context();
  
  TreePtr<PointerType> ptr_type = dyn_treeptr_cast<PointerType>(dest->type);
  if (!ptr_type)
    compile_context.error_throw(location, "Cannot generate initialization code for non-pointer value");
  const TreePtr<Term>& type = ptr_type->target_type;
  
  if (TreePtr<StructType> struct_type = dyn_treeptr_cast<StructType>(type)) {
    TreePtr<Term> result = inner;

    PSI_STD::vector<TreePtr<Statement> > statements;
    // Must generate Try-Finally statements in reverse order so that the innermost one initializes the last member
    for (std::size_t ii = 0, ie = struct_type->members.size(); ii != ie; ++ii) {
      std::size_t idx = ie - ii - 1;
      
      TreePtr<Term> dest_member_ptr(new ElementPtr(dest, idx, location));
      TreePtr<Statement> src_member_ptr_stmt, dest_member_ptr_stmt(new Statement(dest_member_ptr, statement_mode_value, location));
      TreePtr<StatementRef> src_member_ptr_stmt_ref, dest_member_ptr_stmt_ref(new StatementRef(dest_member_ptr_stmt, location));
      if (src) {
        TreePtr<Term> src_member_ptr(new ElementPtr(src, idx, location));
        src_member_ptr_stmt.reset(new Statement(src_member_ptr, statement_mode_value, location));
      }
      
      TreePtr<Term> member_result = lifecycle_init_common(dest_member_ptr_stmt_ref, src_member_ptr_stmt_ref, location, result, which);
      if (member_result) {
        statements.push_back(dest_member_ptr_stmt);
        statements.push_back(src_member_ptr_stmt);
        result = member_result;
      }
    }

    if (!statements.empty()) {
      std::reverse(statements.begin(), statements.end());
      result.reset(new Block(statements, result, location));
      return result;
    } else {
      return TreePtr<Term>();
    }
  } else if (TreePtr<ConstantType> const_type = dyn_treeptr_cast<ConstantType>(type)) {
    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<TypeInstance> inst_type = dyn_treeptr_cast<TypeInstance>(type)) {
    if (!lifecycle_primitive(inst_type)) {
      TreePtr<Term> movable(new InterfaceValue(compile_context.builtins().movable_interface, vector_of(type), location));
      TreePtr<Term> init_call;
      
      switch (which) {
      case 0: {
        TreePtr<Term> init_func(new ElementValue(movable, interface_movable_init, location));
        init_call.reset(new FunctionCall(init_func, vector_of(movable, dest), location));
        break;
      }
      case 1: {
        TreePtr<Term> init_func(new ElementValue(movable, interface_movable_move_init, location));
        init_call.reset(new FunctionCall(init_func, vector_of(movable, dest, src), location));
        break;
      }
      case 2: {
        TreePtr<Term> copyable(new InterfaceValue(compile_context.builtins().copyable_interface, vector_of(type), location));
        TreePtr<Term> init_func(new ElementValue(copyable, interface_copyable_copy_init, location));
        init_call.reset(new FunctionCall(init_func, vector_of(copyable, dest, src), location));
        break;
      }
      default: PSI_FAIL("unreachable");
      }
      
      TreePtr<Term> result = Block::make(location, vector_of(init_call), inner);
      TreePtr<Term> cleanup_func(new ElementValue(movable, interface_movable_fini, location));
      TreePtr<Term> cleanup(new FunctionCall(cleanup_func, vector_of(movable, dest), location));
      result.reset(new TryFinally(result, cleanup, true, location));
      return result;
    } else if (which == 0) {
      // No need to initialize primitive types
      return TreePtr<Term>();
    } else {
      // Copy value from original data structure
      PSI_NOT_IMPLEMENTED();
      return inner;
    }
  } else {
    return TreePtr<Term>();
  }
}

/**
 * \brief Generate code to initialize a data structure.
 * 
 * This will default initialize all primitive types (which is a no-op except
 * for ConstantType), and initialize generic types using their MoveConstructible
 * implementations.
 * 
 * \param extra A term to evaluate inside the initialization code such that if this
 * term raises an error, destructors will be run.
 */
TreePtr<Term> lifecycle_init(const TreePtr<Term>& pointer, const SourceLocation& location, const TreePtr<Term>& inner) {
  TreePtr<Term> result = lifecycle_init_common(pointer, TreePtr<Term>(), location, inner, 0);
  return result ? result : inner;
}

TreePtr<Term> lifecycle_move_init(const TreePtr<Term>& dest_pointer, const TreePtr<Term>& src_pointer, const SourceLocation& location, const TreePtr<Term>& inner) {
  TreePtr<Term> result = lifecycle_init_common(dest_pointer, src_pointer, location, inner, 1);
  return result ? result : inner;
}

TreePtr<Term> lifecycle_copy_init(const TreePtr<Term>& dest_pointer, const TreePtr<Term>& src_pointer, const SourceLocation& location, const TreePtr<Term>& inner) {
  TreePtr<Term> result = lifecycle_init_common(dest_pointer, src_pointer, location, inner, 2);
  return result ? result : inner;
}

/**
 * Implement fini, move and copy.
 * 
 * \param which fini=0, move=1, copy=2.
 */
TreePtr<Term> lifecycle_postinit_common(const TreePtr<Term>& dest, const TreePtr<Term>& src, const SourceLocation& location, int which) {
  CompileContext& compile_context = dest.compile_context();
  
  TreePtr<PointerType> ptr_type = dyn_treeptr_cast<PointerType>(dest->type);
  if (!ptr_type)
    compile_context.error_throw(location, "Cannot generate finalization code for non-pointer value");
  const TreePtr<Term>& type = ptr_type->target_type;
  
  if (TreePtr<StructType> struct_type = dyn_treeptr_cast<StructType>(type)) {
    PSI_STD::vector<TreePtr<Statement> > statements;
    for (std::size_t ii = 0, ie = struct_type->members.size(); ii != ie; ++ii) {
      TreePtr<Term> dest_member_ptr(new ElementPtr(dest, ii, location));
      TreePtr<Statement> src_member_ptr_stmt, dest_member_ptr_stmt(new Statement(dest_member_ptr, statement_mode_value, location));
      TreePtr<StatementRef> src_member_ptr_stmt_ref, dest_member_ptr_stmt_ref(new StatementRef(dest_member_ptr_stmt, location));
      if (src) {
        TreePtr<Term> src_member_ptr(new ElementPtr(src, ii, location));
        src_member_ptr_stmt.reset(new Statement(src_member_ptr, statement_mode_value, location));
      }
      
      TreePtr<Term> member_result = lifecycle_postinit_common(dest_member_ptr_stmt_ref, src_member_ptr_stmt_ref, location, which);
      if (member_result) {
        statements.push_back(dest_member_ptr_stmt);
        statements.push_back(src_member_ptr_stmt);
        statements.push_back(TreePtr<Statement>(new Statement(member_result, statement_mode_destroy, member_result.location())));
      }
    }
    
    if (!statements.empty()) {
      TreePtr<Term> empty(new DefaultValue(compile_context.builtins().empty_type, location));
      return TreePtr<Term>(new Block(statements, empty, location));
    }
  } else if (TreePtr<TypeInstance> inst_type = dyn_treeptr_cast<TypeInstance>(type)) {
    if (!lifecycle_primitive(inst_type)) {
      switch (which) {
      case 0: {
        TreePtr<Term> movable(new InterfaceValue(compile_context.builtins().movable_interface, vector_of(type), location));
        TreePtr<Term> fini_func(new ElementValue(movable, interface_movable_fini, location));
        return TreePtr<Term>(new FunctionCall(fini_func, vector_of(movable, dest), location));
      }
      
      case 1: {
        TreePtr<Term> movable(new InterfaceValue(compile_context.builtins().movable_interface, vector_of(type), location));
        TreePtr<Term> move_func(new ElementValue(movable, interface_movable_move, location));
        return TreePtr<Term>(new FunctionCall(move_func, vector_of(movable, dest, src), location));
      }
      
      case 2: {
        TreePtr<Term> copyable(new InterfaceValue(compile_context.builtins().copyable_interface, vector_of(type), location));
        TreePtr<Term> copy_func(new ElementValue(copyable, interface_copyable_copy, location));
        return TreePtr<Term>(new FunctionCall(copy_func, vector_of(copyable, dest, src), location));
      }
      default: PSI_FAIL("unreachable");
      }
    } else if (which != 0) {
      PSI_NOT_IMPLEMENTED();
    }
  }
  
  return TreePtr<Term>();
}

/**
 * \brief Generate code to finalize a data structure.
 * 
 * This will find any members which have custom finalizers and generate code to call them.
 */
TreePtr<Term> lifecycle_fini(const TreePtr<Term>& pointer, const SourceLocation& location) {
  return lifecycle_postinit_common(pointer, TreePtr<Term>(), location, 0);
}

TreePtr<Term> lifecycle_move(const TreePtr<Term>& dest_pointer, const TreePtr<Term>& src_pointer, const SourceLocation& location) {
  return lifecycle_postinit_common(dest_pointer, src_pointer, location, 1);
}

TreePtr<Term> lifecycle_copy(const TreePtr<Term>& dest_pointer, const TreePtr<Term>& src_pointer, const SourceLocation& location) {
  return lifecycle_postinit_common(dest_pointer, src_pointer, location, 2);
}
}
}
