#include "Macros.hpp"
#include "Parser.hpp"
#include "Enums.hpp"
#include "Interface.hpp"
#include "TermBuilder.hpp"

#include <boost/format.hpp>

namespace Psi {
namespace Compiler {
/**
 * \brief Parameters to lifecycle functions.
 */
struct AggregateLifecycleParameters {
  /// \brief Generic specialization parameters
  PSI_STD::vector<TreePtr<Term> > parameters;
  
  /// \brief Destination variable
  TreePtr<Term> dest;
  /// \brief Source variable, if a two parameter function
  TreePtr<Term> src;
};

/**
 * \brief Parameter passed to aggregate member macros.
 */
struct AggregateMemberParameter {
  /// \brief Containing generic type
  TreePtr<GenericType> generic;

  /// \brief Initialization parameters
  AggregateLifecycleParameters lc_init;
  /// \brief Move parameters
  AggregateLifecycleParameters lc_move;
  /// \brief Copy parameters
  AggregateLifecycleParameters lc_copy;
  /// \brief Finalization parameters
  AggregateLifecycleParameters lc_fini;
};


/**
 * \brief Result returned from aggregate member macros.
 */
struct AggregateMemberResult {
  AggregateMemberResult() : no_move(false), no_copy(false) {}
  
  /// \brief Member type, or no data if NULL.
  TreePtr<Term> member_type;
  
  /// \brief Do not generate movable interface
  PsiBool no_move;
  /// \brief Do not generate copyable interface
  PsiBool no_copy;
  
  /// \brief Initialization code
  TreePtr<Term> lc_init;
  /// \brief Move code
  TreePtr<Term> lc_move;
  /// \brief Copy code
  TreePtr<Term> lc_copy;
  /// \brief Finalization code
  TreePtr<Term> lc_fini;
  
  PSI_STD::vector<TreePtr<OverloadValue> > overloads;
};

/**
 * \brief Result of building members of an aggregate.
 */
struct AggregateBodyResult {
  /// \brief Member types
  PSI_STD::vector<TreePtr<Term> > members;
  /// \brief Member names
  PSI_STD::map<String, unsigned> names;
  /// \brief Overloads
  PSI_STD::vector<TreePtr<OverloadValue> > overloads;
  
  GenericType::GenericTypePrimitive primitive_mode;
  
  template<typename V>
  static void visit(V& v) {
    v("members", &AggregateBodyResult::members)
    ("names", &AggregateBodyResult::names)
    ("overloads", &AggregateBodyResult::overloads)
    ("primitive_mode", &AggregateBodyResult::primitive_mode);
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
  
  void list_constructor_body(CompileError& err, const PSI_STD::vector<TreePtr<Term> >& bodies) {
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = bodies.begin(), ie = bodies.end(); ii != ie; ++ii)
      err.info((*ii)->location(), "Constructor body defined here.");
  }
  
  AggregateBodyResult evaluate(const TreePtr<GenericType>& generic) {
    TreePtr<EvaluateContext> member_context = evaluate_context_dictionary(m_evaluate_context->module(), generic->location(), m_arguments.names, m_evaluate_context);
    
    AggregateBodyResult result;
    
    AggregateMemberParameter parameter;
    parameter.generic = generic;

    TreePtr<Term> instance = TermBuilder::instance(generic, vector_from<TreePtr<Term> >(m_arguments.list), generic->location());
    
    ImplementationHelper movable_helper(generic->location().named_child("Movable"), generic->compile_context().builtins().movable_interface,
                                        m_arguments.list, vector_of(instance), default_);
    ImplementationHelper::FunctionSetup
      lc_init      = lc_setup(movable_helper, interface_movable_init, "init", parameter.lc_init),
      lc_fini      = lc_setup(movable_helper, interface_movable_fini, "fini", parameter.lc_fini),
      lc_clear     = lc_setup(movable_helper, interface_movable_clear, "clear"),
      lc_move_init = lc_setup(movable_helper, interface_movable_move_init, "move_init"),
      lc_move      = lc_setup(movable_helper, interface_movable_move, "move", parameter.lc_move);
      
    ImplementationHelper copyable_helper(generic->location().named_child("Copyable"), generic->compile_context().builtins().copyable_interface,
                                         m_arguments.list, vector_of(instance), default_);
    ImplementationHelper::FunctionSetup
      lc_copy_init = lc_setup(copyable_helper, interface_copyable_copy_init, "copy_init"),
      lc_copy      = lc_setup(copyable_helper, interface_copyable_copy, "copy", parameter.lc_copy);
      
    bool no_move = false, no_copy = false;
    PSI_STD::vector<TreePtr<Term> > init_body, fini_body, move_body, copy_body;
    
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
          (stmt.expression, member_context, generic->compile_context().builtins().macro_member_tag, parameter, stmt_location.logical);
        
        if (member_result.member_type) {
          result.members.push_back(member_result.member_type->parameterize(stmt_location, m_arguments.list));
          if (stmt.name)
            result.names[member_name] = result.members.size();
        }
        
        no_move = no_move || member_result.no_move;
        no_copy = no_copy || member_result.no_copy;
        
        if (member_result.lc_init) init_body.push_back(member_result.lc_init);
        if (member_result.lc_fini) fini_body.push_back(member_result.lc_fini);
        if (member_result.lc_move) move_body.push_back(member_result.lc_move);
        if (member_result.lc_copy) copy_body.push_back(member_result.lc_copy);
        
        result.overloads.insert(result.overloads.end(), member_result.overloads.begin(), member_result.overloads.end());
      }
    }
    
