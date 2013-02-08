#include "AggregateLowering.hpp"
#include "Aggregate.hpp"
#include "Recursive.hpp"
#include "Instructions.hpp"
#include "InstructionBuilder.hpp"
#include "FunctionalBuilder.hpp"
#include "Utility.hpp"

#include <boost/next_prior.hpp>
#include <set>
#include <map>
#include <queue>
#include <utility>

namespace Psi {
  namespace Tvm {
    /**
     * \brief Return a type with identical lowered representation but a different origin.
     */
    LoweredType LoweredType::with_origin(const ValuePtr<>& new_origin) {
      boost::shared_ptr<Base> value;
      
      switch (mode()) {
      case LoweredType::mode_register: value = boost::make_shared<RegisterType>(checked_cast<const RegisterType&>(*m_value)); break;
      case LoweredType::mode_split: value = boost::make_shared<SplitType>(checked_cast<const SplitType&>(*m_value)); break;
      case LoweredType::mode_blob: value = boost::make_shared<Base>(*m_value); break;

      default:
      case LoweredType::mode_empty: PSI_FAIL("Cannot update origin of empty lowered type");
      }
      
      value->origin = new_origin;
      return LoweredType(value);
    }
    
    AggregateLoweringPass::ElementOffsetGenerator::ElementOffsetGenerator(AggregateLoweringRewriter *rewriter, const SourceLocation& location)
    : m_rewriter(rewriter), m_location(location), m_global(true) {
      m_offset = m_size = FunctionalBuilder::size_value(rewriter->context(), 0, location);
      m_alignment = FunctionalBuilder::size_value(rewriter->context(), 1, location);
    }
    
    /**
     * \brief Append an element to the data list.
     */
    void AggregateLoweringPass::ElementOffsetGenerator::next(bool global, const ValuePtr<>& el_size, const ValuePtr<>& el_alignment) {
      if (!size_equals_constant(m_size, 0) && !size_equals_constant(el_alignment, 1))
        m_offset = FunctionalBuilder::align_to(m_size, el_alignment, m_location);
      m_size = FunctionalBuilder::add(m_offset, el_size, m_location);
      m_global = m_global && global;
    }
    
    /// \brief Ensure \c size if a multiple of \c alignment
    void AggregateLoweringPass::ElementOffsetGenerator::finish() {
      m_size = FunctionalBuilder::align_to(m_size, m_alignment, m_location);
    }
    
    /// \copydoc ElementOffsetGenerator::next(const ValuePtr<>&,const ValuePtr<>&)
    void AggregateLoweringPass::ElementOffsetGenerator::next(const LoweredType& type) {
      next(type.global(), type.size(), type.alignment());
    }
    
    /// \copydoc ElementOffsetGenerator::next(const LoweredType&)
    void AggregateLoweringPass::ElementOffsetGenerator::next(const ValuePtr<>& type) {
      next(m_rewriter->rewrite_type(type));
    }
      
    AggregateLoweringPass::AggregateLoweringRewriter::AggregateLoweringRewriter(AggregateLoweringPass *pass)
    : m_pass(pass) {
    }

    /**
     * Check whether a type has been lowered and if so return it, otherwise
     * return NULL.
     */
    LoweredType AggregateLoweringPass::AggregateLoweringRewriter::lookup_type(const ValuePtr<>& type) {
      const LoweredType *x = m_type_map.lookup(unrecurse(type));
      if (x)
        return *x;
      return LoweredType();
    }
    
    /**
     * Utility function which runs rewrite_value and asserts that the resulting
     * value is in a register and is non-NULL.
     */
    LoweredValueSimple AggregateLoweringPass::AggregateLoweringRewriter::rewrite_value_register(const ValuePtr<>& value) {
      return rewrite_value(value).register_simple();
    }
    
    /**
     * \brief Get a value which must already have been rewritten.
     */
    LoweredValue AggregateLoweringPass::AggregateLoweringRewriter::lookup_value(const ValuePtr<>& value) {
      const LoweredValue *x = m_value_map.lookup(unrecurse(value));
      if (x)
        return *x;
      return LoweredValue();
    }

    /**
     * Utility function which runs lookup_value and asserts that the resulting
     * value is on the stack and is non-NULL.
     */
    LoweredValueSimple AggregateLoweringPass::AggregateLoweringRewriter::lookup_value_register(const ValuePtr<>& value) {
      return lookup_value(value).register_simple();
    }
    
    /**
     * \brief Simplify a function argument type.
     * 
     * This can be used by TargetCallback implementations to avoid handling certain types, namely
     * <ul>
     * <li>Exists</li>
     * <li>EmptyType</li>
     * <li>ConstantType</li>
     * </ul>
     * since these should always be handled in a consistent way.
     */
    ValuePtr<> AggregateLoweringPass::AggregateLoweringRewriter::simplify_argument_type(const ValuePtr<>& type) {
      if (ValuePtr<Exists> exists = dyn_cast<Exists>(type)) {
        return unwrap_exists(exists);
      } else if (ValuePtr<EmptyType> empty = dyn_cast<EmptyType>(type)) {
        return FunctionalBuilder::struct_type(type->context(), default_, type->location());
      } else if (ValuePtr<ConstantType> constant = dyn_cast<ConstantType>(type)) {
        return constant->value()->type();
      } else if (isa<Metatype>(type)) {
        std::vector<ValuePtr<> > members(2, FunctionalBuilder::size_type(type->context(), type->location()));
        return FunctionalBuilder::struct_type(type->context(), members, type->location());
      } else {
        return type;
      }
    }

