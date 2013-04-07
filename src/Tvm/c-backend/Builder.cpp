#include "Builder.hpp"
#include "CModule.hpp"

#include "../AggregateLowering.hpp"
#include "../FunctionalBuilder.hpp"
#include "../Jit.hpp"

#include <sstream>
#include <boost/format.hpp>
#include <boost/ptr_container/ptr_map.hpp>

namespace Psi {
namespace Tvm {
namespace CBackend {
class CModuleCallback : public AggregateLoweringPass::TargetCallback {
  CCompiler *m_c_compiler;
  
public:
  CModuleCallback(CCompiler *c_compiler) : m_c_compiler(c_compiler) {}
  
  ValuePtr<FunctionType> lower_function_type(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionType>& ftype) {
    unsigned n_phantom = ftype->n_phantom();
    std::vector<ValuePtr<> > parameter_types;
    for (std::size_t ii = 0, ie = ftype->parameter_types().size() - n_phantom; ii != ie; ++ii)
      parameter_types.push_back(rewriter.rewrite_type(ftype->parameter_types()[ii + n_phantom]).register_type());
    ValuePtr<> result_type = rewriter.rewrite_type(ftype->result_type()).register_type();
    return FunctionalBuilder::function_type(ftype->calling_convention(), result_type, parameter_types, 0, ftype->sret(), ftype->location());
  }
  
  virtual void lower_function_call(AggregateLoweringPass::FunctionRunner& runner, const ValuePtr<Call>& term) {
    ValuePtr<FunctionType> ftype = lower_function_type(runner, term->target_function_type());

    std::vector<ValuePtr<> > parameters;
    std::size_t n_phantom = term->target_function_type()->n_phantom();
    for (std::size_t ii = 0, ie = ftype->parameter_types().size(); ii != ie; ++ii)
      parameters.push_back(runner.rewrite_value_register(term->parameters[ii + n_phantom]).value);
    
    ValuePtr<> lowered_target = runner.rewrite_value_register(term->target).value;
    ValuePtr<> cast_target = FunctionalBuilder::pointer_cast(lowered_target, ftype, term->location());
    ValuePtr<> result = runner.builder().call(cast_target, parameters, term->location());
    runner.add_mapping(term, LoweredValue::register_(runner.rewrite_type(term->type()), false, result));
  }
  
  virtual ValuePtr<Instruction> lower_return(AggregateLoweringPass::FunctionRunner& runner, const ValuePtr<>& value, const SourceLocation& location) {
    ValuePtr<> lowered = runner.rewrite_value_register(value).value;
    return runner.builder().return_(lowered, location);
  }
  
  virtual ValuePtr<Function> lower_function(AggregateLoweringPass& pass, const ValuePtr<Function>& function) {
    ValuePtr<FunctionType> ftype = lower_function_type(pass.global_rewriter(), function->function_type());
    return pass.target_module()->new_function(function->name(), ftype, function->location());
  }
  
  virtual void lower_function_entry(AggregateLoweringPass::FunctionRunner& runner, const ValuePtr<Function>& source_function, const ValuePtr<Function>& target_function) {
    Function::ParameterList::iterator ii = source_function->parameters().begin(), ie = source_function->parameters().end();
    Function::ParameterList::iterator ji = target_function->parameters().begin();
    std::advance(ii, source_function->function_type()->n_phantom());
    for (; ii != ie; ++ii, ++ji)
      runner.add_mapping(*ii, LoweredValue::register_(runner.rewrite_type((*ii)->type()), false, *ji));
  }
  
  virtual std::pair<ValuePtr<>, std::size_t> type_from_size(Context& context, std::size_t size, const SourceLocation& location) {
    PSI_NOT_IMPLEMENTED();
  }
  
  virtual std::pair<ValuePtr<>, std::size_t> type_from_alignment(Context& context, std::size_t alignment, const SourceLocation& location) {
    PSI_NOT_IMPLEMENTED();
  }
  
  virtual TypeSizeAlignment type_size_alignment(const ValuePtr<>& type) {
    const PrimitiveType *pt;
    if (ValuePtr<IntegerType> int_type = dyn_cast<IntegerType>(type)) {
      pt = &m_c_compiler->primitive_types.int_types[int_type->width()];
    } else if (ValuePtr<FloatType> float_type = dyn_cast<FloatType>(type)) {
      pt = &m_c_compiler->primitive_types.float_types[float_type->width()];
    } else if (isa<ByteType>(type) || isa<BooleanType>(type)) {
      return TypeSizeAlignment(1,1);
    } else if (isa<PointerType>(type)) {
      return TypeSizeAlignment(m_c_compiler->primitive_types.pointer_size, m_c_compiler->primitive_types.pointer_alignment);
    } else if (isa<EmptyType>(type)) {
      return TypeSizeAlignment(0,1);
    } else if (isa<BlockType>(type)) {
      return TypeSizeAlignment(0,0);
    } else {
      PSI_FAIL("unexpected type");
    }
    if (pt->name.empty())
      type->context().error_context().error_throw(type->location(), "Primitive type not supported");
    return TypeSizeAlignment(pt->size, pt->alignment);
  }
  
