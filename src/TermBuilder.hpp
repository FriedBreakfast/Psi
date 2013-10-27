#ifndef HPP_PSI_TERM_BUILDER
#define HPP_PSI_TERM_BUILDER

#include "Utility.hpp"
#include "Tree.hpp"

namespace Psi {
namespace Compiler {
struct PSI_COMPILER_EXPORT TermBuilder : NonConstructible {
/// \name Types
//@{
  static TreePtr<Term> metatype(CompileContext& compile_context);
  static TreePtr<Term> size_type(CompileContext& compile_context);
  static TreePtr<Term> bottom_type(CompileContext& compile_context);
  static TreePtr<Term> empty_type(CompileContext& compile_context);
  static TreePtr<Term> boolean_type(CompileContext& compile_context);
  static TreePtr<Term> upref_type(CompileContext& compile_context);
  
  static TreePtr<Term> number_type(CompileContext& compile_context, NumberType::ScalarType type);
  static TreePtr<Term> pointer(const TreePtr<Term>& type, const TreePtr<Term>& upref, const SourceLocation& location);
  static TreePtr<Term> pointer(const TreePtr<Term>& type, const SourceLocation& location);
  static TreePtr<Term> exists(const TreePtr<Term>& result_type, const PSI_STD::vector<TreePtr<Term> >& parameter_types, const SourceLocation& location);
  static TreePtr<Term> exists_parameter(const TreePtr<Term>& exists_term, unsigned index, const SourceLocation& location);
  static TreePtr<Term> exists_value(const TreePtr<Term>& exists_term, const SourceLocation& location);
  static TreePtr<Term> constant(const TreePtr<Term>& value, const SourceLocation& location);
  static TreePtr<FunctionType> function_type(ResultMode result_mode, const TreePtr<Term>& result_type,
                                             const PSI_STD::vector<FunctionParameterType>& parameter_types,
                                             const PSI_STD::vector<TreePtr<InterfaceValue> >& interfaces, const SourceLocation& location);
  static TreePtr<ArrayType> array_type(const TreePtr<Term>& element_type, const TreePtr<Term>& length, const SourceLocation& location);
  static TreePtr<ArrayType> array_type(const TreePtr<Term>& element_type, unsigned length, const SourceLocation& location);
  static TreePtr<StructType> struct_type(CompileContext& compile_context, const PSI_STD::vector<TreePtr<Term> >& member_types, const SourceLocation& location);
  
  static TreePtr<Term> string_type(const TreePtr<Term>& length, const SourceLocation& location);
  static TreePtr<Term> string_type(unsigned length, CompileContext& compile_context, const SourceLocation& location);

  /**
   * \brief Create a new generic type
   */
  template<typename T, typename U, typename V>
  static TreePtr<GenericType> generic(CompileContext& compile_context, const PSI_STD::vector<TreePtr<Term> >& pattern,
                                      const T& primitive_mode, const SourceLocation& location, const U& member_callback, const V& overloads_callback) {
    return tree_from(::new GenericType(compile_context, pattern, primitive_mode, member_callback, overloads_callback, location));
  }

  /**
   * \brief Create a new generic type
   */
  template<typename T, typename U>
  static TreePtr<GenericType> generic(CompileContext& compile_context, const PSI_STD::vector<TreePtr<Term> >& pattern,
                                      const T& primitive_mode, const SourceLocation& location, const U& member_callback) {
    return tree_from(::new GenericType(compile_context, pattern, primitive_mode, member_callback, default_, location));
  }
  
  static TreePtr<TypeInstance> instance(const TreePtr<GenericType>& generic, const PSI_STD::vector<TreePtr<Term> >& parameters, const SourceLocation& location);
  static TreePtr<TypeInstance> instance(const TreePtr<GenericType>& generic, const SourceLocation& location);
//@}
  
/// \name Constructors
//@{
  static TreePtr<Term> empty_value(CompileContext& compile_context);
  static TreePtr<Term> movable(const TreePtr<Term>& value, const SourceLocation& location);
  static TreePtr<Term> default_value(const TreePtr<Term>& type, const SourceLocation& location);
  static TreePtr<Term> integer_value(CompileContext& context, NumberType::ScalarType type, uint64_t value, const SourceLocation& location);
  static TreePtr<Term> upref(const TreePtr<Term>& outer_type, const TreePtr<Term>& outer_index, const TreePtr<Term>& next, const SourceLocation& location);
  static TreePtr<Term> upref(const TreePtr<Term>& outer_type, unsigned outer_index, const TreePtr<Term>& next, const SourceLocation& location);
  static TreePtr<Term> upref_null(CompileContext& compile_context);
  
  static TreePtr<Term> size_value(unsigned index, CompileContext& compile_context, const SourceLocation& location);
  static unsigned size_from(const TreePtr<Term>& value, const SourceLocation& location);
  static bool size_equals(const TreePtr<Term>& value, std::size_t n);
  
  static TreePtr<Term> struct_value(const TreePtr<StructType>& type, const PSI_STD::vector<TreePtr<Term> >& members, const SourceLocation& location);
  static TreePtr<Term> struct_value(CompileContext& compile_context, const PSI_STD::vector<TreePtr<Term> >& members, const SourceLocation& location);
  static TreePtr<Term> string_value(CompileContext& compile_context, const String& data, const SourceLocation& location);
  static TreePtr<Term> instance_value(const TreePtr<TypeInstance>& instance, const TreePtr<Term>& member_value, const SourceLocation& location);