    if (no_move && (!init_body.empty() || !fini_body.empty() || !move_body.empty() || !copy_body.empty())) {
      CompileError err(generic->compile_context().error_context(), generic->location());
      err.info("Move or copy constructor bodies supplied for a class where the move interface is disabled.");
      list_constructor_body(err, init_body);
      list_constructor_body(err, fini_body);
      list_constructor_body(err, move_body);
      list_constructor_body(err, copy_body);
      err.end_throw();
    }
    
    if (no_copy && !copy_body.empty()) {
      CompileError err(generic->compile_context().error_context(), generic->location());
      err.info("Copy constructor bodies supplied for a class where the copy interface is disabled.");
      list_constructor_body(err, copy_body);
      err.end_throw();
    }
    
    result.primitive_mode = GenericType::primitive_recurse;
    if (no_move || no_copy || !init_body.empty() || !fini_body.empty() || !move_body.empty() || !copy_body.empty())
      result.primitive_mode = GenericType::primitive_never;
    
    if (!no_move) {
      PSI_STD::vector<TreePtr<Term> > movable_members(5);
      movable_members[interface_movable_init] = build_init(movable_helper, lc_init, init_body);
      movable_members[interface_movable_clear] = build_clear(movable_helper, lc_clear, fini_body);
      movable_members[interface_movable_fini] = build_fini(movable_helper, lc_fini, fini_body.empty() ? default_ : movable_members[interface_movable_clear]);
      movable_members[interface_movable_move] = build_move(movable_helper, lc_move, move_body);
      movable_members[interface_movable_move_init] = build_move_init(movable_helper, lc_move_init, move_body.empty() ? default_ : movable_members[interface_movable_move]);
      result.overloads.push_back(movable_helper.finish(TermBuilder::struct_value(generic->compile_context(), movable_members, movable_helper.location())));
      
      if (!no_copy) {
        PSI_STD::vector<TreePtr<Term> > copyable_members(3);
        copyable_members[interface_copyable_movable] = TermBuilder::interface_value(generic->compile_context().builtins().movable_interface, vector_of(instance), default_, copyable_helper.location());
        copyable_members[interface_copyable_copy] = build_copy(copyable_helper, lc_copy, copy_body);
        copyable_members[interface_copyable_copy_init] = build_copy_init(copyable_helper, lc_copy_init, copy_body.empty() ? default_ : copyable_members[interface_copyable_copy]);
        result.overloads.push_back(copyable_helper.finish(TermBuilder::struct_value(generic->compile_context(), copyable_members, copyable_helper.location())));
      }
    }

    return result;
  }
  
