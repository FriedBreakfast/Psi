#include "Macros.hpp"
#include "Parser.hpp"
#include "Enums.hpp"
#include "Interface.hpp"

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
  Parser::ParserLocation dest_name;
  /// \brief Name of source variable
  Parser::ParserLocation src_name;
  /// \brief Function body
  SharedPtr<Parser::TokenExpression> body;
  
  /**
   * Doesn't hold any Tree references so don't bother with proper visit() implementation
   */
  template<typename V>
  static void visit(V& v) {}
};

/**
 * Helper class for generating aggregate types; contains functions common
 * to struct and union.
 */
struct AggregateMacroCommon {
  SharedPtr<Parser::TokenExpression> generic_parameters_expr, members_expr, interfaces_expr;

  void split_parameters(CompileContext& compile_context,
                        const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                        const SourceLocation& location);
  
  PSI_STD::vector<TreePtr<Anonymous> > argument_list;
  PSI_STD::vector<TreePtr<Term> > argument_type_list;

  void parse_arguments(const TreePtr<EvaluateContext>& evaluate_context,
                       const SourceLocation& location);
  
  TreePtr<EvaluateContext> member_context;
  PSI_STD::vector<TreePtr<Term> > member_types;
  std::map<String, unsigned> member_names;

  void parse_members(const SourceLocation& location);

  AggregateLifecycleImpl lc_init, lc_fini, lc_move, lc_move_init, lc_copy, lc_copy_init;
  
  void parse_interfaces(const SourceLocation& location);
  
  void run(const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
           const TreePtr<EvaluateContext>& evaluate_context,
           const SourceLocation& location);
};

void AggregateMacroCommon::split_parameters(CompileContext& compile_context,
                                            const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                            const SourceLocation& location) {
  switch (parameters.size()) {
  case 1:
    if (!(members_expr = expression_as_token_type(parameters[0], Parser::TokenExpression::square_bracket)))
      compile_context.error_throw(location, "Sole parameter to struct macro is not a [...]");
    break;
    
  case 2:
    if (generic_parameters_expr = expression_as_token_type(parameters[0], Parser::TokenExpression::bracket)) {
      if (!(members_expr = expression_as_token_type(parameters[1], Parser::TokenExpression::square_bracket)))
        compile_context.error_throw(location, "Members (second) parameter to struct macro is not a [...]");
    } else {
      if (!(members_expr = expression_as_token_type(parameters[0], Parser::TokenExpression::square_bracket)))
        compile_context.error_throw(location, "First parameter to struct macro is neither (...) nor [...]");
      if (!(interfaces_expr = expression_as_token_type(parameters[1], Parser::TokenExpression::square_bracket)))
        compile_context.error_throw(location, "Interfaces (second) parameter to struct macro is not a [...]");
    }
    break;
    
  case 3:
    if (!(generic_parameters_expr = expression_as_token_type(parameters[0], Parser::TokenExpression::bracket)))
      compile_context.error_throw(location, "First parameter to struct macro is not a (...)");
    if (!(members_expr = expression_as_token_type(parameters[1], Parser::TokenExpression::square_bracket)))
      compile_context.error_throw(location, "Second parameter to struct macro is not a [...]");
    if (!(interfaces_expr = expression_as_token_type(parameters[2], Parser::TokenExpression::square_bracket)))
      compile_context.error_throw(location, "Third parameter to struct macro is not a [...]");
    break;
    
  default:
    compile_context.error_throw(location, "struct macro expects from 1 to 3 arguments");
  }
}

/**
 * Parse generic arguments to an aggregate type.
 */
