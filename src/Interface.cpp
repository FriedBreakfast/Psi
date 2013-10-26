#include "Compiler.hpp"
#include "Interface.hpp"
#include "Tree.hpp"
#include "TermBuilder.hpp"
#include "Macros.hpp"
#include "Parser.hpp"
#include "Aggregate.hpp"
#include "Implementation.hpp"

#include <boost/format.hpp>

namespace Psi {
namespace Compiler {
/**
  * Parse generic arguments to an aggregate type, returning a list of anonymous terms and names for each argument.
  */
PatternArguments parse_pattern_arguments(const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location, const Parser::Text& text) {
  PatternArguments result;

  PSI_STD::vector<SharedPtr<Parser::FunctionArgument> > generic_parameters_parsed =
    Parser::parse_type_argument_declarations(evaluate_context->compile_context().error_context(), location.logical, text);
    
  for (PSI_STD::vector<SharedPtr<Parser::FunctionArgument> >::const_iterator ii = generic_parameters_parsed.begin(), ie = generic_parameters_parsed.end(); ii != ie; ++ii) {
    PSI_ASSERT(*ii && (*ii)->type);
    const Parser::FunctionArgument& argument_expr = **ii;
    
    String expr_name;
    LogicalSourceLocationPtr argument_logical_location;
    if (argument_expr.name) {
      expr_name = String(argument_expr.name->begin, argument_expr.name->end);
      argument_logical_location = location.logical->new_child(expr_name);
    } else {
      argument_logical_location = location.logical;
    }
    SourceLocation argument_location(argument_expr.location, argument_logical_location);

    if (argument_expr.mode != parameter_mode_input)
      evaluate_context->compile_context().error_throw(argument_location, "Pattern parameters must be declared with ':'");

    TreePtr<EvaluateContext> argument_context = evaluate_context_dictionary(evaluate_context->module(), argument_location, result.names, evaluate_context);
    TreePtr<Term> argument_type = compile_term(argument_expr.type, argument_context, argument_location.logical);
    TreePtr<Anonymous> argument = TermBuilder::anonymous(argument_type, term_mode_value, argument_location);
    result.list.push_back(argument);

    if (argument_expr.name)
      result.names[expr_name] = argument;
  }
  
  return result;
}

/**
  * \brief Convert a list of anonymous terms to a pattern of their types suitable for use with function types, generic types, etc.
  */
PSI_STD::vector<TreePtr<Term> > arguments_to_pattern(const PSI_STD::vector<TreePtr<Anonymous> >& arguments, const PSI_STD::vector<TreePtr<Anonymous> >& previous) {
  PSI_STD::vector<TreePtr<Anonymous> > my_arguments = previous;
  PSI_STD::vector<TreePtr<Term> > result;
  for (PSI_STD::vector<TreePtr<Anonymous> >::const_iterator ii = arguments.begin(), ie = arguments.end(); ii != ie; ++ii) {
    result.push_back((*ii)->type->parameterize((*ii)->location(), previous));
    my_arguments.push_back(*ii);
  }
  return result;
}

/**
  * Meta-information about an interface.
  */
class InterfaceMetadata : public Tree {
public:
  static const VtableType vtable;
  
  struct Entry {
    String name;
    TreePtr<InterfaceMemberCallback> callback;
    
    Entry(const String& name_, const TreePtr<InterfaceMemberCallback>& callback_)
    : name(name_), callback(callback_) {}
    
    template<typename V>
    static void visit(V& v) {
      v("name", &Entry::name)
      ("callback", &Entry::callback);
    }
  };
  
  PSI_STD::vector<Entry> entries;
  PSI_STD::map<String, unsigned> entry_names;
  
  InterfaceMetadata(CompileContext& compile_context, const PSI_STD::vector<Entry>& entries_, const SourceLocation& location)
  : Tree(&vtable, compile_context, location), entries(entries_) {
    unsigned index = 0;
    for (PSI_STD::vector<Entry>::const_iterator ii = entries.begin(), ie = entries.end(); ii != ie; ++ii, ++index)
      entry_names.insert(std::make_pair(ii->name, index));
  }
  
