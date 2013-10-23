#include "TermBuilder.hpp"

namespace Psi {
namespace Compiler {
/// \brief Get the type of types
TreePtr<Term> TermBuilder::metatype(CompileContext& compile_context) {
  return compile_context.builtins().metatype;
}

/// \brief Get the type of expressions which cannot exit normally
TreePtr<Term> TermBuilder::bottom_type(CompileContext& compile_context) {
  return compile_context.builtins().bottom_type;
}

/// \brief Get the empty type
TreePtr<Term> TermBuilder::empty_type(CompileContext& compile_context) {
  return compile_context.builtins().empty_type;
}

/// \brief Get the empty type
TreePtr<Term> TermBuilder::boolean_type(CompileContext& compile_context) {
  return compile_context.builtins().boolean_type;
}

/// \brief Get the type of upward references
TreePtr<Term> TermBuilder::upref_type(CompileContext& compile_context) {
  return compile_context.builtins().upref_type;
}

TreePtr<Term> TermBuilder::size_type(CompileContext& compile_context) {
  return compile_context.builtins().uptr_type;
}

/// \brief Get a primitive type
TreePtr<Term> TermBuilder::number_type(CompileContext& compile_context, NumberType::ScalarType type) {
  const BuiltinTypes& b = compile_context.builtins();
  switch (type) {
  case NumberType::n_bool: return b.boolean_type;
  case NumberType::n_i8: return b.i8_type;
  case NumberType::n_i16: return b.i16_type;
  case NumberType::n_i32: return b.i32_type;
  case NumberType::n_i64: return b.i64_type;
  case NumberType::n_iptr: return b.iptr_type;
  case NumberType::n_u8: return b.u8_type;
  case NumberType::n_u16: return b.u16_type;
  case NumberType::n_u32: return b.u32_type;
  case NumberType::n_u64: return b.u64_type;
  case NumberType::n_uptr: return b.uptr_type;
  case NumberType::n_f32:
  case NumberType::n_f64: PSI_FAIL("Floating point types not yet implemented");
  default: PSI_FAIL("Unknown number type key");
  }
}

/// \brief Get a pointer type
TreePtr<Term> TermBuilder::pointer(const TreePtr<Term>& type, const TreePtr<Term>& upref, const SourceLocation& location) {
  return type->compile_context().get_functional(PointerType(type, upref, location), location);
}

/// \brief Get a pointer type
TreePtr<Term> TermBuilder::pointer(const TreePtr<Term>& type, const SourceLocation& location) {
  return pointer(type, upref_null(type->compile_context()), location);
}

/// \brief Get a type for <tt>exists x.f(x)</tt>
TreePtr<Term> TermBuilder::exists(const TreePtr<Term>& result_type, const PSI_STD::vector<TreePtr<Term> >& parameter_types, const SourceLocation& location) {
  return result_type->compile_context().get_functional(Exists(result_type, parameter_types, location), location);
}

/// \brief Get a constant type for a value
TreePtr<Term> TermBuilder::constant(const TreePtr<Term>& value, const SourceLocation& location) {
  return value->compile_context().get_functional(ConstantType(value, location), location);
}

/// \brief Get a function type.
TreePtr<FunctionType> TermBuilder::function_type(ResultMode result_mode, const TreePtr<Term>& result_type,
                                                 const PSI_STD::vector<FunctionParameterType>& parameter_types,
                                                 const PSI_STD::vector<TreePtr<InterfaceValue> >& interfaces, const SourceLocation& location) {
  return result_type->compile_context().get_functional(FunctionType(result_mode, result_type, parameter_types, interfaces, location), location);
}

/// \brief Get an array type.
TreePtr<ArrayType> TermBuilder::array_type(const TreePtr<Term>& element_type, const TreePtr<Term>& length, const SourceLocation& location) {
  return element_type->compile_context().get_functional(ArrayType(element_type, length, location), location);
}

/// \copydoc TermBuilder::array_type
TreePtr<ArrayType> TermBuilder::array_type(const TreePtr<Term>& element_type, unsigned length, const SourceLocation& location) {
  return array_type(element_type, size_value(length, element_type->compile_context(), location), location);
}

/// \brief Get a struct type.
TreePtr<StructType> TermBuilder::struct_type(CompileContext& compile_context, const PSI_STD::vector<TreePtr<Term> >& member_types, const SourceLocation& location) {
  return compile_context.get_functional(StructType(member_types, location), location);
}

/// \brief Get a string type of fixed length.
TreePtr<Term> TermBuilder::string_type(const TreePtr<Term>& length, const SourceLocation& location) {
  return array_type(length->compile_context().builtins().u8_type, length, location);
}

/// \copydoc TermBuilder::string_type
TreePtr<Term> TermBuilder::string_type(unsigned length, CompileContext& compile_context, const SourceLocation& location) {
  return string_type(size_value(length, compile_context, location), location);
}

/**
 * \brief Create a new instance of a generic type.
 */
TreePtr<TypeInstance> TermBuilder::instance(const TreePtr<GenericType>& generic, const PSI_STD::vector<TreePtr<Term> >& parameters, const SourceLocation& location) {
  return generic->compile_context().get_functional(TypeInstance(generic, parameters), location);
}

/// \brief Get the value of the empty type.
TreePtr<Term> TermBuilder::empty_value(CompileContext& compile_context) {
  return compile_context.builtins().empty_value;
}

/**
 * \brief Make the tree \c value movable.
 * 
 * If \c value is a reference to an lvalue, this will return a reference to an rvalue
 * of the same type. This means that normally read only operations acting on the result
 * of \c value will expect to be able to modify it.
 */
TreePtr<Term> TermBuilder::movable(const TreePtr<Term>& value, const SourceLocation& location) {
  if (value->mode == term_mode_lref)
    return value->compile_context().get_functional(MovableValue(value), location);
  else
    return value;
}

/// \brief Get the default value of a given type.
TreePtr<Term> TermBuilder::default_value(const TreePtr<Term>& type, const SourceLocation& location) {
  return type->compile_context().get_functional(DefaultValue(type, location), location);
}

/// \brief Create a value using a builtin constructor
TreePtr<Term> TermBuilder::integer_value(CompileContext& context, NumberType::ScalarType type, uint64_t value, const SourceLocation& location) {
  return context.get_functional(IntegerConstant(type, value), location);
}

/// \brief Get an upward reference
TreePtr<Term> TermBuilder::upref(const TreePtr<Term>& outer_type, const TreePtr<Term>& outer_index, const TreePtr<Term>& next, const SourceLocation& location) {
  PSI_ASSERT(outer_type || tree_isa<UpwardReference>(next));
  const Term *nonnull_term = outer_type ? outer_type.get() : next.get();
  return nonnull_term->compile_context().get_functional(UpwardReference(outer_type, outer_index, next, location), location);
}

/// \copydoc TermBuilder::upref
TreePtr<Term> TermBuilder::upref(const TreePtr<Term>& outer_type, unsigned outer_index, const TreePtr<Term>& next, const SourceLocation& location) {
  PSI_ASSERT(outer_type || tree_isa<UpwardReference>(next));
  const Term *nonnull_term = outer_type ? outer_type.get() : next.get();
  return upref(outer_type, size_value(outer_index, nonnull_term->compile_context(), location), next, location);
}

/// \brief Get the value of the empty type.
TreePtr<Term> TermBuilder::upref_null(CompileContext& compile_context) {
  return compile_context.builtins().upref_null;
}

/**
 * \brief Create an index term from an integer.
 */
TreePtr<Term> TermBuilder::size_value(unsigned index, CompileContext& compile_context, const SourceLocation& location) {
  return compile_context.get_functional(IntegerConstant(NumberType::n_uptr, index), location);
}

/**
 * \brief Convert a constant index to an integer.
 * 
 * \param location Location for error reporting.
 */
unsigned TermBuilder::size_from(const TreePtr<Term>& value, const SourceLocation& location) {
  TreePtr<IntegerConstant> inner = term_unwrap_dyn_cast<IntegerConstant>(value);
  if (!inner)
    value->compile_context().error_throw(location, "Expected a constant integer value");
  return inner->value;
}

/**
 * \brief Compare a constant index to an integer.
 */
bool TermBuilder::size_equals(const TreePtr<Term>& value, std::size_t n) {
  TreePtr<IntegerConstant> inner = term_unwrap_dyn_cast<IntegerConstant>(value);
  return inner && (n == inner->value);
}

/// \brief Value for StructType types.
TreePtr<Term> TermBuilder::struct_value(const TreePtr<StructType>& type, const PSI_STD::vector<TreePtr<Term> >& members, const SourceLocation& location) {
  return type->compile_context().get_functional(StructValue(type, members), location);
}

/// \brief Value for StructType types.
TreePtr<Term> TermBuilder::struct_value(CompileContext& compile_context, const PSI_STD::vector<TreePtr<Term> >& members, const SourceLocation& location) {
  PSI_STD::vector<TreePtr<Term> > member_types;
  member_types.reserve(members.size());
  for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = members.begin(), ie = members.end(); ii != ie; ++ii)
    member_types.push_back((*ii)->type);
  return struct_value(struct_type(compile_context, member_types, location), members, location);
}

/**
 * \brief Get a string value.
 * 
 * This type of this will be an array of chars.
 */
TreePtr<Term> TermBuilder::string_value(CompileContext& compile_context, const String& data, const SourceLocation& location) {
  return compile_context.get_functional(StringValue(data), location);
}

/// \brief Value for TypeInstance, an instance of a GenericType
TreePtr<Term> TermBuilder::instance_value(const TreePtr<TypeInstance>& instance, const TreePtr<Term>& member_value, const SourceLocation& location) {
  return instance->compile_context().get_functional(TypeInstanceValue(instance, member_value), location);
}

/// \brief Get the value of an interface with given parameters
TreePtr<Term> TermBuilder::interface_value(const TreePtr<Interface>& interface, const PSI_STD::vector<TreePtr<Term> >& parameters, const TreePtr<Implementation>& implementation, const SourceLocation& location) {
  return interface->compile_context().get_functional(InterfaceValue(interface, parameters, implementation), location);
}

/// \brief Get the value of an interface with given parameters
TreePtr<Term> TermBuilder::interface_value(const TreePtr<Interface>& interface, const PSI_STD::vector<TreePtr<Term> >& parameters, const SourceLocation& location) {
  return interface_value(interface, parameters, default_, location);
}

/// \brief Get the value of an aggregate member from an aggregate.
TreePtr<Term> TermBuilder::element_value(const TreePtr<Term>& aggregate, const TreePtr<Term>& index, const SourceLocation& location) {
  return aggregate->compile_context().get_functional(ElementValue(aggregate, index), location);
}

/// \copydoc TermBuilder::element_value
TreePtr<Term> TermBuilder::element_value(const TreePtr<Term>& aggregate, unsigned index, const SourceLocation& location) {
  return element_value(aggregate, size_value(index, aggregate->compile_context(), location), location);
}

/// \brief Get a pointer to an element of an aggreagte from a poiner to that aggregate
TreePtr<Term> TermBuilder::element_pointer(const TreePtr<Term>& aggregate, const TreePtr<Term>& index, const SourceLocation& location) {
  return aggregate->compile_context().get_functional(ElementPointer(aggregate, index), location);
}

/// \copydoc TermBuilder::element_pointer
TreePtr<Term> TermBuilder::element_pointer(const TreePtr<Term>& aggregate, unsigned index, const SourceLocation& location) {
  return element_pointer(aggregate, size_value(index, aggregate->compile_context(), location), location);
}

/**
 * \brief Convert a pointer into a reference.
 */
TreePtr<Term> TermBuilder::ptr_target(const TreePtr<Term>& pointer, const SourceLocation& location) {
  return pointer->compile_context().get_functional(PointerTarget(pointer), location);
}

/**
 * \brief Convert a reference into a pointer.
 */
TreePtr<Term> TermBuilder::ptr_to(const TreePtr<Term>& value, const SourceLocation& location) {
  return value->compile_context().get_functional(PointerTo(value), location);
}

/**
 * \brief Get a reference to an outer object from an object whose type is Pointer.
 */
TreePtr<Term> TermBuilder::outer_pointer(const TreePtr<Term>& reference, const SourceLocation& location) {
  return reference->compile_context().get_functional(OuterPointer(reference), location);
}

/**
 * \brief Initializes an object at a memory location.
 * 
 * The term \c inner is evaluated after the object has been initialized;
 * however should \c inner exit abnormally the object will be automatically
 * finalized.
 */
TreePtr<Term> TermBuilder::initialize_value(const TreePtr<Term>& target_ptr, const TreePtr<Term>& assign_value, const TreePtr<Term>& inner, const SourceLocation& location) {
  return tree_from(::new InitializeValue(target_ptr, assign_value, inner, location));
}

/// \brief Finalize an object at a memory location.
TreePtr<Term> TermBuilder::finalize_value(const TreePtr<Term>& target_ptr, const SourceLocation& location) {
  return tree_from(::new FinalizeValue(target_ptr, location));
}

/// \brief Assign a value to an existing object at a memory location.
TreePtr<Term> TermBuilder::assign_value(const TreePtr<Term>& target_ptr, const TreePtr<Term>& assign_value, const SourceLocation& location) {
  return tree_from(::new AssignValue(target_ptr, assign_value, location));
}

/**
 * \brief Create a new Statement.
 * 
 * Statement values are evaluated inside blocks, and other references to the statement object
 * are taken to re-use a previous value (which must be in scope).
 */
TreePtr<Statement> TermBuilder::statement(const TreePtr<Term>& value, StatementMode mode, const SourceLocation& location) {
  return tree_from(::new Statement(value, mode, location));
}

/// \brief Create a block
TreePtr<Term> TermBuilder::block(const PSI_STD::vector<TreePtr<Statement> >& statements, const TreePtr<Term>& result, const SourceLocation& location) {
  return tree_from(::new Block(statements, result, location));
}

TreePtr<Term> TermBuilder::block(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& values, const TreePtr<Term>& result) {
  PSI_STD::vector<TreePtr<Statement> > statements;
  statements.reserve(values.size());
  for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = values.begin(), ie = values.end(); ii != ie; ++ii)
    statements.push_back(statement(*ii, statement_mode_destroy, location));
  TreePtr<Term> my_result;
  if (result) {
    my_result = result;
  } else {
    PSI_ASSERT(!values.empty());
    my_result = values.front()->compile_context().builtins().empty_value;
  }
  return block(statements, my_result, location);
}

