#include "Macros.hpp"
#include "Parser.hpp"
#include "Enums.hpp"
#include "Implementation.hpp"
#include "Interface.hpp"
#include "TermBuilder.hpp"
#include "Aggregate.hpp"

#include <boost/format.hpp>

namespace Psi {
namespace Compiler {
/**
 * \brief Result of building members of an aggregate.
 */
struct AggregateBodyResult {
  /// \brief Member types
  PSI_STD::vector<TreePtr<Term> > members;
  /// \brief Member names
  PSI_STD::map<String, unsigned> names;
  
  GenericType::GenericTypePrimitive primitive_mode;

  /// \brief Do not generate movable interface
  PsiBool no_move;
  /// \brief Do not generate copyable interface
  PsiBool no_copy;

  /// \brief Callback to generate movable interface functions
  PSI_STD::vector<SharedDelayedValue<AggregateMovableResult, AggregateMovableParameter> > movable_callbacks;
  /// \brief Callback to generate copyable interface functions
  PSI_STD::vector<SharedDelayedValue<AggregateCopyableResult, AggregateCopyableParameter> > copyable_callbacks;
  /// \brief Callback to generate interface overloads
  PSI_STD::vector<SharedDelayedValue<PSI_STD::vector<TreePtr<OverloadValue> >, AggregateMemberArgument> > overload_callbacks;
  
  template<typename V>
  static void visit(V& v) {
    v("members", &AggregateBodyResult::members)
    ("names", &AggregateBodyResult::names)
    ("primitive_mode", &AggregateBodyResult::primitive_mode)
    ("no_move", &AggregateBodyResult::no_move)
    ("no_copy", &AggregateBodyResult::no_copy)
    ("movable_callbacks", &AggregateBodyResult::movable_callbacks)
    ("copyable_callbacks", &AggregateBodyResult::copyable_callbacks)
    ("overload_callbacks", &AggregateBodyResult::overload_callbacks);
  }
};

class AggregateBodyCallback {
  PatternArguments m_arguments;
  Parser::Text m_body;
  TreePtr<EvaluateContext> m_evaluate_context;
  
public:
  AggregateBodyCallback(const PatternArguments& arguments, const Parser::Text& body, const TreePtr<EvaluateContext>& evaluate_context)
  : m_arguments(arguments), m_body(body), m_evaluate_context(evaluate_context) {
  }
  
  AggregateBodyResult evaluate(const TreePtr<GenericType>& generic) {
    TreePtr<EvaluateContext> member_context = evaluate_context_dictionary(m_evaluate_context->module(), generic->location(), m_arguments.names, m_evaluate_context);
    
    AggregateBodyResult result;
    result.no_move = false;
    result.no_copy = false;
    
    AggregateMemberArgument member_argument;
    member_argument.generic = generic;
    member_argument.parameters = m_arguments.list;
    member_argument.instance = TermBuilder::instance(generic, vector_from<TreePtr<Term> >(m_arguments.list), generic->location());
    
    PSI_STD::vector<SourceLocation> movable_locations, copyable_locations;
    
    // Handle members
    PSI_STD::vector<SharedPtr<Parser::Statement> > members_parsed = Parser::parse_statement_list(generic->compile_context().error_context(), generic->location().logical, m_body);
    for (PSI_STD::vector<SharedPtr<Parser::Statement> >::const_iterator ii = members_parsed.begin(), ie = members_parsed.end(); ii != ie; ++ii) {
      if (*ii && (*ii)->expression) {
        const Parser::Statement& stmt = **ii;
        String member_name;
        LogicalSourceLocationPtr member_logical_location;
        if (stmt.name) {
          member_name = String(stmt.name->begin, stmt.name->end);
          member_logical_location = generic->location().logical->new_child(member_name);
        } else {
          member_logical_location = generic->location().logical;
        }
        SourceLocation stmt_location(stmt.location, member_logical_location);

        if (stmt.name) {
          if (stmt.mode != statement_mode_value)
            m_evaluate_context->compile_context().error_throw(stmt_location, "Aggregate members must be declared with ':'");
        } else {
          PSI_ASSERT(stmt.mode == statement_mode_destroy); // Enforced by the parser
        }
        
        AggregateMemberResult member_result = compile_expression<AggregateMemberResult>
          (stmt.expression, member_context, generic->compile_context().builtins().macro_member_tag, member_argument, stmt_location.logical);
        
        if (member_result.member_type) {
          result.members.push_back(member_result.member_type->parameterize(stmt_location, m_arguments.list));
          if (stmt.name)
            result.names[member_name] = result.members.size();
        }
        
        result.no_move = result.no_move || member_result.no_move;
        result.no_copy = result.no_copy || member_result.no_copy;
        
        if (!member_result.movable_callback.empty()) {
          movable_locations.push_back(stmt_location);
          result.movable_callbacks.push_back(member_result.movable_callback);
        }
        
        if (!member_result.copyable_callback.empty()) {
          if (member_result.movable_callback.empty())
            movable_locations.push_back(stmt_location);
          copyable_locations.push_back(stmt_location);
          result.copyable_callbacks.push_back(member_result.copyable_callback);
        }
        
        if (!member_result.overloads_callback.empty())
          result.overload_callbacks.push_back(member_result.overloads_callback);
      }
    }
    
    if (result.no_move && (!result.movable_callbacks.empty() || !result.copyable_callbacks.empty())) {
      CompileError err(generic->compile_context().error_context(), generic->location());
      err.info("Move or copy constructor bodies supplied for a class where the move interface is disabled.");
      for (PSI_STD::vector<SourceLocation>::const_iterator ii = movable_locations.begin(), ie = movable_locations.end(); ii != ie; ++ii)
        err.info(*ii, "Constructor body defined here.");
      err.end_throw();
    }
    
    if (result.no_copy && !result.copyable_callbacks.empty()) {
      CompileError err(generic->compile_context().error_context(), generic->location());
      err.info("Copy constructor bodies supplied for a class where the copy interface is disabled.");
      for (PSI_STD::vector<SourceLocation>::const_iterator ii = copyable_locations.begin(), ie = copyable_locations.end(); ii != ie; ++ii)
        err.info(*ii, "Constructor body defined here.");
      err.end_throw();
    }
    
    result.primitive_mode = GenericType::primitive_recurse;
    if (result.no_move || result.no_copy || !result.movable_callbacks.empty() || !result.copyable_callbacks.empty())
      result.primitive_mode = GenericType::primitive_never;

    return result;
  }
  