    /**
     * Unwrap an Exists term by generating parameter placeholders with the location of this term so
     * that an error can be reported.
     */
    ValuePtr<> AggregateLoweringPass::AggregateLoweringRewriter::unwrap_exists(const ValuePtr<Exists>& exists) {
      std::vector<ValuePtr<> > parameters;
      for (std::size_t ii = 0, ie = exists->parameter_types().size(); ii != ie; ++ii) {
        ValuePtr<> ty = exists->parameter_type_after(parameters);
        parameters.push_back(exists->context().new_placeholder_parameter(ty, exists->location()));
      }
      return exists->result_after(parameters);
    }
    
    AggregateLoweringPass::FunctionRunner::FunctionRunner(AggregateLoweringPass* pass, const ValuePtr<Function>& old_function)
    : AggregateLoweringRewriter(pass), m_old_function(old_function) {
      m_new_function = pass->target_callback->lower_function(*pass, old_function);
      if (!old_function->blocks().empty()) {
        ValuePtr<Block> new_entry = new_function()->new_block(old_function->blocks().front()->location());
        builder().set_insert_point(new_entry);
        pass->target_callback->lower_function_entry(*this, old_function, new_function());
      }
    }

    /**
     * \brief Add a key,value pair to the existing term mapping.
     */
    void AggregateLoweringPass::FunctionRunner::add_mapping(const ValuePtr<>& source, const LoweredValue& target) {
      PSI_ASSERT(&source->context() == &old_function()->context());
      m_value_map.insert(std::make_pair(source, target));
    }
    
    /**
     * \brief Map a block from the old function to the new one.
     */
    ValuePtr<Block> AggregateLoweringPass::FunctionRunner::rewrite_block(const ValuePtr<Block>& block) {
      return value_cast<Block>(lookup_value_register(block).value);
    }

    /**
     * \brief Store a value to a pointer.
     * 
     * Overload for building functions. As well as implementing the store_value operation
     * inherited from AggregateLoweringRewriter, this is also used to implement the actual
     * store instruction.
     * 
     * \param value Value to store. This should be a value from the original,
     * not rewritten module.
     * 
     * \param ptr Memory to store to. This should be a value from the rewritten
     * module.
     * 
     * \pre \code isa<PointerType>(ptr->type()) \endcode
     */
    void AggregateLoweringPass::FunctionRunner::store_value(const LoweredValue& value, const ValuePtr<>& ptr, const SourceLocation& location) {
      switch (value.mode()) {
      case LoweredValue::mode_register: {
        ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(ptr, value.register_value()->type(), location);
        builder().store(value.register_value(), cast_ptr, location);
        return;
      }
      
      case LoweredValue::mode_split: {
        const LoweredValue::EntryVector& entries = value.split_entries();
        ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(ptr, FunctionalBuilder::byte_type(context(), location), location);
        ElementOffsetGenerator gen(this, location);
        for (LoweredValue::EntryVector::const_iterator ii = entries.begin(), ie = entries.end(); ii != ie; ++ii) {
          gen.next(ii->type());
          ValuePtr<> offset_ptr = FunctionalBuilder::pointer_offset(cast_ptr, gen.offset(), location);
          store_value(*ii, offset_ptr, location);
        }
        return;
      }
      
      default:
        PSI_FAIL("unexpected enum value");
      }
    }
    
    /**
     * Switch the insert point of a FunctionRunner to just before the end of an existing block.
     * 
     * Stores current block state back to state map before restoring state of specified block,
     * even when both blocks are the same (this helps with initializing the first entry in the
     * map, or when a block is built immediately after its dominating block).
     */
    void AggregateLoweringPass::FunctionRunner::switch_to_block(const ValuePtr<Block>& block) {
      PSI_ASSERT(block->terminated());
      BlockBuildState& old_st = m_block_state[builder().block()];
      old_st.types = m_type_map;
      old_st.values = m_value_map;
      BlockSlotMapType::const_iterator new_st = m_block_state.find(block);
      PSI_ASSERT(new_st != m_block_state.end());
      m_type_map = new_st->second.types;
      m_value_map = new_st->second.values;
      builder().set_insert_point(block->instructions().back());
    }

