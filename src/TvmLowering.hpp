#ifndef HPP_PSI_TVMLOWERING
#define HPP_PSI_TVMLOWERING

#include <boost/optional.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/unordered_set.hpp>

#include "Tree.hpp"
#include "SharedMap.hpp"
#include "PlatformCompile.hpp"

#include "Tvm/Core.hpp"
#include "Tvm/Jit.hpp"
#include "Tvm/Function.hpp"

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
      
      TvmResultBase() {}
      TvmResultBase(const Tvm::ValuePtr<>& value_) : value(value_) {}
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
      TvmResult(const TvmResultScope& scope_, const Tvm::ValuePtr<>& value_)
      : TvmResultBase(value_), scope(scope_) {}
      
      static TvmResult bottom() {return TvmResult();}
    };

    /**
     * Value type of list of interface implementations visible in the current scope.
     */
    struct TvmGeneratedImplementation {
      PSI_STD::vector<TreePtr<Term> > parameters;
      TvmResult result;
      std::set<TreePtr<ModuleGlobal> > dependencies;
    };
    
    typedef SharedMap<TreePtr<Interface>, SharedList<TvmGeneratedImplementation> > TvmGeneratedImplementationSet;
    
    TvmResult tvm_check_implementation(const TvmGeneratedImplementationSet& implementations, const TreePtr<Interface>& interface,
                                       const PSI_STD::vector<TreePtr<Term> >& parameters, std::set<TreePtr<ModuleGlobal> >& dependencies);
    
    struct TvmGenericScope;
    class TvmFunctionalBuilder;
    
    class TvmScope : boost::noncopyable {
      friend TvmResult tvm_lower_generic(TvmScope& scope, TvmFunctionalBuilder& builder, const TreePtr<GenericType>& generic);

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
      static TvmScopePtr new_root();
      static TvmScopePtr new_(const TvmScopePtr& parent);
      
      /**
       * \brief Small number depths relate to fixed structures.
       * 
       * Any depth higher than 2 is inside a function.
       */
      enum {
        depth_root=0,
        depth_module=1,
        depth_global=2
      };
      
      unsigned depth() const {return m_depth;}
      TvmScope* root();
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
    
    struct TvmTargetSymbol {
      std::string name;
      Tvm::ValuePtr<> type;
      Tvm::Linkage linkage;
    };

    /**
     * Ties target information to the root scope, since types may be target
     * dependent.
     */
    class TvmTargetScope {
      TvmTargetScope *m_jit_target;
      CompileContext *m_compile_context;
      Tvm::Context *m_tvm_context;
      PropertyValue m_target;
      TvmScopePtr m_scope;

      typedef boost::unordered_map<TreePtr<LibrarySymbol>, TvmTargetSymbol> LibrarySymbolMap;
      LibrarySymbolMap m_library_symbols;
      
    public:
      TvmTargetScope(CompileContext& compile_context, Tvm::Context& tvm_context, const PropertyValue& target);
      TvmTargetScope(TvmTargetScope& jit_target, const PropertyValue& target);
      CompileContext& compile_context() {return *m_compile_context;}
      Tvm::Context& tvm_context() {return *m_tvm_context;}
      const TvmScopePtr& scope() {return m_scope;}
      const PropertyValue& target() {return m_target;}
      TvmResult build_type(const TreePtr<Term>& value, const SourceLocation& location);
      const TvmTargetSymbol& library_symbol(const TreePtr<LibrarySymbol>& lib_global);
      PropertyValue evaluate_callback(const TreePtr<TargetCallback>& callback);
    };
    
    class TvmJitCompiler;
    
    /// Status of global compilation
    struct TvmGlobalStatus {
      enum GlobalStatusId {
        global_ready, ///< Construction not started
        global_in_progress, ///< Construction running
        global_built, ///< Construction finished
        global_built_all ///< Construction finished, including dependencies
      };
      
      GlobalStatusId status;
      Tvm::ValuePtr<Tvm::Global> lowered;
      std::set<TreePtr<ModuleGlobal> > dependencies;
      unsigned priority;
      Tvm::ValuePtr<Tvm::Function> init, fini;
      
      TvmGlobalStatus() : status(global_ready), priority(0) {}
      explicit TvmGlobalStatus(const Tvm::ValuePtr<Tvm::Global>& lowered_) : status(global_ready), lowered(lowered_), priority(0) {}
    };
    
    class SymbolNameSet {
      boost::unordered_map<std::string, unsigned> m_unique_names;
      boost::unordered_map<TreePtr<Global>, std::string> m_symbol_names;
      
    public:
      std::string unique_name(const std::string& base);
      
      const std::string& symbol_name(const TreePtr<ModuleGlobal>& name);
    };

    std::string symbol_implementation_name(const TreePtr<Interface>& interface, const PSI_STD::vector<TreePtr<Term> >& parameters);
    
    class TvmObjectCompilerBase {
      TvmJitCompiler *m_jit_compiler;
      TvmTargetScope *m_target;
      TvmScopePtr m_scope;
      TreePtr<Module> m_module;
      Tvm::Module *m_tvm_module;
      TvmGeneratedImplementationSet m_implementations;
      SymbolNameSet m_symbol_names;
      
      /** Notify a global with a matching name to one that has been requested already exists.
       * The derived class should check this matches the global used to create the symbol. */
      virtual void notify_existing_global(const TreePtr<Global>& global, const Tvm::ValuePtr<Tvm::Global>& tvm_global) = 0;
      virtual void notify_global(const TreePtr<ModuleGlobal>& global, const Tvm::ValuePtr<Tvm::Global>& tvm_global) = 0;
      virtual void notify_external_global(const TreePtr<ModuleGlobal>& global, const Tvm::ValuePtr<Tvm::Global>& tvm_global) = 0;
      virtual void notify_library_symbol(const TreePtr<LibrarySymbol>& lib_sym, const Tvm::ValuePtr<Tvm::Global>& tvm_global) = 0;
      
    public:
      TvmObjectCompilerBase(TvmJitCompiler *jit_compiler, TvmTargetScope *target, const TreePtr<Module>& module, Tvm::Module *tvm_module);
      CompileContext& compile_context() {return m_target->compile_context();}
      Tvm::Context& tvm_context() {return m_target->tvm_context();}
      TvmTargetScope& target() {return *m_target;}
      const TvmScopePtr& scope() {return m_scope;}
      TvmJitCompiler& jit_compiler() {return *m_jit_compiler;}
      Tvm::ValuePtr<Tvm::Global> get_global_bare(const TreePtr<Global>& global);
      TvmResult get_global(const TreePtr<Global>& global);
      TvmResult get_global_evaluate(const TreePtr<GlobalEvaluate>& evaluate);
      
      TvmResult check_implementation(const TreePtr<Interface>& interface, const PSI_STD::vector<TreePtr<Term> >& parameters, std::set<TreePtr<ModuleGlobal> >& dependencies);
      TvmResult get_implementation(const TreePtr<Interface>& interface, const PSI_STD::vector<TreePtr<Term> >& parameters,
                                   std::set<TreePtr<ModuleGlobal> >& dependencies, const SourceLocation& location,
                                   const TreePtr<Implementation>& maybe_implementation);

      void run_module_global(const TreePtr<ModuleGlobal>& global, TvmGlobalStatus& status);
      Tvm::Module *tvm_module() {return m_tvm_module;}
      void reset_tvm_module(Tvm::Module *module);
    };

    class TvmObjectCompiler : public TvmObjectCompilerBase {
    public:
      TvmObjectCompiler(const PropertyValue *target);
    };
    
    void tvm_object_build(Tvm::Module& module, const PSI_STD::vector<TreePtr<Global> >& globals);
    
    class TvmJitObjectCompiler : public TvmObjectCompilerBase {
      virtual void notify_existing_global(const TreePtr<Global>& global, const Tvm::ValuePtr<Tvm::Global>& tvm_global);
      virtual void notify_global(const TreePtr<ModuleGlobal>& global, const Tvm::ValuePtr<Tvm::Global>& tvm_global);
      virtual void notify_external_global(const TreePtr<ModuleGlobal>& global, const Tvm::ValuePtr<Tvm::Global>& tvm_global);
      virtual void notify_library_symbol(const TreePtr<LibrarySymbol>& lib_sym, const Tvm::ValuePtr<Tvm::Global>& tvm_global);

    public:
      TvmJitObjectCompiler(TvmJitCompiler *jit_compiler, TvmTargetScope *target, const TreePtr<Module>& module);
    };
    
    class TvmJitCompiler {
      friend class TvmJitObjectCompiler;
      
      TvmTargetScope *m_target;
      boost::shared_ptr<Tvm::Jit> m_jit;

      typedef boost::unordered_map<TreePtr<Library>, boost::shared_ptr<Platform::PlatformLibrary> > LibraryMap;
      LibraryMap m_libraries;
      
      typedef boost::unordered_map<TreePtr<ModuleGlobal>, TvmGlobalStatus> BuiltGlobalMap;
      BuiltGlobalMap m_built_globals;
      BuiltGlobalMap m_pending_built_globals;
      const TvmGlobalStatus& built_or_pending_global(const TreePtr<ModuleGlobal>& global);

      // This is const so that all uses of m_built_globals can be easily located
      const BuiltGlobalMap& built_globals() const {return m_built_globals;}
      
      std::vector<boost::shared_ptr<Tvm::Module> > m_built_modules;
      typedef std::vector<std::pair<TvmJitObjectCompiler*, boost::shared_ptr<Tvm::Module> > > CurrentModuleList;
      CurrentModuleList m_current_modules;
      
      typedef boost::unordered_map<TreePtr<LibrarySymbol>, Tvm::ValuePtr<Tvm::Global> > LibrarySymbolMap;
      LibrarySymbolMap m_library_symbols;
      LibrarySymbolMap m_pending_library_symbols;
      boost::shared_ptr<Tvm::Module> m_library_module;

      // This is const so that all uses of m_library_symbols can be easily located
      const LibrarySymbolMap& library_symbols() const {return m_library_symbols;}
      
      typedef boost::unordered_map<TreePtr<Module>, boost::shared_ptr<TvmJitObjectCompiler> > ModuleCompilerMap;
      ModuleCompilerMap m_modules;

      TvmJitObjectCompiler& module_compiler(const TreePtr<Module>& module);
      std::set<TreePtr<ModuleGlobal> > initializer_dependencies(const TreePtr<ModuleGlobal>& global, bool already_built);
      Tvm::ValuePtr<Tvm::Global> build_module_global(const TreePtr<ModuleGlobal>& global);
      Tvm::ValuePtr<Tvm::Global> build_library_symbol(const TreePtr<LibrarySymbol>& lib_sym);
      void jit_commit();
      void jit_rollback();

    public:
      TvmJitCompiler(TvmTargetScope& target, const PropertyValue& jit_configuration);
      void jit_compile(const std::vector<TreePtr<Global> >& globals);
      void *jit_get(const TreePtr<Global>& global);
      void *compile(const TreePtr<Global>& global);
      void load_library(const TreePtr<Library>& library);
    };
    
    /**
     * \brief Utility class to group a TVM context, a TvmTargetScope and a TvmJitCompiler together.
     */
    class TvmJit {
      Tvm::Context m_tvm_context;
      TvmTargetScope m_target_scope;
      TvmJitCompiler m_jit_compiler;
      
      const PropertyValue& target_configuration(CompileErrorPair& err_loc, const PropertyValue& configuration);
      const PropertyValue& jit_configuration(CompileErrorPair& err_loc, const PropertyValue& configuration);
      
    public:
      TvmJit(CompileContext& compile_context, CompileErrorPair& err_loc, const PropertyValue& configuration);
      Tvm::Context& tvm_context() {return m_tvm_context;}
      TvmJitCompiler& jit_compiler() {return m_jit_compiler;}
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
      virtual TvmResult build_global(const TreePtr<Global>& global) = 0;
      virtual TvmResult build_global_evaluate(const TreePtr<GlobalEvaluate>& global) = 0;
      virtual TvmResult build_implementation(const TreePtr<Interface>& interface, const PSI_STD::vector<TreePtr<Term> >& parameters,
                                             const SourceLocation& location, const TreePtr<Implementation>& maybe_implementation=TreePtr<Implementation>()) = 0;
      /// Load a value from memory
      virtual TvmResult load(const Tvm::ValuePtr<>& ptr, const SourceLocation& location) = 0;

      TvmResult build_implementation_value(const TreePtr<InterfaceValue>& interface, const SourceLocation& location);
    };
    
    void tvm_lower_function(TvmObjectCompilerBase& tvm_compiler, const TreePtr<Function>& function, const Tvm::ValuePtr<Tvm::Function>& output, std::set<TreePtr<ModuleGlobal> >& dependencies);
    void tvm_lower_init(TvmObjectCompilerBase& tvm_compiler, const TreePtr<Module>& module, const TreePtr<Term>& body, const Tvm::ValuePtr<Tvm::Function>& output, std::set<TreePtr<ModuleGlobal> >& dependencies);
    TvmResult tvm_lower_functional(TvmFunctionalBuilder& builder, const TreePtr<Term>& term);
    TvmResult tvm_lower_generic(TvmScope& scope, TvmFunctionalBuilder& builder, const TreePtr<GenericType>& generic);
  }
}

#endif