  template<typename V>
  static void visit(V& v) {
    // Only need to list stuff for GC in callbacks, so leave m_body out
    v("arguments", &AggregateBodyCallback::m_arguments)
    ("evaluate_context", &AggregateBodyCallback::m_evaluate_context);
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
};

typedef SharedDelayedValue<AggregateBodyResult, TreePtr<GenericType> > AggregateBodyDelayedValue;

#if 0
/**
 * Parse move-construct-destroy implementation.
 * This will have up to 5 function bodies: Construct, destroy, move, copy, assign
 */
void AggregateMacroCommon::parse_constructors(const SourceLocation& location) {
  for (PSI_STD::vector<SharedPtr<Parser::Lifecycle> >::const_iterator ii = lifecycle_exprs.begin(), ie = lifecycle_exprs.end(); ii != ie; ++ii) {
    const Parser::Lifecycle& lc = **ii;
    AggregateLifecycleImpl *lc_impl;
    bool binary = true, nullable = false;
    String function_name = lc.function_name.str();
    if (function_name == "init") {
      binary = false;
      lc_impl = &lc_init;
    } else if (function_name == "fini") {
      binary = false;
      lc_impl = &lc_fini;
    } else if (function_name == "move") {
      lc_impl = &lc_move;
    } else if (function_name == "copy") {
      nullable = true;
      lc_impl = &lc_copy;
    } else {
      member_context->compile_context().error_throw(location, boost::format("Unknown lifecycle function: %s") % function_name);
    }
    
    if (lc_impl->mode != AggregateLifecycleImpl::mode_default)
      member_context->compile_context().error_throw(location, boost::format("Duplicate lifecycle function: %s") % function_name);
    
    if (!lc.body) {
      if (!nullable)
        member_context->compile_context().error_throw(location, boost::format("Lifecycle function cannot be deleted: %s") % function_name);
    } else {
      if (lc.body->token_type != Parser::token_square_bracket)
        member_context->compile_context().error_throw(location, boost::format("Lifecycle function definition is not a [...]") % function_name);
    }

    if (binary) {
      if (!lc.src_name)
        member_context->compile_context().error_throw(location, boost::format("Lifecycle function %s requires two arguments") % function_name);
      lc_impl->src_name = *lc.src_name;
    } else {
      if (lc.src_name)
        member_context->compile_context().error_throw(location, boost::format("Lifecycle function %s only takes one argument") % function_name);
    }

    lc_impl->mode = lc.body ? AggregateLifecycleImpl::mode_impl : AggregateLifecycleImpl::mode_delete;
    lc_impl->binary = binary;
    lc_impl->body = lc.body;
    lc_impl->dest_name = lc.dest_name;
    lc_impl->physical_location = lc.location;
  }
}
#endif

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

struct StructOverloadsCallback : StructCallbackBase {
  StructOverloadsCallback(const AggregateBodyDelayedValue& common) : StructCallbackBase(common) {}
  template<typename V> static void visit(V& v) {visit_base<StructCallbackBase>(v);}

  PSI_STD::vector<TreePtr<OverloadValue> > evaluate(const TreePtr<GenericType>& self) {
    return m_common.get(self).overloads;
  }
};

class StructMacroCallback : public MacroMemberCallback {
public:
  static const MacroMemberCallbackVtable vtable;
  
  StructMacroCallback(CompileContext& compile_context, const SourceLocation& location)
  : MacroMemberCallback(&vtable, compile_context, location) {
  }

  static TreePtr<Term> evaluate_impl(const StructMacroCallback& self,
                                     const TreePtr<Term>&,
                                     const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                     const TreePtr<EvaluateContext>& evaluate_context,
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

    AggregateBodyDelayedValue shared_callback(self.compile_context(), location,
                                              AggregateBodyCallback(arguments, members_expr->text, evaluate_context));
    
    TreePtr<GenericType> generic = TermBuilder::generic(self.compile_context(), arguments.pattern, StructPrimitiveModeCallback(shared_callback), location,
                                                        StructTypeCallback(shared_callback), StructOverloadsCallback(shared_callback));

    if (generic->pattern.empty()) {
      return TermBuilder::instance(generic, default_, location);
    } else {
      PSI_NOT_IMPLEMENTED();
    }
  }
};

const MacroMemberCallbackVtable StructMacroCallback::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(StructMacroCallback, "psi.compiler.StructMacroCallback", MacroMemberCallback);

TreePtr<Term> struct_macro(CompileContext& compile_context, const SourceLocation& location) {
  TreePtr<MacroMemberCallback> callback(::new StructMacroCallback(compile_context, location));
  return make_macro_term(make_macro(compile_context, location, callback), location);
}
}
}
