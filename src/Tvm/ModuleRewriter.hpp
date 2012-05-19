#ifndef HPP_PSI_TVM_MODULEREWRITER
#define HPP_PSI_TVM_MODULEREWRITER

#include "Core.hpp"

#include <boost/unordered_map.hpp>

namespace Psi {
  namespace Tvm {
    /**
     * Base class for types which rewrite entire modules.
     */
    class ModuleRewriter {
      Module *m_source_module;
      Module m_target_module;
      typedef boost::unordered_map<GlobalTerm*, GlobalTerm*> GlobalMapType;
      GlobalMapType m_global_map;
      
    protected:
      void global_map_put(GlobalTerm*,GlobalTerm*);
      GlobalTerm* global_map_get(GlobalTerm*);
      
    public:
      ModuleRewriter(Module*, Context* =0);
      
      /// \brief The module being rewritten
      Module* source_module() {return m_source_module;}
      /// \brief The module where rewritten symbols are created
      Module* target_module() {return &m_target_module;}
            
      GlobalTerm* target_symbol(GlobalTerm*);
      FunctionTerm* target_symbol(FunctionTerm*);
      GlobalVariableTerm* target_symbol(GlobalVariableTerm*);
      
      void update(bool incremental=true);
      
    private:
      virtual void update_implementation(bool incremental) = 0;
    };
  }
}

#endif
