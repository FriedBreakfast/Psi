#include "Macros.hpp"

namespace Psi {
  namespace Compiler {
    /**
     * \brief A term whose sole purpose is to carry macros, and therefore
     * cannot be used as a type.
     */
    class PureMacroTerm : public Term {
    public:
      static const TermVtable vtable;

      PSI_STD::vector<TreePtr<Implementation> > implementations;

      PureMacroTerm(CompileContext& context, const SourceLocation& location)
      : Term(&vtable, context, location) {
      }
        
      PureMacroTerm(const TreePtr<Term>& type,
                    const PSI_STD::vector<TreePtr<Implementation> >& implementations_,
                    const SourceLocation& location)
      : Term(&vtable, type, location),
      implementations(implementations_) {
      }

      static TreePtr<> interface_search_impl(const PureMacroTerm& self,
                                             const TreePtr<Interface>& interface,
                                             const List<TreePtr<Term> >& parameters) {
        for (PSI_STD::vector<TreePtr<Implementation> >::const_iterator ii = self.implementations.begin(), ie = self.implementations.end(); ii != ie; ++ii) {
          if ((*ii)->matches(interface, parameters))
            return (*ii)->value;
        }

        return default_;
      }
      
      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<Term>(v);
        v("implementations", &PureMacroTerm::implementations);
      }
    };

    const TermVtable PureMacroTerm::vtable = PSI_COMPILER_TERM(PureMacroTerm, "psi.compiler.PureMacroTerm", Term);

    class PureMacroConstructor {
      TreePtr<Macro> m_macro;

    public:
      PureMacroConstructor(const TreePtr<Macro>& macro)
      : m_macro(macro) {
      }

      template<typename Visitor>
      static void visit(Visitor& v) {
        v("macro", &PureMacroConstructor::m_macro);
      }

      TreePtr<Term> evaluate(const TreePtr<Term>& self) {
        CompileContext& compile_context = self.compile_context();
        TreePtr<Implementation> impl(new Implementation(compile_context, m_macro, compile_context.builtins().macro_interface,
                                                        default_, PSI_STD::vector<TreePtr<Term> >(1, self), self.location()));
        PSI_STD::vector<TreePtr<Implementation> > implementations(1, impl);
        return TreePtr<Term>(new PureMacroTerm(compile_context.builtins().metatype, implementations, self.location()));
      }
    };

    TreePtr<Term> make_macro_term(CompileContext& compile_context,
                                  const SourceLocation& location,
                                  const TreePtr<Macro>& macro) {
      return tree_callback<Term>(compile_context, location, PureMacroConstructor(macro));
    }
    
    TreePtr<Term> none_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<GenericType> generic_type(new GenericType(compile_context.builtins().empty_type, default_, default_, location));
      TreePtr<Term> type(new TypeInstance(generic_type, default_, location));
      return TreePtr<Term>(new NullValue(type, location));
    }
  }
}
