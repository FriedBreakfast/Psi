#ifndef HPP_PSI_TVMLOWERING
#define HPP_PSI_TVMLOWERING

#include "Tree.hpp"

#include "Tvm/Core.hpp"
#include "Tvm/Jit.hpp"

namespace Psi {
  namespace Compiler {
    /**
     * \brief Result of building types and functional values by TvmFunctionalBuilder.
     */
    template<typename T=Tvm::Value>
    struct TvmFunctional {
      /// \brief Result value.
      Tvm::ValuePtr<T> value;
      /// \brief If this is a type, this is true if the type is primitive.
      bool primitive;
      
      TvmFunctional() : primitive(false) {}
      TvmFunctional(const Tvm::ValuePtr<T>& value_) : value(value_), primitive(false) {}
      TvmFunctional(const Tvm::ValuePtr<T>& value_, bool primitive_) : value(value_), primitive(primitive_) {}
    };
    
    class TvmFunctionalBuilderCallback {
    public:
      virtual TvmFunctional<> build_hook(const TreePtr<Term>& term) = 0;
      virtual TvmFunctional<Tvm::RecursiveType> build_generic_hook(const TreePtr<GenericType>& generic) = 0;
    };

    /**
     * \brief Utility class for building functional values.
     * 
     * This is used by both the global and function compiler.
     */
    class TvmFunctionalBuilder {
      Tvm::Context *m_context;
      TvmFunctionalBuilderCallback *m_callback;
      typedef boost::unordered_map<TreePtr<Term>, TvmFunctional<> > FunctionalValueMap;
      FunctionalValueMap m_values;
      
      TvmFunctional<> build_type(const TreePtr<Type>& type);
      TvmFunctional<> build_primitive_type(const TreePtr<PrimitiveType>& type);
      TvmFunctional<> build_function_type(const TreePtr<FunctionType>& type);
      TvmFunctional<> build_type_instance(const TreePtr<TypeInstance>& type);
      
    public:
      TvmFunctionalBuilder(Tvm::Context *context, TvmFunctionalBuilderCallback *callback);
      Tvm::Context& context() {return *m_context;}
      TvmFunctional<> build(const TreePtr<Term>& term);
      Tvm::ValuePtr<> build_value(const TreePtr<Term>& term);
      bool is_primitive(const TreePtr<Term>& term);
    };
    
    /**
     * \brief Compilation context component which handles TVM translation.
     */
    class TvmCompiler : TvmFunctionalBuilderCallback {
      typedef boost::unordered_map<TreePtr<ModuleGlobal>, Tvm::ValuePtr<Tvm::Global> > ModuleGlobalMap;
      
      struct TvmModule {
        boost::shared_ptr<Tvm::Module> module;
        ModuleGlobalMap symbols;
        std::vector<boost::shared_ptr<Platform::PlatformLibrary> > platform_dependencies;
      };        
      
      CompileContext *m_compile_context;
      Tvm::Context m_tvm_context;
      TvmFunctionalBuilder m_functional_builder;
      boost::shared_ptr<Tvm::Jit> m_jit;

      PropertyValue m_local_target;
      
      typedef boost::unordered_map<TreePtr<Module>, TvmModule> ModuleMap;
      ModuleMap m_modules;
      typedef boost::unordered_map<TreePtr<Library>, boost::shared_ptr<Platform::PlatformLibrary> > LibraryMap;
      LibraryMap m_libraries;
      typedef boost::unordered_map<TreePtr<GenericType>, TvmFunctional<Tvm::RecursiveType> > GenericTypeMap;
      GenericTypeMap m_generics;
      
      virtual TvmFunctional<> build_hook(const TreePtr<Term>& value);
      virtual TvmFunctional<Tvm::RecursiveType> build_generic_hook(const TreePtr<GenericType>& generic);
      
    public:
      TvmCompiler(CompileContext *compile_context);
      ~TvmCompiler();
      
      CompileContext& compile_context() {return *m_compile_context;}
      
      void add_compiled_module(const TreePtr<Module>& module, const boost::shared_ptr<Platform::PlatformLibrary>& lib);

      boost::shared_ptr<Platform::PlatformLibrary> load_library(const TreePtr<Library>& lib);
      Tvm::ValuePtr<Tvm::Global> build_global(const TreePtr<Global>& global);
      TvmFunctional<> build(const TreePtr<Term>& value);
      TvmFunctional<Tvm::RecursiveType> build_generic(const TreePtr<GenericType>& generic);
      void* jit_compile(const TreePtr<Global>& global);
      
      static std::string mangle_name(const LogicalSourceLocationPtr& location);
    };

    void tvm_lower_function(TvmCompiler& tvm_compiler, const TreePtr<Function>& function, const Tvm::ValuePtr<Tvm::Function>& output);
  }
}

#endif
