#include "Compiler.hpp"
#include "Parser.hpp"
#include "Utility.hpp"
#include "Tree.hpp"

#include <boost/format.hpp>

namespace Psi {
  namespace Compiler {
    class NamedMemberMacro : public Macro {
      typedef std::map<String, TreePtr<MacroDotCallback> > NameMapType;
      String m_name;
      TreePtr<MacroEvaluateCallback> m_evaluate;
      NameMapType m_members;

    public:
      static const MacroVtable vtable;

      NamedMemberMacro(CompileContext& compile_context,
                       const SourceLocation& location,
                       const String& name,
                       const TreePtr<MacroEvaluateCallback>& evaluate,
                       const NameMapType& members)
      : Macro(compile_context, location),
      m_name(name),
      m_evaluate(evaluate),
      m_members(members) {
        m_vptr = reinterpret_cast<const SIVtable*>(&vtable);
      }

      template<typename Visitor>
      static void visit_impl(NamedMemberMacro& self, Visitor& visitor) {
        PSI_FAIL("not implemented");
        Macro::visit_impl(self, visitor);
        visitor
        ("name", self.m_name)
        ("evaluate", self.m_evaluate)
        ("members", self.m_members);
      }
      
      static TreePtr<Term> evaluate_impl(NamedMemberMacro& self,
                                         const TreePtr<Term>& value,
                                         const List<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        if (self.m_evaluate) {
          return self.m_evaluate->evaluate(value, parameters, evaluate_context, location);
        } else {
          self.compile_context().error_throw(location, boost::format("Macro '%s' does not support evaluation") % self.m_name);
        }
      }

      static TreePtr<Term> dot_impl(NamedMemberMacro& self,
                                    const TreePtr<Term>& value,
                                    const SharedPtr<Parser::Expression>& parameter,
                                    const TreePtr<EvaluateContext>& evaluate_context,
                                    const SourceLocation& location) {
        if (parameter->expression_type != Parser::expression_token)
          self.compile_context().error_throw(location, boost::format("Token following dot on '%s' is not a name") % self.m_name);

        const Parser::TokenExpression& token_expression = checked_cast<Parser::TokenExpression&>(*parameter);
        String member_name(token_expression.text.begin, token_expression.text.end);
        NameMapType::const_iterator it = self.m_members.find(member_name);

        if (it == self.m_members.end())
          self.compile_context().error_throw(location, boost::format("'%s' has no member named '%s'") % self.m_name % member_name);

        return it->second->dot(value, evaluate_context, location);
      }
    };

    const MacroVtable NamedMemberMacro::vtable =
    PSI_COMPILER_MACRO(NamedMemberMacro, "psi.compiler.NamedMemberMacro", Macro);

    /**
     * \brief Create an interface macro.
     */
    TreePtr<Macro> make_macro(CompileContext& compile_context,
                              const SourceLocation& location,
                              const String& name,
                              const TreePtr<MacroEvaluateCallback>& evaluate,
                              const std::map<String, TreePtr<MacroDotCallback> >& members) {
      return TreePtr<Macro>(new NamedMemberMacro(compile_context, location, name, evaluate, members));
    }

    /**
     * \brief Create an interface macro.
     */
    TreePtr<Macro> make_macro(CompileContext& compile_context,
                              const SourceLocation& location,
                              const String& name,
                              const TreePtr<MacroEvaluateCallback>& evaluate) {
      return make_macro(compile_context, location, name, evaluate, std::map<String, TreePtr<MacroDotCallback> >());
    }

    /**
     * \brief Create an interface macro.
     */
    TreePtr<Macro> make_macro(CompileContext& compile_context,
                              const SourceLocation& location,
                              const String& name,
                              const std::map<String, TreePtr<MacroDotCallback> >& members) {
      return make_macro(compile_context, location, name, TreePtr<MacroEvaluateCallback>(), members);
    }

    /**
     * \brief Create an interface macro.
     */
    TreePtr<Macro> make_macro(CompileContext& compile_context,
                              const SourceLocation& location,
                              const String& name) {
      return make_macro(compile_context, location, name, TreePtr<MacroEvaluateCallback>(), std::map<String, TreePtr<MacroDotCallback> >());
    }

    /**
     * \brief Create a term whose type is suitable for attaching interfaces.
     *
     * The type of this term will be an instance of RecursiveType. The term itself
     * will carry no data.
     */
    TreePtr<Term> make_macro_term() {
      PSI_FAIL("not implemented");
    }

    /**
     * \brief Attach a compile-time interface to a type.
     *
     * This is a utility function to attach a compile-time interface which takes a single parameter
     * to a type. This function creates the whole implementation structure.
     *
     * \param interface Interface being implemented.
     * \param type Type to attach the implementation to. This type is also the single parameter
     * to the interface.
     * \param value Value of the implementation.
     */
    void attach_compile_implementation(const TreePtr<Interface>& interface, const TreePtr<ImplementationTerm>& type, const TreePtr<>& value, const SourceLocation& location) {
      TreePtr<Implementation> impl(new Implementation(interface->compile_context(), location));
      impl->interface = interface;
      impl->value = value;
      impl->interface_parameters.push_back(type);
      type->implementations.push_back(impl);
    }
  }
}
