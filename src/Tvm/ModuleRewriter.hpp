#ifndef HPP_PSI_TVM_MODULEREWRITER
#define HPP_PSI_TVM_MODULEREWRITER

#include "Core.hpp"

#include <boost/unordered_map.hpp>
#include <boost/shared_ptr.hpp>

namespace Psi {
  namespace Tvm {
    /**
     * Base class for types which rewrite entire modules.
     */
    class ModuleRewriter {
      boost::shared_ptr<Module> m_source_module;
      boost::shared_ptr<Module> m_target_module;
      typedef boost::unordered_map<ValuePtr<Global>, ValuePtr<Global> > GlobalMapType;
      GlobalMapType m_global_map;
      
    protected:
      void global_map_put(const ValuePtr<Global>& key, const ValuePtr<Global>& value);
      ValuePtr<Global> global_map_get(const ValuePtr<Global>& key);
      
    public:
      ModuleRewriter(const boost::shared_ptr<Module>&, Context* =0);
      
      /// \brief The module being rewritten
      const boost::shared_ptr<Module>& source_module() {return m_source_module;}
      /// \brief The module where rewritten symbols are created
      const boost::shared_ptr<Module>& target_module() {return m_target_module;}
            
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
