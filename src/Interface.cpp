#include "Compiler.hpp"
#include "Parser.hpp"
#include "Utility.hpp"
#include "Tree.hpp"

namespace Psi {
  namespace Compiler {
    class InterfaceMacro : public Macro {
      std::string m_name;
      GCPtr<EvaluateCallback> m_evaluate;
      std::map<std::string, GCPtr<DotCallback> > m_members;

      virtual void gc_visit(GCVisitor& visitor) {
        Macro::gc_visit(visitor);
        visitor % m_evaluate;
        for (std::map<std::string, GCPtr<DotCallback> >::iterator ii = m_members.begin(), ie = m_members.end(); ii != ie; ++ii)
          visitor.visit_ptr(ii->second);
      }
      
    public:
      InterfaceMacro(CompileContext& compile_context,
                     const std::string& name,
                     const GCPtr<EvaluateCallback>& evaluate,
                     const std::map<std::string, GCPtr<DotCallback> >& members)
      : Macro(compile_context), m_name(name), m_evaluate(evaluate), m_members(members) {
      }

      virtual ~InterfaceMacro() {
      }

      virtual std::string name() {
        return m_name;
      }

      virtual LookupResult<GCPtr<EvaluateCallback> > evaluate_lookup(const std::vector<boost::shared_ptr<Parser::Expression> >&) {
        if (m_evaluate)
          return LookupResult<GCPtr<EvaluateCallback> >::make_match(m_evaluate);
        else
          return LookupResult<GCPtr<EvaluateCallback> >::make_none();
      }

      virtual LookupResult<GCPtr<DotCallback> > dot_lookup(const boost::shared_ptr<Parser::Expression>& member) {
        if (member->expression_type != Parser::expression_token)
          LookupResult<GCPtr<DotCallback> >::make_none();

        const Parser::TokenExpression& token_expression = checked_cast<Parser::TokenExpression&>(*member);
        std::string member_name(token_expression.text.begin, token_expression.text.end);
        std::map<std::string, GCPtr<DotCallback> >::const_iterator it = m_members.find(member_name);

        if (it == m_members.end())
          return LookupResult<GCPtr<DotCallback> >::make_none();

        return LookupResult<GCPtr<DotCallback> >::make_match(it->second);
      }
    };

    /**
     * \brief Create an interface macro.
     */
    GCPtr<Macro> make_interface(CompileContext& compile_context,
                                const std::string& name,
                                const GCPtr<EvaluateCallback>& evaluate,
                                const std::map<std::string, GCPtr<DotCallback> >& members) {
      return GCPtr<Macro>(new InterfaceMacro(compile_context, name, evaluate, members));
    }

    GCPtr<Macro> make_interface(CompileContext& compile_context,
                                const std::string& name,
                                const GCPtr<EvaluateCallback>& evaluate) {
      return make_interface(compile_context, name, evaluate, std::map<std::string, GCPtr<DotCallback> >());
    }

    GCPtr<Macro> make_interface(CompileContext& compile_context,
                                const std::string& name,
                                const std::map<std::string, GCPtr<DotCallback> >& members) {
      return make_interface(compile_context, name, GCPtr<EvaluateCallback>(), members);
    }

    GCPtr<Macro> make_interface(CompileContext& compile_context,
                                const std::string& name) {
      return make_interface(compile_context, name, GCPtr<EvaluateCallback>(), std::map<std::string, GCPtr<DotCallback> >());
    }
  }
}