void AggregateMacroCommon::parse_arguments(const TreePtr<EvaluateContext>& evaluate_context,
                                           const SourceLocation& location) {
  std::map<String, TreePtr<Term> > argument_names;

  Parser::ImplicitArgumentDeclarations generic_parameters_parsed =
    Parser::parse_function_argument_implicit_declarations(generic_parameters_expr->text);
    
  for (PSI_STD::vector<SharedPtr<Parser::FunctionArgument> >::const_iterator ii = generic_parameters_parsed.arguments.begin(), ie = generic_parameters_parsed.arguments.end(); ii != ie; ++ii) {
    PSI_ASSERT(*ii && (*ii)->type);
    const Parser::FunctionArgument& argument_expr = **ii;
    
    String expr_name;
    LogicalSourceLocationPtr argument_logical_location;
    if (argument_expr.name) {
      expr_name = String(argument_expr.name->begin, argument_expr.name->end);
      argument_logical_location = location.logical->named_child(expr_name);
    } else {
      argument_logical_location = location.logical->new_anonymous_child();
    }
    SourceLocation argument_location(argument_expr.location.location, argument_logical_location);

    if (argument_expr.mode != parameter_mode_input)
      evaluate_context.compile_context().error_throw(argument_location, "Generic type parameters must be declared with ':'");

    TreePtr<EvaluateContext> argument_context = evaluate_context_dictionary(evaluate_context->module(), argument_location, argument_names, evaluate_context);
    TreePtr<Term> argument_type = compile_expression(argument_expr.type, argument_context, argument_location.logical);
    argument_type_list.push_back(argument_type->parameterize(argument_location, argument_list));
    TreePtr<Anonymous> argument(new Anonymous(argument_type, argument_location));
    argument_list.push_back(argument);

    if (argument_expr.name)
      argument_names[expr_name] = argument;
  }

  member_context = evaluate_context_dictionary(evaluate_context->module(), location, argument_names, evaluate_context);
}

/**
 * Parse members of an aggregate type.
 */
void AggregateMacroCommon::parse_members(const SourceLocation& location) {
  // Handle members
  PSI_STD::vector<SharedPtr<Parser::Statement> > members_parsed = Parser::parse_namespace(members_expr->text);
  for (PSI_STD::vector<SharedPtr<Parser::Statement> >::const_iterator ii = members_parsed.begin(), ie = members_parsed.end(); ii != ie; ++ii) {
    if (*ii && (*ii)->expression) {
      const Parser::Statement& stmt = **ii;
      String member_name;
      LogicalSourceLocationPtr member_logical_location;
      if (stmt.name) {
        member_name = String(stmt.name->begin, stmt.name->end);
        member_logical_location = location.logical->named_child(member_name);
      } else {
        member_logical_location = location.logical->new_anonymous_child();
      }
      SourceLocation stmt_location(stmt.location.location, member_logical_location);

      if (stmt.name) {
        if (stmt.mode != statement_mode_value)
          member_context.compile_context().error_throw(stmt_location, "Struct members must be declared with ':'");
      } else {
        PSI_ASSERT(stmt.mode == statement_mode_destroy);
      }
      
      TreePtr<Term> member_type = compile_expression(stmt.expression, member_context, stmt_location.logical);
      member_type = member_type->parameterize(stmt_location, argument_list);
      member_types.push_back(member_type);

      if (stmt.name)
        member_names[member_name] = member_types.size();
    }
  }
}

