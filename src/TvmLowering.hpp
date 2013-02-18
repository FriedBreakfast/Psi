#ifndef HPP_PSI_TVMLOWERING
#define HPP_PSI_TVMLOWERING

#include "Tree.hpp"

#include "Tvm/Core.hpp"
#include "Tvm/Jit.hpp"

namespace Psi {
  namespace Compiler {
    enum TvmStorage {
      /// \brief Functional value.
      tvm_storage_functional,
      /// \brief Normal object allocated on the stack.
      tvm_storage_stack,
      /// \brief Reference to L-value.
      tvm_storage_lvalue_ref,
      /**
      * \brief Reference to R-value.
      * 
      * Note that this only behaves as an R-value reference when it is about
      * to go out of scope, otherwise it behaves as an L-value.
      */
      tvm_storage_rvalue_ref,
      /**
      * \brief Indicates this result cannot be evaluated.
      * 
      * This is not a storage class, but indicates that the result of the
      * expression being described can never be successfully evaluated.
      */
      tvm_storage_bottom
    };

    /**
     * \brief Result of generating code to compute a variable.
     */
    class TvmResult {
      TreePtr<Term> m_type;
      TvmStorage m_storage;
      Tvm::ValuePtr<> m_value, m_upref;
      bool m_primitive, m_register, m_constant;
      
      TvmResult(const TreePtr<Term>& type, TvmStorage storage, const Tvm::ValuePtr<>& value, const Tvm::ValuePtr<>& upref, bool primitive, bool register_)
      : m_type(type), m_storage(storage), m_value(value), m_upref(upref), m_primitive(primitive), m_register(register_) {}

    public:
      TvmResult() : m_storage(tvm_storage_bottom) {}
      
      static TvmResult bottom()
      {return TvmResult(TreePtr<Term>(), tvm_storage_bottom, Tvm::ValuePtr<>(), Tvm::ValuePtr<>(), false, false);}
      static TvmResult on_stack(const TreePtr<Term>& type, const Tvm::ValuePtr<>& value)
      {return TvmResult(type, tvm_storage_stack, value, Tvm::ValuePtr<>(), false, false);}
      static TvmResult in_register(const TreePtr<Term>& type, TvmStorage storage, const Tvm::ValuePtr<>& value)
      {return TvmResult(type, storage, value, Tvm::ValuePtr<>(), false, false);}
      static TvmResult functional(const TreePtr<Term>& type, const Tvm::ValuePtr<>& value, bool constant)
      {return TvmResult(type, tvm_storage_functional, value, Tvm::ValuePtr<>(), false, constant);}
      static TvmResult type(const TreePtr<Term>& type, const Tvm::ValuePtr<>& value, bool primitive, bool register_, const Tvm::ValuePtr<>& upref=Tvm::ValuePtr<>())
      {return TvmResult(type, tvm_storage_functional, value, upref, primitive, register_);}

      const TreePtr<Term>& type() const {return m_type;}
      /// \brief Storage type of this result
      TvmStorage storage() const {return m_storage;}
      /// \brief Value of this variable if it is not stored on the stack (i.e. functional or reference)
      const Tvm::ValuePtr<>& value() const {return m_value;}
      /// \brief If this is a type, the upward reference associated with this type.
      const Tvm::ValuePtr<>& upref() const {return m_upref;}
      /// \brief If this is a type, this is true if the type is primitive.
      bool primitive() const {return m_primitive;}
      /// \brief If this type can be stored in a register, or if this is a value, whether values whose size is based on it can be stored in a register.
      bool register_() const {return m_register;}
    };
    
    /**
     * \brief Result of building generic type.
     */
    struct TvmGenericResult {
      Tvm::ValuePtr<Tvm::RecursiveType> generic;
      /// Whether this type is primitive regardless of its parameters
      int primitive_mode;
    };
    
    class TvmFunctionalBuilder;
    
    class TvmFunctionalBuilderCallback {
    public:
      virtual TvmResult build_hook(TvmFunctionalBuilder& builder, const TreePtr<Term>& term) = 0;
      virtual TvmResult build_define_hook(TvmFunctionalBuilder& builder, const TreePtr<GlobalDefine>& define) = 0;
      virtual TvmGenericResult build_generic_hook(TvmFunctionalBuilder& builder, const TreePtr<GenericType>& generic) = 0;
      virtual Tvm::ValuePtr<> load_hook(TvmFunctionalBuilder& builder, const Tvm::ValuePtr<>& ptr, const SourceLocation& location) = 0;
    };

