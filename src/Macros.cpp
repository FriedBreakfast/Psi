#include "Macros.hpp"
#include "Parser.hpp"

#include <boost/format.hpp>

namespace Psi {
  namespace Compiler {
    const SIVtable MacroMemberCallback::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.MacroMemberCallback", Tree);

    class NamedMemberMacro : public Macro {
      typedef std::map<String, TreePtr<MacroMemberCallback> > NameMapType;
      TreePtr<MacroMemberCallback> m_evaluate;
      NameMapType m_members;

    public:
      static const MacroVtable vtable;

      NamedMemberMacro(CompileContext& compile_context,
                       const SourceLocation& location,
                       const TreePtr<MacroMemberCallback>& evaluate,
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
        return evaluate_dot_impl(self, value, parameter, empty_list<SharedPtr<Parser::Expression> >(), evaluate_context, location);
      }
      
      static TreePtr<Term> evaluate_dot_impl(const NamedMemberMacro& self,
                                             const TreePtr<Term>& value,
                                             const SharedPtr<Parser::Expression>& member,
                                             const List<SharedPtr<Parser::Expression> >& parameters,
                                             const TreePtr<EvaluateContext>& evaluate_context,
                                             const SourceLocation& location) {
        if (member->expression_type != Parser::expression_token)
          self.compile_context().error_throw(location, boost::format("Token following dot on '%s' is not a name") % self.location().logical->error_name(location.logical));

        const Parser::TokenExpression& token_expression = checked_cast<Parser::TokenExpression&>(*member);
        String member_name(token_expression.text.begin, token_expression.text.end);
        NameMapType::const_iterator it = self.m_members.find(member_name);

        if (it == self.m_members.end())
          self.compile_context().error_throw(location, boost::format("'%s' has no member named '%s'") % self.location().logical->error_name(location.logical) % member_name);

        return it->second->evaluate(value, parameters, evaluate_context, location);
      }
    };

    const MacroVtable NamedMemberMacro::vtable =
    PSI_COMPILER_MACRO(NamedMemberMacro, "psi.compiler.NamedMemberMacro", Macro);

    /**
     * \brief Create an interface macro.
     */
    TreePtr<Macro> make_macro(CompileContext& compile_context,
                              const SourceLocation& location,
                              const TreePtr<MacroMemberCallback>& evaluate,
                              const std::map<String, TreePtr<MacroMemberCallback> >& members) {
      return TreePtr<Macro>(new NamedMemberMacro(compile_context, location, evaluate, members));
    }

    /**
     * \brief Create an interface macro.
     */
    TreePtr<Macro> make_macro(CompileContext& compile_context,
                              const SourceLocation& location,
                              const TreePtr<MacroMemberCallback>& evaluate) {
      return make_macro(compile_context, location, evaluate, std::map<String, TreePtr<MacroMemberCallback> >());
    }

    /**
     * \brief Create an interface macro.
     */
    TreePtr<Macro> make_macro(CompileContext& compile_context,
                              const SourceLocation& location,
                              const std::map<String, TreePtr<MacroMemberCallback> >& members) {
      return make_macro(compile_context, location, TreePtr<MacroMemberCallback>(), members);
    }
    
    typedef std::vector<std::pair<TreePtr<MetadataType>, TreePtr<> > > MetadataListType;
    
    namespace {
      class MakeMetadataCallback {
        TreePtr<Term> m_value;
        MetadataListType m_metadata;
        
      public:
        MakeMetadataCallback(const TreePtr<Term>& value, const MetadataListType& metadata)
        : m_value(value), m_metadata(metadata) {}
        