/**
 * \brief Call a function.
 */
TreePtr<Term> TermBuilder::function_call(const TreePtr<Term>& function, const PSI_STD::vector<TreePtr<Term> >& arguments, const SourceLocation& location) {
  // Need to copy arguments array because the FunctionCall constructor will steal the memory
  PSI_STD::vector<TreePtr<Term> > arguments_copy(arguments);
  return tree_from(::new FunctionCall(function, arguments_copy, location));
}

/**
 * \brief Make a list of phantom ConstantType values available during evaluation of \c body.
 */
TreePtr<Term> TermBuilder::solidify_during(const PSI_STD::vector<TreePtr<Term> >& value, const TreePtr<Term>& body, const SourceLocation& location) {
  return tree_from(::new SolidifyDuring(value, body, location));
}

TreePtr<Term> TermBuilder::introduce_implementation(const PSI_STD::vector<TreePtr<Implementation> >& implementations, const TreePtr<Term>& value, const SourceLocation& location) {
  return tree_from(::new IntroduceImplementation(implementations, value, location));
}

/// \brief Jump to a label
TreePtr<JumpTo> TermBuilder::jump_to(const TreePtr<JumpTarget>& target, const TreePtr<Term>& argument, const SourceLocation& location) {
  return tree_from(::new JumpTo(target, argument, location));
}