  template<typename V>
  static void visit(V& v) {
    visit_base<Tree>(v);
    v("entries", &InterfaceMetadata::entries)
    ("entry_names", &InterfaceMetadata::entry_names);
  }
};

const TreeVtable InterfaceMetadata::vtable = PSI_COMPILER_TREE(InterfaceMetadata, "psi.compiler.InterfaceMetadata", Tree);

namespace {
  struct InterfaceDefineCommonResult {
    /// \brief Interface generic member type
    TreePtr<Term> member_type;
    /// \brief Callbacks used to define and evaluate interface members
    TreePtr<InterfaceMetadata> metadata;
    
    template<typename V>
    static void visit(V& v) {
      v("member_type", &InterfaceDefineCommonResult::member_type)
      ("metadata", &InterfaceDefineCommonResult::metadata);
    }
  };
  
  class InterfaceDefineCommonCallback {
    PatternArguments m_arguments;
    TreePtr<EvaluateContext> m_evaluate_context;
    Parser::Text m_text;
    
  public:
    InterfaceDefineCommonCallback(const PatternArguments& arguments, const TreePtr<EvaluateContext>& evaluate_context, const Parser::Text& text)
    : m_arguments(arguments), m_evaluate_context(evaluate_context), m_text(text) {
    }
    
    template<typename V>
    static void visit(V& v) {
      v("arguments", &InterfaceDefineCommonCallback::m_arguments)
      ("evaluate_context", &InterfaceDefineCommonCallback::m_evaluate_context);
    }
    
    InterfaceDefineCommonResult evaluate(const TreePtr<GenericType>& generic) {
      CompileContext& compile_context = generic->compile_context();
      const SourceLocation& location = generic->location();
      
      TreePtr<EvaluateContext> member_context = evaluate_context_dictionary(m_evaluate_context->module(), location, m_arguments.names, m_evaluate_context);        
      PSI_STD::vector<SharedPtr<Parser::Statement> > members = Parser::parse_namespace(compile_context.error_context(), location.logical, m_text);
      
      InterfaceMemberArgument member_argument;
      member_argument.generic = generic;
      member_argument.parameters.push_back(TermBuilder::anonymous(TermBuilder::upref_type(compile_context), term_mode_value, location));
      member_argument.parameters.insert(member_argument.parameters.end(), m_arguments.list.begin(), m_arguments.list.end());
      member_argument.self_pointer_type = TermBuilder::pointer(TermBuilder::instance(generic, member_argument.parameters, location), location);
      
      PSI_STD::vector<TreePtr<Term> > member_types;
      PSI_STD::vector<InterfaceMetadata::Entry> metadata_entries;

      for (PSI_STD::vector<SharedPtr<Parser::Statement> >::const_iterator ii = members.begin(), ie = members.end(); ii != ie; ++ii) {
        if (*ii && (*ii)->expression) {
          Parser::Statement& stmt = **ii;
          PSI_ASSERT(stmt.name);
          String name = stmt.name->str();
          SourceLocation member_location(stmt.location, location.logical->new_child(name));
          
          if (stmt.mode != statement_mode_value)
            compile_context.error_throw(location, boost::format("Interface member '%s' not defined with ':'") % name);
          
          InterfaceMemberResult member = compile_expression<InterfaceMemberResult>
            (stmt.expression, member_context, compile_context.builtins().macro_interface_member_tag, member_argument, member_location.logical);
          
          if (!member.type)
            compile_context.error_throw(location, boost::format("Interface member '%s' did not give a type") % name);
          if (!member.callback)
            compile_context.error_throw(location, boost::format("Interface member '%s' did not return an evaluation callback") % name);

          member_types.push_back(member.type->parameterize(member_location, m_arguments.list));
          metadata_entries.push_back(InterfaceMetadata::Entry(name, member.callback));
        }
      }
      
      InterfaceDefineCommonResult result;
      result.member_type = TermBuilder::struct_type(compile_context, member_types, location);
      result.metadata.reset(::new InterfaceMetadata(compile_context, metadata_entries, location));
      
      return result;
    }
  };
  
  typedef SharedDelayedValue<InterfaceDefineCommonResult, TreePtr<GenericType> > InterfaceDefineCommonDelayedValue;
  
