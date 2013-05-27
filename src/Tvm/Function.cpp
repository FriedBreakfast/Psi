#include "Aggregate.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "FunctionalBuilder.hpp"
#include "Instructions.hpp"
#include "Utility.hpp"

#include <boost/next_prior.hpp>
#include <boost/unordered_set.hpp>

namespace Psi {
  namespace Tvm {
    ResolvedParameter::ResolvedParameter(const ValuePtr<>& type, unsigned depth, unsigned index, const SourceLocation& location)
    : FunctionalValue(type->context(), location),
    m_parameter_type(type),
    m_depth(depth),
    m_index(index) {
    }
    
    template<typename V>
    void ResolvedParameter::visit(V& v) {
      visit_base<FunctionalValue>(v);
      v("parameter_type", &ResolvedParameter::m_parameter_type)
      ("depth", &ResolvedParameter::m_depth)
      ("index", &ResolvedParameter::m_index);
    }
    
    ValuePtr<> ResolvedParameter::check_type() const {
      if (!m_parameter_type->is_type())
        error_context().error_throw(location(), "First argument to function_type_resolved_parameter is not a type");
      return m_parameter_type;
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(ResolvedParameter, SimpleOp, resolved_parameter)

    FunctionType::FunctionType(CallingConvention calling_convention, const ValuePtr<>& result_type,
                               const std::vector<ValuePtr<> >& parameter_types,
                               unsigned n_phantom, bool sret, const SourceLocation& location)
    : HashableValue(result_type->context(), term_function_type, location),
    m_calling_convention(calling_convention),
    m_parameter_types(parameter_types),
    m_n_phantom(n_phantom),
    m_sret(sret),
    m_result_type(result_type) {
    }

    class ParameterResolverRewriter : public RewriteCallback {
      std::vector<ValuePtr<ParameterPlaceholder> > m_parameters;
      std::size_t m_depth;

    public:
      ParameterResolverRewriter(Context& context, const std::vector<ValuePtr<ParameterPlaceholder> >& parameters)
      : RewriteCallback(context), m_parameters(parameters), m_depth(0) {
      }

      virtual ValuePtr<> rewrite(const ValuePtr<>& term) {
        if (ValuePtr<ParameterPlaceholder> parameter = dyn_cast<ParameterPlaceholder>(term)) {
          ValuePtr<> type = rewrite(term->type());
          for (unsigned i = 0, e = m_parameters.size(); i != e; ++i) {
            if (m_parameters[i] == term)
              return FunctionalBuilder::parameter(type, m_depth, i, m_parameters[i]->location());
          }
          if (type != term->type())
            error_context().error_throw(term->location(), "type of unresolved function parameter cannot depend on type of resolved function parameter");
          return term;
        } else if (ValuePtr<FunctionType> function_type = dyn_cast<FunctionType>(term)) {
          ++m_depth;
          ValuePtr<> result = function_type->rewrite(*this);
          --m_depth;
          return result;
        } else if (ValuePtr<Exists> exists = dyn_cast<Exists>(term)) {
          ++m_depth;
          ValuePtr<> result = exists->rewrite(*this);
          --m_depth;
          return result;
        } else if (ValuePtr<HashableValue> hashable = dyn_cast<HashableValue>(term)) {
          return hashable->rewrite(*this);
        } else {
          return term;
        }
      }
    };

    /**
     * Get a function type term.
     *
     * Phantom parameters which exist to allow functions to take
     * parameters of general types without having to know the details
     * of those types; similar to \c forall quantification in
     * Haskell. This is also how callback functions are passed since a
     * user-specified parameter to the callback will usually have an
     * unknown type, but this must be the same type as passed to the
     * callback.
     *
     * The distinction between phantom and regular parameters is not
     * just about types, since for example in order to access an array
     * the type of array elements must be known, and therefore must be
     * part of \c parameters.
     *
     * \param calling_convention Calling convention of this function.
     *
     * \param result_type The result type of the function. This may
     * depend on \c parameters and \c phantom_parameters.
     *
     * \param n_phantom Number of phantom parameters - these do not
     * actually cause any data to be passed at machine level.
     * 
     * \param sret If set, the last parameter is treated as a pointer
     * to return value storage.
     *
     * \param parameters Ordinary function parameters.
     */
    ValuePtr<FunctionType> Context::get_function_type(CallingConvention calling_convention,
                                                      const ValuePtr<>& result_type,
                                                      const std::vector<ValuePtr<ParameterPlaceholder> >& parameters,
                                                      unsigned n_phantom,
                                                      bool sret,
                                                      const SourceLocation& location) {
      PSI_ASSERT(n_phantom <= parameters.size());

      std::vector<ValuePtr<ParameterPlaceholder> > previous_parameters;
      std::vector<ValuePtr<> > resolved_parameter_types;
      for (unsigned ii = 0, ie = parameters.size(); ii != ie; ++ii) {
        resolved_parameter_types.push_back(ParameterResolverRewriter(*this, previous_parameters).rewrite(parameters[ii]->type()));
        previous_parameters.push_back(parameters[ii]);
      }

      ValuePtr<> resolved_result_type = ParameterResolverRewriter(*this, previous_parameters).rewrite(result_type);
      
      return get_functional(FunctionType(calling_convention, resolved_result_type, resolved_parameter_types,
                                         n_phantom, sret, location));
    }

    namespace {
      class ParameterTypeRewriter : public RewriteCallback {
        std::vector<ValuePtr<> > m_previous;
        std::size_t m_depth;

      public:
        ParameterTypeRewriter(Context& context, const std::vector<ValuePtr<> >& previous)
        : RewriteCallback(context), m_previous(previous), m_depth(0) {}

        virtual ValuePtr<> rewrite(const ValuePtr<>& term) {
          if (ValuePtr<ResolvedParameter> parameter = dyn_cast<ResolvedParameter>(term)) {
            if (parameter->depth() == m_depth)
              return m_previous[parameter->index()];
            else
              return parameter->rewrite(*this);
          } else if (ValuePtr<FunctionType> function_type = dyn_cast<FunctionType>(term)) {
            ++m_depth;
            ValuePtr<> result = function_type->rewrite(*this);
            --m_depth;
            return result;
          } else if (ValuePtr<HashableValue> hashable = dyn_cast<HashableValue>(term)) {
            return hashable->rewrite(*this);
          } else {
            return term;
          }
        }
      };
    }

    /**
     * Get the type of a parameter, given previous parameters.
     *
     * \param previous Earlier parameters. Length of this array gives
     * the index of the parameter type to get.
     */
    ValuePtr<> FunctionType::parameter_type_after(const SourceLocation& location, const std::vector<ValuePtr<> >& previous) {
      if (previous.size() >= parameter_types().size())
        error_context().error_throw(location, "too many parameters specified");
      return ParameterTypeRewriter(context(), previous).rewrite(parameter_types()[previous.size()]);
    }

    /**
     * Get the return type of a function of this type, given previous
     * parameters.
     */
    ValuePtr<> FunctionType::result_type_after(const SourceLocation& location, const std::vector<ValuePtr<> >& parameters) {
      if (parameters.size() != parameter_types().size())
        error_context().error_throw(location, "incorrect number of parameters");
      return ParameterTypeRewriter(context(), parameters).rewrite(result_type());
    }

    template<typename V>
    void FunctionType::visit(V& v) {
      visit_base<HashableValue>(v);
      v("calling_convention", &FunctionType::m_calling_convention)
      ("parameter_types", &FunctionType::m_parameter_types)
      ("n_phantom", &FunctionType::m_n_phantom)
      ("result_type", &FunctionType::m_result_type);
    }
    
    ValuePtr<> FunctionType::check_type() const {
      for (std::vector<ValuePtr<> >::const_iterator ii = m_parameter_types.begin(), ie = m_parameter_types.end(); ii != ie; ++ii)
        if (!(*ii)->is_type())
          error_context().error_throw(location(), "Function argument type is not a type");
        
      if (m_sret) {
        if (!isa<EmptyType>(m_result_type))
          error_context().error_throw(location(), "Function types with sret set must return void");
      } else {
        if (!m_result_type->is_type())
          error_context().error_throw(location(), "Function result type is not a type");
      }
        
      return FunctionalBuilder::type_type(context(), location());
    }

    PSI_TVM_HASHABLE_IMPL(FunctionType, HashableValue, function)

    Exists::Exists(const ValuePtr<>& result, const std::vector<ValuePtr<> >& parameter_types, const SourceLocation& location)
    : HashableValue(result->context(), term_exists, location),
    m_parameter_types(parameter_types),
    m_result(result) {
    }

    /**
     * Get an exists expression.
     */
    ValuePtr<Exists> Context::get_exists(const ValuePtr<>& result,
                                         const std::vector<ValuePtr<ParameterPlaceholder> >& parameters,
                                         const SourceLocation& location) {
      std::vector<ValuePtr<ParameterPlaceholder> > previous_parameters;
      std::vector<ValuePtr<> > resolved_parameter_types;
      for (unsigned ii = 0, ie = parameters.size(); ii != ie; ++ii) {
        resolved_parameter_types.push_back(ParameterResolverRewriter(*this, previous_parameters).rewrite(parameters[ii]->type()));
        previous_parameters.push_back(parameters[ii]);
      }

      ValuePtr<> resolved_result = ParameterResolverRewriter(*this, previous_parameters).rewrite(result);
      
      return get_functional(Exists(resolved_result, resolved_parameter_types, location));
    }

    /**
     * Get the type of a parameter, given previous parameters.
     *
     * \param previous Earlier parameters. Length of this array gives
     * the index of the parameter type to get.
     */
    ValuePtr<> Exists::parameter_type_after(const std::vector<ValuePtr<> >& previous) {
      return ParameterTypeRewriter(context(), previous).rewrite(parameter_types()[previous.size()]);
    }

    /**
     * Get the return type of a function of this type, given previous
     * parameters.
     */
    ValuePtr<> Exists::result_after(const std::vector<ValuePtr<> >& parameters) {
      if (parameters.size() != parameter_types().size())
        error_context().error_throw(location(), "incorrect number of parameters");
      return ParameterTypeRewriter(context(), parameters).rewrite(m_result);
    }

    template<typename V>
    void Exists::visit(V& v) {
      visit_base<HashableValue>(v);
      v("parameter_types", &Exists::m_parameter_types)
      ("result", &Exists::m_result);
    }
    
    ValuePtr<> Exists::check_type() const {
      for (std::vector<ValuePtr<> >::const_iterator ii = m_parameter_types.begin(), ie = m_parameter_types.end(); ii != ie; ++ii)
        if (!(*ii)->is_type())
          error_context().error_throw(location(), "Exists argument type is not a type");
      return FunctionalBuilder::type_type(context(), location());
    }

    PSI_TVM_HASHABLE_IMPL(Exists, HashableValue, function)
    
    Unwrap::Unwrap(const ValuePtr<>& value, const SourceLocation& location)
    : FunctionalValue(value->context(), location),
    m_value(value) {
    }
    
    template<typename V>
    void Unwrap::visit(V& v) {
      visit_base<FunctionalValue>(v);
      v("value", &Unwrap::m_value);
    }
    
    ValuePtr<> Unwrap::check_type() const {
      ValuePtr<Exists> exists;
      if (!m_value || !(exists = dyn_cast<Exists>(m_value->type())))
        error_context().error_throw(location(), "unwrap parameter does not have exists type");

      std::vector<ValuePtr<> > parameters;
      for (unsigned ii = 0, ie = exists->parameter_types().size(); ii != ie; ++ii)
        parameters.push_back(FunctionalBuilder::unwrap_param(m_value, ii, location()));

      return exists->result_after(parameters);
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(Unwrap, FunctionalValue, unwrap)
    
    UnwrapParameter::UnwrapParameter(const ValuePtr<>& value, unsigned index, const SourceLocation& location)
    : FunctionalValue(value->context(), location),
    m_value(value),
    m_index(index) {
    }
    
    template<typename V>
    void UnwrapParameter::visit(V& v) {
      visit_base<FunctionalValue>(v);
      v("value", &UnwrapParameter::m_value)
      ("index", &UnwrapParameter::m_index);
    }    
    
    ValuePtr<> UnwrapParameter::check_type() const {
      ValuePtr<Exists> exists;
      if (!m_value || !(exists = dyn_cast<Exists>(m_value->type())))
        error_context().error_throw(location(), "unwrap parameter does not have exists type");
      
      if (m_index >= exists->parameter_types().size())
        error_context().error_throw(location(), "unwrap_param index out of range");

      std::vector<ValuePtr<> > parameters;
      for (unsigned ii = 0, ie = m_index; ii != ie; ++ii)
        parameters.push_back(FunctionalBuilder::unwrap_param(m_value, ii, location()));

      return exists->parameter_type_after(parameters);
    }
    
    void hashable_check_source_hook(UnwrapParameter& self, CheckSourceParameter&) {
      self.error_context().error_throw(self.location(), "unwrap_param used outside its context");
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(UnwrapParameter, FunctionalValue, unwrap_param)

    ParameterPlaceholder::ParameterPlaceholder(Context& context, const ValuePtr<>& type, const SourceLocation& location)
    : Value(context, term_parameter_placeholder, type, location),
    m_parameter_type(type) {
    }
    
    Value* ParameterPlaceholder::disassembler_source() {
      return this;
    }
    
    /**
     * \internal Since check_source checks that the \c available map does not contain this term,
     * this method always throws.
     */
    void ParameterPlaceholder::check_source_hook(CheckSourceParameter&) {
      error_context().error_throw(location(), "Parameter placeholder used in wrong context");
    }
    
    template<typename V>
    void ParameterPlaceholder::visit(V& v) {
      visit_base<Value>(v);
    }
    
    PSI_TVM_VALUE_IMPL(ParameterPlaceholder, Value);

    ValuePtr<ParameterPlaceholder> Context::new_placeholder_parameter(const ValuePtr<>& type, const SourceLocation& location) {
      return ValuePtr<ParameterPlaceholder>(::new ParameterPlaceholder(*this, type, location));
    }

    BlockMember::BlockMember(TermType term_type, const ValuePtr<>& type, const SourceLocation& location)
    : Value(type->context(), term_type, type, location),
    m_block(NULL) {
    }
    
    Value* BlockMember::disassembler_source() {
      return this;
    }
    
    Instruction::Instruction(const ValuePtr<>& type, const char *operation, const SourceLocation& location)
    : BlockMember(term_instruction, type, location),
    m_operation(operation) {
    }
    
    /**
     * \brief Check that a value is available to this instruction.
     * 
     * If the test fails, throw an exception.
     */
    void Instruction::require_available(const ValuePtr<>& value) {
      CheckSourceParameter cs(CheckSourceParameter::mode_before_instruction, this);
      value->check_source(cs);
    }

    /**
     * \brief Remove this instruction from its block.
     */
    void Instruction::remove() {
      PSI_ASSERT(block_ptr() && m_instruction_list_hook.is_linked());
      block_ptr()->erase_instruction(*this);
    }

    void Instruction::check_source_hook(CheckSourceParameter& parameter) {
      switch (parameter.mode) {
      case CheckSourceParameter::mode_before_instruction: {
        Instruction *insn = value_cast<Instruction>(parameter.point);
        if (insn->block_ptr()->dominated_by(block_ptr())) {
          return;
        } else if (insn->block_ptr() == block_ptr()) {
          if (block_ptr()->instructions().before(*this, *insn))
            return;
        }
        break;
      }
      
      case CheckSourceParameter::mode_after_block: {
        Block *block = value_cast<Block>(parameter.point);
        if (block->same_or_dominated_by(block_ptr()))
          return;
        break;
      }
      
      case CheckSourceParameter::mode_before_block: {
        Block *block = value_cast<Block>(parameter.point);
        if (block->dominated_by(block_ptr()))
          return;
        break;
      }
      
      case CheckSourceParameter::mode_global:
        break;
      }
      
      error_context().error_throw(location(), "Result of PHI term used in wrong context");
    }

    TerminatorInstruction::TerminatorInstruction(Context& context, const char* operation, const SourceLocation& location)
    : Instruction(FunctionalBuilder::empty_type(context, location), operation, location) {
    }

    bool TerminatorInstruction::isa_impl(const Value& ptr) {
      const Instruction *insn = dyn_cast<Instruction>(&ptr);
      if (!insn)
        return false;
      
      const char *op = insn->operation_name();
      if ((op == ConditionalBranch::operation)
        || (op == UnconditionalBranch::operation)
        || (op == Unreachable::operation))
        return true;
      
      return false;
    }

    /**
     * \brief Check whether this block is dominated by another.
     *
     * If \c block is NULL, this will return true since a NULL
     * dominator block refers to the function entry, i.e. before the
     * entry block is run, and therefore eveything is dominated by it.
     * 
     * If \c block is the same as \c this, this function returns false.
     */
    bool Block::dominated_by(Block *block) {
      if (!block)
        return true;

      for (Block *b = m_dominator.get(); b; b = b->m_dominator.get()) {
        if (block == b)
          return true;
      }
      return false;
    }
    
    /**
     * \brief Return true if \c block dominates this block, or block == this.
     */
    bool Block::same_or_dominated_by(Block *block) {
      return (this == block) || dominated_by(block);
    }
    
    /**
     * \brief Find the latest block which dominates both of the specified blocks.
     * 
     * \pre \code first->function() == second->function() \endcode
     */
    ValuePtr<Block> Block::common_dominator(const ValuePtr<Block>& first, const ValuePtr<Block>& second) {
      PSI_ASSERT(first->function() == second->function());

      for (ValuePtr<Block> i = first; i; i = i->dominator()) {
        if (second->same_or_dominated_by(i))
          return i;
      }
      
      for (ValuePtr<Block> i = second; i; i = i->dominator()) {
        if (first->same_or_dominated_by(i))
          return i;
      }
      
      return ValuePtr<Block>();
    }

    void Block::insert_instruction(const ValuePtr<Instruction>& insn, const ValuePtr<Instruction>& insert_before) {
      if (insn->m_block)
        error_context().error_throw(insn->location(), "Instruction has already been inserted into a block");

      if (terminated() && !insert_before)
        error_context().error_throw(insn->location(), "cannot add instruction at end of already terminated block");
      
      if (insert_before) {
        if (insert_before->block() != this)
          error_context().error_throw(insn->location(), "instruction specified as insertion point is not part of this block");

        if (isa<TerminatorInstruction>(insn))
          error_context().error_throw(insn->location(), "terminating instruction cannot be inserted other than at the end of a block");
      }
      
      m_instructions.insert(insert_before, *insn);
      insn->m_block = this;
      insn->type_check();
    }
    
    void Block::erase_phi(Phi& phi) {
      PSI_ASSERT(phi.block_ptr() == this);
      m_phi_nodes.erase(phi);
    }
    
    void Block::erase_instruction(Instruction& instruction) {
      PSI_ASSERT(instruction.block_ptr() == this);
      m_instructions.erase(instruction);
    }

    /**
     * \brief Get the list of blocks which this one can exit to (including exceptions)
     */
    std::vector<ValuePtr<Block> > Block::successors() {
      std::vector<ValuePtr<Block> > result;
      if (ValuePtr<TerminatorInstruction> terminator = dyn_cast<TerminatorInstruction>(m_instructions.back()))
        result = terminator->successors();
      if (m_landing_pad)
        result.push_back(m_landing_pad);
      return result;
    }
    
    template<typename V>
    void Block::visit(V& v) {
      visit_base<Value>(v);
      v("function", &Block::m_function)
      ("dominator", &Block::m_dominator)
      ("landing_pad", &Block::m_landing_pad)
      ("instructions", &Block::m_instructions)
      ("phi_nodes", &Block::m_phi_nodes);
    }

    PSI_TVM_VALUE_IMPL(Block, Value)

    /**
     * \brief Add a value for a phi term along an incoming edge.
     *
     * \param block The block which jumps into the block containing
     * this phi node causing it to take on the given value.
     *
     * \param value Value the phi term takes on. This must not be a
     * phantom value, since it makes no sense for phi terms to allow
     * phantom values.
     */
    void Phi::add_edge(const ValuePtr<Block>& incoming_block, const ValuePtr<>& value) {
      CheckSourceParameter cs(CheckSourceParameter::mode_after_block, incoming_block.get());
      value->check_source(cs);
      
      if (!incoming_block->same_or_dominated_by(block_ptr()->dominator()))
        error_context().error_throw(value->location(), "incoming edge added to PHI term for block which does not dominate the current one");
      
      for (std::vector<PhiEdge>::const_iterator ii = m_edges.begin(), ie = m_edges.end(); ii != ie; ++ii) {
        if (ii->block == incoming_block)
          error_context().error_throw(value->location(), "incoming edge added for the same block twice");
      }
      
      PhiEdge e;
      e.block = incoming_block;
      e.value = value;
      
      m_edges.push_back(e);
    }

    Phi::Phi(const ValuePtr<>& type, const SourceLocation& location)
    : BlockMember(term_phi, type, location) {
    }
    
    /**
     * \brief Find the value corresponding to a specific incoming block.
     */
    ValuePtr<> Phi::incoming_value_from(const ValuePtr<Block>& block) {
      for (std::vector<PhiEdge>::const_iterator ii = m_edges.begin(), ie = m_edges.end(); ii != ie; ++ii) {
        if (ii->block == block)
          return ii->value;
      }
      error_context().error_throw(location(), "Incoming block not found in PHI node");
    }
    
    /**
     * \brief Remove from its block.
     */
    void Phi::remove() {
      PSI_ASSERT(block_ptr() && m_phi_list_hook.is_linked());
      block_ptr()->erase_phi(*this);
    }
    
    template<typename V>
    void Phi::visit(V& v) {
      visit_base<Value>(v);
      v("edges", &Phi::m_edges);
    }

    void Phi::check_source_hook(CheckSourceParameter& parameter) {
      switch (parameter.mode) {
      case CheckSourceParameter::mode_before_instruction: {
        Instruction *insn = value_cast<Instruction>(parameter.point);
        if (insn->block_ptr()->same_or_dominated_by(block_ptr()))
          return;
        break;
      }
      
      case CheckSourceParameter::mode_after_block: {
        Block *block = value_cast<Block>(parameter.point);
        if (block->same_or_dominated_by(block_ptr()))
          return;
        break;
      }
      
      case CheckSourceParameter::mode_before_block: {
        Block *block = value_cast<Block>(parameter.point);
        if (block->dominated_by(block_ptr()))
          return;
        break;
      }
      
      case CheckSourceParameter::mode_global:
        break;
      }
      
      error_context().error_throw(location(), "Result of PHI term used in wrong context");
    }

    PSI_TVM_VALUE_IMPL(Phi, Value)

    /**
     * \brief Create a new Phi node.
     *
     * Phi nodes allow values from non-dominating blocks to be used by
     * selecting a value based on which block was run immediately
     * before this one.
     *
     * \param type Type of this term. All values that this term can
     * take on must be of the same type.
     */
    ValuePtr<Phi> Block::insert_phi(const ValuePtr<>& type, const SourceLocation& location) {
      CheckSourceParameter cs(CheckSourceParameter::mode_before_block, this);
      type->check_source(cs);
      
      ValuePtr<Phi> phi(::new Phi(type, location));
      m_phi_nodes.push_back(*phi);
      phi->m_block = this;
      return phi;
    }

    Block::Block(Function *function, const ValuePtr<Block>& dominator,
                 bool is_landing_pad, const ValuePtr<Block>& landing_pad, const SourceLocation& location)
    : Value(function->context(), term_block, FunctionalBuilder::block_type(function->context(), location), location),
    m_function(function),
    m_dominator(dominator),
    m_landing_pad(landing_pad),
    m_is_landing_pad(is_landing_pad) {
      if (dominator && (dominator->function_ptr() != function))
        function->context().error_context().error_throw(location, "Dominator block in a different function");
      if (landing_pad && (landing_pad->function_ptr() != function))
        function->context().error_context().error_throw(location, "Landing pad in a different function");
    }
    
    Value* Block::disassembler_source() {
      return this;
    }
    
    void Block::check_source_hook(CheckSourceParameter& parameter) {
      if (parameter.mode == CheckSourceParameter::mode_before_instruction) {
        if (TerminatorInstruction *insn = dyn_cast<TerminatorInstruction>(parameter.point)) {
          if (insn->block_ptr()->same_or_dominated_by(dominator()))
            return;
        }
      }
      
      error_context().error_throw(location(), "Block address used in incorrect context");
    }

    FunctionParameter::FunctionParameter(Context& context, Function *function, const ValuePtr<>& type, bool phantom, const SourceLocation& location)
    : Value(context, term_function_parameter, type, location),
    m_phantom(phantom),
    m_function(function) {
    }
    
    template<typename V>
    void FunctionParameter::visit(V& v) {
      visit_base<Value>(v);
    }
    
    void FunctionParameter::check_source_hook(CheckSourceParameter& parameter) {
      switch (parameter.mode) {
      case CheckSourceParameter::mode_before_instruction:
        if (value_cast<Instruction>(parameter.point)->block_ptr()->function_ptr() == function_ptr()) {
          if (parameter_phantom())
            check_phantom_available(parameter, this);
          return;
        }
        break;
        
      case CheckSourceParameter::mode_before_block:
      case CheckSourceParameter::mode_after_block:
        if (value_cast<Block>(parameter.point)->function_ptr() == function_ptr()) {
          if (parameter_phantom())
            check_phantom_available(parameter, this);
          return;
        }
        break;
        
      case CheckSourceParameter::mode_global:
        break;
      }
      
      error_context().error_throw(location(), "function parameter used in wrong context");
    }
    
    Value* FunctionParameter::disassembler_source() {
      return this;
    }
    
    PSI_TVM_VALUE_IMPL(FunctionParameter, Value);

    /**
     * \brief Create a new function.
     */
    ValuePtr<Function> Module::new_function(const std::string& name, const ValuePtr<FunctionType>& type, const SourceLocation& location) {
      PSI_ASSERT(type);
      ValuePtr<Function> result(::new Function(context(), type, name, this, location));
      add_member(result);
      return result;
    }
    
    /**
     * \brief Create a new constructor or destructor function.
     * 
     * This merely creates a function with the correct signature and private linkage for a constructor/destructor
     * function; it does not add it to either the constructor or destructor list. This must be done by the user.
     */
    ValuePtr<Function> Module::new_constructor(const std::string& name, const SourceLocation& location) {
      ValuePtr<FunctionType> type = FunctionalBuilder::constructor_type(context(), location);
      ValuePtr<Function> result(::new Function(context(), type, name, this, location));
      result->set_linkage(link_local);
      add_member(result);
      return result;
    }
    
    Function::Function(Context& context, const ValuePtr<FunctionType>& type, const std::string& name, Module* module, const SourceLocation& location)
    : Global(context, term_function, type, name, module, location) {

      std::vector<ValuePtr<> > previous;
      unsigned n_phantom = type->n_phantom();

      for (unsigned ii = 0, ie = type->parameter_types().size(); ii != ie; ++ii) {
        ValuePtr<FunctionParameter> p(::new FunctionParameter(context, this, type->parameter_type_after(location, previous), ii < n_phantom, location));
        m_parameters.push_back(*p);
        previous.push_back(p);
      }
      m_result_type = type->result_type_after(location, previous);
    }

    ValuePtr<FunctionType> Function::function_type() const {
      return value_cast<FunctionType>(value_cast<PointerType>(type())->target_type());
    }

    /**
     * \brief Create a new block.
     * 
     * \param dominator Dominating block. If this is NULL, only parameters
     * are available in this block.
     */
    ValuePtr<Block> Function::new_block(const SourceLocation& location, const ValuePtr<Block>& dominator, const ValuePtr<Block>& landing_pad) {
      ValuePtr<Block> b(::new Block(this, dominator, false, landing_pad, location));
      m_blocks.push_back(*b);
      return b;
    }

    /**
     * \brief Create a new block.
     * 
     * \param dominator Dominating block. If this is NULL, only parameters
     * are available in this block.
     */
    ValuePtr<Block> Function::new_landing_pad(const SourceLocation& location, const ValuePtr<Block>& dominator, const ValuePtr<Block>& landing_pad) {
      ValuePtr<Block> b(::new Block(this, dominator, true, landing_pad, location));
      m_blocks.push_back(*b);
      return b;
    }

    /**
     * Add a name for a term within this function.
     */
    void Function::add_term_name(const ValuePtr<>& term, const std::string& name) {
      m_name_map.insert(std::make_pair(term, name));
    }

    template<typename V>
    void Function::visit(V& v) {
      visit_base<Global>(v);
      v("parameters", &Function::m_parameters)
      ("result_type", &Function::m_result_type)
      ("exception_personality", &Function::m_exception_personality)
      ("name_map", &Function::m_name_map)
      ("blocks", &Function::m_blocks);
    }
    
    PSI_TVM_VALUE_IMPL(Function, Global);

    /**
     * \brief Return an insert point which is just after the given source term.
     * 
     * \param source Source to insert instructions after. This should be a
     * value source as returned by Term::source().
     */
    InstructionInsertPoint InstructionInsertPoint::after_source(const ValuePtr<>& source) {
      ValuePtr<Block> block;

      switch (source->term_type()) {
      // switch statements allow some weird stuff...
      {
      case term_function:
        block = value_cast<Function>(source)->blocks().front();
        goto block_function_common;

      case term_block:
        block = value_cast<Block>(source);
        goto block_function_common;
      
      block_function_common:
        if (block->instructions().empty())
          return InstructionInsertPoint(block);
        else
          return InstructionInsertPoint(*block->instructions().begin());
      }

      case term_instruction: {
        ValuePtr<Instruction> insn = value_cast<Instruction>(source);
        ValuePtr<Block> block = insn->block();
        Block::InstructionList::const_iterator insn_it = block->instructions().iterator_to(insn);
        ++insn_it;
        if (insn_it == block->instructions().end())
          return InstructionInsertPoint(block);
        else
          return InstructionInsertPoint(*insn_it);
      }

      default:
        PSI_FAIL("unexpected term type");
      }
    }
    
    /**
     * \brief Insert instruction at this point.
     * 
     * Need to check that all values are available!
     */
    void InstructionInsertPoint::insert(const ValuePtr<Instruction>& instruction) {
      m_block->insert_instruction(instruction, m_instruction);
    }

    void check_phantom_available(CheckSourceParameter& parameter, Value *phantom) {
      Block *block = NULL;
      Instruction *instruction = NULL;
      
      switch (parameter.mode) {
      case CheckSourceParameter::mode_before_block:
        block = value_cast<Block>(parameter.point)->dominator().get();
        break;
        
      case CheckSourceParameter::mode_after_block:
        block = value_cast<Block>(parameter.point);
        break;

      case CheckSourceParameter::mode_before_instruction:
        instruction = value_cast<Instruction>(parameter.point);
        block = instruction->block_ptr();
        break;
        
      case CheckSourceParameter::mode_global:
        phantom->error_context().error_throw(phantom->location(), "Phantom value required to have been instantiated by this point");
      }

      for (; block; block = block->dominator().get(), instruction = NULL) {
        for (Block::InstructionList::iterator ii = block->instructions().begin(), ie = block->instructions().end(); (ii != ie) && (ii->get() != instruction); ++ii) {
          Solidify *solid = dyn_cast<Solidify>(ii->get());
          if (!solid)
            continue;
          
          ValuePtr<ConstantType> const_ty = dyn_cast<ConstantType>(solid->value->type());
          if (!const_ty)
            phantom->error_context().error_throw(phantom->location(), "Argument to solidify does not appear to have constant type");
          
          if (phantom == const_ty->value().get())
            return;
        }
      }
      
      phantom->error_context().error_throw(phantom->location(), "Phantom value required to have been instantiated by this point");
    }
  }
}