/// \brief Create a jump label
TreePtr<JumpTarget> TermBuilder::jump_target(const TreePtr<Term>& value, StatementMode argument_mode, const TreePtr<Anonymous>& argument, const SourceLocation& location) {
  ResultMode result_mode;
  switch (argument_mode) {
  case statement_mode_value: result_mode = result_mode_by_value; break;
  case statement_mode_functional: result_mode = result_mode_functional; break;
  case statement_mode_ref: result_mode = result_mode_lvalue; break;
  case statement_mode_destroy: value->compile_context().error_throw(location, "Jump target argument mode may not be 'destroy'");
  default: PSI_FAIL("Unrecognised statement mode");
  }
  return tree_from(::new JumpTarget(value, result_mode, argument, location));
}

/// \brief Create a jump label
TreePtr<JumpTarget> TermBuilder::jump_target(const TreePtr<Term>& value, const SourceLocation& location) {
  return tree_from(::new JumpTarget(value, result_mode_by_value, TreePtr<Anonymous>(), location));
}

/// \brief Create a function exit label
TreePtr<JumpTarget> TermBuilder::exit_target(const TreePtr<Term>& type, ResultMode result_mode, const SourceLocation& location) {
  TreePtr<Anonymous> argument = anonymous(type, term_mode_value, location);
  return tree_from(::new JumpTarget(TreePtr<Term>(), result_mode, argument, location));
}