  class InterfaceDefineMemberCallback {
    InterfaceDefineCommonDelayedValue m_common;
    
  public:
    InterfaceDefineMemberCallback(const InterfaceDefineCommonDelayedValue& common) : m_common(common) {}
    template<typename V> static void visit(V& v) {v("common", &InterfaceDefineMemberCallback::m_common);}
    
    TreePtr<Term> evaluate(const TreePtr<GenericType>& generic) {
      return m_common.get(generic).member_type;
    }
  };
}

class InterfaceTermEvaluateMacro : public Macro {
  TreePtr<Interface> m_interface;
  TreePtr<InterfaceMetadata> m_metadata;
  
public:
  static const VtableType vtable;
  
  InterfaceTermEvaluateMacro(const TreePtr<Interface>& interface, const TreePtr<InterfaceMetadata>& metadata, const SourceLocation& location)
  : Macro(&vtable, interface->compile_context(), location),
  m_interface(interface),
  m_metadata(metadata) {
  }
  
  template<typename V>
  static void visit(V& v) {
    visit_base<Macro>(v);
    v("interface", &InterfaceTermEvaluateMacro::m_interface)
    ("metadata", &InterfaceTermEvaluateMacro::m_metadata);
  }
  
  static TreePtr<Term> evaluate_impl(const InterfaceTermEvaluateMacro& self,
                                      const TreePtr<Term>& PSI_UNUSED(value),
                                      const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                      const TreePtr<EvaluateContext>& evaluate_context,
                                      const MacroTermArgument& PSI_UNUSED(argument),
                                      const SourceLocation& location) {
    PSI_STD::vector<TreePtr<Term> > arguments = compile_call_arguments(parameters, evaluate_context, location);
    return TermBuilder::interface_value(self.m_interface, arguments, location);
  }

  static TreePtr<Term> dot_impl(const InterfaceTermEvaluateMacro& self,
                                const TreePtr<Term>& PSI_UNUSED(value),
                                const SharedPtr<Parser::Expression>& member,
                                const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                const TreePtr<EvaluateContext>& evaluate_context,
                                const MacroTermArgument& PSI_UNUSED(argument),
                                const SourceLocation& location) {
    // Call to a member
    SharedPtr<Parser::TokenExpression> ident;
    if (!(ident = Parser::expression_as_token_type(member, Parser::token_identifier)))
      self.compile_context().error_throw(location, "Interface member name after '.' is not an identifier");
    
    String name = ident->text.str();
    PSI_STD::map<String, unsigned>::const_iterator name_it = self.m_metadata->entry_names.find(name);
    if (name_it == self.m_metadata->entry_names.end()) {
      CompileError err(self.compile_context().error_context(), location);
      err.info(boost::format("Interface '%s' does not have a member named '%s'") % self.m_interface->location().logical->error_name(location.logical) % name);
      err.info(self.m_interface->location(), "Interface defined here");
      err.end_throw();
    }
    
    const InterfaceMetadata::Entry& entry = self.m_metadata->entries[name_it->second];
    return entry.callback->evaluate(vector_of<unsigned>(0, name_it->second), parameters,
                                    evaluate_context, location);
  }
};

const MacroVtable InterfaceTermEvaluateMacro::vtable = PSI_COMPILER_MACRO(InterfaceTermEvaluateMacro, "psi.compiler.InterfaceTermEvaluateMacro", Macro, TreePtr<Term>, MacroTermArgument);

namespace {
  class InterfaceAggregateMemberCallback {
    TreePtr<Interface> m_interface;
    TreePtr<InterfaceMetadata> m_metadata;
    TreePtr<EvaluateContext> m_evaluate_context;
    SourceLocation m_location;
    SharedPtr<Parser::TokenExpression> m_parameters_expression, m_body_expression;
    
  public:
    InterfaceAggregateMemberCallback(const TreePtr<Interface>& interface, const TreePtr<InterfaceMetadata>& metadata,
                                     const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location,
                                     const SharedPtr<Parser::TokenExpression>& parameters_expression,
                                     const SharedPtr<Parser::TokenExpression>& body_expression)
    : m_interface(interface), m_metadata(metadata),
    m_evaluate_context(evaluate_context), m_location(location),
    m_parameters_expression(parameters_expression), m_body_expression(body_expression) {}
    
