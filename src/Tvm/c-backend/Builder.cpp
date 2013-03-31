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

CModuleBuilder::CModuleBuilder(CCompiler* c_compiler)
: m_c_compiler(c_compiler) {
}

void CModuleBuilder::run(Tvm::Module& module) {
  CModuleCallback lowering_callback;
  AggregateLoweringPass aggregate_lowering_pass(&module, &lowering_callback);
  aggregate_lowering_pass.remove_unions = false;
  aggregate_lowering_pass.split_arrays = true;
  aggregate_lowering_pass.split_structs = true;
  aggregate_lowering_pass.memcpy_to_bytes = true;
  aggregate_lowering_pass.update();
  
  CModule c_module(m_c_compiler);
  
  for (Module::ModuleMemberList::iterator i = module.members().begin(), e = module.members().end(); i != e; ++i) {
    const ValuePtr<Global>& term = i->second;
    ValuePtr<Global> rewritten_term = aggregate_lowering_pass.target_symbol(term);
#if 0
    switch (rewritten_term->term_type()) {
    case term_global_variable: {
      ValuePtr<GlobalVariable> global = value_cast<GlobalVariable>(rewritten_term);
      declare_global(decl, global);
      decl << ";\n";
      if (global->value()) {
        defined = true;
        define_global(def, global);
      }
      break;
    }

    case term_function: {
      ValuePtr<Function> func = value_cast<Function>(rewritten_term);
      declare_function(decl, func);
      decl << ";\n";
      if (!func->blocks().empty()) {
        defined = true;
        define_function(def, func);
      }
      break;
    }

    default:
      PSI_FAIL("unexpected global term type");
    }
#endif
  }
  
  std::ostringstream source;
  c_module.emit(source);
}

#if 0
boost::optional<unsigned> CModuleBuilder::get_priority(const ConstructorPriorityMap& priority_map, const ValuePtr<Function>& function) {
  ConstructorPriorityMap::const_iterator ci = priority_map.find(function);
  if (ci != priority_map.end())
    return ci->second;
  return boost::none;
}

void CModuleBuilder::declare_function(std::ostream& os, const ValuePtr<Function>& function) {
  ValuePtr<FunctionType> ftype = function->function_type();
  
  boost::optional<unsigned> constructor_priority = get_priority(m_constructor_functions, function);
  boost::optional<unsigned> destructor_priority = get_priority(m_destructor_functions, function);
  bool has_attribute = constructor_priority || destructor_priority;
  
  if (has_attribute) {
    os << "__attribute__((";
    if (constructor_priority) os << "constructor(" << *constructor_priority << "),";
    if (destructor_priority) os << "destructor(" << *destructor_priority << "),";
    os << "))";
  }
  
  if (function->private_()) os << "static ";
  if (function->blocks().empty()) os << "extern ";
  
  os << build_type(ftype->result_type()) << ' ' << function->name() << '(';
  std::size_t n = 0;
  for (std::vector<ValuePtr<> >::const_iterator ii = ftype->parameter_types().begin(), ie = ftype->parameter_types().end(); ii != ie; ++ii, ++n)
    os << build_type(*ii) << ' ' << m_parameter_prefix << n;
  os << ')';
}
#endif
}
}
}