  static TreePtr<Term> interface_value(const TreePtr<Interface>& interface, const PSI_STD::vector<TreePtr<Term> >& parameters, const TreePtr<Implementation>& implementation, const SourceLocation& location);
  static TreePtr<Term> interface_value(const TreePtr<Interface>& interface, const PSI_STD::vector<TreePtr<Term> >& parameters, const SourceLocation& location);
//@}
  
/// \brief Aggregate type access
//@{
  static TreePtr<Term> element_value(const TreePtr<Term>& aggregate, const TreePtr<Term>& index, const SourceLocation& location);
  static TreePtr<Term> element_value(const TreePtr<Term>& aggregate, unsigned index, const SourceLocation& location);
  static TreePtr<Term> element_pointer(const TreePtr<Term>& aggregate, const TreePtr<Term>& index, const SourceLocation& location);
  static TreePtr<Term> element_pointer(const TreePtr<Term>& aggregate, unsigned index, const SourceLocation& location);
  static TreePtr<Term> element_type(const TreePtr<Term>& aggregate_type, unsigned index, const SourceLocation& location);
  static TreePtr<Term> ptr_target(const TreePtr<Term>& pointer, const SourceLocation& location);
  static TreePtr<Term> ptr_to(const TreePtr<Term>& value, const SourceLocation& location);
  static TreePtr<Term> outer_pointer(const TreePtr<Term>& reference, const SourceLocation& location);
//@}

/// \name Lifecycle functions
//@{
  static TreePtr<Term> initialize_value(const TreePtr<Term>& target_ref, const TreePtr<Term>& assign_value, const TreePtr<Term>& inner, const SourceLocation& location);
  static TreePtr<Term> finalize_value(const TreePtr<Term>& target_ref, const SourceLocation& location);
  static TreePtr<Term> assign_value(const TreePtr<Term>& target_ref, const TreePtr<Term>& assign_value, const SourceLocation& location);
//@}
  
/// \name Control flow
//@{
  static TreePtr<Statement> statement(const TreePtr<Term>& value, StatementMode mode, const SourceLocation& location);
  static TreePtr<Term> block(const PSI_STD::vector<TreePtr<Statement> >& statements, const TreePtr<Term>& result, const SourceLocation& location);
  static TreePtr<Term> block(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& values, const TreePtr<Term>& result=TreePtr<Term>());
  static TreePtr<Term> function_call(const TreePtr<Term>& function, const PSI_STD::vector<TreePtr<Term> >& arguments, const SourceLocation& location);
  static TreePtr<Term> solidify_during(const PSI_STD::vector<TreePtr<Term> >& value, const TreePtr<Term>& body, const SourceLocation& location);
  static TreePtr<Term> introduce_implementation(const PSI_STD::vector<TreePtr<Implementation> >& implementations, const TreePtr<Term>& value, const SourceLocation& location);
  static TreePtr<JumpTo> jump_to(const TreePtr<JumpTarget>& target, const TreePtr<Term>& argument, const SourceLocation& location);
  static TreePtr<JumpTarget> jump_target(const TreePtr<Term>& value, const SourceLocation& location);
  static TreePtr<JumpTarget> jump_target(const TreePtr<Term>& value, StatementMode argument_mode, const TreePtr<Anonymous>& argument, const SourceLocation& location);
  static TreePtr<JumpTarget> exit_target(const TreePtr<Term>& type, ResultMode result_mode, const SourceLocation& location);
  static TreePtr<FunctionalEvaluate> functional_eval(const TreePtr<Term>& value, const SourceLocation& location);
  static TreePtr<Term> to_functional(const TreePtr<Term>& value, const SourceLocation& location);
  static void to_functional(PSI_STD::vector<TreePtr<Term> >& values, const SourceLocation& location);
//@}
  
/// \name Globals
//@{
  /**
   * \brief Create a global function.
   */
  template<typename T>
  static TreePtr<ModuleGlobal> function(const TreePtr<Module>& module, const TreePtr<FunctionType>& type, Linkage linkage,
                                        const PSI_STD::vector<TreePtr<Anonymous> >& arguments,
                                        const TreePtr<JumpTarget>& return_target, const SourceLocation& location,
                                        const T& body_callback, const String& symbol_name=String()) {
    return tree_from(::new Function(module, symbol_name, type, linkage, arguments, return_target, location, body_callback));
  }
  
  /**
   * \brief Create a global variable.
   */
  template<typename T>
  static TreePtr<ModuleGlobal> global_variable(const TreePtr<Module>& module, const TreePtr<Term>& type, Linkage linkage,
                                               bool constant, bool merge, const SourceLocation& location,
                                               const T& value_callback, const String& symbol_name=String()) {
    return tree_from(::new GlobalVariable(module, symbol_name, type, linkage, constant, merge, location, value_callback));
  }

  static TreePtr<Global> global_variable(const TreePtr<Module>& module, Linkage linkage, bool constant, bool merge, const SourceLocation& location, const TreePtr<Term>& value);
  static TreePtr<Term> to_global_functional(const TreePtr<Module>& module, const TreePtr<Term>& value, const SourceLocation& location);
  static TreePtr<Term> global_evaluate(const TreePtr<Module>& module, const TreePtr<Term>& value, const SourceLocation& location);
  static TreePtr<GlobalStatement> global_statement(const TreePtr<Module>& module, const TreePtr<Term>& value, StatementMode mode, const SourceLocation& location);
//@}

/// \name External functions
//@{
  static TreePtr<Library> library(const TreePtr<TargetCallback>& callback, const SourceLocation& location);
  static TreePtr<Term> library_symbol(const TreePtr<Library>& library, const TreePtr<TargetCallback>& callback, const TreePtr<Term>& type, const SourceLocation& location);
//@}
  
  static TreePtr<Term> parameter(const TreePtr<Term>& type, unsigned depth, unsigned index, const SourceLocation& location);
  static TreePtr<Anonymous> anonymous(const TreePtr<Term>& type, TermMode mode, const SourceLocation& location);
};
}
}

#endif
