#include "Macros.hpp"

namespace Psi {
  namespace Compiler {
    /**
     * \brief A term whose sole purpose is to carry macros, and therefore
     * cannot be used as a type.
     */
    class PureMacroTerm : public Term {
      PSI_STD::vector<TreePtr<Implementation> > m_implementations;
      
    public:
      static const TermVtable vtable;
        
      PureMacroTerm(const TreePtr<Term>& type,
                    const PSI_STD::vector<TreePtr<Implementation> >& implementations,
                    const SourceLocation& location)
      : Term(type, location),
      m_implementations(implementations) {
        PSI_COMPILER_TREE_INIT();
      }
    };
    
    const TermVtable PureMacroTerm::vtable = PSI_COMPILER_TERM(PureMacroTerm, "psi.compiler.PureMacroTerm", Term);

    TreePtr<Term> make_macro_term(CompileContext& compile_context,
                                  const SourceLocation& location,
                                  const TreePtr<Macro>& macro) {
      TreePtr<Implementation> impl(new Implementation(compile_context, macro, default_, default_, location));
      PSI_STD::vector<TreePtr<Implementation> > implementations(1, impl);
      return TreePtr<Term>(new PureMacroTerm(compile_context.metatype(), implementations, location));
    }
    
    TreePtr<Term> none_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<GenericType> generic_type(new GenericType(compile_context.empty_type(), default_, default_, location));
      TreePtr<Term> type(new TypeInstance(generic_type, default_, location));
      return TreePtr<Term>(new NullValue(type, location));
    }
  }
}