void AggregateMacroCommon::parse_interfaces(const SourceLocation& location) {
  PSI_STD::vector<SharedPtr<Parser::Implementation> > interfaces_parsed = Parser::parse_implementation_list(interfaces_expr->text);

  // Remove any empty entries
  for (PSI_STD::vector<SharedPtr<Parser::Implementation> >::iterator ii = interfaces_parsed.begin(); ii != interfaces_parsed.end();) {
    if (*ii)
      ++ii;
    else
      ii = interfaces_parsed.erase(ii);
  }
  
  // Look for an unnamed interface which will be the move/construct/destroy interface
  SharedPtr<Parser::Implementation> mcd_impl;
  for (PSI_STD::vector<SharedPtr<Parser::Implementation> >::iterator ii = interfaces_parsed.begin(), ie = interfaces_parsed.end(); ii != ie; ++ii) {
    if ((*ii)->constructor) {
      mcd_impl = *ii;
      interfaces_parsed.erase(ii);
      break;
    }
  }
  
  if (mcd_impl) {
    // Parse move-construct-destroy implementation.
    // This will have up to 5 function bodies: Construct, destroy, move, copy, assign
    SharedPtr<Parser::TokenExpression> mcd_expr = expression_as_token_type(mcd_impl->value, Parser::TokenExpression::square_bracket);
    if (!mcd_expr)
      member_context.compile_context().error_throw(location, "Lifecycle functions are not enclosed in a [...]");
    PSI_STD::vector<SharedPtr<Parser::Lifecycle> > lifecycle_exprs = parse_lifecycle_list(mcd_expr->text);
    
    for (PSI_STD::vector<SharedPtr<Parser::Lifecycle> >::const_iterator ii = lifecycle_exprs.begin(), ie = lifecycle_exprs.end(); ii != ie; ++ii) {
      const Parser::Lifecycle& lc = **ii;
      AggregateLifecycleImpl *lc_impl;
      bool binary = true, nullable = false;
      String function_name(lc.function_name.begin, lc.function_name.end);
      if (function_name == "init") {
        binary = false;
        lc_impl = &lc_init;
      } else if (function_name == "fini") {
        binary = false;
        lc_impl = &lc_fini;
      } else if (function_name == "move") {
        lc_impl = &lc_move;
      } else if (function_name == "move_init") {
        lc_impl = &lc_move_init;
      } else if (function_name == "copy") {
        nullable = true;
        lc_impl = &lc_copy;
      } else if (function_name == "copy_init") {
        nullable = true;
        lc_impl = &lc_copy_init;
      } else {
        member_context.compile_context().error_throw(location, boost::format("Unknown lifecycle function: %s") % function_name);
      }
      
      if (lc_impl->mode != AggregateLifecycleImpl::mode_default)
        member_context.compile_context().error_throw(location, boost::format("Duplicate lifecycle function: %s") % function_name);
      
      if (!lc_impl->body) {
        if (!nullable)
          member_context.compile_context().error_throw(location, boost::format("Lifecycle function cannot be deleted: %s") % function_name);
      } else {
        if (lc.body->token_type != Parser::TokenExpression::square_bracket)
          member_context.compile_context().error_throw(location, boost::format("Lifecycle function definition is not a [...]") % function_name);
      }

      if (binary) {
        if (!lc.src_name)
          member_context.compile_context().error_throw(location, boost::format("Lifecycle function %s requires two arguments") % function_name);
        lc_impl->src_name = *lc.src_name;
      } else {
        if (lc.src_name)
          member_context.compile_context().error_throw(location, boost::format("Lifecycle function %s only takes one argument") % function_name);
      }

      lc_impl->mode = lc.body ? AggregateLifecycleImpl::mode_impl : AggregateLifecycleImpl::mode_delete;
      lc_impl->binary = binary;
      lc_impl->body = lc.body;
      lc_impl->dest_name = lc.dest_name;
      lc_impl->physical_location = lc.location.location;
    }
  }
  
  // Handle remaining interfaces
#if 1
  if (!interfaces_parsed.empty())
    PSI_NOT_IMPLEMENTED();
#else
  for (PSI_STD::vector<SharedPtr<Parser::Implementation> >::const_iterator ii = interfaces_parsed.begin(), ie = interfaces_parsed.end(); ii != ie; ++ii) {
    const Parser::Implementation& impl = **ii;
    if (!impl.interface)
      member_context.compile_context().error_throw(impl.location, "Interface name missing; only a single entry in a struct interface list may be empty");
    
    TreePtr<Term> interface = compile_expression(impl.interface, member_context, location.logical);
  }
#endif
}
  
void AggregateMacroCommon::run(const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                               const TreePtr<EvaluateContext>& evaluate_context,
                               const SourceLocation& location) {
  split_parameters(evaluate_context.compile_context(), parameters, location);
  if (generic_parameters_expr)
    parse_arguments(evaluate_context, location);
  parse_members(location);
  if (interfaces_expr)
    parse_interfaces(location);
}

class StructLifecycleCommon {
protected:
  TreePtr<Term> m_type_instance;
  TreePtr<EvaluateContext> m_evaluate_context;
  