  template<typename V>
  static void visit(V& v) {
    // Only need to list stuff for GC in callbacks, so leave m_body out
    v("arguments", &AggregateBodyCallback::m_arguments)
    ("evaluate_context", &AggregateBodyCallback::m_evaluate_context);
  }
};

typedef SharedDelayedValue<AggregateBodyResult, TreePtr<GenericType> > AggregateBodyDelayedValue;

class AggregateOverloadsCallback {
  PatternArguments m_arguments;
  TreePtr<EvaluateContext> m_evaluate_context;
  AggregateBodyDelayedValue m_body;
  
public:
  AggregateOverloadsCallback(const PatternArguments& arguments, const TreePtr<EvaluateContext>& evaluate_context, const AggregateBodyDelayedValue& body)
  : m_arguments(arguments), m_evaluate_context(evaluate_context), m_body(body) {}

  ImplementationHelper::FunctionSetup lc_setup(ImplementationHelper& helper, int index, const char *name) {
    return helper.member_function_setup(index, helper.location().named_child(name), default_);
  }

  ImplementationHelper::FunctionSetup lc_setup(ImplementationHelper& helper, int index, const char *name, AggregateLifecycleParameters& parameters) {
    ImplementationHelper::FunctionSetup result = lc_setup(helper, index, name);
    parameters.dest = result.parameters[1];
    if (result.parameters.size() > 2)
      parameters.src = result.parameters[2];
    return result;
  }
  
