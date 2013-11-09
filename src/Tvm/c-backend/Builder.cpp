#include "Builder.hpp"
#include "CModule.hpp"

#include "../AggregateLowering.hpp"
#include "../FunctionalBuilder.hpp"

#include <list>
#include <iostream>
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
  
  virtual TypeSizeAlignment type_size_alignment(const ValuePtr<>& type, const SourceLocation& PSI_UNUSED(location)) {
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
  
  std::map<ValuePtr<Global>, unsigned> constructor_priorities(m_module->constructors().begin(), m_module->constructors().end());
  std::map<ValuePtr<Global>, unsigned> destructor_priorities(m_module->destructors().begin(), m_module->destructors().end());

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

      std::map<ValuePtr<Global>, unsigned>::const_iterator jt = constructor_priorities.find(term);
      if (jt != constructor_priorities.end())
        c_func->constructor_priority = jt->second;

      jt = destructor_priorities.find(term);
      if (jt != destructor_priorities.end())
        c_func->destructor_priority = jt->second;
      
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
    c_gv->linkage = gv->linkage();
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
    
    c_function->linkage = function->linkage();
    
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
  
  typedef std::multimap<ValuePtr<Block>, ValuePtr<Block> > BlockDominatorMap;
  
  void depth_first_block_order(std::vector<ValuePtr<Block> >& order,
                               const ValuePtr<Block>& block,
                               const BlockDominatorMap& blocks_by_dominator) {
    std::pair<BlockDominatorMap::const_iterator, BlockDominatorMap::const_iterator> range = blocks_by_dominator.equal_range(block);
    for (BlockDominatorMap::const_iterator ii = range.first; ii != range.second; ++ii) {
      order.push_back(ii->second);
      depth_first_block_order(order, ii->second, blocks_by_dominator);
    }
  }
}

void CModuleBuilder::build_function_body(const ValuePtr<Function>& function, CFunction* c_function) {
  boost::ptr_map<ValuePtr<Block>, ValueBuilder> block_builders;
  ValueBuilder& entry_value_builder = *block_builders.insert(ValuePtr<Block>(), std::auto_ptr<ValueBuilder>(new ValueBuilder(m_global_value_builder, c_function))).first->second;
  
  // Need to sort blocks by dominator: the order of block emission must be
  // such that if block A occurs between block B and C, and B dominates C,
  // then B dominates A. This is due to C variable scoping rules.
  BlockDominatorMap blocks_by_dominator;
  for (Function::BlockList::iterator ii = function->blocks().begin(), ie = function->blocks().end(); ii != ie; ++ii) {
    const ValuePtr<Block>& block = *ii;
    blocks_by_dominator.insert(std::make_pair(block->dominator(), block));
  }
  
  std::vector<ValuePtr<Block> > block_order;
  depth_first_block_order(block_order, ValuePtr<Block>(), blocks_by_dominator);
  PSI_ASSERT(block_order.size() == function->blocks().size());
  
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
  for (std::vector<ValuePtr<Block> >::iterator ii = block_order.begin(), ie = block_order.end(); ii != ie; ++ii) {
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
    
    for (PhiMapType::const_iterator ji = phi_by_dominator.lower_bound(block), je = phi_by_dominator.upper_bound(block); ji != je; ++ji) {
      if (!m_type_builder.is_void_type(ji->second->type())) {
        CType *type = m_type_builder.build(ji->second->type());
        CExpression *phi_value = block_builder.c_builder().declare(&ji->second->location(), type, c_op_declare, NULL, 0);
        block_builder.phi_put(ji->second, phi_value);
      }
    }
    
    for (Block::PhiList::iterator ji = block->phi_nodes().begin(), je = block->phi_nodes().end(); ji != je; ++ji) {
      const ValuePtr<Phi>& phi = *ji;
      if (!m_type_builder.is_void_type(phi->type())) {
        CType *type = m_type_builder.build(phi->type());
        CExpression *temporary_value = block_builder.phi_get(*ji);
        CExpression *phi_value = block_builder.c_builder().declare(&phi->location(), type, c_op_declare, temporary_value, 0);
        block_builder.put(*ji, phi_value);
      }
    }

    for (Block::InstructionList::iterator ji = block->instructions().begin(), je = block->instructions().end(); ji != je; ++ji)
      block_builder.build(*ji);
  }
  
  for (; depth > 0; --depth)
    entry_value_builder.c_builder().nullary(&function->location(), c_op_block_end);
}

CJit::CJit(CompileErrorContext& error_context, const boost::shared_ptr<CCompiler>& compiler, const Psi::PropertyValue& configuration)
: m_error_context(&error_context), m_compiler(compiler) {
  m_dump_code = configuration.path_bool("jit_dump");
}

CJit::~CJit() {
}

void CJit::destroy() {
  delete this;
}

void CJit::add_module(Module *module) {
  std::string source = CModuleBuilder(m_compiler.get(), *module).run();
  if (m_dump_code)
    std::cerr << source;
  boost::shared_ptr<Platform::PlatformLibrary> lib = m_compiler->compile_load_library(error_context().bind(module->location()), source);
  m_modules.insert(std::make_pair(module, lib));
}

void CJit::remove_module(Module *module) {
  ModuleMap::iterator it = m_modules.find(module);
  if (it == m_modules.end())
    error_context().error_throw(module->location(), "Module cannot be removed from this JIT because it has not been added");
  m_modules.erase(it);
}

void* CJit::get_symbol(const ValuePtr<Global>& symbol) {
  ModuleMap::iterator it = m_modules.find(symbol->module());
  if (it == m_modules.end())
    error_context().error_throw(symbol->location(), "Module has not been JIT compiled");
  
  boost::optional<void*> ptr = it->second->symbol(symbol->name());
  if (!ptr)
    error_context().error_throw(symbol->location(), boost::format("Symbol missing from JIT compiled library: %s") % symbol->name());
  
  return *ptr;
}
}
}
}

PSI_TVM_JIT_EXPORT(c, error_handler, configuration) {
  boost::shared_ptr<Psi::Tvm::CBackend::CCompiler> compiler = Psi::Tvm::CBackend::detect_c_compiler(error_handler, configuration);
  return new Psi::Tvm::CBackend::CJit(error_handler.context(), compiler, configuration);
}
