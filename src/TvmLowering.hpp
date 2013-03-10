#ifndef HPP_PSI_TVMLOWERING
#define HPP_PSI_TVMLOWERING

#include "Tree.hpp"
#include "SharedMap.hpp"

#include "Tvm/Core.hpp"
#include "Tvm/Jit.hpp"

namespace Psi {
  namespace Compiler {
    class TvmScope;
    typedef boost::shared_ptr<TvmScope> TvmScopePtr;
    
    struct TvmResultBase {
      /**
       * \brief Result value.
       * 
       * If NULL, indicates the operation always fails.
       */
      Tvm::ValuePtr<> value;
      
      /**
       * If the result is a derived type, this is the upward refernce.
       * This PointerType(DerivedType(a,b)) to become PointerType(a,b) in TVM.
       */
      Tvm::ValuePtr<> upref;
      
      TvmResultBase() {}
      TvmResultBase(const Tvm::ValuePtr<>& value_, const Tvm::ValuePtr<>& upref_)
      : value(value_), upref(upref_) {}
      bool is_bottom() const {return !value;}
    };
    
    struct TvmResultScope {
      TvmScope *scope;
      bool in_progress_generic;
      
      TvmResultScope() : scope(NULL), in_progress_generic(false) {}
      explicit TvmResultScope(TvmScope *scope_) : scope(scope_), in_progress_generic(false) {}
      TvmResultScope(TvmScope *scope_, bool in_progress_generic_) : scope(scope_), in_progress_generic(in_progress_generic_) {}
      TvmResultScope(const TvmScopePtr& scope_) : scope(scope_.get()), in_progress_generic(false) {}
      TvmResultScope(const TvmScopePtr& scope_, bool in_progress_generic_) : scope(scope_.get()), in_progress_generic(in_progress_generic_) {}
    };
    
    struct TvmResult : TvmResultBase {
      TvmResultScope scope;
      
      TvmResult() {}
      TvmResult(const TvmResultScope& scope_, const TvmResultBase& base)
      : TvmResultBase(base), scope(scope_) {}
      TvmResult(const TvmResultScope& scope_, const Tvm::ValuePtr<>& value_, const Tvm::ValuePtr<>& upref_=Tvm::ValuePtr<>())
      : TvmResultBase(value_, upref_), scope(scope_) {}
      
      static TvmResult bottom() {return TvmResult();}
    };
    
    struct TvmGenericScope;
    class TvmFunctionalBuilder;
    
    class TvmScope : boost::noncopyable {
      friend TvmResult tvm_lower_generic(const TvmScopePtr& scope, TvmFunctionalBuilder& builder, const TreePtr<GenericType>& generic);

      typedef boost::unordered_map<TreePtr<Term>, TvmResultBase> VariableMapType;
      typedef boost::unordered_map<TreePtr<GenericType>, TvmResultBase> GenericMapType;
      TvmScopePtr m_parent;
      unsigned m_depth;
      VariableMapType m_variables;
      GenericMapType m_generics;
      TvmGenericScope *m_in_progress_generic_scope;
      
      TvmGenericScope *generic_put_scope(TvmScope *given);
      TvmScope* put_scope(TvmScope *given);
      
    public:
      TvmScope();
      TvmScope(const TvmScopePtr& parent);
      static TvmScopePtr root();
      static TvmScopePtr new_(const TvmScopePtr& parent);
      
      boost::optional<TvmResult> get(const TreePtr<Term>& key);
      void put(const TreePtr<Term>& key, const TvmResult& result);
      boost::optional<TvmResult> get_generic(const TreePtr<GenericType>& key);
      void put_generic(const TreePtr<GenericType>& key, const TvmResult& result);
      
      bool is_root() const {return !m_parent;}
      static TvmResultScope join(const TvmResultScope& lhs, const TvmResultScope& rhs);
    };

    struct TvmGenericScope {
      typedef boost::unordered_map<TreePtr<Term>, TvmResult> VariableMapType;
      typedef boost::unordered_map<TreePtr<GenericType>, TvmResult> GenericMapType;
      VariableMapType variables;
      GenericMapType generics;
    };
    
    class TvmFunctionalBuilder {
      CompileContext *m_compile_context;
      Tvm::Context *m_tvm_context;

    public:
      TvmFunctionalBuilder(CompileContext& compile_context, Tvm::Context& tvm_context);
      CompileContext& compile_context() {return *m_compile_context;}
      Tvm::Context& tvm_context() {return *m_tvm_context;}

      virtual TvmResult build(const TreePtr<Term>& term) = 0;
      virtual TvmResult build_generic(const TreePtr<GenericType>& generic) = 0;
    };
    