  PSI_STD::vector<TreePtr<OverloadValue> > evaluate(const TreePtr<GenericType>& generic) {
    const AggregateBodyResult& body = m_body.get(generic);

    AggregateMemberArgument member_argument;
    member_argument.generic = generic;
    member_argument.parameters = m_arguments.list;
    member_argument.instance = TermBuilder::instance(generic, vector_from<TreePtr<Term> >(m_arguments.list), generic->location());

    PSI_STD::vector<TreePtr<OverloadValue> > overloads;
    for (PSI_STD::vector<SharedDelayedValue<PSI_STD::vector<TreePtr<OverloadValue> >, AggregateMemberArgument> >::const_iterator ii = body.overload_callbacks.begin(), ie = body.overload_callbacks.end(); ii != ie; ++ii) {
      const PSI_STD::vector<TreePtr<OverloadValue> >& member_overloads = ii->get(member_argument);
      overloads.insert(overloads.end(), member_overloads.begin(), member_overloads.end());
    }
    
    PSI_STD::vector<TreePtr<Term> > init_body, fini_body, move_body, copy_body;

    TreePtr<Term> instance = TermBuilder::instance(generic, vector_from<TreePtr<Term> >(m_arguments.list), generic->location());

    AggregateMovableParameter movable_parameter;
    movable_parameter.generic = generic;
    ImplementationHelper movable_helper(generic->location().named_child("Movable"), generic->compile_context().builtins().movable_interface,
                                        m_arguments.list, vector_of(instance), default_);
    ImplementationHelper::FunctionSetup
      lc_init      = lc_setup(movable_helper, interface_movable_init, "init", movable_parameter.lc_init),
      lc_fini      = lc_setup(movable_helper, interface_movable_fini, "fini", movable_parameter.lc_fini),
      lc_clear     = lc_setup(movable_helper, interface_movable_clear, "clear"),
      lc_move_init = lc_setup(movable_helper, interface_movable_move_init, "move_init"),
      lc_move      = lc_setup(movable_helper, interface_movable_move, "move", movable_parameter.lc_move);
      
    for (PSI_STD::vector<SharedDelayedValue<AggregateMovableResult, AggregateMovableParameter> >::const_iterator ii = body.movable_callbacks.begin(), ie = body.movable_callbacks.end(); ii != ie; ++ii) {
      const AggregateMovableResult& result = ii->get(movable_parameter);
      if (result.lc_init) init_body.push_back(result.lc_init);
      if (result.lc_fini) fini_body.push_back(result.lc_fini);
      if (result.lc_move) move_body.push_back(result.lc_move);
    }

    AggregateCopyableParameter copyable_parameter;
    copyable_parameter.generic = generic;
    ImplementationHelper copyable_helper(generic->location().named_child("Copyable"), generic->compile_context().builtins().copyable_interface,
                                         m_arguments.list, vector_of(instance), default_);
    ImplementationHelper::FunctionSetup
      lc_copy_init = lc_setup(copyable_helper, interface_copyable_copy_init, "copy_init"),
      lc_copy      = lc_setup(copyable_helper, interface_copyable_copy, "copy", copyable_parameter.lc_copy);

    for (PSI_STD::vector<SharedDelayedValue<AggregateCopyableResult, AggregateCopyableParameter> >::const_iterator ii = body.copyable_callbacks.begin(), ie = body.copyable_callbacks.end(); ii != ie; ++ii) {
      const AggregateCopyableResult& result = ii->get(copyable_parameter);
      if (result.lc_copy) copy_body.push_back(result.lc_copy);
    }
    
    if (!body.no_move) {
      PSI_STD::vector<TreePtr<Term> > movable_members(5);
      movable_members[interface_movable_init] = build_init(movable_helper, lc_init, init_body);
      movable_members[interface_movable_clear] = build_clear(movable_helper, lc_clear, fini_body);
      movable_members[interface_movable_fini] = build_fini(movable_helper, lc_fini, fini_body.empty() ? default_ : movable_members[interface_movable_clear]);
      movable_members[interface_movable_move] = build_move(movable_helper, lc_move, move_body);
      movable_members[interface_movable_move_init] = build_move_init(movable_helper, lc_move_init, move_body.empty() ? default_ : movable_members[interface_movable_move]);
      overloads.push_back(movable_helper.finish(TermBuilder::struct_value(generic->compile_context(), movable_members, movable_helper.location())));
      
      if (!body.no_copy) {
        PSI_STD::vector<TreePtr<Term> > copyable_members(3);
        copyable_members[interface_copyable_movable] = TermBuilder::interface_value(generic->compile_context().builtins().movable_interface, vector_of(instance), default_, copyable_helper.location());
        copyable_members[interface_copyable_copy] = build_copy(copyable_helper, lc_copy, copy_body);
        copyable_members[interface_copyable_copy_init] = build_copy_init(copyable_helper, lc_copy_init, copy_body.empty() ? default_ : copyable_members[interface_copyable_copy]);
        overloads.push_back(copyable_helper.finish(TermBuilder::struct_value(generic->compile_context(), copyable_members, copyable_helper.location())));
      }
    }
    
    return overloads;
  }
  
  TreePtr<Term> make_body(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& parts) {
    if (parts.empty()) {
      return TermBuilder::empty_value(m_evaluate_context->compile_context());
    } else {
      return TermBuilder::block(location, parts);
    }
  }
  