    /**
     * Run this pass on a single function.
     */
    void AggregateLoweringPass::FunctionRunner::run() {
      /*
       * Check whether any instructions were inserted at the beginning of the
       * function and decide whether a new entry block is necessary in case
       * the user jumps back to the start of the function.
       */
      if (new_function()->blocks().empty())
        return; // This is an external function

      ValuePtr<Block> prolog_block = new_function()->blocks().front();

      std::vector<std::pair<ValuePtr<Block>, ValuePtr<Block> > > sorted_blocks;
      
      // Set up block mapping for all blocks except the entry block,
      // which has already been handled
      for (Function::BlockList::const_iterator ii = old_function()->blocks().begin(), ie = old_function()->blocks().end(); ii != ie; ++ii) {
        ValuePtr<Block> dominator = (*ii)->dominator() ? rewrite_block((*ii)->dominator()) : prolog_block;
        ValuePtr<Block> new_block = new_function()->new_block((*ii)->location(), dominator);
        sorted_blocks.push_back(std::make_pair(*ii, new_block));
        m_value_map.insert(std::make_pair(*ii, LoweredValue::register_(pass().block_type(), false, new_block)));
      }
      
      // Jump from prolog block to entry block
      InstructionBuilder(prolog_block).br(rewrite_block(old_function()->blocks().front()), prolog_block->location());
      
      // Generate PHI nodes and convert instructions!
      for (std::vector<std::pair<ValuePtr<Block>, ValuePtr<Block> > >::iterator ii = sorted_blocks.begin(), ie = sorted_blocks.end(); ii != ie; ++ii) {
        const ValuePtr<Block>& old_block = ii->first;
        const ValuePtr<Block>& new_block = ii->second;

        // Set up variable map from dominating block
        switch_to_block(new_block->dominator());
        m_builder.set_insert_point(new_block);

        // Generate PHI nodes
        for (Block::PhiList::const_iterator ji = old_block->phi_nodes().begin(), je = old_block->phi_nodes().end(); ji != je; ++ji) {
          LoweredValue v = create_phi_node(new_block, rewrite_type((*ji)->type()), (*ji)->location());
          m_value_map.insert(std::make_pair(*ji, v));
        }

        // Create instructions
        for (Block::InstructionList::const_iterator ji = old_block->instructions().begin(), je = old_block->instructions().end(); ji != je; ++ji) {
          const ValuePtr<Instruction>& insn = *ji;
          LoweredValue value = instruction_term_rewrite(*this, insn);
          if (!value.empty())
            m_value_map.insert(std::make_pair(insn, value));
        }
      }
      
      // Populate preexisting PHI nodes with values
      for (std::vector<std::pair<ValuePtr<Block>, ValuePtr<Block> > >::iterator ii = sorted_blocks.begin(), ie = sorted_blocks.end(); ii != ie; ++ii) {
        ValuePtr<Block> old_block = ii->first;
        for (Block::PhiList::const_iterator ji = old_block->phi_nodes().begin(), je = old_block->phi_nodes().end(); ji != je; ++ji) {
          const ValuePtr<Phi>& old_phi_node = *ji;
          LoweredValue new_phi_node = lookup_value(old_phi_node);

          for (unsigned ki = 0, ke = old_phi_node->edges().size(); ki != ke; ++ki) {
            const PhiEdge& e = old_phi_node->edges()[ki];
            switch_to_block(value_cast<Block>(lookup_value_register(e.block).value));
            create_phi_edge(new_phi_node, rewrite_value(e.value));
          }
        }
      }
    }

    /**
     * \brief Allocate space for a specific type.
     */
    ValuePtr<> AggregateLoweringPass::FunctionRunner::alloca_(const LoweredType& type, const SourceLocation& location) {
      ValuePtr<> stack_ptr;
      if (type.mode() == LoweredType::mode_register) {
        stack_ptr = builder().alloca_(type.register_type(), location);
      } else {
        stack_ptr = builder().alloca_(FunctionalBuilder::byte_type(context(), location), type.size(), type.alignment(), location);
      }
      return FunctionalBuilder::pointer_cast(stack_ptr, FunctionalBuilder::byte_type(context(), location), location);
    }
    
    /**
     * Load instructions require special behaviour. The goal is to load each
     * component of an aggregate separately, but this means that the load instruction
     * itself does not have an equivalent in the generated code.
     * 
     * \param load_term Term to assign the result of this load to.
     * 
     * \param ptr Address to load from (new value).
     */
    LoweredValue AggregateLoweringPass::FunctionRunner::load_value(const LoweredType& type, const ValuePtr<>& ptr, const SourceLocation& location) {
      switch (type.mode()) {
      case LoweredType::mode_register: {
        ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(ptr, type.register_type(), location);
        ValuePtr<> load_insn = builder().load(cast_ptr, location);
        return LoweredValue::register_(type, false, load_insn);
      }
      
      case LoweredType::mode_split: {
        const LoweredType::EntryVector& ty_entries = type.split_entries();
        LoweredValue::EntryVector val_entries;
        ElementOffsetGenerator gen(this, location);
        ValuePtr<> byte_ptr = FunctionalBuilder::pointer_cast(ptr, FunctionalBuilder::byte_pointer_type(context(), location), location);
        for (LoweredType::EntryVector::const_iterator ii = ty_entries.begin(), ie = ty_entries.end(); ii != ie; ++ii) {
          gen.next(*ii);
          val_entries.push_back(load_value(*ii, FunctionalBuilder::pointer_offset(byte_ptr, gen.offset(), location), location));
        }
        return LoweredValue::split(type, val_entries);
      }
      
      case LoweredType::mode_blob:
        throw TvmUserError("Cannot load types not supported by the back-end into registers");
      
      default: PSI_FAIL("unexpected enum value");
      }
    }

    LoweredValue AggregateLoweringPass::FunctionRunner::bitcast(const LoweredType& type, const LoweredValue& input, const SourceLocation& location) {
      ValuePtr<> ptr = alloca_(input.type(), location);
      store_value(input, ptr, location);
      return load_value(type, ptr, location);
    }
    
