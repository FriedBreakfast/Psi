#include "TvmLowering.hpp"

#include "Tvm/FunctionalBuilder.hpp"
#include "Tvm/Function.hpp"
#include "Tvm/Recursive.hpp"

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

namespace Psi {
namespace Compiler {
TvmFunctionalBuilder::TvmFunctionalBuilder(Tvm::Context* context, TvmFunctionalBuilderCallback* callback)
: m_context(context), m_callback(callback) {
}

/**
 * \brief Convert a functional operation to TVM.
 */
TvmFunctional<> TvmFunctionalBuilder::build(const TreePtr<Term>& value) {
  FunctionalValueMap::iterator ii = m_values.find(value);
  if (ii != m_values.end())
    return ii->second;
  
  TvmFunctional<> result;
  if (TreePtr<Type> type = dyn_treeptr_cast<Type>(value)) {
    result = build_type(type);
  } else if (TreePtr<TypeInstance> type_inst = dyn_treeptr_cast<TypeInstance>(value)) {
    result = build_type_instance(type_inst);
  } else {
    result = m_callback->build_hook(value);
  }
  
  m_values.insert(std::make_pair(value, result));
  return result;
}

/**
 * \brief Shorthand for build(x).value
 */
Tvm::ValuePtr<> TvmFunctionalBuilder::build_value(const TreePtr<Term>& term) {
  return build(term).value;
}

/**
 * \brief Check if a type is primitive.
 * 
 * Note that this does not convert the type to TVM, although it will use existing results.
 * The behaviour of this function must be consistent with \c build_type.
 */
bool TvmFunctionalBuilder::is_primitive(const TreePtr<Term>& type) {
  FunctionalValueMap::iterator ii = m_values.find(type);
  if (ii != m_values.end())
    return ii->second.primitive;

  if (tree_isa<EmptyType>(type) || tree_isa<PointerType>(type) || tree_isa<UnionType>(type) || tree_isa<PrimitiveType>(type) || tree_isa<BottomType>(type)) {
    return true;
  } else if (tree_isa<FunctionType>(type) || tree_isa<Parameter>(type)) {
    return false;
  } else if (TreePtr<ArrayType> array_ty = dyn_treeptr_cast<ArrayType>(type)) {
    return is_primitive(array_ty->element_type);
  } else if (TreePtr<StructType> struct_ty = dyn_treeptr_cast<StructType>(type)) {
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = struct_ty->members.begin(), ie = struct_ty->members.end(); ii != ie; ++ii) {
      if (!is_primitive(*ii))
        return false;
    }
    return true;
  } else {
    PSI_FAIL("Unknown type subclass");
  }
}

/**
 * \brief Convert a type to TVM.
 */
TvmFunctional<> TvmFunctionalBuilder::build_type(const TreePtr<Type>& type) {
  if (TreePtr<ArrayType> array_ty = dyn_treeptr_cast<ArrayType>(type)) {
    TvmFunctional<> element = build(array_ty->element_type);
    Tvm::ValuePtr<> length = build_value(array_ty->length);
    return TvmFunctional<>(Tvm::FunctionalBuilder::array_type(element.value, length, type->location()), element.primitive);
  } else if (tree_isa<EmptyType>(type)) {
    return TvmFunctional<>(Tvm::FunctionalBuilder::empty_type(context(), type->location()), true);
  } else if (TreePtr<PointerType> pointer_ty = dyn_treeptr_cast<PointerType>(type)) {
    Tvm::ValuePtr<> target = build_value(pointer_ty->target_type);
    return TvmFunctional<>(Tvm::FunctionalBuilder::pointer_type(target, type->location()), true);
  } else if (TreePtr<StructType> struct_ty = dyn_treeptr_cast<StructType>(type)) {
    bool primitive = true;
    std::vector<Tvm::ValuePtr<> > members;
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = struct_ty->members.begin(), ie = struct_ty->members.end(); ii != ie; ++ii) {
      TvmFunctional<> f = build(*ii);
      members.push_back(f.value);
      primitive = primitive && f.primitive;
    }
    return TvmFunctional<>(Tvm::FunctionalBuilder::struct_type(context(), members, type->location()), primitive);
  } else if (TreePtr<UnionType> union_ty = dyn_treeptr_cast<UnionType>(type)) {
    PSI_STD::vector<Tvm::ValuePtr<> > members;
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = struct_ty->members.begin(), ie = struct_ty->members.end(); ii != ie; ++ii)
      members.push_back(build_value(*ii));
    // Unions are always primitive because the user is required to handle copy semantics manually.
    return TvmFunctional<>(Tvm::FunctionalBuilder::union_type(context(), members, type->location()), true);
  } else if (TreePtr<PrimitiveType> primitive_ty = dyn_treeptr_cast<PrimitiveType>(type)) {
    return build_primitive_type(primitive_ty);
  } else if (TreePtr<FunctionType> function_ty = dyn_treeptr_cast<FunctionType>(type)) {
    return build_function_type(function_ty);
  } else if (tree_isa<BottomType>(type)) {
    type.compile_context().error_throw(type.location(), "Bottom type cannot be lowered to TVM");
  } else {
    PSI_FAIL(si_vptr(type.get())->classname);
  }
}