  TreePtr<Term> build_init(ImplementationHelper& helper, const ImplementationHelper::FunctionSetup& f, const PSI_STD::vector<TreePtr<Term> >& body_parts) {
    TreePtr<Term> body = make_body(f.location, body_parts);
    TreePtr<Term> element = TermBuilder::element_value(f.parameters[1], 0, f.location);
    TreePtr<Term> init = TermBuilder::initialize_value(element, TermBuilder::default_value(element->type, f.location), body, f.location);
    return helper.function_finish(f, m_evaluate_context->module(), init);
  }

  TreePtr<Term> build_fini(ImplementationHelper& helper, const ImplementationHelper::FunctionSetup& f, const TreePtr<Term>& clear_func_ptr) {
    TreePtr<Term> cleanup = TermBuilder::finalize_value(TermBuilder::element_value(f.parameters[1], 0, f.location), f.location);
    TreePtr<Term> clear_func = TermBuilder::ptr_target(clear_func_ptr, f.location);
    TreePtr<Term> clear = TermBuilder::function_call(clear_func, vector_of<TreePtr<Term> >(f.parameters[0], f.parameters[1]), f.location);
    TreePtr<Term> body = TermBuilder::block(f.location, vector_of(clear, cleanup));
    return helper.function_finish(f, m_evaluate_context->module(), body);
  }
  
  TreePtr<Term> build_clear(ImplementationHelper& helper, const ImplementationHelper::FunctionSetup& f, const PSI_STD::vector<TreePtr<Term> >& body_parts) {
    TreePtr<Term> extra = make_body(f.location, body_parts);
    TreePtr<Term> element = TermBuilder::element_value(f.parameters[1], 0, f.location);
    TreePtr<Term> cleanup = TermBuilder::assign_value(element, TermBuilder::default_value(element->type, f.location), f.location);
    return helper.function_finish(f, m_evaluate_context->module(), TermBuilder::block(f.location, vector_of(extra, cleanup)));
  }
  
  TreePtr<Term> build_move_init(ImplementationHelper& helper, const ImplementationHelper::FunctionSetup& f, const TreePtr<Term>& move_func_ptr) {
    TreePtr<Term> body, dest = TermBuilder::element_value(f.parameters[1], 0, f.location);
    if (move_func_ptr) {
      TreePtr<Term> move_func = TermBuilder::ptr_target(move_func_ptr, f.location);
      TreePtr<Term> move_call = TermBuilder::function_call(move_func, vector_from<TreePtr<Term> >(f.parameters), f.location);
      body = TermBuilder::initialize_value(dest, TermBuilder::default_value(dest->type, f.location), move_call, f.location);
    } else {
      TreePtr<Term> move_value = TermBuilder::movable(TermBuilder::element_value(f.parameters[2], 0, f.location), f.location);
      body = TermBuilder::initialize_value(dest, move_value, TermBuilder::empty_value(m_evaluate_context->compile_context()), f.location);
    }
    return helper.function_finish(f, m_evaluate_context->module(), body);
  }
  
  TreePtr<Term> build_move(ImplementationHelper& helper, const ImplementationHelper::FunctionSetup& f, const PSI_STD::vector<TreePtr<Term> >& body_parts) {
    TreePtr<Term> body;
    if (body_parts.empty()) {
      TreePtr<Term> move_value = TermBuilder::movable(TermBuilder::element_value(f.parameters[2], 0, f.location), f.location);
      body = TermBuilder::assign_value(TermBuilder::element_value(f.parameters[1], 0, f.location), move_value, f.location);
    } else {
      body = make_body(f.location, body_parts);
    }
    return helper.function_finish(f, m_evaluate_context->module(), body);
  }

  TreePtr<Term> build_copy_init(ImplementationHelper& helper, const ImplementationHelper::FunctionSetup& f, const TreePtr<Term>& copy_func_ptr) {
    TreePtr<Term> body, dest = TermBuilder::element_value(f.parameters[1], 0, f.location);
    if (copy_func_ptr) {
      TreePtr<Term> copy_func = TermBuilder::ptr_target(copy_func_ptr, f.location);
      TreePtr<Term> copy_call = TermBuilder::function_call(copy_func, vector_from<TreePtr<Term> >(f.parameters), f.location);
      body = TermBuilder::initialize_value(dest, TermBuilder::default_value(dest->type, f.location), copy_call, f.location);
    } else {
      body = TermBuilder::initialize_value(dest, TermBuilder::element_value(f.parameters[2], 0, f.location),
                                           TermBuilder::empty_value(m_evaluate_context->compile_context()), f.location);
    }
    return helper.function_finish(f, m_evaluate_context->module(), body);
  }
  