        TreePtr<GenericType> evaluate(const TreePtr<GenericType>& self) const {
          TreePtr<TypeInstance> inst(new TypeInstance(self, default_, self.location()));
          PSI_STD::vector<TreePtr<OverloadValue> > overloads;
          PSI_STD::vector<TreePtr<Term> > pattern;
          for (MetadataListType::const_iterator ii = m_metadata.begin(), ie = m_metadata.end(); ii != ie; ++ii) {
            pattern.assign(1, inst);
            overloads.push_back(TreePtr<Metadata>(new Metadata(ii->second, ii->first, pattern, self.location())));
          }
          return TreePtr<GenericType>(new GenericType(default_, tree_attribute(m_value, &Term::type), overloads, self.location()));
        }
        
        template<typename V>
        static void visit(V& v) {
          v("metadata", &MakeMetadataCallback::m_metadata);
        }
      };
    }
    
    /**
     * \brief Create a Term which carries multiple metadata annotations.
     */
    TreePtr<Term> make_annotated_value(const TreePtr<Term>& value, const MetadataListType& metadata, const SourceLocation& location) {
      CompileContext& compile_context = value.compile_context();
      TreePtr<GenericType> generic = tree_callback<GenericType>(compile_context, location, MakeMetadataCallback(value, metadata));
      TreePtr<TypeInstance> type(new TypeInstance(generic, default_, location));
      return TreePtr<Term>(new TypeInstanceValue(type, value, location));
    }
    
    /**
     * \brief Create a Term which carries multiple metadata annotations and has an empty value.
     */
    TreePtr<Term> make_annotated_value(CompileContext& compile_context, const MetadataListType& metadata, const SourceLocation& location) {
      TreePtr<Term> value(new DefaultValue(compile_context.builtins().empty_type, location));
      return make_annotated_value(value, metadata, location);
    }
    
    /**
     * \brief Create a Term which carries a single metadata annotation.
     * 
     * This term has no value of its own.
     */
    TreePtr<Term> make_metadata_term(const TreePtr<>& value, const TreePtr<MetadataType>& key, const SourceLocation& location) {
      CompileContext& compile_context = key.compile_context();
      MetadataListType ml(1, std::make_pair(key, value));
      return make_annotated_value(compile_context, ml, location);
    }

    /**
     * \brief Create a Term which uses a given macro.
     */
    TreePtr<Term> make_macro_term(const TreePtr<Macro>& macro, const SourceLocation& location) {
      return make_metadata_term(macro, macro.compile_context().builtins().macro_tag, location);
    }
    
    /**
     * \brief Create the default \c __none__ value.
     * 
     * This value has no members and no associated metadata (yet).
     */
    TreePtr<Term> none_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<GenericType> generic_type(new GenericType(default_, compile_context.builtins().empty_type, default_, location));
      TreePtr<Term> type(new TypeInstance(generic_type, default_, location));
      return TreePtr<Term>(new DefaultValue(type, location));
    }
    
    class NamespaceMacro : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;
      
      NamespaceMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroMemberCallback(&vtable, compile_context, location) {
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

    const MacroMemberCallbackVtable NamespaceMacro::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(NamespaceMacro, "psi.compiler.NamespaceMacro", MacroMemberCallback);
    
    TreePtr<Term> namespace_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> m = make_macro(compile_context, location, TreePtr<MacroMemberCallback>(new NamespaceMacro(compile_context, location)));
      return make_macro_term(m, location);
    }
    
    class BuiltinTypeMacro : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;

      BuiltinTypeMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroMemberCallback(&vtable, compile_context, location) {
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
    
    const MacroMemberCallbackVtable BuiltinTypeMacro::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(BuiltinTypeMacro, "psi.compiler.BuiltinTypeMacro", MacroMemberCallback);
    
    TreePtr<Term> builtin_type_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> m = make_macro(compile_context, location, TreePtr<MacroMemberCallback>(new BuiltinTypeMacro(compile_context, location)));
      return make_macro_term(m, location);
    }
    
    class ExternalFunctionMacro : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;
      
      enum ExternalFunctionType {
        extern_builtin,
        extern_c
      };

