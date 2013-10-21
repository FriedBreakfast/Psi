#include "Macros.hpp"
#include "Parser.hpp"
#include "TermBuilder.hpp"

#include <map>
#include <boost/assign/list_of.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

namespace Psi {
  namespace Compiler {
    const SIVtable Macro::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Macro", Tree);
    const SIVtable MacroMemberCallback::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.MacroMemberCallback", Tree);
    
    class DefaultMacro : public Macro {
    public:
      static const MacroVtable vtable;
      
      DefaultMacro(CompileContext& compile_context, const SourceLocation& location)
      : Macro(&vtable, compile_context, location) {
      }
      
      template<typename V>
      static void visit(V& v) {
        visit_base<Macro>(v);
      }

      static TreePtr<Term> evaluate_impl(const DefaultMacro& self,
                                         const TreePtr<Term>& value,
                                         const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const char& PSI_UNUSED(argument),
                                         const SourceLocation& location) {
        if (tree_isa<FunctionType>(value->type)) {
          return compile_function_invocation(value, parameters, evaluate_context, location);
        } else {
          self.compile_context().error_throw(location, boost::format("Cannot evaluate object of type %s") % value->type->location().logical->error_name(location.logical));
        }
      }

      static TreePtr<Term> dot_impl(const DefaultMacro& self,
                                    const TreePtr<Term>& value,
                                    const SharedPtr<Parser::Expression>& member,
                                    const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                    const TreePtr<EvaluateContext>& evaluate_context,
                                    const char& PSI_UNUSED(argument),
                                    const SourceLocation& location) {
        PSI_NOT_IMPLEMENTED();
      }
      
      static TreePtr<Term> cast_impl(const DefaultMacro& PSI_UNUSED(self),
                                     const TreePtr<Term>& value,
                                     const TreePtr<EvaluateContext>& PSI_UNUSED(evaluate_context),
                                     const char& PSI_UNUSED(argument),
                                     const SourceLocation& PSI_UNUSED(location)) {
        return value;
      }
    };

    const MacroVtable DefaultMacro::vtable = PSI_COMPILER_MACRO(DefaultMacro, "psi.compiler.DefaultMacro", Macro, TreePtr<Term>, const char);

    /**
     * \brief Generate the default implementation of Macro.
     * 
     * This is responsible for default behaviour of types, in particular more useful
     * error reporting on failure and processing function calls.
     */
    TreePtr<> default_macro_impl(CompileContext& compile_context, const SourceLocation& location) {
      return tree_from(::new DefaultMacro(compile_context, location));
    }
    
    /**
     * \brief Generate the default implementation of Macro for types.
     */
    TreePtr<> default_type_macro_impl(CompileContext& compile_context, const SourceLocation& location) {
      return tree_from(::new DefaultMacro(compile_context, location));
    }
    
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
                                         const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const char& PSI_UNUSED(argument),
                                         const SourceLocation& location) {
        if (self.m_evaluate) {
          return self.m_evaluate->evaluate(value, parameters, evaluate_context, location);
        } else {
          self.compile_context().error_throw(location, boost::format("Macro '%s' does not support evaluation") % self.location().logical->error_name(location.logical));
        }
      }

      static TreePtr<Term> dot_impl(const NamedMemberMacro& self,
                                    const TreePtr<Term>& value,
                                    const SharedPtr<Parser::Expression>& member,
                                    const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                    const TreePtr<EvaluateContext>& evaluate_context,
                                    const char& PSI_UNUSED(argument),
                                    const SourceLocation& location) {
        if (member->expression_type != Parser::expression_token)
          self.compile_context().error_throw(location, boost::format("Token following dot on '%s' is not a name") % self.location().logical->error_name(location.logical));

        const Parser::TokenExpression& token_expression = checked_cast<Parser::TokenExpression&>(*member);
        String member_name = token_expression.text.str();
        NameMapType::const_iterator it = self.m_members.find(member_name);

        if (it == self.m_members.end())
          self.compile_context().error_throw(location, boost::format("'%s' has no member named '%s'") % self.location().logical->error_name(location.logical) % member_name);

        return it->second->evaluate(value, parameters, evaluate_context, location);
      }
      