  TreePtr<Term> build_copy(ImplementationHelper& helper, const ImplementationHelper::FunctionSetup& f, const PSI_STD::vector<TreePtr<Term> >& body_parts) {
    TreePtr<Term> body;
    if (body_parts.empty()) {
      body = TermBuilder::assign_value(TermBuilder::element_value(f.parameters[1], 0, f.location),
                                       TermBuilder::element_value(f.parameters[2], 0, f.location), f.location);
    } else {
      body = make_body(f.location, body_parts);
    }
    return helper.function_finish(f, m_evaluate_context->module(), body);
  }

  template<typename V>
  static void visit(V& v) {
    v("arguments", &AggregateOverloadsCallback::m_arguments)
    ("evaluate_context", &AggregateOverloadsCallback::m_evaluate_context)
    ("body", &AggregateOverloadsCallback::m_body);
  }
};

class StructCallbackBase {
protected:
  AggregateBodyDelayedValue m_common;

public:
  StructCallbackBase(const AggregateBodyDelayedValue& common) : m_common(common) {}

  template<typename V>
  static void visit(V& v) {
    v("common", &StructCallbackBase::m_common);
  }
};

struct StructPrimitiveModeCallback : StructCallbackBase {
  StructPrimitiveModeCallback(const AggregateBodyDelayedValue& common) : StructCallbackBase(common) {}
  template<typename V> static void visit(V& v) {visit_base<StructCallbackBase>(v);}
  
  int evaluate(const TreePtr<GenericType>& self) {
    return m_common.get(self).primitive_mode;
  }
};

struct StructTypeCallback : StructCallbackBase {
  StructTypeCallback(const AggregateBodyDelayedValue& common) : StructCallbackBase(common) {}
  template<typename V> static void visit(V& v) {visit_base<StructCallbackBase>(v);}
  
  TreePtr<Term> evaluate(const TreePtr<GenericType>& self) {
    return TermBuilder::struct_type(self->compile_context(), m_common.get(self).members, self->location());
  }
};

class StructMacro : public Macro {
public:
  static const MacroVtable vtable;
  
  StructMacro(CompileContext& compile_context, const SourceLocation& location)
  : Macro(&vtable, compile_context, location) {
  }

  static TreePtr<Term> evaluate_impl(const StructMacro& self,
                                     const TreePtr<Term>& PSI_UNUSED(value),
                                     const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                     const TreePtr<EvaluateContext>& evaluate_context,
                                     const MacroTermArgument& PSI_UNUSED(argument),
                                     const SourceLocation& location) {
    
    SharedPtr<Parser::TokenExpression> generic_parameters_expr, members_expr;
    switch (parameters.size()) {
    case 2:
      if (!(generic_parameters_expr = Parser::expression_as_token_type(parameters[0], Parser::token_bracket)))
        self.compile_context().error_throw(location, "first of two parameters to struct macro is not a (...)");
      // Fall through

    case 1:
      if (!(members_expr = Parser::expression_as_token_type(parameters.back(), Parser::token_square_bracket)))
        self.compile_context().error_throw(location, "last parameter to struct macro is not a [...]");
      break;
      
    default:
      self.compile_context().error_throw(location, "struct macro expects one or two arguments");
    }
    
    PatternArguments arguments;
    if (generic_parameters_expr)
      arguments = parse_pattern_arguments(evaluate_context, location, generic_parameters_expr->text);
    
    if (!arguments.dependent.empty())
      self.compile_context().error_throw(location, "struct parameter specification should not contain dependent parameters");

    AggregateBodyDelayedValue shared_callback(self.compile_context(), location,
                                              AggregateBodyCallback(arguments, members_expr->text, evaluate_context));
    
    
    PSI_STD::vector<TreePtr<Term> > pattern = arguments_to_pattern(arguments.list);
    TreePtr<GenericType> generic = TermBuilder::generic(self.compile_context(), pattern, StructPrimitiveModeCallback(shared_callback), location,
                                                        StructTypeCallback(shared_callback), AggregateOverloadsCallback(arguments, evaluate_context, shared_callback));

    if (generic->pattern.empty()) {
      return TermBuilder::instance(generic, default_, location);
    } else {
      PSI_NOT_IMPLEMENTED();
    }
  }
};

const MacroVtable StructMacro::vtable = PSI_COMPILER_MACRO(StructMacro, "psi.compiler.StructMacro", Macro, TreePtr<Term>, MacroTermArgument);

TreePtr<Term> struct_macro(CompileContext& compile_context, const SourceLocation& location) {
  TreePtr<Macro> callback(::new StructMacro(compile_context, location));
  return make_macro_term(callback, location);
}

class DefaultMemberMacroCommon : public Macro {
public:
  static const SIVtable vtable;
  
