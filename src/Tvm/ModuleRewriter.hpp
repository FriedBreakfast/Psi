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
      typedef boost::unordered_map<ValuePtr<Global>, ValuePtr<Global> > GlobalMapType;
      GlobalMapType m_global_map;
      
    protected:
      void global_map_put(const ValuePtr<Global>& key, const ValuePtr<Global>& value);
      ValuePtr<Global> global_map_get(const ValuePtr<Global>& key);
      
    public:
      ModuleRewriter(Module*, Context* =0);
      
      /// \brief The module being rewritten
      Module* source_module() {return m_source_module;}
      /// \brief The module where rewritten symbols are created
      Module* target_module() {return &m_target_module;}
            
      ValuePtr<Global> target_symbol(const ValuePtr<Global>&);
      ValuePtr<Function> target_symbol(const ValuePtr<Function>&);
      ValuePtr<GlobalVariable> target_symbol(const ValuePtr<GlobalVariable>&);
      
      void update(bool incremental=true);
      
    private:
      virtual void update_implementation(bool incremental) = 0;
    };
  }
}

#endif