    /**
     * \brief Compilation context component which handles TVM translation.
     */
    class TvmCompiler {
      class FunctionalBuilderCallback;

      typedef boost::unordered_map<TreePtr<ModuleGlobal>, Tvm::ValuePtr<Tvm::Global> > ModuleGlobalMap;
      typedef boost::unordered_map<TreePtr<LibrarySymbol>, Tvm::ValuePtr<Tvm::Global> > ModuleLibrarySymbolMap;
      typedef boost::unordered_map<TreePtr<GlobalEvaluate>, Tvm::ValuePtr<Tvm::Global> > ModuleFunctionalConstantMap;
      
      struct TvmModule {
        bool jit_current;
        boost::shared_ptr<Tvm::Module> module;
        ModuleGlobalMap symbols;
        ModuleLibrarySymbolMap library_symbols;
        ModuleFunctionalConstantMap functional_constants;
        TvmScopePtr scope;
      };
      
      struct TvmLibrarySymbol {
        std::string name;
        Tvm::ValuePtr<Tvm::Global> value;
      };
      
      struct TvmPlatformLibrary {
        boost::shared_ptr<Platform::PlatformLibrary> library;
        boost::unordered_map<TreePtr<LibrarySymbol>, TvmLibrarySymbol> symbol_info;
      };
      
      CompileContext *m_compile_context;
      Tvm::Context m_tvm_context;
      TvmScopePtr m_root_scope;
      boost::shared_ptr<Tvm::Jit> m_jit;
      boost::shared_ptr<Tvm::Module> m_library_module;

      PropertyValue m_local_target;
      
      typedef boost::unordered_map<TreePtr<Module>, TvmModule> ModuleMap;
      ModuleMap m_modules;
      TvmModule& get_module(const TreePtr<Module>& module);
      
      typedef boost::unordered_map<TreePtr<Library>, TvmPlatformLibrary> LibraryMap;
      LibraryMap m_libraries;
      TvmPlatformLibrary& get_platform_library(const TreePtr<Library>& lib);

      /**
       * \brief Globals which are currently being built.
       * 
       * This prevents certain cases of dependency recursion.
       */
      std::set<TreePtr<ModuleGlobal> > m_in_progress_globals;
      
      Tvm::ValuePtr<Tvm::Global> build_module_global(const TreePtr<ModuleGlobal>& global);
      Tvm::ValuePtr<Tvm::Global> build_library_symbol(const TreePtr<LibrarySymbol>& lib_global);
      void build_global_group(const std::vector<TreePtr<ModuleGlobal> >& group);
      
      TvmResult build(const TreePtr<Term>& value, const TreePtr<Module>& module);
      TvmResult build_type(const TreePtr<Term>& value, const TreePtr<Module>& module, const SourceLocation& location);
      TvmResult build_global_value(const TreePtr<Term>& value, const TreePtr<Module>& module);
      
    public:
      TvmCompiler(CompileContext *compile_context);
      ~TvmCompiler();
      
      CompileContext& compile_context() {return *m_compile_context;}
      Tvm::Context& tvm_context() {return m_tvm_context;}
      
      const TvmScopePtr& root_scope() {return m_root_scope;}
      const TvmScopePtr& module_scope(const TreePtr<Module>& module) {return get_module(module).scope;}
      
      void add_compiled_module(const TreePtr<Module>& module, const boost::shared_ptr<Platform::PlatformLibrary>& lib);

      TvmResult build_global_evaluate(const TreePtr<GlobalEvaluate>& evaluate, const TreePtr<Module>& module);
      TvmResult build_global(const TreePtr<Global>& global, const TreePtr<Module>& module);
      Tvm::ValuePtr<Tvm::Global> build_global_jit(const TreePtr<Global>& global);
      void* jit_compile(const TreePtr<Global>& global);
      
      static std::string mangle_name(const LogicalSourceLocationPtr& location);
    };

    void tvm_lower_function(TvmCompiler& tvm_compiler, const TreePtr<Function>& function, const Tvm::ValuePtr<Tvm::Function>& output);
    void tvm_lower_init(TvmCompiler& tvm_compiler, const TreePtr<Module>& module, const TreePtr<Term>& body, const Tvm::ValuePtr<Tvm::Function>& output);
    TvmResult tvm_lower_functional(TvmFunctionalBuilder& builder, const TreePtr<Term>& term);
    TvmResult tvm_lower_generic(const TvmScopePtr& scope, TvmFunctionalBuilder& builder, const TreePtr<GenericType>& generic);
  }
}

#endif