  DefaultMemberMacroCommon(const VtableType *vptr, CompileContext& compile_context, const SourceLocation& location)
  : Macro(vptr, compile_context, location) {}

  static AggregateMemberResult evaluate_impl(const DefaultMemberMacroCommon& self,
                                             const TreePtr<Term>& value,
                                             const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                             const TreePtr<EvaluateContext>& evaluate_context,
                                             const AggregateMemberArgument& argument,
                                             const SourceLocation& location) {
    TreePtr<Term> expanded = expression_macro(evaluate_context, value, self.compile_context().builtins().macro_term_tag, location)
      ->evaluate<TreePtr<Term> >(value, parameters, evaluate_context, EmptyType(), location);
    return expression_macro(evaluate_context, expanded, self.compile_context().builtins().macro_member_tag, location)->cast<AggregateMemberResult>
      (expanded, evaluate_context, argument, location);
  }
  
  static AggregateMemberResult dot_impl(const DefaultMemberMacroCommon& self,
                                        const TreePtr<Term>& value,
                                        const SharedPtr<Parser::Expression>& member,
                                        const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                        const TreePtr<EvaluateContext>& evaluate_context,
                                        const AggregateMemberArgument& argument,
                                        const SourceLocation& location) {
    TreePtr<Term> expanded = expression_macro(evaluate_context, value, self.compile_context().builtins().macro_term_tag, location)
      ->dot<TreePtr<Term> >(value, member, parameters, evaluate_context, EmptyType(), location);
    return expression_macro(evaluate_context, expanded, self.compile_context().builtins().macro_member_tag, location)->cast<AggregateMemberResult>
      (expanded, evaluate_context, argument, location);
  }
};

const SIVtable DefaultMemberMacroCommon::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.DefaultMemberMacroCommon", Macro);

class DefaultMemberMacro : public DefaultMemberMacroCommon {
public:
  static const VtableType vtable;
  
  DefaultMemberMacro(CompileContext& compile_context, const SourceLocation& location)
  : DefaultMemberMacroCommon(&vtable, compile_context, location) {}

  static AggregateMemberResult cast_impl(const DefaultMemberMacro& self,
                                         const TreePtr<Term>& PSI_UNUSED(value),
                                         const TreePtr<EvaluateContext>& PSI_UNUSED(evaluate_context),
                                         const AggregateMemberArgument& PSI_UNUSED(argument),
                                         const SourceLocation& location) {
    self.compile_context().error_throw(location, "Aggregate member is not a type");
  }
};

const MacroVtable DefaultMemberMacro::vtable = PSI_COMPILER_MACRO(DefaultMemberMacro, "psi.compiler.DefaultMemberMacro", DefaultMemberMacroCommon, AggregateMemberResult, AggregateMemberArgument);

/**
 * Generate the default macro implementation for aggregate members.
 */
TreePtr<> default_macro_member(CompileContext& compile_context, const SourceLocation& location) {
  return TreePtr<>(::new DefaultMemberMacro(compile_context, location));
}

class DefaultTypeMemberMacro : public DefaultMemberMacroCommon {
public:
  static const VtableType vtable;
  
  DefaultTypeMemberMacro(CompileContext& compile_context, const SourceLocation& location)
  : DefaultMemberMacroCommon(&vtable, compile_context, location) {}

  static AggregateMemberResult cast_impl(const DefaultTypeMemberMacro& PSI_UNUSED(self),
                                         const TreePtr<Term>& value,
                                         const TreePtr<EvaluateContext>& PSI_UNUSED(evaluate_context),
                                         const AggregateMemberArgument& PSI_UNUSED(argument),
                                         const SourceLocation& PSI_UNUSED(location)) {
    AggregateMemberResult result;
    result.member_type = value;
    return result;
  }
};

const MacroVtable DefaultTypeMemberMacro::vtable = PSI_COMPILER_MACRO(DefaultTypeMemberMacro, "psi.compiler.DefaultTypeMemberMacro", DefaultMemberMacroCommon, AggregateMemberResult, AggregateMemberArgument);

/**
 * Generate the default macro implementation for aggregate members which are types.
 */
TreePtr<> default_type_macro_member(CompileContext& compile_context, const SourceLocation& location) {
  return TreePtr<>(::new DefaultTypeMemberMacro(compile_context, location));
}

template<typename Result, typename Parameter>
class LifecycleMacroCallback {
  TreePtr<Term> Result::*m_result_member;
  AggregateLifecycleParameters Parameter::*m_parameter_member;
  TreePtr<EvaluateContext> m_evaluate_context;
  SourceLocation m_location;
  Parser::TokenExpression m_dest;
  Maybe<Parser::TokenExpression> m_source;
  SharedPtr<Parser::TokenExpression> m_body;
  
public:
  LifecycleMacroCallback(TreePtr<Term> Result::*result_member, AggregateLifecycleParameters Parameter::*parameter_member,
                         const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location,
                         const Parser::TokenExpression& dest, const Maybe<Parser::TokenExpression>& source, const SharedPtr<Parser::TokenExpression>& body)
  : m_result_member(result_member), m_parameter_member(parameter_member),
  m_evaluate_context(evaluate_context), m_location(location),
  m_dest(dest), m_source(source), m_body(body) {
  }
  
