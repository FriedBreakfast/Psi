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
  SharedPtr<Parser::TokenExpression> generic_parameters_expr, members_expr, constructor_expr, interfaces_expr;

  void split_parameters(CompileContext& compile_context,
                        const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                        const SourceLocation& location);
  
  PSI_STD::vector<String> argument_names;
  PSI_STD::vector<TreePtr<Anonymous> > argument_list;
  PSI_STD::vector<TreePtr<Term> > argument_type_list;

  void parse_arguments(const TreePtr<EvaluateContext>& evaluate_context,
                       const SourceLocation& location);
  
  TreePtr<EvaluateContext> member_context;
  PSI_STD::vector<TreePtr<Term> > member_types;
  std::map<String, unsigned> member_names;

  void parse_members(const SourceLocation& location);

  void parse_constructors(const SourceLocation& location);
  
  AggregateLifecycleImpl lc_init, lc_fini, lc_move, lc_copy;
  
  void parse_interfaces(const SourceLocation& location);
  
  void run(const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
           const TreePtr<EvaluateContext>& evaluate_context,
           const SourceLocation& location);
};

void AggregateMacroCommon::split_parameters(CompileContext& compile_context,
                                            const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                            const SourceLocation& location) {
  if ((parameters.size() < 1) || (parameters.size() > 4))
    compile_context.error_throw(location, "struct macro expects from 1 to 4 arguments");
  
  std::size_t index = 0;
  if (generic_parameters_expr = expression_as_token_type(parameters[index], Parser::TokenExpression::bracket))
    ++index;
  if (index == parameters.size())
    compile_context.error_throw(location, "struct macro missing [...] members parameter ");
  if (!(members_expr = expression_as_token_type(parameters[index], Parser::TokenExpression::square_bracket)))
    compile_context.error_throw(location, boost::format("Parameter %s to struct macro is not a [...]") % index);
  ++index;
  
  if (index < parameters.size()) {
    if (constructor_expr = expression_as_token_type(parameters[index], Parser::TokenExpression::bracket))
      ++index;
    if (index < parameters.size()) {
      if (interfaces_expr = expression_as_token_type(parameters[index], Parser::TokenExpression::square_bracket))
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

/**
 * Parse move-construct-destroy implementation.
 * This will have up to 5 function bodies: Construct, destroy, move, copy, assign
 */
void AggregateMacroCommon::parse_constructors(const SourceLocation& location) {
  PSI_STD::vector<SharedPtr<Parser::Lifecycle> > lifecycle_exprs = parse_lifecycle_list(constructor_expr->text);
  
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
    } else if (function_name == "copy") {
      nullable = true;
      lc_impl = &lc_copy;
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

void AggregateMacroCommon::parse_interfaces(const SourceLocation& location) {
  PSI_STD::vector<SharedPtr<Parser::Implementation> > interfaces_parsed = Parser::parse_implementation_list(interfaces_expr->text);

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
  if (constructor_expr)
    parse_constructors(location);
  if (interfaces_expr)
    parse_interfaces(location);
}

class AggregateLifecycleBase {
  TreePtr<GenericType> m_generic;
  TreePtr<EvaluateContext> m_evaluate_context;
  PSI_STD::vector<String> m_argument_names;
  PSI_STD::vector<TreePtr<Term> > m_member_types;

public:
  AggregateLifecycleBase(const TreePtr<GenericType>& generic, const TreePtr<EvaluateContext>& evaluate_context, const AggregateMacroCommon& helper)
  : m_generic(generic),
  m_evaluate_context(evaluate_context),
  m_argument_names(helper.argument_names),
  m_member_types(helper.member_types) {
  }
  
  template<typename V>
  static void visit(V& v) {
    v("generic", &AggregateLifecycleBase::m_generic)
    ("evaluate_context", &AggregateLifecycleBase::m_evaluate_context)
    ("argument_names", &AggregateLifecycleBase::m_argument_names);
    ("member_types", &AggregateLifecycleBase::m_member_types);
  }
  
  const TreePtr<GenericType>& generic() const {return m_generic;}
  const TreePtr<EvaluateContext>& evaluate_context() const {return m_evaluate_context;}
  const PSI_STD::vector<String>& argument_names() const {return m_argument_names;}
  const PSI_STD::vector<TreePtr<Term> >& member_types() const {return m_member_types;}
};

class StructLifecycleMain {
protected:
  TreePtr<Term> m_empty_type, m_empty_value;

  SourceLocation m_location;
  TreePtr<EvaluateContext> m_evaluate_context;
  TreePtr<Interface> m_movable_interface, m_copyable_interface;

  TreePtr<Term> m_outer_type;
  
  PSI_STD::vector<TreePtr<Term> > m_member_movable;
  PSI_STD::vector<TreePtr<Term> > m_member_copyable;
  
  /// Type of movable and copyable interface specific to this class
  TreePtr<Term> m_movable_type, m_copyable_type;
  
  struct LifecycleFunctionSetup {
    SourceLocation location;
    TreePtr<FunctionType> function_type;
    TreePtr<StructType> type_instance;
    TreePtr<Term> body;
    PSI_STD::vector<TreePtr<Anonymous> > instance_arguments;
    TreePtr<Term> src_argument, dest_argument;
    PSI_STD::vector<TreePtr<Term> > pattern;
    TreePtr<Term> pattern_instance;
  };
  
  StructLifecycleMain(const TreePtr<Implementation>& self, AggregateLifecycleBase& base, bool with_copy) {
    m_location = self.location();
    m_evaluate_context = base.evaluate_context();
    m_empty_type = self.compile_context().builtins().empty_type;
    m_empty_value.reset(new DefaultValue(m_empty_type, m_location));

    m_movable_interface = self.compile_context().builtins().movable_interface;
    m_copyable_interface = self.compile_context().builtins().copyable_interface;
    for (std::size_t ii = 0, ie = base.member_types().size(); ii != ie; ++ii) {
      TreePtr<Term> move_value(new InterfaceValue(m_movable_interface, vector_of(base.member_types()[ii]), m_location));
      m_member_movable.push_back(move_value);
      TreePtr<Term> copy_value(new InterfaceValue(m_copyable_interface, vector_of(base.member_types()[ii]), m_location));
      m_member_copyable.push_back(copy_value);
    }
    
    // Construct value type of implementation
    TreePtr<Term> introduce_type(new IntroduceType(base.generic()->pattern, m_empty_type, self.location()));
    TreePtr<GenericType> movable_generic;
    TreePtr<Term> generic_instance(new TypeInstance(base.generic(), base.generic()->pattern, self.location()));
    m_outer_type = interface_type(movable_generic, base.generic()->pattern, PSI_STD::vector<TreePtr<Term> >(1, generic_instance));
  }
  
  /**
   * \param empty_value Whether to assign LifecycleFunctionSetup::body an empty value when no body is present;
   * otherwise this value is null.
   */
  LifecycleFunctionSetup lifecycle_setup(const AggregateLifecycleImpl& impl, const LogicalSourceLocationPtr& location, bool empty_value) {
    LifecycleFunctionSetup lf;
    lf.location = SourceLocation(impl.physical_location, location);
    
    // Set up template arguments
    PSI_STD::vector<FunctionParameterType> argument_types;
    argument_types.push_back(FunctionParameterType(parameter_mode_input, m_outer_type));
    argument_types.push_back(FunctionParameterType(parameter_mode_functional, m_pattern_instance));
    if (impl.binary)
      argument_types.push_back(FunctionParameterType(parameter_mode_functional, m_pattern_instance));
    
    /// \todo Need to import interfaces specified for the generic type!
    lf.function_type.reset(new FunctionType(result_mode_by_value, m_empty_type, argument_types,
                                            default_, SourceLocation(impl.physical_location, location)));
    
    PSI_STD::vector<TreePtr<Term> > arguments;
    for (PSI_STD::vector<FunctionParameterType>::const_iterator ii = argument_types.begin(), ie = argument_types.end(); ii != ie; ++ii) {
      TreePtr<Term> ty = lf.function_type->parameter_type_after(lf.location, arguments);
      lf.arguments.push_back(TreePtr<Anonymous>(new Anonymous(ty, lf.location)));
      arguments.push_back(lf.arguments.back());
    }

    if (impl.binary) {
      lf.src_argument = lf.arguments[lf.arguments.size()-2];
      lf.dest_argument = lf.arguments[lf.arguments.size()-1];
    } else {
      lf.src_argument = lf.arguments.back();
    }
    
    if (impl.mode == AggregateLifecycleImpl::mode_impl) {
      PSI_ASSERT(impl.body);
      lf.body = compile_expression(impl.body, m_evaluate_context, location);
    } else {
      PSI_ASSERT(!impl.body);
      if (empty_value)
        lf.body = m_empty_value;
    }
    
    return lf;
  }
  
  TreePtr<Implementation> lifecycle_implementation(const TreePtr<Interface>& interface,
                                                   const PSI_STD::vector<TreePtr<Function> >& value,
                                                   const SourceLocation& location) {
    PSI_STD::vector<TreePtr<Term> > wrapped_values;
    for (PSI_STD::vector<TreePtr<Function> >::const_iterator ii = value.begin(), ie = value.end(); ii != ie; ++ii) {
      TreePtr<Function> wrapper(new Function(result_mode_by_value, m_empty_type, XX, default_, ii->location()));
    }
    return TreePtr<Implementation>(new Implementation(default_, value, interface, m_generic->pattern.size(), vector_of(m_pattern_instance), location));
  }
  
  TreePtr<Function> build_init_like(const LifecycleFunctionSetup& lf, const SourceLocation& location) {
    TreePtr<Term> inner = lf.body;
    
    const TreePtr<Term>& argument = lf.arguments.front();
    for (std::size_t ii = 0, ie = m_member_movable.size(); ii != ie; ++ii) {
      std::size_t idx = ie - ii - 1;
      const TreePtr<Term>& ty_interface = m_member_movable[idx];
      
      PSI_STD::vector<TreePtr<Statement> > statements;
      TreePtr<Term> member_ptr(new ElementPtr(argument, idx, location));
      TreePtr<Statement> member_ptr_stmt(new Statement(member_ptr, statement_mode_value, member_ptr.location()));
      statements.push_back(member_ptr_stmt);
      TreePtr<Term> member_ptr_ref(new StatementRef(member_ptr_stmt, member_ptr_stmt.location()));
      
      TreePtr<Term> construct_func(new ElementValue(ty_interface, interface_movable_init, location));
      TreePtr<Term> construct(new FunctionCall(construct_func, vector_of(member_ptr_ref), location));
      TreePtr<Statement> construct_stmt(new Statement(construct, statement_mode_destroy, construct.location()));
      statements.push_back(construct_stmt);
      
      TreePtr<Term> cleanup_func(new ElementValue(ty_interface, interface_movable_fini, location));
      TreePtr<Term> cleanup(new FunctionCall(cleanup_func, vector_of(member_ptr_ref), location));
      TreePtr<Term> result(new TryFinally(inner, cleanup, true, location));
      
      inner.reset(new Block(statements, result, location));
    }
    
    return TreePtr<Function>(new Function(m_evaluate_context->module(), false, lf.function_type,
                                          lf.arguments, inner, default_, location));
  }
};

class StructMovableMain : public StructLifecycleMain {
public:
  StructMovableMain(const TreePtr<Implementation>& self, const AggregateLifecycleBase& base) : StructLifecycleMain(self, base, false) {
  }
  
  TreePtr<Function> build_init(const AggregateLifecycleImpl& lc_init) {
    LifecycleFunctionSetup lf = lifecycle_setup(lc_init, default_, true);
    SourceLocation location = m_location.named_child("init").relocate(lc_init.physical_location);
    return build_init_like(lf, location);
  }

  TreePtr<Function> build_fini(const AggregateLifecycleImpl& lc_fini) {
    LifecycleFunctionSetup lf = lifecycle_setup(lc_fini, default_, false);

    SourceLocation location = m_location.named_child("fini").relocate(lc_fini.physical_location);
    const TreePtr<Term>& argument = lf.arguments.front();
    PSI_STD::vector<TreePtr<Statement> > statements;
    if (lf.body) {
      TreePtr<Statement> body(new Statement(lf.body, statement_mode_destroy, lf.body.location()));
      statements.push_back(body);
    }
    
    for (std::size_t ii = 0, ie = m_member_movable.size(); ii != ie; ++ii) {
      std::size_t idx = ie - ii - 1;
      const TreePtr<Term>& ty_interface = m_member_movable[idx];
      
      TreePtr<Term> member_ptr(new ElementPtr(argument, idx, location));
      TreePtr<Term> cleanup_func(new ElementValue(ty_interface, interface_movable_fini, location));
      TreePtr<Term> cleanup(new FunctionCall(cleanup_func, vector_of(member_ptr), location));
      TreePtr<Statement> cleanup_stmt(new Statement(cleanup, statement_mode_destroy, cleanup.location()));
      statements.push_back(cleanup_stmt);
    }
    TreePtr<Term> body(new Block(statements, m_empty_value, location));
    
    return TreePtr<Function>(new Function(m_evaluate_context->module(), false, lf.function_type,
                                          lf.arguments, body, default_, location));
  }
  
  TreePtr<Function> build_move_init(const AggregateLifecycleImpl& lc_move) {
    LifecycleFunctionSetup lf = lifecycle_setup(lc_move, default_, true);
    SourceLocation location = m_location.named_child("move_init").relocate(lc_move.physical_location);
    if (lc_move.mode == AggregateLifecycleImpl::mode_impl) {
      return build_init_like(lf, location);
    } else {
      TreePtr<Term> value = m_empty_value;
      const TreePtr<Term>& dest = lf.dest_argument;
      const TreePtr<Term>& src = lf.src_argument;
      for (std::size_t ii = 0, ie = m_member_movable.size(); ii != ie; ++ii) {
        std::size_t idx = ie - ii - 1;
        const TreePtr<Term>& ty_interface = m_member_movable[idx];
        
        PSI_STD::vector<TreePtr<Statement> > statements;
        TreePtr<Term> dest_member_ptr(new ElementPtr(dest, idx, location));
        TreePtr<Statement> dest_member_ptr_stmt(new Statement(dest_member_ptr, statement_mode_value, dest_member_ptr.location()));
        statements.push_back(dest_member_ptr_stmt);
        TreePtr<Term> dest_member_ptr_ref(new StatementRef(dest_member_ptr_stmt, dest_member_ptr_stmt.location()));
        
        TreePtr<Term> src_member_ptr(new ElementPtr(src, idx, location));

        TreePtr<Term> construct_func(new ElementValue(ty_interface, interface_movable_move_init, location));
        TreePtr<Term> construct(new FunctionCall(construct_func, vector_of(dest_member_ptr_ref, src_member_ptr), location));
        TreePtr<Statement> construct_stmt(new Statement(construct, statement_mode_destroy, construct.location()));
        statements.push_back(construct_stmt);
        
        TreePtr<Term> cleanup_func(new ElementValue(ty_interface, interface_movable_fini, location));
        TreePtr<Term> cleanup(new FunctionCall(cleanup_func, vector_of(dest_member_ptr_ref), location));
        TreePtr<Term> result(new TryFinally(value, cleanup, true, location));
        
        value.reset(new Block(statements, result, location));
      }
      
      return TreePtr<Function>(new Function(m_evaluate_context->module(), false, lf.function_type,
                                            lf.arguments, value, default_, location));
    }
  }
  
  TreePtr<Function> build_move(const AggregateLifecycleImpl& lc_move) {
    LifecycleFunctionSetup lf = lifecycle_setup(lc_move, default_, false);
    SourceLocation location = m_location.named_child("move").relocate(lc_move.physical_location);
    TreePtr<Term> body;
    if (lf.body) {
      body = lf.body;
    } else {
      const TreePtr<Term>& dest = lf.dest_argument;
      const TreePtr<Term>& src = lf.src_argument;
      PSI_STD::vector<TreePtr<Statement> > statements;
      for (std::size_t ii = 0, ie = m_member_movable.size(); ii != ie; ++ii) {
        std::size_t idx = ie - ii - 1;
        const TreePtr<Term>& ty_interface = m_member_movable[idx];
        
        TreePtr<Term> dest_member_ptr(new ElementPtr(dest, idx, location));
        TreePtr<Term> src_member_ptr(new ElementPtr(src, idx, location));
        TreePtr<Term> move_func(new ElementValue(ty_interface, interface_movable_move, location));
        TreePtr<Term> move_call(new FunctionCall(move_func, vector_of(dest_member_ptr, src_member_ptr), location));
        TreePtr<Statement> move_stmt(new Statement(move_call, statement_mode_destroy, location));
        statements.push_back(move_stmt);
      }
      body.reset(new Block(statements, m_empty_value, location));
    }
    
    return TreePtr<Function>(new Function(m_evaluate_context->module(), false, lf.function_type,
                                          lf.arguments, body, default_, location));
  }
};

class StructMovableCallback : public AggregateLifecycleBase {
  AggregateLifecycleImpl m_lc_init, m_lc_fini, m_lc_move;
  
public:
  StructMovableCallback(const TreePtr<GenericType>& generic, const AggregateMacroCommon& helper)
  : AggregateLifecycleBase(generic, helper),
  m_lc_init(helper.lc_init),
  m_lc_fini(helper.lc_fini),
  m_lc_move(helper.lc_move) {
  }
  
  TreePtr<Implementation> evaluate(const TreePtr<Implementation>& self) {
    StructMovableMain main(self);

    PSI_STD::vector<TreePtr<Function> > members(4);
    members[interface_movable_init] = main.build_init(m_lc_init);
    members[interface_movable_fini] = main.build_fini(m_lc_fini);
    members[interface_movable_move_init] = main.build_move_init(m_lc_move);
    members[interface_movable_move] = main.build_move(m_lc_move);
    return main.lifecycle_implementation(main.movable_interface(), members, self.location());
  }
  
  template<typename V>
  static void visit(V& v) {
    visit_base<StructLifecycleCommon>(v);
    v("lc_init", &StructMovableCallback::m_lc_init)
    ("lc_fini", &StructMovableCallback::m_lc_fini)
    ("lc_move", &StructMovableCallback::m_lc_move);
  }
};

class StructCopyableMain : public StructLifecycleMain {
public:
  TreePtr<Function> build_copy_init(const AggregateLifecycleImpl& lc_copy_init, const AggregateLifecycleImpl& lc_copy) {
    LifecycleFunctionSetup lf = lifecycle_setup(lc_copy_init, default_, true);
    SourceLocation location = m_location.named_child("copy_init").relocate(lc_copy_init.physical_location);
    if (lc_copy_init.mode == AggregateLifecycleImpl::mode_impl) {
      return build_init_like(lf, location);
    } else {
      TreePtr<Term> value = m_empty_value;
      const TreePtr<Term>& dest = lf.dest_argument;
      const TreePtr<Term>& src = lf.src_argument;
      for (std::size_t ii = 0, ie = m_member_movable.size(); ii != ie; ++ii) {
        std::size_t idx = ie - ii - 1;
        const TreePtr<Term>& move_interface = m_member_movable[idx];
        const TreePtr<Term>& copy_interface = m_member_copyable[idx];
        
        PSI_STD::vector<TreePtr<Statement> > statements;
        TreePtr<Term> dest_member_ptr(new ElementPtr(dest, idx, location));
        TreePtr<Statement> dest_member_ptr_stmt(new Statement(dest_member_ptr, statement_mode_value, dest_member_ptr.location()));
        statements.push_back(dest_member_ptr_stmt);
        TreePtr<Term> dest_member_ptr_ref(new StatementRef(dest_member_ptr_stmt, dest_member_ptr_stmt.location()));
        
        TreePtr<Term> src_member_ptr(new ElementPtr(src, idx, location));

        TreePtr<Term> construct_func(new ElementValue(copy_interface, interface_copyable_copy_init, location));
        TreePtr<Term> construct(new FunctionCall(construct_func, vector_of(dest_member_ptr_ref, src_member_ptr), location));
        TreePtr<Statement> construct_stmt(new Statement(construct, statement_mode_destroy, construct.location()));
        statements.push_back(construct_stmt);
        
        TreePtr<Term> cleanup_func(new ElementValue(move_interface, interface_movable_fini, location));
        TreePtr<Term> cleanup(new FunctionCall(cleanup_func, vector_of(dest_member_ptr_ref), location));
        TreePtr<Term> result(new TryFinally(value, cleanup, true, location));
        
        value.reset(new Block(statements, result, location));
      }
      
      return TreePtr<Function>(new Function(m_evaluate_context->module(), false, lf.function_type,
                                            lf.arguments, value, default_, location));
    }
  }
  
  TreePtr<Function> build_copy(const AggregateLifecycleImpl& lc_copy) {
    LifecycleFunctionSetup lf = lifecycle_setup(lc_copy, default_, false);
    SourceLocation location = m_location.named_child("copy").relocate(lc_copy.physical_location);
    TreePtr<Term> body;
    if (lf.body) {
      body = lf.body;
    } else {
      const TreePtr<Term>& dest = lf.dest_argument;
      const TreePtr<Term>& src = lf.src_argument;
      PSI_STD::vector<TreePtr<Statement> > statements;
      for (std::size_t ii = 0, ie = m_member_copyable.size(); ii != ie; ++ii) {
        std::size_t idx = ie - ii - 1;
        const TreePtr<Term>& ty_interface = m_member_copyable[idx];
        
        TreePtr<Term> dest_member_ptr(new ElementPtr(dest, idx, location));
        TreePtr<Term> src_member_ptr(new ElementPtr(src, idx, location));
        TreePtr<Term> move_func(new ElementValue(ty_interface, interface_copyable_copy, location));
        TreePtr<Term> move_call(new FunctionCall(move_func, vector_of(dest_member_ptr, src_member_ptr), location));
        TreePtr<Statement> move_stmt(new Statement(move_call, statement_mode_destroy, location));
        statements.push_back(move_stmt);
      }
      body.reset(new Block(statements, m_empty_value, location));
    }
    
    return TreePtr<Function>(new Function(m_evaluate_context->module(), false, lf.function_type,
                                          lf.arguments, body, default_, location));
  }
};

/**
 * \brief Copy 
 */
class StructCopyableCallback : public AggregateLifecycleBase {
  AggregateLifecycleImpl m_lc_copy;
  PSI_STD::vector<TreePtr<Term> > m_member_copyable;
  SourceLocation m_location;
  
public:
  typedef Implementation TreeResultType;
  
  StructCopyableCallback(const AggregateMacroCommon& helper)
  : AggregateLifecycleBase(helper),
  m_lc_copy(helper.lc_copy) {
  }
  
  TreePtr<Implementation> evaluate(const TreePtr<Implementation>& self) {
    StructCopyableMain main(self);

    PSI_STD::vector<TreePtr<Function> > members(2);
    members[interface_copyable_copy] = main.build_copy(m_lc_copy);
    members[interface_copyable_copy_init] = main.build_copy_init(m_lc_copy);
    return main.lifecycle_implementation(main.copyable_interface(), members, self.location());
  }

  template<typename V>
  static void visit(V& v) {
    visit_base<StructLifecycleCommon>(v);
    v("lc_copy", &StructCopyableCallback::m_lc_copy)
    ("member_movable", &StructMovableCallback::m_member_movable);
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
    if (helper.lc_init.mode || helper.lc_fini.mode || helper.lc_move.mode || helper.lc_copy.mode)
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