namespace {
  std::vector<std::string> string_split(const std::string& s, char c) {
    std::vector<std::string> parts;
    std::string::size_type pos = 0;
    while (true) {
      std::string::size_type next = s.find(c, pos);
      if (next == std::string::npos) {
        parts.push_back(s.substr(pos));
        break;
      } else {
        parts.push_back(s.substr(pos, next-pos));
        pos = next + 1;
      }
    }
    return parts;
  }
  
  Tvm::ValuePtr<> build_int_type(Tvm::Context& context, const SourceLocation& location, bool is_signed, const std::vector<std::string>& parts) {
    if (parts.size() != 3)
      return Tvm::ValuePtr<>();
    
    unsigned bits;
    try {
      bits = boost::lexical_cast<unsigned>(parts[2]);
    } catch (...) {
      return Tvm::ValuePtr<>();
    }
    
    Tvm::IntegerType::Width width;
    switch (bits) {
    case 8: width = Tvm::IntegerType::i8; break;
    case 16: width = Tvm::IntegerType::i16; break;
    case 32: width = Tvm::IntegerType::i32; break;
    case 64: width = Tvm::IntegerType::i64; break;
    case 128: width = Tvm::IntegerType::i128; break;
    default: return Tvm::ValuePtr<>();
    }
    
    return Tvm::FunctionalBuilder::int_type(context, width, is_signed, location);
  }
}

TvmFunctional<> TvmFunctionalBuilder::build_primitive_type(const TreePtr<PrimitiveType>& type) {
  Tvm::ValuePtr<> tvm_type;
  
  std::vector<std::string> parts = string_split(type->name, '.');
  if (parts[0] == "core") {
    if (parts.size() >= 1) {
      if (parts[1] == "int")
        tvm_type = build_int_type(context(), type.location(), true, parts);
      else if (parts[1] == "uint")
        tvm_type = build_int_type(context(), type.location(), false, parts);
    }
  }
  
  if (!tvm_type)
    type.compile_context().error_throw(type.location(), boost::format("Unknown primitive type '%s'") % type->name);
  
  return TvmFunctional<>(tvm_type, true);
}

TvmFunctional<> TvmFunctionalBuilder::build_function_type(const TreePtr<FunctionType>& type) {
  std::vector<Tvm::ValuePtr<> > parameter_types;
  for (PSI_STD::vector<FunctionParameterType>::const_iterator ii = type->parameter_types.begin(), ie = type->parameter_types.end(); ii != ie; ++ii) {
    TvmFunctional<> parameter = build(ii->type);
    
    bool by_value;
    switch (ii->mode) {
    case parameter_mode_input:
    case parameter_mode_rvalue:
      by_value = parameter.primitive;
      break;
      
    case parameter_mode_output:
    case parameter_mode_io:
      by_value  = false;
      break;
      
    case parameter_mode_functional:
      by_value = true;
      if (!parameter.primitive)
        type.compile_context().error_throw(type.location(), "Functional parameter does not have a primitive type");
      break;
      
    default: PSI_FAIL("Unrecognised function parameter mode");
    }
    
    parameter_types.push_back(by_value ? parameter.value : Tvm::FunctionalBuilder::pointer_type(parameter.value, type.location()));
  }
  
  TvmFunctional<> result = build(type->result_type);
  Tvm::ValuePtr<> result_type;
  bool sret;
  switch (type->result_mode) {
  case result_mode_by_value:
    if (result.primitive) {
      sret = false;
      result_type = result.value;
    } else {
      sret = true;
      result_type = Tvm::FunctionalBuilder::pointer_type(result.value, type.location());
    }
    break;
    
  case result_mode_functional:
    sret = false;
    result_type = result.value;
    break;
    
  case result_mode_rvalue:
  case result_mode_lvalue:
    sret = false;
    result_type = Tvm::FunctionalBuilder::pointer_type(result.value, type.location());
    break;
    
  default: PSI_FAIL("Unrecognised function result mode");
  }
  
  // Function types are not primitive because there is a function cannot be copied
  return TvmFunctional<>(Tvm::FunctionalBuilder::function_type(Tvm::cconv_c, result_type, parameter_types, 0, sret, type.location()), false);
}