    template<typename V>
    static void visit(V& v) {
      v("interface", &InterfaceAggregateMemberCallback::m_interface)
      ("metadata", &InterfaceAggregateMemberCallback::m_metadata)
      ("evaluate_context", &InterfaceAggregateMemberCallback::m_evaluate_context)
      ("location", &InterfaceAggregateMemberCallback::m_location)
      ("parameters_expression", &InterfaceAggregateMemberCallback::m_parameters_expression)
      ("body_expression", &InterfaceAggregateMemberCallback::m_body_expression);
    }
    
    PSI_STD::vector<TreePtr<OverloadValue> > evaluate(const AggregateMemberArgument& argument) {
      CompileContext& compile_context = argument.generic->compile_context();
      
      PSI_STD::vector<TreePtr<Anonymous> > wildcards;
      PSI_STD::vector<TreePtr<Term> > interface_parameters;
      if (m_parameters_expression) {
        PSI_NOT_IMPLEMENTED();
      } else {
        // Default is a single argument which is the containing type
        wildcards = argument.parameters;
        interface_parameters.push_back(argument.instance);
      }
      
      ImplementationHelper helper(m_location, m_interface, wildcards, interface_parameters, default_);

      PSI_STD::vector<TreePtr<Term> > entry_values(m_metadata->entries.size());
      PSI_STD::vector<SharedPtr<Parser::Statement> > entries = Parser::parse_namespace(compile_context.error_context(), m_location.logical, m_body_expression->text);
      for (PSI_STD::vector<SharedPtr<Parser::Statement> >::const_iterator ii = entries.begin(), ie = entries.end(); ii != ie; ++ii) {
        if (*ii) {
          const Parser::Statement& stmt = **ii;
          PSI_ASSERT(stmt.name && stmt.expression); // Enforced by parser
          String name = stmt.name->str();
          PSI_STD::map<String, unsigned>::const_iterator name_it = m_metadata->entry_names.find(name);
          if (name_it == m_metadata->entry_names.end())
            compile_context.error_throw(m_location.relocate(stmt.location), boost::format("Interface '%s' has no member named '%s'") % m_interface->location().logical->error_name(m_location.logical) % name);
          PSI_ASSERT(name_it->second < entry_values.size());
          if (entry_values[name_it->second])
            compile_context.error_throw(m_location.relocate(stmt.location), boost::format("Multiple values specified for '%s'") % name);
          
          const InterfaceMetadata::Entry& entry = m_metadata->entries[name_it->second];
          PSI_ASSERT(name == entry.name);
          SourceLocation value_loc(stmt.location, m_location.logical->new_child(name));
          TreePtr<Term> value = entry.callback->implement(stmt.expression, m_evaluate_context, value_loc);
          
          entry_values[name_it->second] = value;
        }
      }
      
      bool failed = false;
      for (std::size_t ii = 0, ie = entry_values.size(); ii != ie; ++ii) {
        if (!entry_values[ii])
          compile_context.error_context().error(m_location, boost::format("No value specified for '%s'") % m_metadata->entries[ii].name);
      }
      if (failed)
        throw CompileException();
      
      TreePtr<Term> inner_value = TermBuilder::struct_value(compile_context, entry_values, m_location);
      TreePtr<Implementation> impl = helper.finish(inner_value);
      return vector_of<TreePtr<OverloadValue> >(impl);
    }
  };
}

class InterfaceAggregateMemberMacro : public Macro {
  TreePtr<Interface> m_interface;
  TreePtr<InterfaceMetadata> m_metadata;
  
public:
  static const VtableType vtable;
  
  InterfaceAggregateMemberMacro(const TreePtr<Interface>& interface, const TreePtr<InterfaceMetadata>& metadata, const SourceLocation& location)
  : Macro(&vtable, interface->compile_context(), location),
  m_interface(interface),
  m_metadata(metadata) {
  }

  template<typename V>
  static void visit(V& v) {
    visit_base<Macro>(v);
    v("interface", &InterfaceAggregateMemberMacro::m_interface)
    ("metadata", &InterfaceAggregateMemberMacro::m_metadata);
  }
  
