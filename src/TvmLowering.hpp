#ifndef HPP_PSI_TVMLOWERING
#define HPP_PSI_TVMLOWERING

#include "Tree.hpp"

#include "Tvm/Core.hpp"
#include "Tvm/Jit.hpp"

namespace Psi {
  namespace Compiler {
    /**
     * \brief Compilation context componen which handles TVM translation.
     */
    class TvmCompiler {
      typedef boost::unordered_map<TreePtr<ModuleGlobal>, Tvm::ValuePtr<Tvm::Global> > ModuleGlobalMap;
      
      struct TvmModule {
        boost::shared_ptr<Tvm::Module> module;
        ModuleGlobalMap symbols;
        std::vector<boost::shared_ptr<Platform::PlatformLibrary> > platform_dependencies;
      };
      
      CompileContext *m_compile_context;
      Tvm::Context m_tvm_context;
      boost::shared_ptr<Tvm::Jit> m_jit;

      PropertyValue m_local_target;
      
      typedef boost::unordered_map<TreePtr<Module>, TvmModule> ModuleMap;
      ModuleMap m_modules;
      typedef boost::unordered_map<TreePtr<Library>, boost::shared_ptr<Platform::PlatformLibrary> > LibraryMap;
      LibraryMap m_libraries;
      
    public:
      TvmCompiler(CompileContext *compile_context);
      ~TvmCompiler();
      
      CompileContext& compile_context() {return *m_compile_context;}
      
      Tvm::ValuePtr<> tvm_global_type(const TreePtr<Term>& type);
      Tvm::ValuePtr<> tvm_global_value(const TreePtr<Term>& value);

      void add_compiled_module(const TreePtr<Module>& module, const boost::shared_ptr<Platform::PlatformLibrary>& lib);

      boost::shared_ptr<Platform::PlatformLibrary> load_library(const TreePtr<Library>& lib);
      Tvm::ValuePtr<Tvm::Global> build(const TreePtr<Global>& global);
      void* jit_compile(const TreePtr<Global>& global);
    };
  }
}

#endif