TvmFunctional<> TvmFunctionalBuilder::build_type_instance(const TreePtr<TypeInstance>& type) {
  TvmFunctional<Tvm::RecursiveType> recursive = m_callback->build_generic_hook(type->generic);  
  std::vector<Tvm::ValuePtr<> > parameters;
  for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = type->parameters.begin(), ie = type->parameters.end(); ii != ie; ++ii)
    parameters.push_back(build_value(*ii));
  Tvm::ValuePtr<> inst = Tvm::FunctionalBuilder::apply(recursive.value, parameters, type->location());
  return TvmFunctional<>(inst, recursive.primitive);
}

TvmCompiler::TvmCompiler(CompileContext *compile_context)
: m_compile_context(compile_context),
m_functional_builder(&m_tvm_context, this) {
  boost::shared_ptr<Tvm::JitFactory> factory = Tvm::JitFactory::get("llvm");
  m_jit = factory->create_jit();
}

TvmCompiler::~TvmCompiler() {
}

TvmFunctional<> TvmCompiler::build_hook(const TreePtr<Term>& value) {
  if (TreePtr<Global> global = dyn_treeptr_cast<Global>(value))
    return TvmFunctional<>(build_global(global), true);
  
  compile_context().error_throw(value->location(), "Value is required in a global context but is not a global value.");
}

TvmFunctional<Tvm::RecursiveType> TvmCompiler::build_generic_hook(const TreePtr<GenericType>& generic) {
  return build_generic(generic);
}

/**
 * \brief Build a global or constant value.
 */
TvmFunctional<> Psi::Compiler::TvmCompiler::build(const TreePtr<Term>& value) {
  return m_functional_builder.build(value);
}

/**
 * \brief Load a library.
 */
boost::shared_ptr<Platform::PlatformLibrary> TvmCompiler::load_library(const TreePtr<Library>& lib) {
  LibraryMap::iterator lib_it = m_libraries.find(lib);
  if (lib_it != m_libraries.end())
    return lib_it->second;
  
  PropertyValue pv = lib->callback->evaluate(m_local_target, m_local_target);
  boost::shared_ptr<Platform::PlatformLibrary> sys_lib = Platform::load_library(pv);
  m_libraries.insert(std::make_pair(lib, sys_lib));
  
  return sys_lib;
}

std::string TvmCompiler::mangle_name(const LogicalSourceLocationPtr& location) {
  std::ostringstream ss;
  ss << "_Y";
  std::vector<LogicalSourceLocationPtr> ancestors;
  for (LogicalSourceLocationPtr ptr = location; ptr->parent(); ptr = ptr->parent())
    ancestors.push_back(ptr);
  for (std::vector<LogicalSourceLocationPtr>::reverse_iterator ii = ancestors.rbegin(), ie = ancestors.rend(); ii != ie; ++ii) {
    const String& name = (*ii)->name();
    ss << name.length() << name;
  }
  return ss.str();
}

/**
 * \brief Create a Tvm::Global from a Global.
 */
Tvm::ValuePtr<Tvm::Global> TvmCompiler::build_global(const TreePtr<Global>& global) {
  if (TreePtr<ModuleGlobal> mod_global = dyn_treeptr_cast<ModuleGlobal>(global)) {
    TreePtr<Module> module = mod_global->module;
    if (TreePtr<ExternalGlobal> ext_global = dyn_treeptr_cast<ExternalGlobal>(global)) {
      PSI_NOT_IMPLEMENTED();
    } else {
      TvmModule& tvm_module = m_modules[module];
      if (!tvm_module.module)
        tvm_module.module.reset(new Tvm::Module(&m_tvm_context, module->name, module->location()));
      
      ModuleGlobalMap::iterator it = tvm_module.symbols.find(mod_global);
      if (it != tvm_module.symbols.end())
        return it->second;
      
      std::string symbol_name = mangle_name(global->location().logical);
      
      Tvm::ValuePtr<> type = m_functional_builder.build_value(mod_global->type);

      if (TreePtr<Function> function = dyn_treeptr_cast<Function>(global)) {
        Tvm::ValuePtr<Tvm::FunctionType> tvm_ftype = Tvm::dyn_cast<Tvm::FunctionType>(type);
        if (!tvm_ftype)
          compile_context().error_throw(function->location(), "Type of function is not a function type");
        Tvm::ValuePtr<Tvm::Function> tvm_func = tvm_module.module->new_function(symbol_name, tvm_ftype, function->location());
        tvm_module.symbols.insert(std::make_pair(function, tvm_func));
        tvm_lower_function(*this, function, tvm_func);
        return tvm_func;
      } else if (TreePtr<GlobalVariable> global_var = dyn_treeptr_cast<GlobalVariable>(global)) {
        Tvm::ValuePtr<Tvm::GlobalVariable> tvm_gvar = tvm_module.module->new_global_variable(symbol_name, type, global_var->location());
        tvm_module.symbols.insert(std::make_pair(global_var, tvm_gvar));
        PSI_NOT_IMPLEMENTED();
        return tvm_gvar;
      } else {
        PSI_FAIL("Unknown module global type");
      }
    }
  } else if (TreePtr<LibrarySymbol> lib_global = dyn_treeptr_cast<LibrarySymbol>(global)) {
    PSI_NOT_IMPLEMENTED();
  } else {
    PSI_FAIL("Unknown global type");
  }
}