/**
 * \brief Create a tree which runs a mutating evaluation and returns a functional result.
 * 
 * This tree marks where the tree should be evaluated and allows the result to be referred back to later.
 */
TreePtr<FunctionalEvaluate> TermBuilder::functional_eval(const TreePtr<Term>& value, const SourceLocation& location) {
  return tree_from(::new FunctionalEvaluate(value, location));
}

/**
 * \brief Wrap value in a FunctionalEvaluate tree if it is not a functional value already.
 * 
 * This wraps non-pure values and references.
 */
TreePtr<Term> TermBuilder::to_functional(const TreePtr<Term>& value, const SourceLocation& location) {
  if (!value)
    return TreePtr<Term>();
  
  PSI_ASSERT(!value->type || value->type->is_functional());
  if (!value->pure || (value->mode != term_mode_value))
    return functional_eval(value, location);
  else
    return value;
}

/**
 * \brief Wrap every value in a vector in a FunctionalEvaluate tree if it is not pure
 */
void TermBuilder::to_functional(PSI_STD::vector<TreePtr<Term> >& values, const SourceLocation& location) {
  for (PSI_STD::vector<TreePtr<Term> >::iterator ii = values.begin(), ie = values.end(); ii != ie; ++ii)
    *ii = to_functional(*ii, location);
}