    LoweredType AggregateLoweringPass::FunctionRunner::rewrite_type(const ValuePtr<>& type_orig) {
      ValuePtr<> type = unrecurse(type_orig);
      
      LoweredType global_lookup = pass().global_rewriter().lookup_type(type);
      if (!global_lookup.empty())
        return global_lookup;

      const LoweredType *lookup = m_type_map.lookup(type);
      if (lookup)
        return *lookup;
      
      LoweredType result;
      if (ValuePtr<FunctionalValue> func_type = dyn_cast<FunctionalValue>(type)) {
        result = type_term_rewrite(*this, func_type);
      } else if (ValuePtr<Exists> exists = dyn_cast<Exists>(type)) {
        result = rewrite_type(unwrap_exists(exists));
      } else if (isa<ParameterPlaceholder>(type)) {
        throw TvmUserError("Encountered parameter placeholder in aggregate lowering");
      } else {
        result = type_term_rewrite_parameter(*this, type);
      }
      
      if (result.global()) {
        pass().global_rewriter().m_type_map.insert(std::make_pair(type, result));
      } else {
        PSI_ASSERT(!result.empty());
        m_type_map.insert(std::make_pair(type, result));
      }
      
      return result;
    }
    
    LoweredValue AggregateLoweringPass::FunctionRunner::rewrite_value(const ValuePtr<>& value_orig) {
      ValuePtr<> value = unrecurse(value_orig);
      
      LoweredValue global_lookup = pass().global_rewriter().lookup_value(value);
      if (!global_lookup.empty())
        return global_lookup;

      const LoweredValue *lookup = m_value_map.lookup(value);
      if (lookup) {
        // Not all values in the value map are necessarily valid - instructions which do not
        // produce a value have NULL entries. However, if the value is used, it must be valid.
        PSI_ASSERT(!lookup->empty());
        return *lookup;
      }

      LoweredValue result;
      if (ValuePtr<Exists> exists = dyn_cast<Exists>(value)) {
        result = rewrite_value(unwrap_exists(exists));
      } else if (ValuePtr<FunctionalValue> functional = dyn_cast<FunctionalValue>(value)) {
        result = functional_term_rewrite(*this, functional);
      } else {
        throw TvmUserError("Unexpected term type encountered in value lowering");
      }
      
      PSI_ASSERT(!result.empty());
      m_value_map.insert(std::make_pair(value, result));
      return result;
    }
    
    /**
     * \brief Create a set of PHI nodes for a particular type.
     * 
     * \param block Block into which to insert the created PHI node.
     * 
     * \param phi_term Value which should map to the newly created PHI node. At the root of a composite
     * PHI node this will be a PHI term, but in general it will not be.
     */
    LoweredValue AggregateLoweringPass::FunctionRunner::create_phi_node(const ValuePtr<Block>& block, const LoweredType& type, const SourceLocation& location) {
      switch (type.mode()) {
      case LoweredType::mode_register: {
        ValuePtr<Phi> new_phi = block->insert_phi(type.register_type(), location);
        return LoweredValue::register_(type, false, new_phi);
      }
      
      case LoweredType::mode_split: {
        const LoweredType::EntryVector& entries = type.split_entries();
        LoweredValue::EntryVector value_entries;
        for (LoweredType::EntryVector::const_iterator ii = entries.begin(), ie = entries.end(); ii != ie; ++ii)
          value_entries.push_back(create_phi_node(block, *ii, location));
        return LoweredValue::split(type, value_entries);
      }
      
      case LoweredType::mode_blob:
        throw TvmUserError("Phi nodes do not work with types not supported by the back-end");
      
      default: PSI_FAIL("unexpected enum value");
      }
    }
    
    /**
     * \brief Initialize the values used by a PHI node, or a set of PHI nodes representing parts of a single value.
     * 
     * \param phi_term PHI term in the new function
     * \param value Value to be passed through the new PHI node
     * 
     * The instruction builder insert point should be set to the end of the block being jumped from.
     */
    void AggregateLoweringPass::FunctionRunner::create_phi_edge(const LoweredValue& phi_term, const LoweredValue& value) {
      switch (value.type().mode()) {
      case LoweredType::mode_register:
        value_cast<Phi>(phi_term.register_value())->add_edge(m_builder.block(), value.register_value());
        return;
      
      case LoweredType::mode_split: {
        PSI_ASSERT(phi_term.mode() == LoweredValue::mode_split);
        const LoweredValue::EntryVector& phi_entries = phi_term.split_entries();
        const LoweredValue::EntryVector& value_entries = value.split_entries();
        PSI_ASSERT(phi_entries.size() == value_entries.size());
        for (std::size_t ii = 0, ie = phi_entries.size(); ii != ie; ++ii)
          create_phi_edge(phi_entries[ii], value_entries[ii]);
        return;
      }

      case LoweredType::mode_blob:
        throw TvmUserError("Phi nodes do not work with types not supported by the back-end");

      default: PSI_FAIL("unrecognised enum value");
      }
    }

    AggregateLoweringPass::ModuleLevelRewriter::ModuleLevelRewriter(AggregateLoweringPass *pass)
    : AggregateLoweringRewriter(pass) {
    }
    