  Result evaluate(const Parameter& parameter) {
    const AggregateLifecycleParameters& lc_func = parameter.*m_parameter_member;
    
    PSI_STD::map<String, TreePtr<Term> > body_variables;
    body_variables[m_dest.text.str()] = lc_func.dest;
    if (m_source)
      body_variables[m_source->text.str()] = lc_func.src;
    TreePtr<EvaluateContext> body_context =
      evaluate_context_dictionary(m_evaluate_context->module(), m_location, body_variables, m_evaluate_context);
        
    Result result;
    result.*m_result_member = compile_from_bracket(m_body, body_context, m_location);
    return result;
  }
  
  template<typename V>
  static void visit(V& v) {
    // Only this things which require GC
    v("evaluate_context", &LifecycleMacroCallback::m_evaluate_context)
    ("location", &LifecycleMacroCallback::m_location);
  }
};

class LifecycleMacro : public Macro {
  int m_which;
  
public:
  static const VtableType vtable;
  
  enum Which {
    which_init,
    which_fini,
    which_move,
    which_copy
  };

  LifecycleMacro(Which which, CompileContext& compile_context, const SourceLocation& location)
  : Macro(&vtable, compile_context, location), m_which(which) {}
  
  template<typename V>
  static void visit(V& v) {
    visit_base<Macro>(v);
    v("which", &LifecycleMacro::m_which);
  }
  
  
  template<typename Result, typename Parameter>
  static SharedDelayedValue<Result,Parameter> callback(TreePtr<Term> Result::*result_member, AggregateLifecycleParameters Parameter::*parameter_member,
                                                       const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location,
                                                       const Parser::TokenExpression& dest, const Maybe<Parser::TokenExpression>& source,
                                                       const SharedPtr<Parser::TokenExpression>& body) {
    return SharedDelayedValue<Result,Parameter>(evaluate_context->compile_context(), location,
                                                LifecycleMacroCallback<Result, Parameter>(result_member, parameter_member, evaluate_context, location, dest, source, body));
  }
  