/**
 * \brief Get a global variable.
 * 
 * This constructor does not support self-referencing globals, and hence
 * allows a simple value to be passed in.
 */
TreePtr<Global> TermBuilder::global_variable(const TreePtr<Module>& module, Linkage linkage, bool constant, bool merge, const SourceLocation& location, const TreePtr<Term>& value) {
  return global_variable(module, value->type, linkage, constant, merge, location, value);
}

class GlobalEvaluateRewriter : public TermRewriter {
  const TreePtr<Module> *m_module;
  
public:
  static const VtableType vtable;
  
  GlobalEvaluateRewriter(const TreePtr<Module> *module)
  : TermRewriter(&vtable), m_module(module) {}
  
  static TreePtr<Term> rewrite_impl(GlobalEvaluateRewriter& self, const TreePtr<Term>& value) {
    if (!value)
      return TreePtr<Term>();

    PSI_ASSERT(value->pure);
    if (TreePtr<FunctionalEvaluate> eval = dyn_treeptr_cast<FunctionalEvaluate>(value)) {
      return TermBuilder::global_evaluate(*self.m_module, eval->value, eval->location());
    } else if (tree_isa<Global>(value)) {
      return value;
    } else {
      return treeptr_cast<Functional>(value)->rewrite(self, value->location());
    }
  }
};

const TermRewriterVtable GlobalEvaluateRewriter::vtable = PSI_COMPILER_TERM_REWRITER(GlobalEvaluateRewriter, "psi.compiler.GlobalEvaluateRewriter", TermRewriter);

/**
 * \brief Convert a functional term to a global functional term.
 * 
 * This replaces any instances of FunctionalEvaluate with GlobalEvaluate.
 */
TreePtr<Term> TermBuilder::to_global_functional(const TreePtr<Module>& module, const TreePtr<Term>& value, const SourceLocation& PSI_UNUSED(location)) {
  return GlobalEvaluateRewriter(&module).rewrite(value);
}

TreePtr<Term> TermBuilder::global_evaluate(const TreePtr<Module>& module, const TreePtr<Term>& value, const SourceLocation& location) {
  return TreePtr<Term>(::new GlobalEvaluate(module, value, location));
}

/**
 * \brief Get a global statement.
 * 
 * This is a cross between a definition and a global variable:
 * it acts as one or the other according to the \c mode, which
 * behaves like \c mode of Statement.
 */
TreePtr<GlobalStatement> TermBuilder::global_statement(const TreePtr<Module>& module, const TreePtr<Term>& value, StatementMode mode, const SourceLocation& location) {
  return tree_from(::new GlobalStatement(module, value, mode, location));
}

/**
 * \brief Create a library.
 * 
 * A library provides a callback which identifies external libraries providing a
 * set of symbols in a platform-specific way.
 */
TreePtr<Library> TermBuilder::library(const TreePtr<TargetCallback>& callback, const SourceLocation& location) {
  return tree_from(::new Library(callback, location));
}

/**
 * \brief Create a library symbol tree.
 * 
 * A library symbol tree provides a callback which selects a symbol from a library
 * in a platform-specific way.
 */
TreePtr<Term> TermBuilder::library_symbol(const TreePtr<Library>& library, const TreePtr<TargetCallback>& callback, const TreePtr<Term>& type, const SourceLocation& location) {
  return tree_from(::new LibrarySymbol(library, callback, type, location));
}

/**
 * \brief Create a parameter.
 * 
 * Parameters are used to unify patterns: for example, they allow dependent function types
 * to be represented without cycles, so simple hashing can be used to compare them.
 */
TreePtr<Term> TermBuilder::parameter(const TreePtr<Term>& type, unsigned depth, unsigned index, const SourceLocation& location) {
  return type->compile_context().get_functional(Parameter(type, depth, index), location);
}

/**
 * \brief Create an anonymous term.
 * 
 * The value this term will take at runtime is unspecified.
 * This is used to represent function parameters, and is also used as a placeholder during generic
 * type construction before being replaced by Parameter.
 */
TreePtr<Anonymous> TermBuilder::anonymous(const TreePtr<Term>& type, TermMode mode, const SourceLocation& location) {
  return TreePtr<Anonymous>(::new Anonymous(type, mode, location));
}
}
}