    void AggregateLoweringPass::ModuleLevelRewriter::explode_constant_value(const ValuePtr<>& value, std::vector<ExplodeEntry>& entries, std::size_t& offset, const SourceLocation& location) {
      if (isa<IntegerType>(value->type()) || isa<PointerType>(value->type()) || isa<FloatType>(value->type())) {
        TypeSizeAlignment size = pass().target_callback->type_size_alignment(value->type());
        offset = (offset + size.alignment - 1) & ~(size.alignment - 1);
        PSI_ASSERT((size.alignment != 0) && ((size.alignment & (size.alignment - 1)) == 0)); // Power of 2 check
        ExplodeEntry entry = {offset, size.size, value};
        entries.push_back(entry);
        offset += size.size;
      } else if (ValuePtr<StructValue> struct_val = dyn_cast<StructValue>(value)) {
        PSI_FAIL("need to align aggregate types in explode");
        for (unsigned ii = 0, ie = struct_val->n_members(); ii != ie; ++ii)
          explode_constant_value(struct_val->member_value(ii), entries, offset, location);
      } else if (ValuePtr<ArrayValue> array_val = dyn_cast<ArrayValue>(value)) {
        PSI_FAIL("need to align aggregate types in explode");
        for (unsigned ii = 0, ie = array_val->length(); ii != ie; ++ii)
          explode_constant_value(array_val->value(ii), entries, offset, location);
      } else {
        PSI_FAIL("Unexpected tree type in constant explosion");
      }
    }
      
    ValuePtr<> AggregateLoweringPass::ModuleLevelRewriter::implode_constant_value(const ValuePtr<>& type, const std::vector<ExplodeEntry>& entries, std::size_t& offset, const SourceLocation& location) {
      if (isa<IntegerType>(type) || isa<PointerType>(type) || isa<FloatType>(type)) {
        TypeSizeAlignment size = pass().target_callback->type_size_alignment(type);
        offset = (offset + size.alignment - 1) & ~(size.alignment - 1);
        PSI_ASSERT((size.alignment != 0) && ((size.alignment & (size.alignment - 1)) == 0)); // Power of 2 check
        
        PSI_NOT_IMPLEMENTED();

        offset += size.size;
      } else if (ValuePtr<StructType> struct_ty = dyn_cast<StructType>(type)) {
        PSI_FAIL("need to align aggregate types in implode");
        std::vector<ValuePtr<> > members;
        for (unsigned ii = 0, ie = struct_ty->n_members(); ii != ie; ++ii)
          members.push_back(implode_constant_value(struct_ty->member_type(ii), entries, offset, location));
        return FunctionalBuilder::struct_value(context(), members, location);
      } else if (ValuePtr<ArrayType> array_ty = dyn_cast<ArrayType>(type)) {
        PSI_FAIL("need to align aggregate types in implode");
        ValuePtr<IntegerValue> length_val = dyn_cast<IntegerValue>(array_ty->length());
        if (!length_val)
          throw TvmUserError("Array length not constant in value implosion");
        std::vector<ValuePtr<> > elements;
        for (unsigned ii = 0, ie = length_val->value().unsigned_value_checked(); ii != ie; ++ii)
          elements.push_back(implode_constant_value(array_ty->element_type(), entries, offset, location));
        return FunctionalBuilder::array_value(array_ty->element_type(), elements, location);
      } else {
        PSI_FAIL("Unexpected tree type in constant implosion");
      }
    }
    
    void AggregateLoweringPass::ModuleLevelRewriter::explode_lowered_value(const LoweredValue& value, std::vector<ExplodeEntry>& entries, std::size_t& offset, const SourceLocation& location) {
      ValuePtr<IntegerValue> alignment = dyn_cast<IntegerValue>(value.type().alignment());
      if (!alignment)
        throw TvmUserError("Type with non-constant alignment found during static bitcast");
      
      std::size_t value_alignment = alignment->value().unsigned_value_checked();
      PSI_ASSERT((value_alignment != 0) && ((value_alignment & (value_alignment - 1)) == 0)); // Power of 2 check
      offset = (offset + value_alignment - 1) & ~(value_alignment - 1);
      
      switch (value.mode()) {
      case LoweredValue::mode_register:
        explode_constant_value(value.register_value(), entries, offset, location);
        return;
      
      case LoweredValue::mode_split:
        for (LoweredValue::EntryVector::const_iterator ii = value.split_entries().begin(), ie = value.split_entries().end(); ii != ie; ++ii)
          explode_lowered_value(*ii, entries, offset, location);
        return;
      
      default: PSI_FAIL("unexpected LoweredValue mode");
      }
    }
    
    LoweredValue AggregateLoweringPass::ModuleLevelRewriter::implode_lowered_value(const LoweredType& type, const std::vector<ExplodeEntry>& entries, std::size_t& offset, const SourceLocation& location) {
      ValuePtr<IntegerValue> alignment = dyn_cast<IntegerValue>(type.alignment());
      if (!alignment)
        throw TvmUserError("Type with non-constant alignment found during static bitcast");
      
      std::size_t value_alignment = alignment->value().unsigned_value_checked();
      PSI_ASSERT((value_alignment != 0) && ((value_alignment & (value_alignment - 1)) == 0)); // Power of 2 check
      offset = (offset + value_alignment - 1) & ~(value_alignment - 1);

      switch (type.mode()) {
      case LoweredType::mode_register:
        return LoweredValue::register_(type, true, implode_constant_value(type.register_type(), entries, offset, location));
      
      case LoweredType::mode_split: {
        LoweredValue::EntryVector value_entries;
        for (LoweredType::EntryVector::const_iterator ii = type.split_entries().begin(), ie = type.split_entries().end(); ii != ie; ++ii)
          value_entries.push_back(implode_lowered_value(*ii, entries, offset, location));
        return LoweredValue::split(type, value_entries);
      }
      
      default: PSI_FAIL("unexpected LoweredType mode");
      }
    }