      static TreePtr<Term> cast_impl(const NamedMemberMacro& PSI_UNUSED(self),
                                     const TreePtr<Term>& value,
                                     const TreePtr<EvaluateContext>& PSI_UNUSED(evaluate_context),
                                     const char& PSI_UNUSED(argument),
                                     const SourceLocation& PSI_UNUSED(location)) {
        return value;
      }
    };

    const MacroVtable NamedMemberMacro::vtable =
    PSI_COMPILER_MACRO(NamedMemberMacro, "psi.compiler.NamedMemberMacro", Macro, TreePtr<Term>, const char);

    /**
     * \brief Create an interface macro.
     */
    TreePtr<Macro> make_macro(CompileContext& compile_context,
                              const SourceLocation& location,
                              const TreePtr<MacroMemberCallback>& evaluate,
                              const std::map<String, TreePtr<MacroMemberCallback> >& members) {
      return tree_from(::new NamedMemberMacro(compile_context, location, evaluate, members));
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
    
    struct ConstantMetadataSetup {
      TreePtr<MetadataType> type;
      TreePtr<> value;
      unsigned n_wildcards;
      /// Extra pattern variables
      PSI_STD::vector<TreePtr<Term> > pattern;
      
      template<typename V>
      static void visit(V& v) {
        v("type", &ConstantMetadataSetup::type)
        ("value", &ConstantMetadataSetup::value)
        ("n_wildcards", &ConstantMetadataSetup::n_wildcards)
        ("pattern", &ConstantMetadataSetup::pattern);
      }
    };
    
    namespace {
      class MakeMetadataCallback {
        std::vector<ConstantMetadataSetup> m_metadata;
        
      public:
        MakeMetadataCallback(const std::vector<ConstantMetadataSetup>& metadata)
        : m_metadata(metadata) {}
        
        PSI_STD::vector<TreePtr<OverloadValue> > evaluate(const TreePtr<GenericType>& self) const {
          TreePtr<Term> inst = TermBuilder::instance(self, default_, self->location());

          PSI_STD::vector<TreePtr<OverloadValue> > overloads;
          for (std::vector<ConstantMetadataSetup>::const_iterator ii = m_metadata.begin(), ie = m_metadata.end(); ii != ie; ++ii) {
            PSI_STD::vector<TreePtr<Term> > pattern = ii->pattern;
            pattern.insert(pattern.begin(), inst);
            overloads.push_back(Metadata::new_(ii->value, ii->type, ii->n_wildcards, pattern, self->location()));
          }

          return overloads;
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
    TreePtr<Term> make_annotated_value(const TreePtr<Term>& value, const std::vector<ConstantMetadataSetup>& metadata, const SourceLocation& location) {
      TreePtr<GenericType> generic = TermBuilder::generic(value->compile_context(), default_, GenericType::primitive_recurse,
                                                          location, value->type, MakeMetadataCallback(metadata));
      return TermBuilder::instance_value(TermBuilder::instance(generic, default_, location), value, location);
    }
    
    /**
     * \brief Create a Term which carries multiple metadata annotations and has an empty value.
     */
    TreePtr<Term> make_annotated_type(CompileContext& compile_context, const std::vector<ConstantMetadataSetup>& metadata, const SourceLocation& location) {
      TreePtr<GenericType> generic = TermBuilder::generic(compile_context, default_, GenericType::primitive_never,
                                                          location, TermBuilder::empty_type(compile_context), MakeMetadataCallback(metadata));
      return TermBuilder::instance(generic, default_, location);
    }

    /**
     * \brief Create a Term which uses a given macro.
     */
    TreePtr<Term> make_macro_term(const TreePtr<Macro>& macro, const SourceLocation& location) {
      CompileContext& compile_context = macro->compile_context();
      ConstantMetadataSetup meta;
      meta.type = compile_context.builtins().type_macro_tag;
      meta.value = macro;
      meta.n_wildcards = 0;
      meta.pattern.push_back(compile_context.builtins().term_compile_argument);
      return make_annotated_type(compile_context, vector_of(meta), location);
    }
    
    class PointerMacro : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;

      PointerMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroMemberCallback(&vtable, compile_context, location) {
      }
      
      static TreePtr<Term> evaluate_impl(const PointerMacro& self,
                                         const TreePtr<Term>&,
                                         const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        if (parameters.size() != 1)
          self.compile_context().error_throw(location, "Pointer macro expects 1 parameter");
        
        SharedPtr<Parser::TokenExpression> name;
        if (!(name = Parser::expression_as_token_type(parameters[0], Parser::token_bracket)))
          self.compile_context().error_throw(location, "Parameter to pointer macro is not a (...)");
        
        SharedPtr<Parser::Expression> target_expr = Parser::parse_expression(self.compile_context().error_context(), location.logical, name->text);
        TreePtr<Term> target_type = compile_term(target_expr, evaluate_context, location.logical);
        
        return TermBuilder::pointer(target_type, location);
      }
    };
    
    const MacroMemberCallbackVtable PointerMacro::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(PointerMacro, "psi.compiler.PointerMacro", MacroMemberCallback);

    /**
     * \brief Pointer macro.
     */
    TreePtr<Term> pointer_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<MacroMemberCallback> callback(::new PointerMacro(compile_context, location));
      return make_macro_term(make_macro(compile_context, location, callback), location);
    }
    
    class NamespaceMemberMacro : public Macro {
      TreePtr<Term> m_arg_type;
      
    public:
      static const MacroVtable vtable;

      NamespaceMemberMacro(const TreePtr<Term>& arg_type, const SourceLocation& location)
      : Macro(&vtable, arg_type->compile_context(), location),
      m_arg_type(arg_type) {
      }
      
      template<typename V>
      static void visit(V& v) {
        visit_base<Macro>(v);
        v("arg_type", &NamespaceMemberMacro::m_arg_type);
      }

      static void evaluate_impl(void*,
                                const NamespaceMemberMacro& self,
                                const TreePtr<Term>&,
                                const PSI_STD::vector<SharedPtr<Parser::Expression> >&,
                                const TreePtr<EvaluateContext>&,
                                const void*,
                                const SourceLocation& location) {
        self.compile_context().error_throw(location, "Cannot evaluate a namespace");
      }

      static void dot_impl(void *result,
                           const NamespaceMemberMacro& self,
                           const TreePtr<Term>& value,
                           const SharedPtr<Parser::Expression>& member,
                           const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                           const TreePtr<EvaluateContext>& evaluate_context,
                           const void *argument,
                           const SourceLocation& location) {
        SharedPtr<Parser::TokenExpression> name;
        if (!(name = Parser::expression_as_token_type(member, Parser::token_identifier)))
          self.compile_context().error_throw(location, "Namespace member argument is not an identifier");
        
        String member_name = name->text.str();
        TreePtr<Namespace> ns = metadata_lookup_as<Namespace>(self.compile_context().builtins().namespace_tag, evaluate_context, value, location);
        PSI_STD::map<String, TreePtr<Term> >::const_iterator ns_it = ns->members.find(member_name);
        if (ns_it == ns->members.end())
          self.compile_context().error_throw(location, boost::format("Namespace '%s' has no member '%s'") % value->location().logical->error_name(location.logical) % member_name);
        
        TreePtr<Term> member_value = ns_it->second;
        TreePtr<Macro> member_value_macro = expression_macro(evaluate_context, member_value, self.m_arg_type, location);
        if (parameters.empty())
          member_value_macro->cast_raw(result, member_value, evaluate_context, argument, location);
        else
          member_value_macro->evaluate_raw(result, member_value, parameters, evaluate_context, argument, location);
      }
      
      static void cast_impl(void *result,
                            const NamespaceMemberMacro& self,
                            const TreePtr<Term>& value,
                            const TreePtr<EvaluateContext>& PSI_UNUSED(evaluate_context),
                            const void *PSI_UNUSED(argument),
                            const SourceLocation& location) {
        if (self.m_arg_type->convert_match(self.compile_context().builtins().term_compile_argument))
          new (result) TreePtr<Term> (value);
        else
          self.compile_context().error_throw(location, "Can't cast namespace to required data type.");
      }
    };
    
    const MacroVtable NamespaceMemberMacro::vtable = PSI_COMPILER_MACRO_RAW(NamespaceMemberMacro, "psi.compiler.NamespaceMemberMacro", Macro);
    
    class NamespaceMacroMetadata : public Metadata {
      static PSI_STD::vector<TreePtr<Term> > make_pattern(CompileContext& compile_context, const SourceLocation& location) {
        PSI_STD::vector<TreePtr<Term> > result;
        result.push_back(TermBuilder::parameter(compile_context.builtins().metatype, 0, 0, location));
        return result;
      }
      
    public:
      static const VtableType vtable;
      
      NamespaceMacroMetadata(CompileContext& compile_context, const SourceLocation& location)
      : Metadata(&vtable, compile_context, compile_context.builtins().macro_tag,
                 1, make_pattern(compile_context, location), location) {
      }
      
      static TreePtr<> get_impl(const NamespaceMacroMetadata& PSI_UNUSED(self), const PSI_STD::vector<TreePtr<Term> >& wildcards, const SourceLocation& location) {
        return TreePtr<>(::new NamespaceMemberMacro(wildcards[0], location));
      }
    };
    
    const MetadataVtable NamespaceMacroMetadata::vtable = PSI_COMPILER_METADATA(NamespaceMacroMetadata, "psi.compiler.NamespaceMacroMetadata", Metadata);

    namespace {
      class NamespaceMetadataCallback {
        TreePtr<Namespace> m_namespace;
        
      public:
        NamespaceMetadataCallback(const TreePtr<Namespace>& namespace_)
        : m_namespace(namespace_) {}
        
        PSI_STD::vector<TreePtr<OverloadValue> > evaluate(const TreePtr<GenericType>& self) const {
          TreePtr<Term> inst = TermBuilder::instance(self, default_, self->location());

          PSI_STD::vector<TreePtr<OverloadValue> > overloads;
          overloads.push_back(Metadata::new_(m_namespace, self->compile_context().builtins().namespace_tag, 0, PSI_STD::vector<TreePtr<Term> >(1, inst), self->location()));
          overloads.push_back(TreePtr<OverloadValue>(::new NamespaceMacroMetadata(self->compile_context(), self->location())));

          return overloads;
        }
        
        template<typename V>
        static void visit(V& v) {
          v("namespace", &NamespaceMetadataCallback::m_namespace);
        }
      };
    }

    class NamespaceMacro : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;
      
      NamespaceMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroMemberCallback(&vtable, compile_context, location) {
      }
      
      static TreePtr<Term> evaluate_impl(const NamespaceMacro& self,
                                         const TreePtr<Term>&,
                                         const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        if (parameters.size() != 1)
          self.compile_context().error_throw(location, "Namespace macro expects 1 parameter");
        
        SharedPtr<Parser::TokenExpression> name;
        if (!(name = Parser::expression_as_token_type(parameters[0], Parser::token_square_bracket)))
          self.compile_context().error_throw(location, "Parameter to namespace macro is not a [...]");
        
        PSI_STD::vector<SharedPtr<Parser::Statement> > statements = Parser::parse_namespace(self.compile_context().error_context(), location.logical, name->text);

        TreePtr<Namespace> ns = compile_namespace(statements, evaluate_context, location);

        TreePtr<GenericType> generic = TermBuilder::generic(self.compile_context(), default_, GenericType::primitive_never,
                                                            location, TermBuilder::empty_type(self.compile_context()), NamespaceMetadataCallback(ns));
        return TermBuilder::instance(generic, default_, location);
      }
    };

    const MacroMemberCallbackVtable NamespaceMacro::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(NamespaceMacro, "psi.compiler.NamespaceMacro", MacroMemberCallback);
    
    TreePtr<Term> namespace_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<MacroMemberCallback> callback(::new NamespaceMacro(compile_context, location));
      TreePtr<Macro> m = make_macro(compile_context, location, callback);
      return make_macro_term(m, location);
    }
    
    namespace {
      static const std::map<std::string, NumberType::ScalarType> number_type_names =
      boost::assign::map_list_of("bool", NumberType::n_bool)
      ("i8", NumberType::n_i8)("i16", NumberType::n_i16)("i32", NumberType::n_i32)("i64", NumberType::n_i64)("iptr", NumberType::n_iptr)
      ("u8", NumberType::n_u8)("u16", NumberType::n_u16)("u32", NumberType::n_u32)("u64", NumberType::n_u64)("uptr", NumberType::n_uptr);
    }
    
    class NumberTypeMacro : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;

      NumberTypeMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroMemberCallback(&vtable, compile_context, location) {
      }

      static TreePtr<Term> evaluate_impl(const NumberTypeMacro& self,
                                         const TreePtr<Term>&,
                                         const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>&,
                                         const SourceLocation& location) {
        if (parameters.size() != 1)
          self.compile_context().error_throw(location, "Wrong number of parameters to builtin type macro");
        
        SharedPtr<Parser::TokenExpression> name;
        if (!(name = Parser::expression_as_token_type(parameters[0], Parser::token_brace)))
          self.compile_context().error_throw(location, "Parameter to builtin type macro is not a {...}");
        
        std::string name_s = name->text.str();
        std::map<std::string, NumberType::ScalarType>::const_iterator it = number_type_names.find(name_s);
        if (it == number_type_names.end())
          self.compile_context().error_throw(location, boost::format("Unknown builtin type '%s'") % name_s);
        
        
        return TermBuilder::number_type(self.compile_context(), it->second);
      }
    };
    
    const MacroMemberCallbackVtable NumberTypeMacro::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(NumberTypeMacro, "psi.compiler.NumberTypeMacro", MacroMemberCallback);
    
    TreePtr<Term> number_type_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<MacroMemberCallback> callback(::new NumberTypeMacro(compile_context, location));
      TreePtr<Macro> m = make_macro(compile_context, location, callback);
      return make_macro_term(m, location);
    }
    
    class BuiltinFunctionMacro : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;

      BuiltinFunctionMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroMemberCallback(&vtable, compile_context, location) {
      }

      static TreePtr<Term> evaluate_impl(const BuiltinFunctionMacro& self,
                                         const TreePtr<Term>&,
                                         const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        if (parameters.size() != 2)
          self.compile_context().error_throw(location, "Wrong number of parameters to builtin function macro (expected 2)");
        
        SharedPtr<Parser::TokenExpression> name, arguments;
        if (!(name = Parser::expression_as_token_type(parameters[0], Parser::token_brace)))
          self.compile_context().error_throw(location, "First parameter to builtin function macro is not a {...}");
        
        if (!(arguments = Parser::expression_as_token_type(parameters[1], Parser::token_bracket)))
          self.compile_context().error_throw(location, "Second parameter to builtin function macro is not a (...)");
        
        PSI_STD::vector<TreePtr<Term> > argument_types;
        PSI_STD::vector<SharedPtr<Parser::Expression> > argument_expressions = Parser::parse_positional_list(self.compile_context().error_context(), location.logical, arguments->text);
        for (PSI_STD::vector<SharedPtr<Parser::Expression> >::iterator ii = argument_expressions.begin(), ie = argument_expressions.end(); ii != ie; ++ii)
          argument_types.push_back(compile_term(*ii, evaluate_context, location.logical));
        
        if (argument_types.empty())
          self.compile_context().error_throw(location, "Builtin function macro types argument must contain at least one entry (the return type, which should be last)");

        TreePtr<Term> result_type = argument_types.back();
        argument_types.pop_back();
       
        String name_s = name->text.str();
        PSI_NOT_IMPLEMENTED();
      }
      
      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<MacroMemberCallback>(v);
      }
    };

    const MacroMemberCallbackVtable BuiltinFunctionMacro::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(BuiltinFunctionMacro, "psi.compiler.BuiltinFunctionMacro", MacroMemberCallback);
    
    TreePtr<Term> builtin_function_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<MacroMemberCallback> callback(::new BuiltinFunctionMacro(compile_context, location));
      TreePtr<Macro> m = make_macro(compile_context, location, callback);
      return make_macro_term(m, location);
    }
    
    class NumberValueMacro : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;

      NumberValueMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroMemberCallback(&vtable, compile_context, location) {
      }

      static TreePtr<Term> evaluate_impl(const NumberValueMacro& self,
                                         const TreePtr<Term>&,
                                         const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        if (parameters.size() != 2)
          self.compile_context().error_throw(location, "Wrong number of parameters to builtin value macro (expected 3)");
        
        SharedPtr<Parser::TokenExpression> data, type_expr;
        if (!(type_expr = Parser::expression_as_token_type(parameters[0], Parser::token_bracket)))
          self.compile_context().error_throw(location, "First parameter to builtin number constant macro is not a {...}");

        if (!(data = Parser::expression_as_token_type(parameters[1], Parser::token_brace)))
          self.compile_context().error_throw(location, "Second parameter to builtin number constant macro is not a {...}");
        
        TreePtr<Term> type = compile_term(Parser::parse_expression(self.compile_context().error_context(), location.logical, type_expr->text), evaluate_context, location.logical);
        TreePtr<NumberType> number_type = term_unwrap_dyn_cast<NumberType>(type);
        if (!number_type)
          self.compile_context().error_throw(location, "First parameter to builtin number constant macro is not a primitive numerical type");
        if (number_type->vector_size != 0)
          self.compile_context().error_throw(location, "Primitive numerical constants of vector types are not supported");
        if (!NumberType::is_integer(number_type->scalar_type))
          self.compile_context().error_throw(location, "Non-integer numerical constants of are not supported");

        std::string data_s = data->text.str();
        uint64_t value;
        if (NumberType::is_signed(number_type->scalar_type)) {
          try {
            value = boost::lexical_cast<int64_t>(data_s);
          } catch (boost::bad_lexical_cast&) {
            self.compile_context().error_throw(location, boost::format("'%s' is not a valid signed integer") % data_s);
          }
        } else {
          try {
            value = boost::lexical_cast<uint64_t>(data_s);
          } catch (boost::bad_lexical_cast&) {
            self.compile_context().error_throw(location, boost::format("'%s' is not a valid unsigned integer") % data_s);
          }
        }
       
        return TermBuilder::integer_value(self.compile_context(), (NumberType::ScalarType)number_type->scalar_type, value, location);
      }
    };

    const MacroMemberCallbackVtable NumberValueMacro::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(NumberValueMacro, "psi.compiler.NumberValueMacro", MacroMemberCallback);

    TreePtr<Term> number_value_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<MacroMemberCallback> callback(::new NumberValueMacro(compile_context, location));
      TreePtr<Macro> m = make_macro(compile_context, location, callback);
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
      
      static PropertyValue evaluate_impl(const TargetCallbackConst& self, const PropertyValue&, const PropertyValue&) {
        return self.m_value;
      }
      
      template<typename V>
      static void visit(V& v) {
        visit_base<TargetCallback>(v);
        v("value", &TargetCallbackConst::m_value);
      }
    };
    
    const TargetCallbackVtable TargetCallbackConst::vtable = PSI_COMPILER_TARGET_CALLBACK(TargetCallbackConst, "psi.compiler.TargetCallbackConst", TargetCallback);
    
    TreePtr<TargetCallback> make_target_callback_const(CompileContext& compile_context,
                                                       const SourceLocation& location,
                                                       const SharedPtr<Parser::Expression>& value) {
      SharedPtr<Parser::TokenExpression> value_cast;
      if (!(value_cast = Parser::expression_as_token_type(value, Parser::token_brace)))
        compile_context.error_throw(location, "First parameter to library macro is not a {...}");
      
      PropertyValue pv;
      try {
        pv = PropertyValue::parse(value_cast->text.begin, value_cast->text.end,
                                  value_cast->text.location.first_line,
                                  value_cast->text.location.first_column);
      } catch (PropertyValueParseError& ex) {
        PhysicalSourceLocation loc = value_cast->text.location;
        loc.first_line = loc.last_line = ex.line();
        loc.first_column = loc.last_column = ex.column();
        compile_context.error_throw(location.relocate(loc), "Error parsing JSON data");
      }
      
      return TreePtr<TargetCallback>(::new TargetCallbackConst(compile_context, pv, location));
    }
    
    TreePtr<TargetCallback> make_target_callback(CompileContext& compile_context,
                                                 const SourceLocation& location,
                                                 const SharedPtr<Parser::Expression>& parameter_names_expr,
                                                 const SharedPtr<Parser::Expression>& body_expr,
                                                 const TreePtr<EvaluateContext>& evaluate_context) {
      
      SharedPtr<Parser::TokenExpression> parameter_names_cast, body_cast;
      if (!(parameter_names_cast = Parser::expression_as_token_type(parameter_names_expr, Parser::token_bracket)))
        compile_context.error_throw(location, "First parameter to library macro is not a (...)");

      if (!(body_cast = Parser::expression_as_token_type(body_expr, Parser::token_square_bracket)))
        compile_context.error_throw(location, "Second parameter to library macro is not a [...]");

      std::map<String, TreePtr<Term> > parameter_dict;
      
      PSI_STD::vector<Parser::TokenExpression> parameter_names = Parser::parse_identifier_list(compile_context.error_context(), location.logical, parameter_names_cast->text);
      switch (parameter_names.size()) {
      default: compile_context.error_throw(location, "Expected zero, one or two argument names specified for library macro");
      case 2: parameter_dict.insert(std::make_pair(parameter_names[1].text.str(), TreePtr<Term>()));
      case 1: parameter_dict.insert(std::make_pair(parameter_names[0].text.str(), TreePtr<Term>()));
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
        return TreePtr<MacroMemberCallback>(::new LibrarySymbolMacro(compile_context, location));
      }

      static TreePtr<Term> evaluate_impl(const LibrarySymbolMacro& self,
                                         const TreePtr<Term>& value,
                                         const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        TreePtr<TargetCallback> callback;
        switch (parameters.size()) {
        case 2: callback = make_target_callback_const(self.compile_context(), location, parameters[1]); break;
        case 3: callback = make_target_callback(self.compile_context(), location, parameters[1], parameters[2], evaluate_context); break;
        default: self.compile_context().error_throw(location, "Wrong number of parameters to library macro (expected 3 or 4)");
        }
        
        SharedPtr<Parser::TokenExpression> type_text;
        if (!(type_text = Parser::expression_as_token_type(parameters[0], Parser::token_bracket)))
          self.compile_context().error_throw(location, "First argument to library symbol macro is not a (...)");
        
        SharedPtr<Parser::Expression> type_expr = Parser::parse_expression(self.compile_context().error_context(), location.logical, type_text->text);
        TreePtr<Term> type = compile_term(type_expr, evaluate_context, location.logical);
        TreePtr<Library> library = metadata_lookup_as<Library>(self.compile_context().builtins().library_tag, evaluate_context, value, location);

        return TermBuilder::library_symbol(library, callback, type, location);
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
                                         const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        TreePtr<TargetCallback> callback;
        switch (parameters.size()) {
        case 1: callback = make_target_callback_const(self.compile_context(), location, parameters[0]); break;
        case 2: callback = make_target_callback(self.compile_context(), location, parameters[0], parameters[1], evaluate_context); break;
        default: self.compile_context().error_throw(location, "Wrong number of parameters to library macro (expected 1 or 2)");
        }
        
        TreePtr<Library> lib = TermBuilder::library(callback, location);
        
        PSI_STD::map<String, TreePtr<MacroMemberCallback> > macro_members;
        macro_members["symbol"] = LibrarySymbolMacro::get(self.compile_context(), location);

        std::vector<ConstantMetadataSetup> ml;

        ConstantMetadataSetup md_macro;
        md_macro.type = self.compile_context().builtins().type_macro_tag;
        md_macro.value = make_macro(self.compile_context(), location, macro_members);
        md_macro.n_wildcards = 0;
        md_macro.pattern.push_back(self.compile_context().builtins().term_compile_argument);
        ml.push_back(md_macro);

        ConstantMetadataSetup md_lib;
        md_lib.type = self.compile_context().builtins().library_tag;
        md_lib.value = lib;
        md_lib.n_wildcards = 0;
        ml.push_back(md_lib);

        return make_annotated_type(self.compile_context(), ml, location);
      }
    };

    const MacroMemberCallbackVtable LibraryMacro::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(LibraryMacro, "psi.compiler.LibraryMacro", MacroMemberCallback);

    TreePtr<Term> library_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<MacroMemberCallback> callback(::new LibraryMacro(compile_context, location));
      return make_macro_term(make_macro(compile_context, location, callback), location);
    }
    
    class StringMacro : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;
      
      StringMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroMemberCallback(&vtable, compile_context, location) {
      }

      static TreePtr<Term> evaluate_impl(const StringMacro& self,
                                         const TreePtr<Term>&,
                                         const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        if (parameters.size() != 1)
          self.compile_context().error_throw(location, "String macro expects one argument");
        
        SharedPtr<Parser::TokenExpression> value_text;
        if (!(value_text = Parser::expression_as_token_type(parameters[0], Parser::token_brace)))
          self.compile_context().error_throw(location, "Argument to string macro is not a {...}");
        
        std::vector<char> utf8_data(value_text->text.begin, value_text->text.end);
        utf8_data = string_unescape(utf8_data);
        utf8_data.push_back('\0');
        String utf8_str(&utf8_data.front());

        TreePtr<Term> zero_size = TermBuilder::size_value(0, self.compile_context(), location);
        TreePtr<Term> string_val = TermBuilder::string_value(self.compile_context(), utf8_str, location);
        TreePtr<Global> string_global = TermBuilder::global_variable(evaluate_context->module(), link_local, true, true, location, string_val);
        TreePtr<Term> string_base_ref = TermBuilder::element_value(string_global, zero_size, location);
        return TermBuilder::ptr_to(string_base_ref, location);
      }
    };
    
    const MacroMemberCallbackVtable StringMacro::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(StringMacro, "psi.compiler.StringMacro", MacroMemberCallback);

    /**
     * \brief Macro which generates C strings.
     */
    TreePtr<Term> string_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<MacroMemberCallback> callback(::new StringMacro(compile_context, location));
      return make_macro_term(make_macro(compile_context, location, callback), location);
    }

    class NewMacro : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;
      
      NewMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroMemberCallback(&vtable, compile_context, location) {
      }

      static TreePtr<Term> evaluate_impl(const NewMacro& self,
                                         const TreePtr<Term>&,
                                         const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        if (parameters.size() != 1)
          self.compile_context().error_throw(location, "new macro expects one argument");
        
        TreePtr<Term> type = compile_term(parameters[0], evaluate_context, location.logical);
        return TermBuilder::default_value(type, location);
      }
    };
    
    const MacroMemberCallbackVtable NewMacro::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(NewMacro, "psi.compiler.NewMacro", MacroMemberCallback);

    /**
     * \brief Macro which constructs default values.
     */
    TreePtr<Term> new_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<MacroMemberCallback> callback(::new NewMacro(compile_context, location));
      return make_macro_term(make_macro(compile_context, location, callback), location);
    }
    
