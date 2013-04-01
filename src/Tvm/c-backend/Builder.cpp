#include "Builder.hpp"
#include "CModule.hpp"

#include "../AggregateLowering.hpp"
#include "../FunctionalBuilder.hpp"

#include <sstream>

namespace Psi {
namespace Tvm {
namespace CBackend {
class CModuleCallback : public AggregateLoweringPass::TargetCallback {
public:
  ValuePtr<FunctionType> lower_function_type(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionType>& ftype) {
    unsigned n_phantom = ftype->n_phantom();
    std::vector<ValuePtr<> > parameter_types;
    for (std::size_t ii = 0, ie = ftype->parameter_types().size() - n_phantom; ii != ie; ++ii)
      parameter_types.push_back(rewriter.rewrite_type(ftype->parameter_types()[ii + n_phantom]).register_type());
    ValuePtr<> result_type = rewriter.rewrite_type(ftype->result_type()).register_type();
    return FunctionalBuilder::function_type(ftype->calling_convention(), result_type, parameter_types, 0, ftype->sret(), ftype->location());
  }
  
  virtual void lower_function_call(AggregateLoweringPass::FunctionRunner& runner, const ValuePtr<Call>& term) {
    ValuePtr<FunctionType> ftype = lower_function_type(runner, term->function()->function_type());

    std::vector<ValuePtr<> > parameters;
    std::size_t n_phantom = term->target_function_type()->n_phantom();
    for (std::size_t ii = 0, ie = ftype->parameter_types().size(); ii != ie; ++ii)
      parameters.push_back(runner.rewrite_value_register(ftype->parameter_types()[ii + n_phantom]).value);
    
    ValuePtr<> lowered_target = runner.rewrite_value_register(term->target).value;
    ValuePtr<> cast_target = FunctionalBuilder::pointer_cast(lowered_target, ftype, term->location());
    ValuePtr<> result = runner.builder().call(cast_target, parameters, term->location());
    runner.add_mapping(term, LoweredValue::register_(runner.rewrite_type(term->type()), false, result));
  }
  
  virtual ValuePtr<Instruction> lower_return(AggregateLoweringPass::FunctionRunner& runner, const ValuePtr<>& value, const SourceLocation& location) {
    return runner.builder().return_(value, location);
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
    PSI_NOT_IMPLEMENTED();
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
m_global_value_builder(&m_c_module) {
}

void CModuleBuilder::run() {
  CModuleCallback lowering_callback;
  AggregateLoweringPass aggregate_lowering_pass(m_module, &lowering_callback);
  aggregate_lowering_pass.remove_unions = false;
  aggregate_lowering_pass.split_arrays = true;
  aggregate_lowering_pass.split_structs = true;
  aggregate_lowering_pass.memcpy_to_bytes = true;
  aggregate_lowering_pass.update();

  std::vector<std::pair<ValuePtr<GlobalVariable>, CGlobalVariable*> > global_variables;
  std::vector<std::pair<ValuePtr<Function>, CFunction*> > functions;
  for (Module::ModuleMemberList::iterator i = m_module->members().begin(), e = m_module->members().end(); i != e; ++i) {
    const ValuePtr<Global>& term = i->second;
    ValuePtr<Global> rewritten_term = aggregate_lowering_pass.target_symbol(term);
    
    CType *type = m_type_builder.build(term->type());
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
  ValueBuilder local_value_builder(m_global_value_builder, c_function);

  // PHI nodes need to have space prepared in dominator node
  typedef std::multimap<ValuePtr<Block>, ValuePtr<Phi> > PhiMapType;
  PhiMapType phi_by_dominator;
  for (Function::BlockList::iterator ii = function->blocks().begin(), ie = function->blocks().end(); ii != ie; ++ii) {
    const ValuePtr<Block>& block = *ii;
    for (Block::PhiList::iterator ji = block->phi_nodes().begin(), je = block->phi_nodes().end(); ji != je; ++ji)
      phi_by_dominator.insert(std::make_pair(block->dominator(), *ji));
  }

  unsigned depth = 0;
  for (Function::BlockList::iterator ii = function->blocks().begin(), ie = function->blocks().end(); ii != ie; ++ii) {
    const ValuePtr<Block>& block = *ii;
    
    unsigned new_depth = block_depth(block.get());
    PSI_ASSERT(new_depth <= depth+1);
    for (unsigned ii = depth+1; ii != new_depth; --ii)
      local_value_builder.c_builder().nullary(&function->location(), c_op_block_end);
    
    CExpression *label = local_value_builder.c_builder().nullary(&block->location(), c_op_label);
    local_value_builder.put(block, label);
    local_value_builder.c_builder().nullary(&block->location(), c_op_block_begin);
    
    depth = new_depth;
    
    for (Block::InstructionList::iterator ji = block->instructions().begin(), je = block->instructions().end(); ji != je; ++ji)
      local_value_builder.build(block);
    
    for (PhiMapType::const_iterator ji = phi_by_dominator.lower_bound(block), je = phi_by_dominator.upper_bound(block); ji != je; ++ji) {
      CType *type = m_type_builder.build(ji->second->type());
      CExpression *phi_value = local_value_builder.c_builder().declare(&ji->second->location(), type, c_op_declare, NULL, 0);
      local_value_builder.put(ji->second, phi_value);
    }
  }
  
  for (; depth > 0; --depth)
    local_value_builder.c_builder().nullary(&function->location(), c_op_block_end);
}
}
}
}