  static AggregateMemberResult evaluate_impl(const InterfaceAggregateMemberMacro& self,
                                             const TreePtr<Term>& PSI_UNUSED(value),
                                             const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                             const TreePtr<EvaluateContext>& evaluate_context,
                                             const AggregateMemberArgument& PSI_UNUSED(argument),
                                             const SourceLocation& location) {
    SharedPtr<Parser::TokenExpression> params_expr, body;
    switch (parameters.size()) {
    case 1: {
      if (!(body = Parser::expression_as_token_type(parameters[0], Parser::token_square_bracket)))
        self.compile_context().error_throw(location, "Argument to implementation is not a [...]");
      break;
    }
      
    case 2: {
      if (!(params_expr = Parser::expression_as_token_type(parameters[0], Parser::token_square_bracket)))
        self.compile_context().error_throw(location, "First argument to implementation is not a (...)");
      if (!(body = Parser::expression_as_token_type(parameters[1], Parser::token_square_bracket)))
        self.compile_context().error_throw(location, "Second argument to implementation is not a [...]");
      break;
    }
      
    default:
      self.compile_context().error_throw(location, "Interface implementation expects a single argument");
    }

    AggregateMemberResult result;
    result.overloads_callback.reset(self.compile_context(), location,
                                    InterfaceAggregateMemberCallback(self.m_interface, self.m_metadata, evaluate_context,
                                                                     location, params_expr, body));
    return result;
  }
};

const MacroVtable InterfaceAggregateMemberMacro::vtable = PSI_COMPILER_MACRO(InterfaceAggregateMemberMacro, "psi.compiler.InterfaceAggregateMemberMacro", Macro, AggregateMemberResult, AggregateMemberArgument);

namespace {
  /**
    * Callback which generates parameter-less type which
    * presents the interface syntactic interface.
    */
  class InterfaceDefineUserOverloadCallback {
    TreePtr<GenericType> m_generic;
    InterfaceDefineCommonDelayedValue m_common;
    TreePtr<Interface> m_interface;
    
  public:
    InterfaceDefineUserOverloadCallback(const TreePtr<GenericType>& generic, const InterfaceDefineCommonDelayedValue& common, const TreePtr<Interface>& interface)
    : m_generic(generic), m_common(common), m_interface(interface) {}
    
    template<typename V>
    static void visit(V& v) {
      v("generic", &InterfaceDefineUserOverloadCallback::m_generic)
      ("common", &InterfaceDefineUserOverloadCallback::m_common)
      ("interface", &InterfaceDefineUserOverloadCallback::m_interface);
    }
    
    PSI_STD::vector<TreePtr<OverloadValue> > evaluate(const TreePtr<GenericType>& frontend) {
      CompileContext& compile_context = frontend->compile_context();
      const SourceLocation& location = frontend->location();
      
      const InterfaceDefineCommonResult& common_result = m_common.get(m_generic);
      TreePtr<Term> instance = TermBuilder::instance(frontend, location);
      
      PSI_STD::vector<TreePtr<OverloadValue> > result;
      TreePtr<Macro> eval(::new InterfaceTermEvaluateMacro(m_interface, common_result.metadata, location));
      result.push_back(Metadata::new_(eval, compile_context.builtins().type_macro, 0, vector_of(instance, compile_context.builtins().macro_term_tag), location));
      TreePtr<Macro> member(::new InterfaceAggregateMemberMacro(m_interface, common_result.metadata, location));
      result.push_back(Metadata::new_(member, compile_context.builtins().type_macro, 0, vector_of(instance, compile_context.builtins().macro_member_tag), location));
      
      return result;
    }
  };
}

/**
  * Create a new interface.
  */
class InterfaceDefineMacro : public Macro {
public:
  static const MacroVtable vtable;

  InterfaceDefineMacro(CompileContext& compile_context, const SourceLocation& location)
  : Macro(&vtable, compile_context, location) {
  }