  static AggregateMemberResult evaluate_impl(const LifecycleMacro& self,
                                             const TreePtr<Term>& PSI_UNUSED(value),
                                             const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                             const TreePtr<EvaluateContext>& evaluate_context,
                                             const AggregateMemberArgument& PSI_UNUSED(argument),
                                             const SourceLocation& location) {
    if (parameters.size() != 2)
      self.compile_context().error_throw(location, "Lifecycle macro expects two arguments");
    
    SharedPtr<Parser::TokenExpression> args_expr, body_expr;
    if (!(args_expr = Parser::expression_as_token_type(parameters[0], Parser::token_bracket)))
      self.compile_context().error_throw(location, "First argument to lifecycle macro is not a (...)");
    if (!(body_expr = Parser::expression_as_token_type(parameters[1], Parser::token_square_bracket)))
      self.compile_context().error_throw(location, "Second argument to lifecycle macro is not a [...]");
    
    AggregateMemberResult result;
    
    PSI_STD::vector<Parser::TokenExpression> args = Parser::parse_identifier_list(self.compile_context().error_context(), location.logical, args_expr->text);
    switch (self.m_which) {
    case which_init:
      if (args.size() != 1)
        self.compile_context().error_throw(location, "Initialization function expects a single argument");
      result.movable_callback = callback(&AggregateMovableResult::lc_init, &AggregateMovableParameter::lc_init, evaluate_context, location, args[0], default_, body_expr);
      break;

    case which_fini:
      if (args.size() != 1)
        self.compile_context().error_throw(location, "Finalization function expects a single argument");
      result.movable_callback = callback(&AggregateMovableResult::lc_fini, &AggregateMovableParameter::lc_fini, evaluate_context, location, args[0], default_, body_expr);
      break;
      
    case which_move:
      if (args.size() != 2)
        self.compile_context().error_throw(location, "Move function expects two arguments");
      result.movable_callback = callback(&AggregateMovableResult::lc_move, &AggregateMovableParameter::lc_move, evaluate_context, location, args[0], args[1], body_expr);
      break;
      
    case which_copy:
      if (args.size() != 2)
        self.compile_context().error_throw(location, "Copy function expects two arguments");
      result.copyable_callback = callback(&AggregateCopyableResult::lc_copy, &AggregateCopyableParameter::lc_copy, evaluate_context, location, args[0], args[1], body_expr);
      break;
      
    default: PSI_FAIL("Unknown lifecycle 'which'");
    }
    
    return result;
  }
};

const MacroVtable LifecycleMacro::vtable = PSI_COMPILER_MACRO(LifecycleMacro, "psi.compiler.LifecycleMacro", Macro, AggregateMemberResult, AggregateMemberArgument);

/// \brief Create the \c __init__ macro
TreePtr<Term> lifecycle_init_macro(CompileContext& compile_context, const SourceLocation& location) {
  TreePtr<Macro> macro(::new LifecycleMacro(LifecycleMacro::which_init, compile_context, location));
  return make_macro_tag_term(macro, compile_context.builtins().macro_member_tag, location);
}

/// \brief Create the \c __fini__ macro
TreePtr<Term> lifecycle_fini_macro(CompileContext& compile_context, const SourceLocation& location) {
  TreePtr<Macro> macro(::new LifecycleMacro(LifecycleMacro::which_fini, compile_context, location));
  return make_macro_tag_term(macro, compile_context.builtins().macro_member_tag, location);
}

/// \brief Create the \c __move__ macro
TreePtr<Term> lifecycle_move_macro(CompileContext& compile_context, const SourceLocation& location) {
  TreePtr<Macro> macro(::new LifecycleMacro(LifecycleMacro::which_move, compile_context, location));
  return make_macro_tag_term(macro, compile_context.builtins().macro_member_tag, location);
}

/// \brief Create the \c __copy__ macro
TreePtr<Term> lifecycle_copy_macro(CompileContext& compile_context, const SourceLocation& location) {
  TreePtr<Macro> macro(::new LifecycleMacro(LifecycleMacro::which_copy, compile_context, location));
  return make_macro_tag_term(macro, compile_context.builtins().macro_member_tag, location);
}

class LifecycleDisableMacro : public Macro {
  PsiBool m_is_copy;
  
public:
  static const VtableType vtable;

  LifecycleDisableMacro(bool is_copy, CompileContext& compile_context, const SourceLocation& location)
  : Macro(&vtable, compile_context, location), m_is_copy(is_copy) {}
  
  template<typename V>
  static void visit(V& v) {
    visit_base<Macro>(v);
    v("is_copy", &LifecycleDisableMacro::m_is_copy);
  }
  
  static AggregateMemberResult cast_impl(const LifecycleDisableMacro& self,
                                         const TreePtr<Term>& PSI_UNUSED(value),
                                         const TreePtr<EvaluateContext>& PSI_UNUSED(evaluate_context),
                                         const AggregateMemberArgument& PSI_UNUSED(argument),
                                         const SourceLocation& PSI_UNUSED(location)) {
    AggregateMemberResult result;
    result.no_move = true;
    if (self.m_is_copy)
      result.no_copy = true;
    return result;
  }
};

const MacroVtable LifecycleDisableMacro::vtable = PSI_COMPILER_MACRO(LifecycleDisableMacro, "psi.compiler.LifecycleDisableMacro", Macro, AggregateMemberResult, AggregateMemberArgument);

/// \brief Create the \c __no_move__ macro
TreePtr<Term> lifecycle_no_move_macro(CompileContext& compile_context, const SourceLocation& location) {
  TreePtr<Macro> macro(::new LifecycleDisableMacro(false, compile_context, location));
  return make_macro_tag_term(macro, compile_context.builtins().macro_member_tag, location);
}

/// \brief Create the \c __no_copy__ macro
TreePtr<Term> lifecycle_no_copy_macro(CompileContext& compile_context, const SourceLocation& location) {
  TreePtr<Macro> macro(::new LifecycleDisableMacro(true, compile_context, location));
  return make_macro_tag_term(macro, compile_context.builtins().macro_member_tag, location);
}
}
}
