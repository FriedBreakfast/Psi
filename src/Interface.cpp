#include "Compiler.hpp"
#include "Parser.hpp"
#include "Utility.hpp"
#include "Tree.hpp"

#include <boost/format.hpp>

namespace Psi {
  namespace Compiler {
    class InterfaceMacro : public CompileImplementation {
      virtual void gc_visit(GCVisitor& visitor) {
        CompileImplementation::gc_visit(visitor);
        visitor % evaluate;
        for (NameMapType::iterator ii = members.begin(), ie = members.end(); ii != ie; ++ii)
          visitor.visit_ptr(ii->second);
      }

      struct Callback {
        TreePtr<> evaluate(const TreePtr<InterfaceMacro>& interface,
                           const TreePtr<>& value,
                           const ArrayList<SharedPtr<Parser::Expression> >& parameters,
                           const TreePtr<CompileImplementation>& evaluate_context,
                           const SourceLocation& location) {
          if (interface->evaluate) {
            return compile_implementation_wrap<MacroEvaluateCallbackRef>(interface->evaluate).evaluate(value, parameters, evaluate_context, location);
          } else {
            interface->compile_context().error_throw(location, boost::format("Macro '%s' does not support evaluation") % interface->name);
          }
        }

        TreePtr<> dot(const TreePtr<InterfaceMacro>& interface,
                      const TreePtr<>& value,
                      const SharedPtr<Parser::Expression>& parameter,
                      const TreePtr<CompileImplementation>& evaluate_context,
                      const SourceLocation& location) {
          if (parameter->expression_type != Parser::expression_token)
            interface->compile_context().error_throw(location, boost::format("Token following dot on '%s' is not a name") % interface->name);

          const Parser::TokenExpression& token_expression = checked_cast<Parser::TokenExpression&>(*parameter);
          String member_name(token_expression.text.begin, token_expression.text.end);
          NameMapType::const_iterator it = interface->members.find(member_name);

          if (it == interface->members.end())
            interface->compile_context().error_throw(location, boost::format("'%s' has no member named '%s'") % interface->name % member_name);

          return compile_implementation_wrap<MacroDotCallbackRef>(it->second).dot(value, evaluate_context, location);
        }
      };

      static MacroWrapper<Callback, InterfaceMacro> m_vtable;

    public:
      typedef std::map<String, TreePtr<CompileImplementation> > NameMapType;
      TreePtr<CompileImplementation> evaluate;
      NameMapType members;
      String name;

      InterfaceMacro(CompileContext& compile_context, const SourceLocation& location)
      : CompileImplementation(compile_context, location) {
        vtable = compile_context.tree_from_address(location, TreePtr<Type>(), &m_vtable);
      }
    };

    MacroWrapper<InterfaceMacro::Callback, InterfaceMacro> InterfaceMacro::m_vtable;

    /**
     * \brief Create an interface macro.
     */
    TreePtr<CompileImplementation> make_interface(CompileContext& compile_context,
                                                  const SourceLocation& location,
                                                  const String& name,
                                                  const TreePtr<CompileImplementation>& evaluate,
                                                  const std::map<String, TreePtr<CompileImplementation> >& members) {
      TreePtr<InterfaceMacro> result(new InterfaceMacro(compile_context, location));
      result->evaluate = evaluate;
      result->members = members;
      result->name = name;
      return result;
    }

    TreePtr<CompileImplementation> make_interface(CompileContext& compile_context,
                                                  const SourceLocation& location,
                                                  const String& name,
                                                  const TreePtr<CompileImplementation>& evaluate) {
      return make_interface(compile_context, location, name, evaluate, std::map<String, TreePtr<CompileImplementation> >());
    }

    TreePtr<CompileImplementation> make_interface(CompileContext& compile_context,
                                                  const SourceLocation& location,
                                                  const String& name,
                                                  const std::map<String, TreePtr<CompileImplementation> >& members) {
      return make_interface(compile_context, location, name, TreePtr<CompileImplementation>(), members);
    }

    TreePtr<CompileImplementation> make_interface(CompileContext& compile_context,
                                                  const SourceLocation& location,
                                                  const String& name) {
      return make_interface(compile_context, location, name, TreePtr<CompileImplementation>(), std::map<String, TreePtr<CompileImplementation> >());
    }
  }
}
