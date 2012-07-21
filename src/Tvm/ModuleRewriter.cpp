#include "ModuleRewriter.hpp"
#include "Function.hpp"

namespace Psi {
  namespace Tvm {
    /**
     * \param source_module Module to be rewritten
     * 
     * \param target_context Context to create target module in. If NULL,
     * create in the same context as the source module.
     */
    ModuleRewriter::ModuleRewriter(Module *source_module, Context *target_context)
    : m_source_module(source_module),
    m_target_module(new Module(target_context ? target_context : &source_module->context(), source_module->name(), source_module->location())) {
    }
    
    /**
     * \brief Add a mapping to the global variable map.
     * 
     * This asserts various conditions: that key and value are
     * from the correct modules, and that key does not already
     * exist in the global variable map.
     */
    void ModuleRewriter::global_map_put(const ValuePtr<Global>& key, const ValuePtr<Global>& value) {
      PSI_ASSERT(key->module() == source_module());
      PSI_ASSERT(value->module() == target_module());
      
      std::pair<GlobalMapType::iterator, bool> result =
        m_global_map.insert(std::make_pair(key, value));
      PSI_ASSERT(result.second);
    }
    
    /**
     * \brief Get an entry in the rewriter global map.
     * 
     * Returns NULL if the term is not present.
     */
    ValuePtr<Global> ModuleRewriter::global_map_get(const ValuePtr<Global>& term) {
      PSI_ASSERT(term->module() == source_module());
      GlobalMapType::iterator it = m_global_map.find(term);
      if (it != m_global_map.end())
        return it->second;
      else
        return ValuePtr<Global>();
    }
    
    /**
     * \brief Get the symbol in the target module corresponding to the given source module symbol.
     * 
     * Throws an exception if the term is missing.
     */
    ValuePtr<Global> ModuleRewriter::target_symbol(const ValuePtr<Global>& term) {
      if (term->module() != source_module())
        throw TvmUserError("global symbol is not from this rewriter's source module");
      
      ValuePtr<Global> t = global_map_get(term);
      if (!t)
        throw TvmUserError("missing symbol in module rewriter: " + term->name());
      return t;
    }
    
    /// \copydoc ModuleRewriter::target_symbol(GlobalTerm*)
    ValuePtr<Function> ModuleRewriter::target_symbol(const ValuePtr<Function>& term) {
      return value_cast<Function>(target_symbol(term));
    }
    
    /// \copydoc ModuleRewriter::target_symbol(GlobalTerm*)
    ValuePtr<GlobalVariable> ModuleRewriter::target_symbol(const ValuePtr<GlobalVariable>& term) {
      return value_cast<GlobalVariable>(target_symbol(term));
    }
    
    /**
     * \brief Update the target module to correspond to the source module.
     * 
     * \param incremental If true, only symbols which did not exist on the
     * previous pass need to be rewritten. Changes to existing symbols may
     * not be detected when this is true.
     */
    void ModuleRewriter::update(bool incremental) {
      if (!incremental)
        m_global_map.clear();
      update_implementation(incremental);
    }
  }
}