  struct LifecycleFunctionSetup {
    TreePtr<FunctionType> function_type;
    TreePtr<Term> body;
    TreePtr<Term> return_type;
    PSI_STD::vector<TreePtr<Anonymous> > arguments;
  };
  
  LifecycleFunctionSetup lifecycle_setup(const AggregateLifecycleImpl& impl, const LogicalSourceLocationPtr& location) {
    LifecycleFunctionSetup lf;
    lf.return_type = m_type_instance.compile_context().builtins().empty_type;
    lf.arguments.push_back(TreePtr<Anonymous>(new Anonymous(m_type_instance, SourceLocation(impl.src_name.location, location))));
    if (impl.binary)
      lf.arguments.push_back(TreePtr<Anonymous>(new Anonymous(m_type_instance, SourceLocation(impl.dest_name.location, location))));
    FunctionParameterType parameter_type(parameter_mode_functional, m_type_instance);
    lf.function_type.reset(new FunctionType(result_mode_by_value, lf.return_type,
                                            PSI_STD::vector<FunctionParameterType>(impl.binary ? 2 : 1, parameter_type),
                                            default_, SourceLocation(impl.physical_location, location)));
    
    if (impl.mode == AggregateLifecycleImpl::mode_impl) {
      PSI_ASSERT(impl.body);
      lf.body = compile_expression(impl.body, m_evaluate_context, location);
    } else {
      PSI_ASSERT(!impl.body);
      lf.body.reset(new DefaultValue(m_type_instance.compile_context().builtins().empty_type,
                                     SourceLocation(impl.physical_location, location)));
    }
    
    return lf;
  }

public:
  typedef Implementation TreeResultType;
  
  StructLifecycleCommon(const AggregateMacroCommon& helper)
  : m_evaluate_context(helper.member_context) {
  }
  
  template<typename V>
  static void visit(V& v) {
    v("type_instance", &StructLifecycleCommon::m_type_instance)
    ("evaluate_context", &StructLifecycleCommon::m_evaluate_context);
  }
};

class StructMovableCallback : public StructLifecycleCommon {
  AggregateLifecycleImpl m_lc_init, m_lc_fini, m_lc_move, m_lc_move_init;
  /// Member movable interfaces
  PSI_STD::vector<TreePtr<Term> > m_member_movable;
  
public:
  StructMovableCallback(const AggregateMacroCommon& helper)
  : StructLifecycleCommon(helper),
  m_lc_init(helper.lc_init),
  m_lc_fini(helper.lc_fini),
  m_lc_move(helper.lc_move),
  m_lc_move_init(helper.lc_move_init) {
  }
  
  TreePtr<Implementation> evaluate(const TreePtr<Implementation>& self) {
    setup_members();
    build_init();
  }
  
  template<typename V>
  static void visit(V& v) {
    visit_base<StructLifecycleCommon>(v);
    v("lc_init", &StructMovableCallback::m_lc_init)
    ("lc_fini", &StructMovableCallback::m_lc_fini)
    ("lc_move", &StructMovableCallback::m_lc_move)
    ("lc_move_init", &StructMovableCallback::m_lc_move_init)
    ("member_movable", &StructMovableCallback::m_member_movable);
  }
  
private:
  void setup_members() {
  }
  
  TreePtr<Function> build_init() {
    TreePtr<FunctionType> init_type;
    LifecycleFunctionSetup lf = lifecycle_setup(m_lc_init, default_);

    TreePtr<Term> inner = lf.body;
    
    SourceLocation location;
    const TreePtr<Term>& argument = lf.arguments.front();
    for (std::size_t ii = 0, ie = m_member_movable.size(); ii != ie; ++ii) {
      std::size_t idx = ie - ii - 1;
      const TreePtr<Term>& ty_interface = m_member_movable[idx];
      
      PSI_STD::vector<TreePtr<Term> > statements;
      TreePtr<Term> member_ptr(new ElementPtr(argument, idx, location));
      TreePtr<Term> construct_func(new ElementValue(ty_interface, interface_movable_init, location));
      TreePtr<Term> construct(new FunctionCall(construct_func, vector_of(member_ptr), location));
      TreePtr<Term> cleanup_func(new ElementValue(ty_interface, interface_movable_fini, location));
      TreePtr<Term> cleanup(new FunctionCall(cleanup_func, vector_of(member_ptr), location));
      statements.push_back(construct);
      inner.reset(new TryFinally(inner, cleanup, true, location));
    }
    
    return TreePtr<Function>(new Function(m_evaluate_context->module(), false, lf.function_type,
                                          lf.arguments, inner, default_, lf.function_type.location()));
  }
  