  static TreePtr<Term> evaluate_impl(const InterfaceDefineMacro& self,
                                      const TreePtr<Term>& PSI_UNUSED(value),
                                      const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                      const TreePtr<EvaluateContext>& evaluate_context,
                                      const MacroTermArgument& PSI_UNUSED(argument),
                                      const SourceLocation& location) {
    if (parameters.size() != 2)
      self.compile_context().error_throw(location, "Interface definition expects 2 parameters");

    SharedPtr<Parser::TokenExpression> types_expr, members_expr;
    if (!(types_expr = Parser::expression_as_token_type(parameters[0], Parser::token_bracket)))
      self.compile_context().error_throw(location, "First (types) parameter to interface macro is not a (...)");
    if (!(members_expr = Parser::expression_as_token_type(parameters[1], Parser::token_square_bracket)))
      self.compile_context().error_throw(location, "Second (members) parameter to interface macro is not a [...]");

    PatternArguments args = parse_pattern_arguments(evaluate_context, location, types_expr->text);
    if (args.list.empty())
      self.compile_context().error_throw(location, "Interface definition must have at least one parameter");
    
    
    PSI_STD::vector<TreePtr<Anonymous> > generic_args;
    generic_args.push_back(TermBuilder::anonymous(TermBuilder::upref_type(self.compile_context()), term_mode_value, location));
    generic_args.insert(generic_args.end(), args.list.begin(), args.list.end());
    generic_args.insert(generic_args.end(), args.dependent.begin(), args.dependent.end());

    PSI_STD::vector<TreePtr<Term> > generic_pattern = arguments_to_pattern(generic_args);
    InterfaceDefineCommonDelayedValue common(self.compile_context(), location,
                                              InterfaceDefineCommonCallback(args, evaluate_context, members_expr->text));
    TreePtr<GenericType> generic_type = TermBuilder::generic(self.compile_context(), generic_pattern, GenericType::primitive_always,
                                                              location, InterfaceDefineMemberCallback(common));
    
    PSI_STD::vector<TreePtr<Term> > interface_pattern = arguments_to_pattern(args.list);
    PSI_STD::vector<TreePtr<Term> > derived_pattern = arguments_to_pattern(args.dependent, args.list);
    
    // Build value type of interface
    PSI_STD::vector<TreePtr<Term> > generic_instance_args;
    TreePtr<Term> upref = TermBuilder::parameter(self.compile_context().builtins().upref_type, 0, 0, location);
    generic_instance_args.push_back(upref);
    for (unsigned which = 0, idx = 0; which < 2; ++which) {
      const PSI_STD::vector<TreePtr<Term> >& p = (which==0) ? interface_pattern : derived_pattern;
      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = p.begin(), ie = p.end(); ii != ie; ++ii, ++idx)
        generic_instance_args.push_back(TermBuilder::parameter(*ii, 1, idx, default_));
    }
    TreePtr<Term> generic_instance = TermBuilder::instance(generic_type, generic_instance_args, location);
    TreePtr<Term> generic_instance_ptr = TermBuilder::pointer(generic_instance, upref, location);
    TreePtr<Term> exists = TermBuilder::exists(generic_instance_ptr, vector_of<TreePtr<Term> >(self.compile_context().builtins().upref_type), location);
    
    TreePtr<Interface> interface = Interface::new_(0, interface_pattern, default_, derived_pattern, exists, default_, location);
    
    TreePtr<GenericType> frontend_type = TermBuilder::generic(self.compile_context(), default_, GenericType::primitive_never,
                                                              location, TermBuilder::empty_type(self.compile_context()),
                                                              InterfaceDefineUserOverloadCallback(generic_type, common, interface));
    
    return TermBuilder::instance(frontend_type, default_, location);
  }
};

const MacroVtable InterfaceDefineMacro::vtable = PSI_COMPILER_MACRO(InterfaceDefineMacro, "psi.compiler.InterfaceDefineMacro", Macro, TreePtr<Term>, MacroTermArgument);

/**
  * Return a term which is a macro for defining new interfaces.
  */
TreePtr<Term> interface_define_macro(CompileContext& compile_context, const SourceLocation& location) {
  TreePtr<Macro> m(::new InterfaceDefineMacro(compile_context, location));
  return make_macro_term(m, location);
}
}
}
