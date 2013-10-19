#include "Macros.hpp"
#include "Parser.hpp"
#include "Enums.hpp"
#include "Interface.hpp"
#include "TermBuilder.hpp"

#include <boost/format.hpp>

namespace Psi {
namespace Compiler {
/**
 * \brief Common intermediate data for the 5 lifecycle functions.
 * 
 * The functions are
 */
struct AggregateLifecycleImpl {
  enum Mode {
    mode_default=0,
    mode_delete,
    mode_impl
  };
  
  AggregateLifecycleImpl() : mode(mode_default) {}
  
  Mode mode;
  /// \brief Whether this is a two argument function
  bool binary;
  PhysicalSourceLocation physical_location;
  /// \brief Name of destination variable
  Parser::Text dest_name;
  /// \brief Name of source variable
  Parser::Text src_name;
  /// \brief Function body
  SharedPtr<Parser::TokenExpression> body;
  
  /**
   * Doesn't hold any Tree references so don't bother with proper visit() implementation
   */
  template<typename V>
  static void visit(V& PSI_UNUSED(v)) {}
};

/**
 * Helper class for generating aggregate types; contains functions common
 * to struct and union.
 */
class AggregateMacroCommon : public Object {
public:
  static const ObjectVtable vtable;
  
  AggregateMacroCommon(const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                       const TreePtr<EvaluateContext>& evaluate_context,
                       const SourceLocation& location);
  
  TreePtr<EvaluateContext> evaluate_context;
  
  SharedPtr<Parser::TokenExpression> generic_parameters_expr, members_expr, constructor_expr, interfaces_expr;

  void split_parameters(CompileContext& compile_context,
                        const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                        const SourceLocation& location);
  
  PatternArguments arguments;

  void parse_arguments(const TreePtr<EvaluateContext>& evaluate_context,
                       const SourceLocation& location);
  
  TreePtr<EvaluateContext> member_context;
  std::map<String, unsigned> member_names;
  /// Member type list. This has been parameterized, i.e. uses Parameter rather than Anonymous.
  PSI_STD::vector<TreePtr<Term> > member_types;

  void parse_members(const SourceLocation& location);

  void parse_constructors(const SourceLocation& location);
  
  AggregateLifecycleImpl lc_init, lc_fini, lc_move, lc_copy;
  
  void parse_interfaces(const SourceLocation& location);
  