#if 0
    class MacroDefineMacroCallback : public RewriteCallback {
    public:
      static const VtableType vtable;
      
      MacroDefineMacroCallback(CompileContext& compile_context, const TreePtr<EvaluateContext>& evaluate_context_,
                               const PSI_STD::vector<String>& arguments_, const String& text, const SourceLocation& location)
      : RewriteCallback(&vtable, compile_context, text, location),
      evaluate_context(evaluate_context_),
      arguments(arguments_) {
      }
      
      template<typename V>
      static void visit(V& v) {
        visit_base<RewriteCallback>(v);
        v("evaluate_context", &MacroDefineMacroCallback::evaluate_context)
        ("arguments", &MacroDefineMacroCallback::arguments);
      }
      
      TreePtr<EvaluateContext> evaluate_context;
      PSI_STD::vector<String> arguments;
      
      static TreePtr<EvaluateContext> rewrite_impl(const MacroDefineMacroCallback& self,
                                                   const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                                   const TreePtr<EvaluateContext>& evaluate_context,
                                                   const SourceLocation& location) {
        PSI_NOT_IMPLEMENTED();
      }
    };
    
    const RewriteCallbackVtable MacroDefineMacroCallback::vtable = PSI_COMPILER_REWRITE_CALLBACK(MacroDefineMacroCallback, "psi.compiler.MacroDefineMacroCallback", RewriteCallback);