    LoweredValue AggregateLoweringPass::ModuleLevelRewriter::bitcast(const LoweredType& type, const LoweredValue& input, const SourceLocation& location) {
      std::vector<ExplodeEntry> exploded;
      std::size_t offset = 0;
      explode_lowered_value(input, exploded, offset, location);
      offset = 0;
      return implode_lowered_value(type, exploded, offset, location);
    }
    
    LoweredType AggregateLoweringPass::ModuleLevelRewriter::rewrite_type(const ValuePtr<>& type_orig) {
      ValuePtr<> type = unrecurse(type_orig);
      
      const LoweredType *lookup = m_type_map.lookup(type);
      if (lookup)
        return *lookup;
      
      LoweredType result;
      if (ValuePtr<Exists> exists = dyn_cast<Exists>(type)) {
        result = rewrite_type(unwrap_exists(exists));
      } else if (ValuePtr<FunctionalValue> functional = dyn_cast<FunctionalValue>(type)) {
        result = type_term_rewrite(*this, functional);
      } else {
        throw TvmUserError("Unexpected term type in type lowering");
      }
      
      PSI_ASSERT(!result.empty());
      m_type_map.insert(std::make_pair(type, result));
      return result;
    }
    
    LoweredValue AggregateLoweringPass::ModuleLevelRewriter::rewrite_value(const ValuePtr<>& value_orig) {
      ValuePtr<> value = unrecurse(value_orig);
      
      const LoweredValue *lookup = m_value_map.lookup(value);
      if (lookup)
        return *lookup;
      
      if (isa<Global>(value)) {
        PSI_ASSERT(value_cast<Global>(value)->module() != pass().source_module());
        throw TvmUserError("Global from a different module encountered during lowering");
      } else if (isa<FunctionType>(value)) {
        throw TvmUserError("Function type encountered in computed expression");
      }
      
      LoweredValue result;
      if (ValuePtr<Exists> exists = dyn_cast<Exists>(value)) {
        result = rewrite_value(unwrap_exists(exists));
      } else if (ValuePtr<FunctionalValue> functional = dyn_cast<FunctionalValue>(value)) {
        result = functional_term_rewrite(*this, functional);
      } else {
        throw TvmUserError("Non-functional value encountered in global expression: probably instruction or block which has not been inserted into a function");
      }
      
      PSI_ASSERT(!result.empty());
      m_value_map.insert(std::make_pair(value, result));
      return result;
    }

    /**
     * Initialize a global variable build with no elements, zero size and minimum alignment.
     */
    AggregateLoweringPass::GlobalBuildStatus::GlobalBuildStatus(Context& context, const SourceLocation& location) {
      first_element_alignment = max_element_alignment = alignment = FunctionalBuilder::size_value(context, 1, location);
      elements_size = size = FunctionalBuilder::size_value(context, 0, location);
    }
    
    /**
     * Initialize a global variable build with one element, and the specified sizes and alignment.
     */
    AggregateLoweringPass::GlobalBuildStatus::GlobalBuildStatus(const ValuePtr<>& element, const ValuePtr<>& element_size_, const ValuePtr<>& element_alignment_, const ValuePtr<>& size_, const ValuePtr<>& alignment_)
    : elements(1, element),
    elements_size(element_size_),
    first_element_alignment(element_alignment_),
    max_element_alignment(element_alignment_),
    size(size_),
    alignment(alignment_) {
    }

    /**
     * Pad a global to the specified size, assuming that either the next element added or
     * the global variable itself is padded to the specified alignment.
     * 
     * \param status This does not alter the size, alignment or elements_size members of
     * \c status. It only affects the \c elements member.
     * 
     * \param is_value Whether a value is being built. If not, a type is being built.
     */
    void AggregateLoweringPass::global_pad_to_size(GlobalBuildStatus& status, const ValuePtr<>& size, const ValuePtr<>& alignment, bool is_value, const SourceLocation& location) {
      std::pair<ValuePtr<>, std::size_t> padding_type;
      if (ValuePtr<IntegerValue> alignment_val = dyn_cast<IntegerValue>(alignment))
        padding_type = target_callback->type_from_alignment(context(), alignment_val->value().unsigned_value_checked(), location);
      else
        padding_type = std::make_pair(FunctionalBuilder::byte_type(context(), location), 1);
      
      ValuePtr<> count = FunctionalBuilder::div(FunctionalBuilder::sub(size, status.size, location),
                                                FunctionalBuilder::size_value(context(), padding_type.second, location), location);
      if (ValuePtr<IntegerValue> count_value = dyn_cast<IntegerValue>(count)) {
        boost::optional<unsigned> count_value_int = count_value->value().unsigned_value();
        if (!count_value_int)
          throw TvmInternalError("cannot create internal global variable padding due to size overflow");
        if (*count_value_int) {
          ValuePtr<> padding_term = is_value ? FunctionalBuilder::undef(padding_type.first, location) : padding_type.first;
          status.elements.insert(status.elements.end(), *count_value_int, padding_term);
        }
      } else {
        ValuePtr<> array_ty = FunctionalBuilder::array_type(padding_type.first, count, location);
        status.elements.push_back(is_value ? FunctionalBuilder::undef(array_ty, location) : array_ty);
      }
    }
    