  TreePtr<Function> build_move_init(const TreePtr<Term>& instance) {
  }
  
  TreePtr<Function> build_move(const TreePtr<Term>& instance) {
  }
  
  TreePtr<Function> build_fini(const TreePtr<Term>& instance) {
  }
};

class StructCopyableCallback : public StructLifecycleCommon {
public:
  typedef Implementation TreeResultType;
  
  StructCopyableCallback(const AggregateMacroCommon& helper)
  : StructLifecycleCommon(helper) {
  }
  
  TreePtr<Implementation> evaluate(const TreePtr<Implementation>& self) {
  }

  template<typename V>
  static void visit(V& v) {
    visit_base<StructLifecycleCommon>(v);
  }
};

class StructCreateCallback {
  PSI_STD::vector<SharedPtr<Parser::Expression> > m_parameters;
  TreePtr<EvaluateContext> m_evaluate_context;
  
public:
  typedef GenericType TreeResultType;
  
  StructCreateCallback(const List<SharedPtr<Parser::Expression> >& parameters,
                       const TreePtr<EvaluateContext>& evaluate_context)
  : m_parameters(parameters.to_vector()),
  m_evaluate_context(evaluate_context) {
  }
  
  TreePtr<GenericType> evaluate(const TreePtr<GenericType>& self) {
    AggregateMacroCommon helper;
    helper.run(m_parameters, m_evaluate_context, self.location());
    
    int primitive_mode = GenericType::primitive_recurse;
    if (helper.lc_init.mode || helper.lc_fini.mode || helper.lc_move.mode ||
      helper.lc_move_init.mode || helper.lc_copy.mode || helper.lc_copy_init.mode)
      primitive_mode = GenericType::primitive_never;
    
    TreePtr<Implementation> movable_impl = tree_callback(self.compile_context(), self.location(), StructMovableCallback(helper));
    TreePtr<Implementation> copyable_impl = tree_callback(self.compile_context(), self.location(), StructCopyableCallback(helper));
    
    PSI_STD::vector<TreePtr<OverloadValue> > overloads;
    overloads.push_back(movable_impl);
    overloads.push_back(copyable_impl);

    TreePtr<StructType> member_type(new StructType(self.compile_context(), helper.member_types, self.location()));
    TreePtr<GenericType> generic(new GenericType(helper.argument_type_list, member_type, overloads, primitive_mode, self.location()));
    
    return generic;
  }
  
  template<typename V>
  static void visit(V& v) {
    v("parameters", &StructCreateCallback::m_parameters)
    ("evaluate_context", &StructCreateCallback::m_evaluate_context);
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
                                     const List<SharedPtr<Parser::Expression> >& parameters,
                                     const TreePtr<EvaluateContext>& evaluate_context,
                                     const SourceLocation& location) {
    PSI_NOT_IMPLEMENTED();
#if 0
    if (helper.argument_list.empty()) {
      TreePtr<TypeInstance> inst(new TypeInstance(generic, default_, location));
      return inst;
    } else {
      PSI_NOT_IMPLEMENTED();
    }
#endif
  }
};

const MacroMemberCallbackVtable StructMacroCallback::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(StructMacroCallback, "psi.compiler.StructMacroCallback", MacroMemberCallback);

TreePtr<Term> struct_macro(CompileContext& compile_context, const SourceLocation& location) {
  TreePtr<MacroMemberCallback> callback(new StructMacroCallback(compile_context, location));
  return make_macro_term(make_macro(compile_context, location, callback), location);
}
}
}