/**
 * \brief Just-in-time compile a symbol.
 */
void* TvmCompiler::jit_compile(const TreePtr<Global>& global) {
  Tvm::ValuePtr<Tvm::Global> built = build_global(global);
  return m_jit->get_symbol(built);
}

namespace {
  class GenericTypeCallback : public TvmFunctionalBuilderCallback {
  public:
    typedef boost::unordered_map<TreePtr<Anonymous>, Tvm::ValuePtr<> > AnonymousMapType;
    
  private:
    TvmCompiler *m_self;
    const AnonymousMapType *m_parameters;
    
  public:
    GenericTypeCallback(TvmCompiler *self, const AnonymousMapType *parameters)
    : m_self(self), m_parameters(parameters) {}
    
    virtual TvmFunctional<> build_hook(const TreePtr<Term>& term) {
      if (TreePtr<Anonymous> anon = dyn_treeptr_cast<Anonymous>(term)) {
        AnonymousMapType::const_iterator ii = m_parameters->find(anon);
        if (ii == m_parameters->end())
          m_self->compile_context().error_throw(term->location(), "Unrecognised anonymous parameter");
        return ii->second;
      } else {
        m_self->compile_context().error_throw(term->location(), boost::format("Unsupported term type in generic parameter: %s") % si_vptr(term.get())->classname);
      }
    }
    
    virtual TvmFunctional<Tvm::RecursiveType> build_generic_hook(const TreePtr<GenericType>& generic) {
      return m_self->build_generic(generic);
    }
  };
}

/**
 * \brief Lower a generic type.
 */
TvmFunctional<Tvm::RecursiveType> TvmCompiler::build_generic(const TreePtr<GenericType>& generic) {
  GenericTypeMap::iterator gen_it = m_generics.find(generic);
  if (gen_it != m_generics.end())
    return gen_it->second;
  
  PSI_STD::vector<TreePtr<Term> > anonymous_list;
  Tvm::RecursiveType::ParameterList parameters;
  GenericTypeCallback::AnonymousMapType parameter_map;
  GenericTypeCallback type_callback(this, &parameter_map);
  for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = generic->pattern.begin(), ie = generic->pattern.end(); ii != ie; ++ii) {
    // Need to rewrite parameter to anonymous to build lowered type with RecursiveParameter
    // Would've made more seense if I'd built the two systems with a more similar parameter convention.
    TreePtr<Term> rewrite_type = (*ii)->type->specialize(generic->location(), anonymous_list);
    TreePtr<Anonymous> rewrite_anon(new Anonymous(rewrite_type, rewrite_type->location()));
    anonymous_list.push_back(rewrite_anon);
    Tvm::ValuePtr<> type = type_callback.build_hook((*ii)->type).value;
    Tvm::ValuePtr<Tvm::RecursiveParameter> param = Tvm::RecursiveParameter::create(type, false, ii->location());
    parameter_map.insert(std::make_pair(rewrite_anon, param));
    parameters.push_back(*param);
  }
  
  Tvm::ValuePtr<Tvm::RecursiveType> recursive =
    Tvm::RecursiveType::create(Tvm::FunctionalBuilder::type_type(m_tvm_context, generic->location()), parameters, NULL, generic.location());
  TvmFunctional<Tvm::RecursiveType> result(recursive, m_functional_builder.is_primitive(generic->member_type));
  m_generics.insert(std::make_pair(generic, result));
  
  return result;
}
}
}