  template<typename V>
  static void visit(V& v) {
    v("evaluate_context", &AggregateMacroCommon::evaluate_context)
    ("generic_parameters_expr", &AggregateMacroCommon::generic_parameters_expr)
    ("members_expr", &AggregateMacroCommon::members_expr)
    ("constructor_expr", &AggregateMacroCommon::constructor_expr)
    ("interfaces_expr", &AggregateMacroCommon::interfaces_expr)
    ("arguments", &AggregateMacroCommon::arguments)
    ("member_context", &AggregateMacroCommon::member_context)
    ("member_names", &AggregateMacroCommon::member_names)
    ("member_types", &AggregateMacroCommon::member_types)
    ("lc_init", &AggregateMacroCommon::lc_init)
    ("lc_fini", &AggregateMacroCommon::lc_fini)
    ("lc_move", &AggregateMacroCommon::lc_move)
    ("lc_copy", &AggregateMacroCommon::lc_copy);
  }
};

const ObjectVtable AggregateMacroCommon::vtable = PSI_COMPILER_OBJECT(AggregateMacroCommon, "psi.compiler.AggregateMacroCommon", Object);

void AggregateMacroCommon::split_parameters(CompileContext& compile_context,
                                            const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                            const SourceLocation& location) {
  if ((parameters.size() < 1) || (parameters.size() > 4))
    compile_context.error_throw(location, "struct macro expects from 1 to 4 arguments");
  
  std::size_t index = 0;
  if (generic_parameters_expr = expression_as_token_type(parameters[index], Parser::token_bracket))
    ++index;
  if (index == parameters.size())
    compile_context.error_throw(location, "struct macro missing [...] members parameter ");
  if (!(members_expr = expression_as_token_type(parameters[index], Parser::token_square_bracket)))
    compile_context.error_throw(location, boost::format("Parameter %s to struct macro is not a [...]") % index);
  ++index;
  
  if (index < parameters.size()) {
    if (constructor_expr = expression_as_token_type(parameters[index], Parser::token_bracket))
      ++index;
    if (index < parameters.size()) {
      if (interfaces_expr = expression_as_token_type(parameters[index], Parser::token_square_bracket))
        ++index;
    }
  }
  
  if (index < parameters.size())
    compile_context.error_throw(location, "Parameters to struct macro did not match expected pattern : (...)? [...] (...)? [...]?");
}

/**
 * Parse generic arguments to an aggregate type.
 */
void AggregateMacroCommon::parse_arguments(const TreePtr<EvaluateContext>& evaluate_context,
                                           const SourceLocation& location) {
  arguments = parse_pattern_arguments(evaluate_context, location, generic_parameters_expr->text);
  member_context = evaluate_context_dictionary(evaluate_context->module(), location, arguments.names, evaluate_context);
}

/**
 * Parse members of an aggregate type.
 */
void AggregateMacroCommon::parse_members(const SourceLocation& location) {
  // Handle members
  PSI_STD::vector<SharedPtr<Parser::Statement> > members_parsed = Parser::parse_statement_list(compile_context().error_context(), location.logical, members_expr->text);
  for (PSI_STD::vector<SharedPtr<Parser::Statement> >::const_iterator ii = members_parsed.begin(), ie = members_parsed.end(); ii != ie; ++ii) {
    if (*ii && (*ii)->expression) {
      const Parser::Statement& stmt = **ii;
      String member_name;
      LogicalSourceLocationPtr member_logical_location;
      if (stmt.name) {
        member_name = String(stmt.name->begin, stmt.name->end);
        member_logical_location = location.logical->new_child(member_name);
      } else {
        member_logical_location = location.logical;
      }
      SourceLocation stmt_location(stmt.location, member_logical_location);

      if (stmt.name) {
        if (stmt.mode != statement_mode_value)
          member_context->compile_context().error_throw(stmt_location, "Struct members must be declared with ':'");
      } else {
        PSI_ASSERT(stmt.mode == statement_mode_destroy);
      }
      
      TreePtr<Term> member_type = compile_term(stmt.expression, member_context, stmt_location.logical);
      member_type = member_type->parameterize(stmt_location, arguments.list);
      member_types.push_back(member_type);

      if (stmt.name)
        member_names[member_name] = member_types.size();
    }
  }
}

/**
 * Parse move-construct-destroy implementation.
 * This will have up to 5 function bodies: Construct, destroy, move, copy, assign
 */
void AggregateMacroCommon::parse_constructors(const SourceLocation& location) {
  PSI_NOT_IMPLEMENTED();
  PSI_STD::vector<SharedPtr<Parser::Lifecycle> > lifecycle_exprs;// = Parser::parse_lifecycle_list(compile_context().error_context(), location.logical, constructor_expr->text);
  
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

void AggregateMacroCommon::parse_interfaces(const SourceLocation& location) {
  PSI_NOT_IMPLEMENTED();
  PSI_STD::vector<SharedPtr<Parser::Implementation> > interfaces_parsed;// = Parser::parse_implementation_list(compile_context().error_context(), location.logical, interfaces_expr->text);

  // Remove any empty entries
  for (PSI_STD::vector<SharedPtr<Parser::Implementation> >::iterator ii = interfaces_parsed.begin(); ii != interfaces_parsed.end();) {
    if (*ii)
      ++ii;
    else
      ii = interfaces_parsed.erase(ii);
  }
  
  // Handle remaining interfaces
#if 1
  if (!interfaces_parsed.empty())
    PSI_NOT_IMPLEMENTED();
#else
  for (PSI_STD::vector<SharedPtr<Parser::Implementation> >::const_iterator ii = interfaces_parsed.begin(), ie = interfaces_parsed.end(); ii != ie; ++ii) {
    const Parser::Implementation& impl = **ii;
    if (!impl.interface)
      member_context->compile_context().error_throw(impl.location, "Interface name missing; only a single entry in a struct interface list may be empty");
    
    TreePtr<Term> interface = compile_expression(impl.interface, member_context, location.logical);
  }
#endif
}
  
AggregateMacroCommon::AggregateMacroCommon(const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                           const TreePtr<EvaluateContext>& evaluate_context_,
                                           const SourceLocation& location)
: Object(&vtable, evaluate_context_->compile_context()),
evaluate_context(evaluate_context_) {

  split_parameters(evaluate_context->compile_context(), parameters, location);
  if (generic_parameters_expr)
    parse_arguments(evaluate_context, location);
  parse_members(location);
  if (constructor_expr)
    parse_constructors(location);
  if (interfaces_expr)
    parse_interfaces(location);
}

class StructTypeCallback {
  ObjectPtr<AggregateMacroCommon> m_common;

public:
  StructTypeCallback(const ObjectPtr<AggregateMacroCommon>& common) : m_common(common) {}
  
  TreePtr<Term> evaluate(const TreePtr<GenericType>& self) {
    TreePtr<Term> st = TermBuilder::struct_type(self->compile_context(), m_common->member_types, self->location());
    return st->parameterize(self->location(), m_common->arguments.list);
  }
  
  template<typename V>
  static void visit(V& v) {
    v("common", &StructTypeCallback::m_common);
  }
};

class StructOverloadsCallback {
  ObjectPtr<AggregateMacroCommon> m_common;
  
public:
  typedef Implementation TreeResultType;
  
  StructOverloadsCallback(const ObjectPtr<AggregateMacroCommon>& common) : m_common(common) {}
  
  template<typename V>
  static void visit(V& v) {
    v("common", &StructOverloadsCallback::m_common);
  }
  
  TreePtr<Term> generic_instance(const TreePtr<GenericType>& generic) {
    return TermBuilder::instance(generic, vector_from<TreePtr<Term> >(m_common->arguments.list), generic->location());
  }
  
  SourceLocation parameter_location(const SourceLocation& parent, const Parser::Text& name) {
    return parent.named_child(name.str()).relocate(name.location);
  }
  
  TreePtr<Term> build_body(const SourceLocation& location, const AggregateLifecycleImpl& impl,
                           const TreePtr<Term>& dest_ptr, const TreePtr<Term>& src_ptr=TreePtr<Term>()) {
    if (!impl.body)
      return TreePtr<Term>();
    
    std::map<String, TreePtr<Term> > names;
    names[impl.dest_name.str()] = dest_ptr;
    if (src_ptr)
      names[impl.src_name.str()] = src_ptr;
    TreePtr<EvaluateContext> local_context = evaluate_context_dictionary(m_common->evaluate_context->module(), location, names, m_common->evaluate_context);
    
    return compile_from_bracket(impl.body, local_context, location);
  }
  
  TreePtr<Term> combine_body(const TreePtr<Term>& first, const TreePtr<Term>& second, const SourceLocation& location) {
    if (first && second)
      return TermBuilder::block(location, vector_of(first, second));
    else if (first)
      return first;
    else if (second)
      return second;
    else
      return m_common->evaluate_context->compile_context().builtins().empty_value;
  }

  TreePtr<Term> build_init(const TreePtr<Term>& instance, ImplementationHelper& helper, const SourceLocation& base_location) {
    SourceLocation location = base_location.named_child("init");
    ImplementationHelper::FunctionSetup f = helper.member_function_setup(interface_movable_init, location,
                                                                         !m_common->lc_init.body ? default_ :
                                                                         vector_of(parameter_location(location, m_common->lc_init.dest_name)));
    TreePtr<Term> extra = build_body(location, m_common->lc_init, f.parameters[1]);
    if (!extra)
      extra = instance->compile_context().builtins().empty_value;
    TreePtr<Term> element = TermBuilder::element_value(f.parameters[1], 0, location);
    TreePtr<Term> init = TermBuilder::initialize_value(element, TermBuilder::default_value(element->type, location), extra, location);
    return helper.function_finish(f, m_common->evaluate_context->module(), init);
  }
  
  TreePtr<Term> build_fini(const TreePtr<Term>&, ImplementationHelper& helper, const SourceLocation& base_location, const TreePtr<Term>& clear_func_ptr) {
    SourceLocation location = base_location.named_child("fini");
    ImplementationHelper::FunctionSetup f = helper.member_function_setup(interface_movable_fini, location,
                                                                         !m_common->lc_fini.body ? default_ :
                                                                         vector_of(parameter_location(location, m_common->lc_fini.dest_name)));
    
    TreePtr<Term> cleanup = TermBuilder::finalize_value(TermBuilder::element_value(f.parameters[1], 0, location), location);
    TreePtr<Term> body;
    if (m_common->lc_fini.body) {
      TreePtr<Term> clear_func = TermBuilder::ptr_target(clear_func_ptr, location);
      TreePtr<Term> clear = TermBuilder::function_call(clear_func, vector_of<TreePtr<Term> >(f.parameters[0], f.parameters[1]), location);
      body = combine_body(clear, cleanup, location);
    } else {
      body = cleanup;
    }
    return helper.function_finish(f, m_common->evaluate_context->module(), body);
  }
  
  TreePtr<Term> build_clear(const TreePtr<Term>& instance, ImplementationHelper& helper, const SourceLocation& base_location) {
    SourceLocation location = base_location.named_child("clear");
    ImplementationHelper::FunctionSetup f = helper.member_function_setup(interface_movable_clear, location,
                                                                         !m_common->lc_fini.body ? default_ :
                                                                         vector_of(parameter_location(location, m_common->lc_fini.dest_name)));
    TreePtr<Term> extra = build_body(location, m_common->lc_fini, f.parameters[1]);
    TreePtr<Term> element = TermBuilder::element_value(f.parameters[1], 0, location);
    TreePtr<Term> cleanup = TermBuilder::assign_value(element, TermBuilder::default_value(element->type, location), location);
    return helper.function_finish(f, m_common->evaluate_context->module(), combine_body(extra, cleanup, location));
  }
  
  TreePtr<Term> build_move_init(const TreePtr<Term>& instance, ImplementationHelper& helper, const SourceLocation& base_location, const TreePtr<Term>& move_func_ptr) {
    SourceLocation location = base_location.named_child("move_init");
    ImplementationHelper::FunctionSetup f = helper.member_function_setup(interface_movable_move_init, location,
                                                                         !m_common->lc_move.body ? default_ :
                                                                         vector_of(parameter_location(location, m_common->lc_move.dest_name),
                                                                                   parameter_location(location, m_common->lc_move.src_name)));

    TreePtr<Term> body, dest = TermBuilder::element_value(f.parameters[1], 0, location);
    if (m_common->lc_move.body) {
      TreePtr<Term> move_func = TermBuilder::ptr_target(move_func_ptr, location);
      TreePtr<Term> move_call = TermBuilder::function_call(move_func, vector_of<TreePtr<Term> >(f.parameters[0], f.parameters[1], f.parameters[2]), location);
      body = TermBuilder::initialize_value(dest, TermBuilder::default_value(dest->type, location), move_call, location);
    } else {
      TreePtr<Term> move_value = TermBuilder::movable(TermBuilder::element_value(f.parameters[2], 0, location), location);
      body = TermBuilder::initialize_value(dest, move_value, TermBuilder::empty_value(instance->compile_context()), location);
    }
    return helper.function_finish(f, m_common->evaluate_context->module(), body);
  }
  
  TreePtr<Term> build_move(const TreePtr<Term>&, ImplementationHelper& helper, const SourceLocation& base_location) {
    SourceLocation location = base_location.named_child("move");
    ImplementationHelper::FunctionSetup f = helper.member_function_setup(interface_movable_move, location,
                                                                         !m_common->lc_move.body ? default_ :
                                                                         vector_of(parameter_location(location, m_common->lc_move.src_name),
                                                                                   parameter_location(location, m_common->lc_move.dest_name)));
    TreePtr<Term> body = build_body(location, m_common->lc_move, f.parameters[1], f.parameters[2]);
    if (!body) {
      TreePtr<Term> move_value = TermBuilder::movable(TermBuilder::element_value(f.parameters[2], 0, location), location);
      body = TermBuilder::assign_value(TermBuilder::element_value(f.parameters[1], 0, location), move_value, location);
    }
    return helper.function_finish(f, m_common->evaluate_context->module(), body);
  }
  
  TreePtr<Implementation> build_movable(const TreePtr<GenericType>& generic) {
    TreePtr<Term> instance = generic_instance(generic);
    SourceLocation location = generic->location().named_child("Movable");
    ImplementationHelper helper(location, generic->compile_context().builtins().movable_interface,
                                m_common->arguments.list, vector_of(instance), default_);
    
    PSI_STD::vector<TreePtr<Term> > members(5);
    members[interface_movable_init] = build_init(instance, helper, location);
    members[interface_movable_clear] = build_clear(instance, helper, location);
    members[interface_movable_fini] = build_fini(instance, helper, location, members[interface_movable_clear]);
    members[interface_movable_move] = build_move(instance, helper, location);
    members[interface_movable_move_init] = build_move_init(instance, helper, location, members[interface_movable_move]);
    return helper.finish(TermBuilder::struct_value(generic->compile_context(), members, location));
  }

  TreePtr<Term> build_copy_init(const TreePtr<Term>& instance, ImplementationHelper& helper, const SourceLocation& base_location, const TreePtr<Term>& copy_func_ptr) {
    SourceLocation location = base_location.named_child("copy_init");
    ImplementationHelper::FunctionSetup f = helper.member_function_setup(interface_copyable_copy_init, location,
                                                                         !m_common->lc_copy.body ? default_ :
                                                                         vector_of(parameter_location(location, m_common->lc_copy.dest_name),
                                                                                   parameter_location(location, m_common->lc_copy.src_name)));

    TreePtr<Term> body, dest = TermBuilder::element_value(f.parameters[1], 0, location);
    if (m_common->lc_copy.body) {
      TreePtr<Term> copy_func = TermBuilder::ptr_target(copy_func_ptr, location);
      TreePtr<Term> copy_call = TermBuilder::function_call(copy_func, vector_of<TreePtr<Term> >(f.parameters[0], f.parameters[1], f.parameters[2]), location);
      body = TermBuilder::initialize_value(dest, TermBuilder::default_value(dest->type, location), copy_call, location);
    } else {
      body = TermBuilder::initialize_value(dest, TermBuilder::element_value(f.parameters[2], 0, location),
                                           TermBuilder::empty_value(instance->compile_context()), location);
    }
    return helper.function_finish(f, m_common->evaluate_context->module(), body);
  }
  
  TreePtr<Term> build_copy(const TreePtr<Term>&, ImplementationHelper& helper, const SourceLocation& base_location) {
    SourceLocation location = base_location.named_child("copy");
    ImplementationHelper::FunctionSetup f = helper.member_function_setup(interface_copyable_copy, location,
                                                                         !m_common->lc_copy.body ? default_ :
                                                                         vector_of(parameter_location(location, m_common->lc_copy.src_name),
                                                                                   parameter_location(location, m_common->lc_copy.dest_name)));
    
    TreePtr<Term> body = build_body(location, m_common->lc_copy, f.parameters[1], f.parameters[2]);
    if (!body)
      body = TermBuilder::assign_value(TermBuilder::element_value(f.parameters[1], 0, location),
                                       TermBuilder::element_value(f.parameters[2], 0, location), location);
    return helper.function_finish(f, m_common->evaluate_context->module(), body);
  }
  
  TreePtr<Implementation> build_copyable(const TreePtr<GenericType>& generic) {
    TreePtr<Term> instance = generic_instance(generic);
    SourceLocation location = generic->location().named_child("Copyable");
    ImplementationHelper helper(location, generic->compile_context().builtins().copyable_interface,
                                m_common->arguments.list, vector_of(instance), default_);

    PSI_STD::vector<TreePtr<Term> > members(3);
    members[interface_copyable_movable] = TermBuilder::interface_value(generic->compile_context().builtins().movable_interface, vector_of(instance), default_, location);
    members[interface_copyable_copy] = build_copy(instance, helper, location);
    members[interface_copyable_copy_init] = build_copy_init(instance, helper, location, members[interface_copyable_copy]);
    return helper.finish(TermBuilder::struct_value(generic->compile_context(), members, location));
  }
  
  PSI_STD::vector<TreePtr<OverloadValue> > evaluate(const TreePtr<GenericType>& self) {
    PSI_STD::vector<TreePtr<OverloadValue> > overloads;
    overloads.push_back(build_movable(self));
    overloads.push_back(build_copyable(self));
    return overloads;
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
    ObjectPtr<AggregateMacroCommon> common(new AggregateMacroCommon(parameters, evaluate_context, location));

    GenericType::GenericTypePrimitive primitive_mode = GenericType::primitive_recurse;
    if (common->lc_init.mode || common->lc_fini.mode || common->lc_move.mode || common->lc_copy.mode)
      primitive_mode = GenericType::primitive_never;
    
    TreePtr<GenericType> generic = TermBuilder::generic(self.compile_context(), common->arguments.pattern, primitive_mode, location,
                                                        StructTypeCallback(common), StructOverloadsCallback(common));

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
