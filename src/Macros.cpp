#include "Macros.hpp"
#include "Parser.hpp"

#include <boost/format.hpp>

namespace Psi {
  namespace Compiler {
    const SIVtable MacroEvaluateCallback::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.MacroEvaluateCallback", Tree);
    const SIVtable MacroDotCallback::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.MacroDotCallback", Tree);

    class NamedMemberMacro : public Macro {
      typedef std::map<String, TreePtr<MacroDotCallback> > NameMapType;
      TreePtr<MacroEvaluateCallback> m_evaluate;
      NameMapType m_members;

    public:
      static const MacroVtable vtable;

      NamedMemberMacro(CompileContext& compile_context,
                       const SourceLocation& location,
                       const TreePtr<MacroEvaluateCallback>& evaluate,
                       const NameMapType& members)
      : Macro(&vtable, compile_context, location),
      m_evaluate(evaluate),
      m_members(members) {
      }

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<Macro>(v);
        v("evaluate", &NamedMemberMacro::m_evaluate)
        ("members", &NamedMemberMacro::m_members);
      }
      
      static TreePtr<Term> evaluate_impl(const NamedMemberMacro& self,
                                         const TreePtr<Term>& value,
                                         const List<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        if (self.m_evaluate) {
          return self.m_evaluate->evaluate(value, parameters, evaluate_context, location);
        } else {
          self.compile_context().error_throw(location, boost::format("Macro '%s' does not support evaluation") % self.location().logical->error_name(location.logical));
        }
      }

      static TreePtr<Term> dot_impl(const NamedMemberMacro& self,
                                    const TreePtr<Term>& value,
                                    const SharedPtr<Parser::Expression>& parameter,
                                    const TreePtr<EvaluateContext>& evaluate_context,
                                    const SourceLocation& location) {
        if (parameter->expression_type != Parser::expression_token)
          self.compile_context().error_throw(location, boost::format("Token following dot on '%s' is not a name") % self.location().logical->error_name(location.logical));

        const Parser::TokenExpression& token_expression = checked_cast<Parser::TokenExpression&>(*parameter);
        String member_name(token_expression.text.begin, token_expression.text.end);
        NameMapType::const_iterator it = self.m_members.find(member_name);

        if (it == self.m_members.end())
          self.compile_context().error_throw(location, boost::format("'%s' has no member named '%s'") % self.location().logical->error_name(location.logical) % member_name);

        return it->second->dot(value, value, evaluate_context, location);
      }
    };

    const MacroVtable NamedMemberMacro::vtable =
    PSI_COMPILER_MACRO(NamedMemberMacro, "psi.compiler.NamedMemberMacro", Macro);

    /**
     * \brief Create an interface macro.
     */
    TreePtr<Macro> make_macro(CompileContext& compile_context,
                              const SourceLocation& location,
                              const TreePtr<MacroEvaluateCallback>& evaluate,
                              const std::map<String, TreePtr<MacroDotCallback> >& members) {
      return TreePtr<Macro>(new NamedMemberMacro(compile_context, location, evaluate, members));
    }

    /**
     * \brief Create an interface macro.
     */
    TreePtr<Macro> make_macro(CompileContext& compile_context,
                              const SourceLocation& location,
                              const TreePtr<MacroEvaluateCallback>& evaluate) {
      return make_macro(compile_context, location, evaluate, std::map<String, TreePtr<MacroDotCallback> >());
    }

    /**
     * \brief Create an interface macro.
     */
    TreePtr<Macro> make_macro(CompileContext& compile_context,
                              const SourceLocation& location,
                              const std::map<String, TreePtr<MacroDotCallback> >& members) {
      return make_macro(compile_context, location, TreePtr<MacroEvaluateCallback>(), members);
    }
    
    namespace {
      class MakeMacroCallback {
        TreePtr<Macro> m_macro;
        
      public:
        MakeMacroCallback(const TreePtr<Macro>& macro) : m_macro(macro) {}
        