    /**
     * Append the result of building a part of a global variable to the current
     * status of building it.
     */
    void AggregateLoweringPass::global_append(GlobalBuildStatus& status, const GlobalBuildStatus& child, bool is_value, const SourceLocation& location) {
      ValuePtr<> child_start = FunctionalBuilder::align_to(status.size, child.alignment, location);
      if (!child.elements.empty()) {
        global_pad_to_size(status, child_start, child.first_element_alignment, is_value, location);
        status.elements.insert(status.elements.end(), child.elements.begin(), child.elements.end());
        status.elements_size = FunctionalBuilder::add(child_start, child.elements_size, location);
      }

      status.size = FunctionalBuilder::add(child_start, child.size, location);
      status.alignment = FunctionalBuilder::max(status.alignment, child.alignment, location);
      status.max_element_alignment = FunctionalBuilder::max(status.max_element_alignment, child.max_element_alignment, location);
    }
    
    /**
     * If the appropriate flags are set, rewrite the global build status \c status
     * from a sequence of elements to a single element which is a struct of the
     * previous elements.
     * 
     * \param is_value If \c status represents a value this should be true, otherwise
     * \c status represents a type.
     */
    void AggregateLoweringPass::global_group(GlobalBuildStatus& status, bool is_value, const SourceLocation& location) {
      if (!flatten_globals)
        return;
      
      ValuePtr<> new_element;
      if (is_value)
        new_element = FunctionalBuilder::struct_value(context(), status.elements, location);
      else
        new_element = FunctionalBuilder::struct_type(context(), status.elements, location);
      
      status.elements.assign(1, new_element);
      status.first_element_alignment = status.max_element_alignment;
    }

    /**
     * Rewrite the type of a global variable.
     * 
     * \param value Global value being stored.
     * 
     * \param element_types Sequence of elements to be put into
     * the global at the top level.
     */
    AggregateLoweringPass::GlobalBuildStatus AggregateLoweringPass::rewrite_global_type(const ValuePtr<>& value) {
      LoweredType value_ty = global_rewriter().rewrite_type(value->type());
      if (value_ty.mode() == LoweredType::mode_register)
        return GlobalBuildStatus(value_ty.register_type(), value_ty.size(), value_ty.alignment(), value_ty.size(), value_ty.alignment());

      if (ValuePtr<ArrayValue> array_val = dyn_cast<ArrayValue>(value)) {
        GlobalBuildStatus status(context(), value->location());
        for (unsigned i = 0, e = array_val->length(); i != e; ++i)
          global_append(status, rewrite_global_type(array_val->value(i)), false, value->location());
        global_group(status, false, value->location());
        return status;
      } else if (ValuePtr<StructValue> struct_val = dyn_cast<StructValue>(value)) {
        GlobalBuildStatus status(context(), value->location());
        for (unsigned i = 0, e = struct_val->n_members(); i != e; ++i)
          global_append(status, rewrite_global_type(struct_val->member_value(i)), false, value->location());
        global_group(status, false, value->location());
        return status;
      } else if (ValuePtr<UnionValue> union_val = dyn_cast<UnionValue>(value)) {
        GlobalBuildStatus status = rewrite_global_type(union_val->value());
        status.size = value_ty.size();
        status.alignment = value_ty.alignment();
        return status;
      } else {
        PSI_FAIL("unsupported global element");
      }
    }

    AggregateLoweringPass::GlobalBuildStatus AggregateLoweringPass::rewrite_global_value(const ValuePtr<>& value) {
      LoweredType value_ty = global_rewriter().rewrite_type(value->type());
      if (value_ty.mode() == LoweredType::mode_register) {
        LoweredValueSimple rewritten_value = m_global_rewriter.rewrite_value_register(value);
        PSI_ASSERT(rewritten_value.global);
        return GlobalBuildStatus(rewritten_value.value, value_ty.size(), value_ty.alignment(), value_ty.size(), value_ty.alignment());
      }

      if (ValuePtr<ArrayValue> array_val = dyn_cast<ArrayValue>(value)) {
        GlobalBuildStatus status(context(), value->location());
        for (unsigned i = 0, e = array_val->length(); i != e; ++i)
          global_append(status, rewrite_global_value(array_val->value(i)), true, value->location());
        global_group(status, true, value->location());
        return status;
      } else if (ValuePtr<StructValue> struct_val = dyn_cast<StructValue>(value)) {
        GlobalBuildStatus status(context(), value->location());
        for (unsigned i = 0, e = struct_val->n_members(); i != e; ++i)
          global_append(status, rewrite_global_value(struct_val->member_value(i)), true, value->location());
        global_group(status, true, value->location());
        return status;
      } else if (ValuePtr<UnionValue> union_val = dyn_cast<UnionValue>(value)) {
        GlobalBuildStatus status = rewrite_global_value(union_val->value());
        status.size = value_ty.size();
        status.alignment = value_ty.alignment();
        return status;
      } else {
        PSI_FAIL("unsupported global element");
      }
    }