      ExternalFunctionMacro(CompileContext& compile_context, const SourceLocation& location, ExternalFunctionType which_)
      : MacroMemberCallback(&vtable, compile_context, location), which(which_) {
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
        visit_base<MacroMemberCallback>(v);
        v("which", &ExternalFunctionMacro::which);
      }
    };

    const MacroMemberCallbackVtable ExternalFunctionMacro::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(ExternalFunctionMacro, "psi.compiler.ExternalFunctionMacro", MacroMemberCallback);
    
    TreePtr<Term> builtin_function_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> m = make_macro(compile_context, location, TreePtr<MacroMemberCallback>(new ExternalFunctionMacro(compile_context, location, ExternalFunctionMacro::extern_builtin)));
      return make_macro_term(m, location);
    }
    
    TreePtr<Term> c_function_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> m = make_macro(compile_context, location, TreePtr<MacroMemberCallback>(new ExternalFunctionMacro(compile_context, location, ExternalFunctionMacro::extern_c)));
      return make_macro_term(m, location);
    }
    
    class BuiltinValueMacro : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;

      BuiltinValueMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroMemberCallback(&vtable, compile_context, location) {
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

    const MacroMemberCallbackVtable BuiltinValueMacro::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(BuiltinValueMacro, "psi.compiler.BuiltinValueMacro", MacroMemberCallback);

    TreePtr<Term> builtin_value_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> m = make_macro(compile_context, location, TreePtr<MacroMemberCallback>(new BuiltinValueMacro(compile_context, location)));
      return make_macro_term(m, location);
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
    
    class LibrarySymbolMacro : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;

      LibrarySymbolMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroMemberCallback(&vtable, compile_context, location) {
      }
      
      static TreePtr<MacroMemberCallback> get(CompileContext& compile_context, const SourceLocation& location) {
        return TreePtr<MacroMemberCallback>(new LibrarySymbolMacro(compile_context, location));
      }

      static TreePtr<Term> evaluate_impl(const LibrarySymbolMacro& self,
                                         const TreePtr<Term>& value,
                                         const List<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        TreePtr<TargetCallback> callback;
        switch (parameters.size()) {
        case 1: callback = make_target_callback_const(self.compile_context(), location, parameters[0]); break;
        case 2: callback = make_target_callback(self.compile_context(), location, parameters[0], parameters[1], evaluate_context); break;
        default: self.compile_context().error_throw(location, "Wrong number of parameters to library macro (expected 2 or 3)");
        }

        TreePtr<Library> library = metadata_lookup_as<Library>(self.compile_context().builtins().library_tag, value, location);

        return TreePtr<Term>(new LibrarySymbol(library, callback, location));
      }
    };

    const MacroMemberCallbackVtable LibrarySymbolMacro::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(LibrarySymbolMacro, "psi.compiler.LibrarySymbolMacro", MacroMemberCallback);

    class LibraryMacro : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;

      LibraryMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroMemberCallback(&vtable, compile_context, location) {
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
        
        TreePtr<Library> lib(new Library(callback, location));
        
        PSI_STD::map<String, TreePtr<MacroMemberCallback> > macro_members;
        macro_members["symbol"] = LibrarySymbolMacro::get(self.compile_context(), location);

        MetadataListType ml;
        ml.push_back(std::make_pair(self.compile_context().builtins().library_tag, lib));
        ml.push_back(std::make_pair(self.compile_context().builtins().macro_tag, make_macro(self.compile_context(), location, macro_members)));
        return make_annotated_value(self.compile_context(), ml, location);
      }
    };

    const MacroMemberCallbackVtable LibraryMacro::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(LibraryMacro, "psi.compiler.LibraryMacro", MacroMemberCallback);

    TreePtr<Term> library_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> m = make_macro(compile_context, location, TreePtr<MacroMemberCallback>(new LibraryMacro(compile_context, location)));
      return make_macro_term(m, location);
    }
  }
}