#endif

    class MacroDefineMacro : public MacroMemberCallback {
    public:
      static const VtableType vtable;

      MacroDefineMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroMemberCallback(&vtable, compile_context, location) {
      }

      static TreePtr<Term> evaluate_impl(const MacroDefineMacro& self,
                                         const TreePtr<Term>&,
                                         const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        if (parameters.size() != 2)
          self.compile_context().error_throw(location, "macro macro expects two arguments");
        
        SharedPtr<Parser::TokenExpression> args, replace;
        if (!(args = Parser::expression_as_token_type(parameters[0], Parser::token_bracket)))
          self.compile_context().error_throw(location, "First parameter to macro macro is not a (...)");
        if (!(replace = Parser::expression_as_token_type(parameters[1], Parser::token_square_bracket)))
          self.compile_context().error_throw(location, "Second parameter to macro macro is not a [...]");

        PSI_STD::vector<String> arg_names;
        PSI_STD::vector<Parser::TokenExpression> arg_tokens = Parser::parse_identifier_list(self.compile_context().error_context(), location.logical, args->text);
        for (PSI_STD::vector<Parser::TokenExpression>::const_iterator ii = arg_tokens.begin(), ie = arg_tokens.end(); ii != ie; ++ii) {
          if (ii->token_type != Parser::token_identifier)
            self.compile_context().error_throw(location.relocate(ii->location), "Arguments to macro define macro must be identifiers");
          arg_names.push_back(ii->text.str());
        }

#if 0
        String replace_text = replace->text.str();
        TreePtr<RewriteCallback> rewrite(::new MacroDefineMacroCallback(self.compile_context(), evaluate_context, arg_names, replace_text, location));
        
        std::vector<ConstantMetadataSetup> ml;
        ml.push_back(std::make_pair(self.compile_context().builtins().rewrite_tag, rewrite));
#else
        std::vector<ConstantMetadataSetup> ml;
#endif
        return make_annotated_type(self.compile_context(), ml, location);
      }
    };

    const MacroMemberCallbackVtable MacroDefineMacro::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(MacroDefineMacro, "psi.compiler.MacroDefineMacro", MacroMemberCallback);
    
    /**
     * Return a term which is a macro for defining new macros.
     */
    TreePtr<Term> macro_define_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<MacroMemberCallback> callback(::new MacroDefineMacro(compile_context, location));
      return make_macro_term(make_macro(compile_context, location, callback), location);
    }
  }
}