    /**
     * \param source_module Module being rewritten
     * 
     * \param target_callback_ Target specific callback functions.
     * 
     * \param target_context Context to create rewritten module in. Uses the
     * source module if this is NULL.
     */
    AggregateLoweringPass::AggregateLoweringPass(Module *source_module, TargetCallback *target_callback_, Context* target_context)
    : ModuleRewriter(source_module, target_context),
    m_global_rewriter(this),
    target_callback(target_callback_),
    split_arrays(false),
    split_structs(false),
    remove_unions(false),
    remove_sizeof(false),
    pointer_arithmetic_to_bytes(false),
    flatten_globals(false) {
    }
    
    /// \brief Type to use for sizes
    const LoweredType& AggregateLoweringPass::size_type() {
      if (m_size_type.empty())
        m_size_type = m_global_rewriter.rewrite_type(FunctionalBuilder::size_type(source_module()->context(), source_module()->location()));
      return m_size_type;
    }
    
    /// \brief Type to use for pointers
    const LoweredType& AggregateLoweringPass::pointer_type() {
      if (m_pointer_type.empty())
        m_pointer_type = m_global_rewriter.rewrite_type(FunctionalBuilder::byte_pointer_type(source_module()->context(), source_module()->location()));
      return m_pointer_type;
    }
    
    /// \brief Type to use for blocks
    const LoweredType& AggregateLoweringPass::block_type() {
      if (m_block_type.empty())
        m_block_type = m_global_rewriter.rewrite_type(FunctionalBuilder::block_type(source_module()->context(), source_module()->location()));
      return m_block_type;
    }

    void AggregateLoweringPass::update_implementation(bool incremental) {
      if (!incremental)
        m_global_rewriter = ModuleLevelRewriter(this);
      
      std::vector<std::pair<ValuePtr<GlobalVariable>, ValuePtr<GlobalVariable> > > rewrite_globals;
      std::vector<std::pair<ValuePtr<Function>, boost::shared_ptr<FunctionRunner> > > rewrite_functions;
      
      ValuePtr<> byte_type = FunctionalBuilder::byte_type(context(), target_module()->location());
        
      for (Module::ModuleMemberList::iterator i = source_module()->members().begin(),
           e = source_module()->members().end(); i != e; ++i) {
        
        ValuePtr<Global> term = i->second;
        if (global_map_get(term))
          continue;
      
        if (ValuePtr<GlobalVariable> old_var = dyn_cast<GlobalVariable>(term)) {
          std::vector<ValuePtr<> > element_types;
          GlobalBuildStatus status = rewrite_global_type(old_var->value());
          global_pad_to_size(status, status.size, status.alignment, false, term->location());
          ValuePtr<> global_type;
          if (status.elements.empty()) {
            global_type = FunctionalBuilder::empty_type(context(), term->location());
          } else if (status.elements.size() == 1) {
            global_type = status.elements.front();
          } else {
            global_type = FunctionalBuilder::struct_type(context(), status.elements, term->location());
          }
          ValuePtr<GlobalVariable> new_var = target_module()->new_global_variable(old_var->name(), global_type, old_var->location());
          new_var->set_constant(old_var->constant());
          
          if (old_var->alignment()) {
            LoweredValueSimple old_align = m_global_rewriter.rewrite_value_register(old_var->alignment());
            PSI_ASSERT(old_align.global);
            new_var->set_alignment(FunctionalBuilder::max(status.alignment, old_align.value, term->location()));
          } else {
            new_var->set_alignment(status.alignment);
          }

          ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(new_var, byte_type, term->location());
          global_rewriter().m_value_map.insert(std::make_pair(old_var, LoweredValue::register_(pointer_type(), true, cast_ptr)));
          rewrite_globals.push_back(std::make_pair(old_var, new_var));
        } else {
          ValuePtr<Function> old_function = value_cast<Function>(term);
          boost::shared_ptr<FunctionRunner> runner(new FunctionRunner(this, old_function));
          ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(runner->new_function(), byte_type, term->location());
          global_rewriter().m_value_map.insert(std::make_pair(old_function, LoweredValue::register_(pointer_type(), true, cast_ptr)));
          rewrite_functions.push_back(std::make_pair(old_function, runner));
        }
      }
      
      for (std::vector<std::pair<ValuePtr<GlobalVariable>, ValuePtr<GlobalVariable> > >::iterator
           i = rewrite_globals.begin(), e = rewrite_globals.end(); i != e; ++i) {
        ValuePtr<GlobalVariable> source = i->first, target = i->second;
        if (ValuePtr<> source_value = source->value()) {
          GlobalBuildStatus status = rewrite_global_value(source_value);
          global_pad_to_size(status, status.size, status.alignment, true, source->location());
          ValuePtr<> target_value;
          if (status.elements.empty()) {
            target_value = FunctionalBuilder::empty_value(context(), source->location());
          } else if (status.elements.size() == 1) {
            target_value = status.elements.front();
          } else {
            target_value = FunctionalBuilder::struct_value(context(), status.elements, source->location());
          }
          target->set_value(target_value);
        }
        
        global_map_put(i->first, i->second);
      }
      
      for (std::vector<std::pair<ValuePtr<Function>, boost::shared_ptr<FunctionRunner> > >::iterator
           i = rewrite_functions.begin(), e = rewrite_functions.end(); i != e; ++i) {
        i->second->run();
        global_map_put(i->first, i->second->new_function());
      }
    }
  }
}