        TreePtr<GenericType> evaluate(const TreePtr<GenericType>& self) const {
          PSI_STD::vector<TreePtr<OverloadValue> > overloads;
          PSI_STD::vector<TreePtr<Term> > pattern;
          overloads.push_back(TreePtr<Metadata>(new Metadata(default_, m_macro, self.location())));
          return TreePtr<GenericType>(new GenericType(default_, self.compile_context().builtins().empty_type, overloads, self.location()));
        }
        
        template<typename V>
        static void visit(V& v) {
          v("macro", &MakeMacroCallback::m_macro);
        }
      };
    }

    TreePtr<Term> make_macro_term(CompileContext& compile_context,
                                  const SourceLocation& location,
                                  const TreePtr<Macro>& macro) {
      TreePtr<GenericType> generic = tree_callback<GenericType>(compile_context, location, MakeMacroCallback(macro));
      return TreePtr<Term>(new TypeInstance(generic, default_, location));
    }
    
    TreePtr<Term> none_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<GenericType> generic_type(new GenericType(default_, compile_context.builtins().empty_type, default_, location));
      TreePtr<Term> type(new TypeInstance(generic_type, default_, location));
      return TreePtr<Term>(new DefaultValue(type, location));
    }
    
    class NamespaceMacro : public MacroEvaluateCallback {
    public:
      static const MacroEvaluateCallbackVtable vtable;
      
      NamespaceMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroEvaluateCallback(&vtable, compile_context, location) {
      }
      
      static TreePtr<Term> evaluate_impl(const NamespaceMacro& self,
                                         const TreePtr<Term>&,
                                         const List<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        if (parameters.size() != 1)
          self.compile_context().error_throw(location, "Namespace macro expects 1 parameter");
        
        SharedPtr<Parser::TokenExpression> name;
        if (!(name = expression_as_token_type(parameters[0], Parser::TokenExpression::square_bracket)))
          self.compile_context().error_throw(location, "Parameter to namespace macro is not a [...]");
        
        PSI_STD::vector<SharedPtr<Parser::NamedExpression> > statements = Parser::parse_statement_list(name->text);

        return compile_namespace(statements, evaluate_context, location).ns;
      }
    };

    const MacroEvaluateCallbackVtable NamespaceMacro::vtable = PSI_COMPILER_MACRO_EVALUATE_CALLBACK(NamespaceMacro, "psi.compiler.NamespaceMacro", MacroEvaluateCallback);
    
    TreePtr<Term> namespace_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> m = make_macro(compile_context, location, TreePtr<MacroEvaluateCallback>(new NamespaceMacro(compile_context, location)));
      return make_macro_term(compile_context, location, m);
    }
    
    class BuiltinTypeMacro : public MacroEvaluateCallback {
    public:
      static const MacroEvaluateCallbackVtable vtable;

      BuiltinTypeMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroEvaluateCallback(&vtable, compile_context, location) {
      }

      static TreePtr<Term> evaluate_impl(const BuiltinTypeMacro& self,
                                         const TreePtr<Term>&,
                                         const List<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>&,
                                         const SourceLocation& location) {
        if (parameters.size() != 1)
          self.compile_context().error_throw(location, "Wrong number of parameters to builtin type macro");
        
        SharedPtr<Parser::TokenExpression> name;
        if (!(name = expression_as_token_type(parameters[0], Parser::TokenExpression::brace)))
          self.compile_context().error_throw(location, "Parameter to builtin type macro is not a {...}");
        
        String name_s(name->text.begin, name->text.end);
        return TreePtr<Term>(new PrimitiveType(self.compile_context(), name_s, location));
      }
    };
    
    const MacroEvaluateCallbackVtable BuiltinTypeMacro::vtable = PSI_COMPILER_MACRO_EVALUATE_CALLBACK(BuiltinTypeMacro, "psi.compiler.BuiltinTypeMacro", MacroEvaluateCallback);
    
    TreePtr<Term> builtin_type_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> m = make_macro(compile_context, location, TreePtr<MacroEvaluateCallback>(new BuiltinTypeMacro(compile_context, location)));
      return make_macro_term(compile_context, location, m);
    }
    
    class ExternalFunctionMacro : public MacroEvaluateCallback {
    public:
      static const MacroEvaluateCallbackVtable vtable;
      
      enum ExternalFunctionType {
        extern_builtin,
        extern_c
      };

      ExternalFunctionMacro(CompileContext& compile_context, const SourceLocation& location, ExternalFunctionType which_)
      : MacroEvaluateCallback(&vtable, compile_context, location), which(which_) {
      }
      
      unsigned which;

      static TreePtr<Term> evaluate_impl(const ExternalFunctionMacro& self,
                                         const TreePtr<Term>&,
                                         const List<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        if (parameters.size() != 2)
          self.compile_context().error_throw(location, "Wrong number of parameters to builtin function macro (expected 2)");
        
        SharedPtr<Parser::TokenExpression> name, arguments;
        if (!(name = expression_as_token_type(parameters[0], Parser::TokenExpression::brace)))
          self.compile_context().error_throw(location, "First parameter to builtin function macro is not a {...}");
        
        if (!(arguments = expression_as_token_type(parameters[1], Parser::TokenExpression::bracket)))
          self.compile_context().error_throw(location, "Second parameter to builtin function macro is not a (...)");
        
        PSI_STD::vector<TreePtr<Term> > argument_types;
        PSI_STD::vector<SharedPtr<Parser::Expression> > argument_expressions = Parser::parse_positional_list(arguments->text);
        for (PSI_STD::vector<SharedPtr<Parser::Expression> >::iterator ii = argument_expressions.begin(), ie = argument_expressions.end(); ii != ie; ++ii)
          argument_types.push_back(compile_expression(*ii, evaluate_context, location.logical));
        
        if (argument_types.empty())
          self.compile_context().error_throw(location, "Builtin function macro types argument must contain at least one entry (the return type, which should be last)");

        TreePtr<Term> result_type = argument_types.back();
        argument_types.pop_back();
       
        String name_s(name->text.begin, name->text.end);
        
        switch (self.which) {
        case extern_builtin: return TreePtr<Term>(new BuiltinFunction(name_s, result_type, argument_types, location));
        default: PSI_FAIL("Unknown external function type");
        }
      }
      
      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<MacroEvaluateCallback>(v);
        v("which", &ExternalFunctionMacro::which);
      }
    };

    const MacroEvaluateCallbackVtable ExternalFunctionMacro::vtable = PSI_COMPILER_MACRO_EVALUATE_CALLBACK(ExternalFunctionMacro, "psi.compiler.ExternalFunctionMacro", MacroEvaluateCallback);
    
    TreePtr<Term> builtin_function_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> m = make_macro(compile_context, location, TreePtr<MacroEvaluateCallback>(new ExternalFunctionMacro(compile_context, location, ExternalFunctionMacro::extern_builtin)));
      return make_macro_term(compile_context, location, m);
    }
    
    TreePtr<Term> c_function_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> m = make_macro(compile_context, location, TreePtr<MacroEvaluateCallback>(new ExternalFunctionMacro(compile_context, location, ExternalFunctionMacro::extern_c)));
      return make_macro_term(compile_context, location, m);
    }
    
    class BuiltinValueMacro : public MacroEvaluateCallback {
    public:
      static const MacroEvaluateCallbackVtable vtable;

      BuiltinValueMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroEvaluateCallback(&vtable, compile_context, location) {
      }

      static TreePtr<Term> evaluate_impl(const BuiltinValueMacro& self,
                                         const TreePtr<Term>&,
                                         const List<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        if (parameters.size() != 3)
          self.compile_context().error_throw(location, "Wrong number of parameters to builtin value macro (expected 3)");
        
        SharedPtr<Parser::TokenExpression> constructor, data, type_expr;
        if (!(type_expr = expression_as_token_type(parameters[0], Parser::TokenExpression::bracket)))
          self.compile_context().error_throw(location, "First parameter to builtin function macro is not a {...}");

        if (!(constructor = expression_as_token_type(parameters[1], Parser::TokenExpression::brace)))
          self.compile_context().error_throw(location, "Second parameter to builtin function macro is not a {...}");
        
        if (!(data = expression_as_token_type(parameters[2], Parser::TokenExpression::brace)))
          self.compile_context().error_throw(location, "Third parameter to builtin function macro is not a {...}");
        
        TreePtr<Term> type = compile_expression(Parser::parse_expression(type_expr->text), evaluate_context, location.logical);

        String constructor_s(constructor->text.begin, constructor->text.end);
        String data_s(data->text.begin, data->text.end);
       
        return TreePtr<Term>(new BuiltinValue(constructor_s, data_s, type, location));
      }
    };

    const MacroEvaluateCallbackVtable BuiltinValueMacro::vtable = PSI_COMPILER_MACRO_EVALUATE_CALLBACK(BuiltinValueMacro, "psi.compiler.BuiltinValueMacro", MacroEvaluateCallback);

    TreePtr<Term> builtin_value_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> m = make_macro(compile_context, location, TreePtr<MacroEvaluateCallback>(new BuiltinValueMacro(compile_context, location)));
      return make_macro_term(compile_context, location, m);
    }
    
    class TargetCallbackConst : public TargetCallback {
      PropertyValue m_value;
      
    public:
      static const TargetCallbackVtable vtable;
      
      TargetCallbackConst(CompileContext& compile_context, const PropertyValue& value, const SourceLocation& location)
      : TargetCallback(&vtable, compile_context, location),
      m_value(value) {
      }
      
      static PropertyValue evaluate_impl(TargetCallbackConst& self, const PropertyValue&, const PropertyValue&) {
        return self.m_value;
      }
      
      template<typename V>
      static void visit(V& v) {
        visit_base<TargetCallback>(v);
        v("value", &TargetCallbackConst::m_value);
      }
    };
    
    const TargetCallbackVtable TargetCallbackConst::vtable = PSI_COMPILER_TARGET_CALLBACK_VTABLE(TargetCallbackConst, "psi.compiler.TargetCallbackConst", TargetCallback);
    
    TreePtr<TargetCallback> make_target_callback_const(CompileContext& compile_context,
                                                       const SourceLocation& location,
                                                       const SharedPtr<Parser::Expression>& value) {
      SharedPtr<Parser::TokenExpression> value_cast;
      if (!(value_cast = expression_as_token_type(value, Parser::TokenExpression::brace)))
        compile_context.error_throw(location, "First parameter to library macro is not a {...}");
      
      PropertyValue pv;
      try {
        pv = PropertyValue::parse(value_cast->text.begin, value_cast->text.end);
      } catch (std::runtime_error&) {
        compile_context.error_throw(location, "Error parsing JSON data");
      }
      
      return TreePtr<TargetCallback>(new TargetCallbackConst(compile_context, pv, location));
    }
    
    TreePtr<TargetCallback> make_target_callback(CompileContext& compile_context,
                                                 const SourceLocation& location,
                                                 const SharedPtr<Parser::Expression>& parameter_names_expr,
                                                 const SharedPtr<Parser::Expression>& body_expr,
                                                 const TreePtr<EvaluateContext>& evaluate_context) {
      
      SharedPtr<Parser::TokenExpression> parameter_names_cast, body_cast;
      if (!(parameter_names_cast = expression_as_token_type(parameter_names_expr, Parser::TokenExpression::bracket)))
        compile_context.error_throw(location, "First parameter to library macro is not a (...)");

      if (!(body_cast = expression_as_token_type(body_expr, Parser::TokenExpression::square_bracket)))
        compile_context.error_throw(location, "Second parameter to library macro is not a [...]");

      std::map<String, TreePtr<Term> > parameter_dict;
      
      PSI_STD::vector<Parser::TokenExpression> parameter_names = parse_identifier_list(parameter_names_cast->text);
      switch (parameter_names.size()) {
      default: compile_context.error_throw(location, "Expected zero, one or two argument names specified for library macro");
      case 2: parameter_dict.insert(std::make_pair(String(parameter_names[1].text.begin, parameter_names[1].text.end), TreePtr<Term>()));
      case 1: parameter_dict.insert(std::make_pair(String(parameter_names[0].text.begin, parameter_names[0].text.end), TreePtr<Term>()));
      case 0: break;
      }
      
      TreePtr<EvaluateContext> child_context = evaluate_context_dictionary(evaluate_context->module(), location, parameter_dict, evaluate_context);
      
      TreePtr<Term> property_map_type;
      //std::vector<TreePtr<Term> > parameters(2, std::make_pair(parameter_mode_input, property_map_type));
      //TreePtr<Function> callback(new Function(evaluate_context->module(), result_mode_by_value, property_map_type, parameters, body, location));

      PSI_NOT_IMPLEMENTED();
    }

    class LibraryMacro : public MacroEvaluateCallback {
    public:
      static const MacroEvaluateCallbackVtable vtable;

      LibraryMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroEvaluateCallback(&vtable, compile_context, location) {
      }

      static TreePtr<Term> evaluate_impl(const LibraryMacro& self,
                                         const TreePtr<Term>&,
                                         const List<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        TreePtr<TargetCallback> callback;
        switch (parameters.size()) {
        case 1: callback = make_target_callback_const(self.compile_context(), location, parameters[0]); break;
        case 2: callback = make_target_callback(self.compile_context(), location, parameters[0], parameters[1], evaluate_context); break;
        default: self.compile_context().error_throw(location, "Wrong number of parameters to library macro (expected 1 or 2)");
        }

        return TreePtr<Term>(new Library(callback, location));
      }
    };

    const MacroEvaluateCallbackVtable LibraryMacro::vtable = PSI_COMPILER_MACRO_EVALUATE_CALLBACK(LibraryMacro, "psi.compiler.LibraryMacro", MacroEvaluateCallback);

    TreePtr<Term> library_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> m = make_macro(compile_context, location, TreePtr<MacroEvaluateCallback>(new LibraryMacro(compile_context, location)));
      return make_macro_term(compile_context, location, m);
    }
    
    class LibrarySymbolMacro : public MacroEvaluateCallback {
    public:
      static const MacroEvaluateCallbackVtable vtable;

      LibrarySymbolMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroEvaluateCallback(&vtable, compile_context, location) {
      }

      static TreePtr<Term> evaluate_impl(const LibrarySymbolMacro& self,
                                         const TreePtr<Term>&,
                                         const List<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        TreePtr<TargetCallback> callback;
        switch (parameters.size()) {
        case 1: callback = make_target_callback_const(self.compile_context(), location, parameters[1]); break;
        case 2: callback = make_target_callback(self.compile_context(), location, parameters[1], parameters[2], evaluate_context); break;
        default: self.compile_context().error_throw(location, "Wrong number of parameters to library macro (expected 2 or 3)");
        }

        SharedPtr<Parser::TokenExpression> library_expr;
        if (!(library_expr = expression_as_token_type(parameters[0], Parser::TokenExpression::bracket)))
          self.compile_context().error_throw(location, "First parameter to library macro is not a (...)");
        
        TreePtr<Term> library = compile_expression(parameters[0], evaluate_context, location.logical);
        TreePtr<Library> library_cast = dyn_treeptr_cast<Library>(library);
        if (!library_cast)
          self.compile_context().error_throw(location, "Library parameter is not a library expression.");

        return TreePtr<Term>(new LibrarySymbol(library_cast, callback, location));
      }
    };

    const MacroEvaluateCallbackVtable LibrarySymbolMacro::vtable = PSI_COMPILER_MACRO_EVALUATE_CALLBACK(LibrarySymbolMacro, "psi.compiler.LibrarySymbolMacro", MacroEvaluateCallback);

    TreePtr<Term> library_symbol_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> m = make_macro(compile_context, location, TreePtr<MacroEvaluateCallback>(new LibrarySymbolMacro(compile_context, location)));
      return make_macro_term(compile_context, location, m);
    }
  }
}