  virtual ValuePtr<> byte_shift(const ValuePtr<>& value, const ValuePtr<>& result_type, int shift, const SourceLocation& location) {
    PSI_NOT_IMPLEMENTED();
  }
};

CModuleBuilder::CModuleBuilder(CCompiler* c_compiler, Module& module)
: m_c_compiler(c_compiler),
m_module(&module),
m_c_module(m_c_compiler, &module.context().error_context(), module.location()),
m_type_builder(&m_c_module),
m_global_value_builder(&m_type_builder) {
}

std::string CModuleBuilder::run() {
  CModuleCallback lowering_callback(m_c_compiler);
  AggregateLoweringPass aggregate_lowering_pass(m_module, &lowering_callback);
  aggregate_lowering_pass.remove_unions = false;
  aggregate_lowering_pass.memcpy_to_bytes = true;
  aggregate_lowering_pass.update();

  std::vector<std::pair<ValuePtr<GlobalVariable>, CGlobalVariable*> > global_variables;
  std::vector<std::pair<ValuePtr<Function>, CFunction*> > functions;
  for (Module::ModuleMemberList::iterator i = m_module->members().begin(), e = m_module->members().end(); i != e; ++i) {
    const ValuePtr<Global>& term = i->second;
    ValuePtr<Global> rewritten_term = aggregate_lowering_pass.target_symbol(term);
    
    CType *type = m_type_builder.build(rewritten_term->value_type(), rewritten_term->term_type() == term_global_variable);
    const char *name = m_c_module.pool().strdup(term->name().c_str());
    
    switch (rewritten_term->term_type()) {
    case term_global_variable: {
      ValuePtr<GlobalVariable> global = value_cast<GlobalVariable>(rewritten_term);
      CGlobalVariable *c_global = m_c_module.new_global(&term->location(), type, name);
      global_variables.push_back(std::make_pair(global, c_global));
      m_global_value_builder.put(global, c_global);
      break;
    }

    case term_function: {
      ValuePtr<Function> func = value_cast<Function>(rewritten_term);
      CFunction *c_func = m_c_module.new_function(&term->location(), type, name);
      functions.push_back(std::make_pair(func, c_func));
      m_global_value_builder.put(func, c_func);
      break;
    }

    default:
      PSI_FAIL("unexpected global term type");
    }
  }
  
  for (std::vector<std::pair<ValuePtr<GlobalVariable>, CGlobalVariable*> >::const_iterator ii = global_variables.begin(), ie = global_variables.end(); ii != ie; ++ii) {
    const ValuePtr<GlobalVariable>& gv = ii->first;
    CGlobalVariable* c_gv = ii->second;
    c_gv->value = m_global_value_builder.build(gv->value());
    c_gv->is_const = gv->constant();
    c_gv->is_private = gv->private_();
    if (gv->alignment()) {
      ValuePtr<IntegerValue> int_alignment = dyn_cast<IntegerValue>(gv->alignment());
      if (!int_alignment)
        m_module->context().error_context().error_throw(gv->location(), "Alignment of global variable is not a constant");
      
      boost::optional<unsigned> opt_alignment = int_alignment->value().unsigned_value();
      if (!opt_alignment)
        m_module->context().error_context().error_throw(gv->location(), "Alignment of global variable is out of range");
      
      c_gv->alignment = *opt_alignment;
    } else {
      c_gv->alignment = 0;
    }
  }
  
  for (std::vector<std::pair<ValuePtr<Function>, CFunction*> >::const_iterator ii = functions.begin(), ie = functions.end(); ii != ie; ++ii) {
    const ValuePtr<Function>& function = ii->first;
    CFunction *c_function = ii->second;
    
    c_function->is_private = function->private_();
    
    if (!function->blocks().empty()) {
      c_function->is_external = false;
      build_function_body(ii->first, ii->second);
    }
  }
  
  std::ostringstream source;
  m_c_module.emit(source);
  return source.str();
}

namespace {
  /// Get the depth of the block in the function in terms of dominators
  unsigned block_depth(Block *block) {
    unsigned n = 0;
    for (; block; block = block->dominator().get(), ++n) {}
    return n;
  }
}

void CModuleBuilder::build_function_body(const ValuePtr<Function>& function, CFunction* c_function) {
  boost::ptr_map<ValuePtr<Block>, ValueBuilder> block_builders;
  ValueBuilder& entry_value_builder = *block_builders.insert(ValuePtr<Block>(), std::auto_ptr<ValueBuilder>(new ValueBuilder(m_global_value_builder, c_function))).first->second;
  
  // Insert function parameters into builder
  for (Function::ParameterList::iterator ii = function->parameters().begin(), ie = function->parameters().end(); ii != ie; ++ii) {
    const ValuePtr<FunctionParameter>& parameter = *ii;
    CType *type = m_type_builder.build(parameter->type());
    CExpression *c_parameter = entry_value_builder.c_builder().parameter(&parameter->location(), type);
    entry_value_builder.put(parameter, c_parameter);
  }

  // PHI nodes need to have space prepared in dominator node
  typedef std::multimap<ValuePtr<Block>, ValuePtr<Phi> > PhiMapType;
  PhiMapType phi_by_dominator;
  for (Function::BlockList::iterator ii = function->blocks().begin(), ie = function->blocks().end(); ii != ie; ++ii) {
    const ValuePtr<Block>& block = *ii;
    
    CExpression *label = entry_value_builder.c_builder().nullary(&block->location(), c_op_label, false);
    entry_value_builder.put(block, label);

    for (Block::PhiList::iterator ji = block->phi_nodes().begin(), je = block->phi_nodes().end(); ji != je; ++ji)
      phi_by_dominator.insert(std::make_pair(block->dominator(), *ji));
  }
  
  unsigned depth = 0;
  for (Function::BlockList::iterator ii = function->blocks().begin(), ie = function->blocks().end(); ii != ie; ++ii) {
    const ValuePtr<Block>& block = *ii;
    ValueBuilder& block_builder = *block_builders.insert(block, std::auto_ptr<ValueBuilder>(new ValueBuilder(block_builders.at(block->dominator()), c_function))).first->second;
    
    unsigned new_depth = block_depth(block.get());
    PSI_ASSERT(new_depth <= depth+1);
    for (unsigned ii = depth+1; ii != new_depth; --ii)
      block_builder.c_builder().nullary(&function->location(), c_op_block_end);
    
    CExpression *label = block_builder.build(block);
    c_function->instructions.append(label);
    block_builder.c_builder().nullary(&block->location(), c_op_block_begin);
    
    depth = new_depth;
    
    for (Block::InstructionList::iterator ji = block->instructions().begin(), je = block->instructions().end(); ji != je; ++ji)
      block_builder.build(*ji);
    
    for (PhiMapType::const_iterator ji = phi_by_dominator.lower_bound(block), je = phi_by_dominator.upper_bound(block); ji != je; ++ji) {
      CType *type = m_type_builder.build(ji->second->type());
      CExpression *phi_value = block_builder.c_builder().declare(&ji->second->location(), type, c_op_declare, NULL, 0);
      block_builder.put(ji->second, phi_value);
    }
  }
  
  for (; depth > 0; --depth)
    entry_value_builder.c_builder().nullary(&function->location(), c_op_block_end);
}

CJit::CJit(const boost::shared_ptr<JitFactory>& factory, const boost::shared_ptr<CCompiler>& compiler)
: Jit(factory), m_compiler(compiler) {
}

CJit::~CJit() {
}

void CJit::add_module(Module *module) {
  std::string source = CModuleBuilder(m_compiler.get(), *module).run();
  JitModule jm;
  jm.path = boost::make_shared<Platform::TemporaryPath>();
  m_compiler->compile_library(factory()->error_handler().context().bind(module->location()), jm.path->path(), source);
  try {
    jm.library = Platform::load_library(jm.path->path());
  } catch (Platform::PlatformError& ex) {
    factory()->error_handler().context().error_throw(module->location(), ex.what());
  }
  m_modules.insert(std::make_pair(module, jm));
}

void CJit::remove_module(Module *module) {
  ModuleMap::iterator it = m_modules.find(module);
  if (it == m_modules.end())
    factory()->error_handler().context().error_throw(module->location(), "Module cannot be removed from this JIT because it has not been added");
  m_modules.erase(it);
}

void* CJit::get_symbol(const ValuePtr<Global>& symbol) {
  ModuleMap::iterator it = m_modules.find(symbol->module());
  if (it == m_modules.end())
    factory()->error_handler().context().error_throw(symbol->location(), "Module has not been JIT compiled");
  
  boost::optional<void*> ptr = it->second.library->symbol(symbol->name());
  if (!ptr)
    factory()->error_handler().context().error_throw(symbol->location(), boost::format("Symbol missing from JIT compiled library: %s") % symbol->name());
  
  return *ptr;
}
}
}
}

extern "C" PSI_ATTRIBUTE((PSI_EXPORT)) void tvm_jit_new(const boost::shared_ptr<Psi::Tvm::JitFactory>& factory, boost::shared_ptr<Psi::Tvm::Jit>& result) {
  boost::shared_ptr<Psi::Tvm::CBackend::CCompiler> compiler = Psi::Tvm::CBackend::detect_c_compiler(factory->error_handler());
  result = boost::make_shared<Psi::Tvm::CBackend::CJit>(factory, compiler);
}