    /**
     * \brief Utility class for building functional values.
     * 
     * This is used by both the global and function compiler.
     */
    class TvmFunctionalBuilder {
      CompileContext *m_compile_context;
      Tvm::Context *m_tvm_context;
      TvmFunctionalBuilderCallback *m_callback;
      typedef boost::unordered_map<TreePtr<Term>, TvmResult> FunctionalValueMap;
      FunctionalValueMap m_values;
      
      TvmResult build_type_internal(const TreePtr<Type>& type);
      TvmResult build_primitive_type(const TreePtr<PrimitiveType>& type);
      TvmResult build_function_type(const TreePtr<FunctionType>& type);
      TvmResult build_type_instance(const TreePtr<TypeInstance>& type);
      TvmResult build_constructor(const TreePtr<Constructor>& value);
      TvmResult build_other(const TreePtr<Functional>& value);
      bool check_constant(const TreePtr<Term>& value);
      std::pair<bool, bool> check_primitive_register(const TreePtr<Term>& type);

    public:
      TvmFunctionalBuilder(CompileContext *compile_context, Tvm::Context *tvm_context, TvmFunctionalBuilderCallback *callback);
      CompileContext& compile_context() {return *m_compile_context;}
      Tvm::Context& tvm_context() {return *m_tvm_context;}
      TvmResult build(const TreePtr<Term>& term);
      TvmResult build_type(const TreePtr<Term>& term);
      TvmResult build_value(const TreePtr<Term>& term);
      bool is_primitive(const TreePtr<Term>& term);
      bool is_register(const TreePtr<Term>& term);
    };
    
    /**
     * \brief Compilation context component which handles TVM translation.
     */
    class TvmCompiler {
      typedef boost::unordered_map<TreePtr<ModuleGlobal>, Tvm::ValuePtr<Tvm::Global> > ModuleGlobalMap;
      typedef boost::unordered_map<TreePtr<LibrarySymbol>, Tvm::ValuePtr<Tvm::Global> > ModuleLibrarySymbolMap;
      
      struct TvmModule {
        bool jit_current;
        boost::shared_ptr<Tvm::Module> module;
        ModuleGlobalMap symbols;
        ModuleLibrarySymbolMap library_symbols;
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
      boost::shared_ptr<Tvm::Jit> m_jit;
      boost::shared_ptr<Tvm::Module> m_library_module;

      PropertyValue m_local_target;
      
      typedef boost::unordered_map<TreePtr<Module>, TvmModule> ModuleMap;
      ModuleMap m_modules;
      TvmModule& get_module(const TreePtr<Module>& module);
      
      typedef boost::unordered_map<TreePtr<Library>, TvmPlatformLibrary> LibraryMap;
      LibraryMap m_libraries;
      TvmPlatformLibrary& get_platform_library(const TreePtr<Library>& lib);
      
      typedef boost::unordered_map<TreePtr<GenericType>, TvmGenericResult> GenericTypeMap;
      GenericTypeMap m_generics;

      /**
       * \brief Globals which are currently being built.
       * 
       * This prevents certain cases of dependency recursion.
       */
      std::set<TreePtr<ModuleGlobal> > m_in_progress_globals;
      
      class FunctionalBuilderCallback;
      
      Tvm::ValuePtr<Tvm::Global> build_module_global(const TreePtr<ModuleGlobal>& global);
      Tvm::ValuePtr<Tvm::Global> build_library_symbol(const TreePtr<LibrarySymbol>& lib_global);
      void build_global_group(const std::vector<TreePtr<ModuleGlobal> >& group);
      
      TvmResult build_type(const TreePtr<Term>& type);
      bool is_primitive(const TreePtr<Term>& type);
      
    public:
      TvmCompiler(CompileContext *compile_context);
      ~TvmCompiler();
      
      CompileContext& compile_context() {return *m_compile_context;}
      
      void add_compiled_module(const TreePtr<Module>& module, const boost::shared_ptr<Platform::PlatformLibrary>& lib);

      Tvm::ValuePtr<Tvm::Global> build_global(const TreePtr<Global>& global, const TreePtr<Module>& module);
      Tvm::ValuePtr<Tvm::Global> build_global_jit(const TreePtr<Global>& global);
      TvmResult build(const TreePtr<Term>& value, const TreePtr<Module>& module);
      TvmGenericResult build_generic(const TreePtr<GenericType>& generic);
      void* jit_compile(const TreePtr<Global>& global);
      
      static std::string mangle_name(const LogicalSourceLocationPtr& location);
    };

    void tvm_lower_function(TvmCompiler& tvm_compiler, const TreePtr<Function>& function, const Tvm::ValuePtr<Tvm::Function>& output);
  }
}

#endif
