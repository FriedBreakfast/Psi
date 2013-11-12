#include "AggregateLowering.hpp"
#include "Aggregate.hpp"
#include "Recursive.hpp"
#include "Instructions.hpp"
#include "InstructionBuilder.hpp"
#include "FunctionalBuilder.hpp"
#include "Utility.hpp"

#include <boost/make_shared.hpp>

namespace Psi {
  namespace Tvm {
    AggregateLoweringPass::TargetCallback::~TargetCallback() {
    }
    
    namespace {
      ValuePtr<FunctionType> lower_function_type(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionType>& ftype) {
        unsigned n_phantom = ftype->n_phantom();
        std::vector<ParameterType> parameter_types;
        for (std::size_t ii = 0, ie = ftype->parameter_types().size() - n_phantom; ii != ie; ++ii)
          parameter_types.push_back(rewriter.rewrite_type(ftype->parameter_types()[ii + n_phantom].value).register_type());
        ValuePtr<> result_type = rewriter.rewrite_type(ftype->result_type().value).register_type();
        return FunctionalBuilder::function_type(ftype->calling_convention(), result_type, parameter_types, 0, ftype->sret(), ftype->location());
      }
    }
    
    void AggregateLoweringPass::TargetCallback::lower_function_call(FunctionRunner& runner, const ValuePtr<Call>& term) {
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
    
    ValuePtr<Instruction> AggregateLoweringPass::TargetCallback::lower_return(FunctionRunner& runner, const ValuePtr<>& value, const SourceLocation& location) {
      ValuePtr<> lowered = runner.rewrite_value_register(value).value;
      return runner.builder().return_(lowered, location);
    }
    
    ValuePtr<Function> AggregateLoweringPass::TargetCallback::lower_function(AggregateLoweringPass& pass, const ValuePtr<Function>& function) {
      ValuePtr<FunctionType> ftype = lower_function_type(pass.global_rewriter(), function->function_type());
      return pass.target_module()->new_function(function->name(), ftype, function->location());
    }
    
    void AggregateLoweringPass::TargetCallback::lower_function_entry(FunctionRunner& runner, const ValuePtr<Function>& source_function, const ValuePtr<Function>& target_function) {
      Function::ParameterList::iterator ii = source_function->parameters().begin(), ie = source_function->parameters().end();
      Function::ParameterList::iterator ji = target_function->parameters().begin();
      std::advance(ii, source_function->function_type()->n_phantom());
      for (; ii != ie; ++ii, ++ji)
        runner.add_mapping(*ii, LoweredValue::register_(runner.rewrite_type((*ii)->type()), false, *ji));
    }
    
    std::pair<ValuePtr<>, std::size_t> AggregateLoweringPass::TargetCallback::type_from_size(Context& context, std::size_t size, const SourceLocation& location) {
      std::size_t my_size = std::min(size, std::size_t(8));
      IntegerType::Width width;
      switch (my_size) {
      case 1: width = IntegerType::i8; break;
      case 2: width = IntegerType::i16; break;
      case 4: width = IntegerType::i32; break;
      case 8: width = IntegerType::i64; break;
      default: PSI_FAIL("type_from_size argument was not a power of two");
      }
      
      return std::make_pair(FunctionalBuilder::int_type(context, width, false, location), size);
    }
  
    ValuePtr<> AggregateLoweringPass::TargetCallback::byte_shift(const ValuePtr<>& value, const ValuePtr<>& result_type, int shift, const SourceLocation& location) {
      PSI_NOT_IMPLEMENTED();
    }
    
    LoweredType::Base::Base(Mode mode_, const ValuePtr<>& origin_, const ValuePtr<>& size_, const ValuePtr<>& alignment_)
    : mode(mode_), origin(origin_), size(size_), alignment(alignment_) {
    }
    
    LoweredType::RegisterType::RegisterType(const ValuePtr<>& origin, const ValuePtr<>& size, const ValuePtr<>& alignment, const ValuePtr<>& register_type_)
    : Base(mode_register, origin, size, alignment), register_type(register_type_) {
    }
    
    LoweredType::SplitType::SplitType(const ValuePtr<>& origin, const ValuePtr<>& size, const ValuePtr<>& alignment, const EntryVector& entries_)
    : Base(mode_split, origin, size, alignment), entries(entries_) {
      PSI_ASSERT(all_global(entries));
    }
    
    /// \brief Construct a lowered type which is stored in a register
    LoweredType LoweredType::register_(const ValuePtr<>& origin, const ValuePtr<>& size, const ValuePtr<>& alignment, const ValuePtr<>& register_type) {
      return LoweredType(boost::make_shared<RegisterType>(origin, size, alignment, register_type));
    }
    
    /// \brief Construct a lowered type which is split into component types
    LoweredType LoweredType::split(const ValuePtr<>& origin, const ValuePtr<>& size, const ValuePtr<>& alignment, const EntryVector& entries) {
      return LoweredType(boost::make_shared<SplitType>(origin, size, alignment, entries));
    }
    
    /// \brief Construct a lowered type which is treated as a black box
    LoweredType LoweredType::blob(const ValuePtr<>& origin, const ValuePtr<>& size, const ValuePtr<>& alignment) {
      return LoweredType(boost::make_shared<Base>(mode_blob, origin, size, alignment));
    }
    
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
    
    /**
     * \brief Get the size and alignment of a type as integers.
     */
    TypeSizeAlignment LoweredType::size_alignment_const() const {
      ValuePtr<IntegerValue> size_val = dyn_cast<IntegerValue>(size()), alignment_val = dyn_cast<IntegerValue>(alignment());
      if (!size_val || !alignment_val)
        origin()->error_context().error_throw(origin()->location(), "Size and alignment of global type are not constant", CompileError::error_internal);

      TypeSizeAlignment result;
      CompileErrorPair error_pair = origin()->context().error_context().bind(origin()->location());
      result.size = size_val->value().unsigned_value_checked(error_pair);
      result.alignment = alignment_val->value().unsigned_value_checked(error_pair);
      return result;
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
      m_byte_type = FunctionalBuilder::byte_type(pass->source_module()->context(), pass->source_module()->location());
      m_byte_ptr_type = FunctionalBuilder::byte_pointer_type(pass->source_module()->context(), pass->source_module()->location());
    }

    AggregateLoweringPass::AggregateLoweringRewriter::~AggregateLoweringRewriter() {
    }

    /**
     * Check whether a type has been lowered and if so return it, otherwise
     * return NULL.
     */
    LoweredType AggregateLoweringPass::AggregateLoweringRewriter::lookup_type(const ValuePtr<>& type) {
      const LoweredType *x = m_type_map.lookup(type);
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
      const LoweredValue *x = m_value_map.lookup(value);
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
        return simplify_argument_type(constant->value()->type());
      } else if (ValuePtr<ApplyType> apply = dyn_cast<ApplyType>(type)) {
        return simplify_argument_type(apply->unpack());
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
    
    namespace {
      AggregateLayout aggregate_layout_base() {
        AggregateLayout layout;
        layout.size = 0;
        layout.alignment = 1;
        return layout;
      }
      
      void aggregate_layout_merge(AggregateLayout& out, std::size_t offset, const AggregateLayout& in) {
        out.size = std::max(out.size, in.size + offset);
        out.alignment = std::max(out.alignment, in.alignment);
        
        for (std::vector<AggregateLayout::Member>::const_iterator ii = in.members.begin(), ie = in.members.end(); ii != ie; ++ii) {
          AggregateLayout::Member m = *ii;
          m.offset += offset;
          out.members.push_back(m);
        }
      }
    }
    
    /**
     * Generate layout information for an aggregate class, which must only contain
     * fields of fixed type and size.
     * 
     * \param with_members If false, do not return information on members.
     */
    AggregateLayout AggregateLoweringPass::AggregateLoweringRewriter::aggregate_layout(const ValuePtr<>& type, const SourceLocation& location, bool with_members) {
      ValuePtr<> simple_type = simplify_argument_type(type);
      
      if (ValuePtr<StructType> struct_ty = dyn_cast<StructType>(simple_type)) {
        AggregateLayout layout = aggregate_layout_base();
        for (unsigned ii = 0, ie = struct_ty->n_members(); ii != ie; ++ii)
          aggregate_layout_merge(layout, layout.size, aggregate_layout(struct_ty->member_type(ii), location, with_members));
        layout.size = align_to(layout.size, layout.alignment);
        return layout;
      } else if (ValuePtr<ArrayType> array_ty = dyn_cast<ArrayType>(simple_type)) {
        ValuePtr<IntegerValue> length_val = dyn_cast<IntegerValue>(array_ty->length());
        if (!length_val)
          error_context().error_throw(location, "Array length not constant in aggregate layout generation");
        std::size_t array_length = length_val->value().unsigned_value_checked(error_context().bind(location));
        
        AggregateLayout element_layout = aggregate_layout(array_ty->element_type(), location, with_members);
        AggregateLayout layout;
        layout.size = element_layout.size * array_length;
        layout.alignment = element_layout.alignment;
        for (std::size_t ii = 0; ii != array_length; ++ii)
          aggregate_layout_merge(layout, ii * element_layout.size, element_layout);
        PSI_ASSERT(layout.size % layout.alignment == 0);
        return layout;
      } else if (ValuePtr<UnionType> union_ty = dyn_cast<UnionType>(simple_type)) {
        AggregateLayout layout = aggregate_layout_base();
        for (unsigned ii = 0, ie = union_ty->n_members(); ii != ie; ++ii)
          aggregate_layout_merge(layout, 0, aggregate_layout(union_ty->member_type(ii), location, with_members));
        layout.size = align_to(layout.size, layout.alignment);
        return layout;
      } else {
        TypeSizeAlignment szal = m_pass->target_callback->type_size_alignment(simple_type, location);
        AggregateLayout layout;
        layout.size = szal.size;
        layout.alignment = szal.alignment;
        if (with_members) {
          AggregateLayout::Member member;
          member.offset = 0;
          member.size = szal.size;
          member.alignment = szal.alignment;
          member.type = simple_type;
          layout.members.push_back(member);
        }
        return layout;
      }
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
     * \brief Prepare a jump between two blocks.
     * 
     * Insertings \c freea instructions for any dangling \c alloca instances.
     */
    ValuePtr<Block> AggregateLoweringPass::FunctionRunner::prepare_jump(const ValuePtr<Block>& source, const ValuePtr<Block>& target, const SourceLocation& location) {
      ValuePtr<Block> lowered_target = value_cast<Block>(rewrite_value_register(target).value);
      for (; m_alloca_list && !lowered_target->dominated_by(m_alloca_list->insn->block());
           m_alloca_list = m_alloca_list->next) {
        builder().freea(m_alloca_list->insn, location);
      }
      m_edge_map.insert(std::make_pair(std::make_pair(source, target), std::make_pair(builder().block(), false)));
      return lowered_target;
    }
    
    /**
     * \brief Prepare a conditional jump between two blocks.
     * 
     * This may generate an alternative jump target block which performs state fixes from one block
     * to the other.
     * 
     * \return Block in the lowered function which should be jumped to.
     */
    ValuePtr<Block> AggregateLoweringPass::FunctionRunner::prepare_cond_jump(const ValuePtr<Block>& source, const ValuePtr<Block>& target, const SourceLocation& location) {
      ValuePtr<Block> lowered_target = value_cast<Block>(rewrite_value_register(target).value);

      // Check whether we need to generate an edge block
      const AllocaListEntry *alloca_entry = m_alloca_list.get();
      for (; alloca_entry; alloca_entry = alloca_entry->next.get()) {
        if (lowered_target->dominated_by(alloca_entry->insn->block()))
          break;
      }
      
      if (alloca_entry == m_alloca_list.get()) {
        // No frees needed so no edge block required
        m_edge_map.insert(std::make_pair(std::make_pair(source, target), std::make_pair(builder().block(), false)));
        return lowered_target;
      } else {
        ValuePtr<Block> edge_block = builder().new_block(location);
        InstructionBuilder edge_builder(edge_block);
        
        // Note that the alloca list is not modified because we do
        // not generate a build state for the edge block
        for (alloca_entry = m_alloca_list.get();
             alloca_entry && !lowered_target->dominated_by(alloca_entry->insn->block());
             alloca_entry = alloca_entry->next.get()) {
          edge_builder.freea(alloca_entry->insn, location);
        }
        
        edge_builder.br(lowered_target, location);
        
        m_edge_map.insert(std::make_pair(std::make_pair(source, target), std::make_pair(edge_block, true)));
        return edge_block;
      }
    }

    /**
     * \brief Store a value to a pointer.
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
      old_st.alloca_list = m_alloca_list;
      BlockSlotMapType::const_iterator new_st = m_block_state.find(block);
      PSI_ASSERT(new_st != m_block_state.end());
      m_type_map = new_st->second.types;
      m_value_map = new_st->second.values;
      m_alloca_list = new_st->second.alloca_list;
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

      typedef std::vector<std::pair<ValuePtr<Block>, ValuePtr<Block> > > BlockPairList;
      typedef std::vector<std::pair<ValuePtr<Phi>, LoweredValue> > PhiPairList;
      BlockPairList sorted_blocks;
      
      // Set up block mapping for all blocks. The entry block moves away from the
      // start of the function and is jumped to from the prolog block.
      for (Function::BlockList::const_iterator ii = old_function()->blocks().begin(), ie = old_function()->blocks().end(); ii != ie; ++ii) {
        ValuePtr<Block> dominator =
          (*ii)->dominator() ? rewrite_block((*ii)->dominator())
          : !sorted_blocks.empty() ? sorted_blocks.front().first
          : prolog_block;
        ValuePtr<Block> new_block = new_function()->new_block((*ii)->location(), dominator);
        sorted_blocks.push_back(std::make_pair(*ii, new_block));
        m_value_map.insert(std::make_pair(*ii, LoweredValue::register_(pass().block_type(), false, new_block)));
      }
      
      // Jump from prolog block to entry block
      InstructionBuilder(prolog_block).br(rewrite_block(old_function()->blocks().front()), prolog_block->location());
      
      // Generate PHI nodes and convert instructions!
      PhiPairList phi_nodes;
      for (std::vector<std::pair<ValuePtr<Block>, ValuePtr<Block> > >::iterator ii = sorted_blocks.begin(), ie = sorted_blocks.end(); ii != ie; ++ii) {
        const ValuePtr<Block>& old_block = ii->first;
        const ValuePtr<Block>& new_block = ii->second;

        // Set up variable map from dominating block
        switch_to_block(new_block->dominator());
        m_builder.set_insert_point(new_block);

        // Generate PHI nodes
        for (Block::PhiList::const_iterator ji = old_block->phi_nodes().begin(), je = old_block->phi_nodes().end(); ji != je; ++ji) {
          PhiPairList::value_type v(*ji, create_phi_node(new_block, rewrite_type((*ji)->type()), (*ji)->location()));
          phi_nodes.push_back(v);
          m_value_map.insert(v);
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
      for (PhiPairList::const_iterator ii = phi_nodes.begin(), ie = phi_nodes.end(); ii != ie; ++ii) {
        const ValuePtr<Phi>& old_phi_node = ii->first;
        const LoweredValue& new_phi_node = ii->second;
        PSI_ASSERT(!new_phi_node.empty());

        for (unsigned ki = 0, ke = old_phi_node->edges().size(); ki != ke; ++ki) {
          const PhiEdge& e = old_phi_node->edges()[ki];
          // Figure out which block we really jump to the target block from
          // A new block may have been created along the edge between the original source and target blocks
          PhiEdgeMapType::const_iterator li = m_edge_map.find(std::make_pair(e.block, old_phi_node->block()));
          if (li == m_edge_map.end())
            continue; // No jump exists along this edge
            
          if (li->second.second) {
            // Is an edge block
            switch_to_block(li->second.first->dominator());
            builder().set_insert_point(li->second.first);
          } else {
            // Is not an edge block
            switch_to_block(li->second.first);
          }
          
          create_phi_edge(new_phi_node, rewrite_value(e.value));
        }
      }
    }
    
    /**
     * \brief Notify that an alloca() instruction has been inserted.
     * 
     * Must be called for all alloca() instructions in the function.
     */
    void AggregateLoweringPass::FunctionRunner::alloca_push(const ValuePtr<Instruction>& alloc_insn) {
      PSI_ASSERT(isa<Alloca>(alloc_insn) || isa<AllocaConst>(alloc_insn));
      m_alloca_list = boost::make_shared<AllocaListEntry>(m_alloca_list, alloc_insn);
    }
    
    /**
     * \brief Free memory allocated by a specific instruction.
     * 
     * Also inserts \c freea operations for all \c alloca operations after the specified one.
     * If the instruction is not in the alloca list, no instructions are generated because
     * it should have already been freed due to some other instruction.
     * 
     * If \c alloc_insn is NULL, free all remaining allocas.
     */
    void AggregateLoweringPass::FunctionRunner::alloca_free(const ValuePtr<>& alloc_insn, const SourceLocation& location) {
      const AllocaListEntry *entry;
      
      if (alloc_insn) {
        // Look for instruction
        entry = m_alloca_list.get();
        for (; entry; entry = entry->next.get()) {
          if (entry->insn == alloc_insn)
            break;
        }

        // Instruction has already been freed
        if (!entry)
          return;
      }
      
      while (m_alloca_list) {
        bool last = (m_alloca_list->insn == alloc_insn);
        builder().freea(m_alloca_list->insn, location);
        m_alloca_list = m_alloca_list->next;
        if (last)
          break;
      }
    }

    /**
     * \brief Allocate space for a specific type.
     */
    ValuePtr<> AggregateLoweringPass::FunctionRunner::alloca_(const LoweredType& type, const SourceLocation& location) {
      ValuePtr<Instruction> stack_ptr;
      if (type.mode() == LoweredType::mode_register) {
        stack_ptr = builder().alloca_(type.register_type(), location);
      } else {
        stack_ptr = builder().alloca_(FunctionalBuilder::byte_type(context(), location), type.size(), type.alignment(), location);
      }
      alloca_push(stack_ptr);
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
        pass().error_context().error_throw(location, "Cannot load types not supported by the back-end into registers");
      
      default: PSI_FAIL("unexpected enum value");
      }
    }

    LoweredValue AggregateLoweringPass::FunctionRunner::bitcast(const LoweredType& type, const LoweredValue& input, const SourceLocation& location) {
      ValuePtr<> ptr = alloca_(input.type(), location);
      store_value(input, ptr, location);
      return load_value(type, ptr, location);
    }
    
    LoweredType AggregateLoweringPass::FunctionRunner::rewrite_type(const ValuePtr<>& type) {
      LoweredType global_lookup = pass().global_rewriter().lookup_type(type);
      if (!global_lookup.empty())
        return global_lookup;

      const LoweredType *lookup = m_type_map.lookup(type);
      if (lookup)
        return *lookup;
      
      LoweredType result;
      if (ValuePtr<Exists> exists = dyn_cast<Exists>(type)) {
        result = rewrite_type(unwrap_exists(exists));
      } else if (ValuePtr<HashableValue> func_type = dyn_cast<HashableValue>(type)) {
        result = type_term_rewrite(*this, func_type);
      } else if (isa<ParameterPlaceholder>(type)) {
        pass().error_context().error_throw(type->location(), "Encountered parameter placeholder in aggregate lowering");
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
    
    LoweredValue AggregateLoweringPass::FunctionRunner::rewrite_value(const ValuePtr<>& value) {
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
      } else if (ValuePtr<HashableValue> functional = dyn_cast<HashableValue>(value)) {
        result = hashable_term_rewrite(*this, functional);
      } else {
        pass().error_context().error_throw(value->location(), "Unexpected term type encountered in value lowering");
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
        pass().error_context().error_throw(location, "Phi nodes do not work with types not supported by the back-end");
      
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

      // The error in create_phi_node should be tripped before this one can be reached
      case LoweredType::mode_blob: PSI_FAIL("Phi nodes do not work with types not supported by the back-end");

      default: PSI_FAIL("unrecognised enum value");
      }
    }

    AggregateLoweringPass::ModuleLevelRewriter::ModuleLevelRewriter(AggregateLoweringPass *pass)
    : AggregateLoweringRewriter(pass) {
    }
    
    AggregateLoweringPass::ModuleLevelRewriter::~ModuleLevelRewriter() {
    }

    LoweredValue AggregateLoweringPass::ModuleLevelRewriter::bitcast(const LoweredType& type, const LoweredValue& input, const SourceLocation& location) {
      std::vector<ExplodeEntry> exploded;
      std::size_t offset = 0;
      pass().explode_lowered_value(input, exploded, offset, location, true);
      offset = 0;
      return pass().implode_lowered_value(type, exploded, offset, location);
    }
    
    LoweredType AggregateLoweringPass::ModuleLevelRewriter::rewrite_type(const ValuePtr<>& type) {
      const LoweredType *lookup = m_type_map.lookup(type);
      if (lookup)
        return *lookup;
      
      LoweredType result;
      if (ValuePtr<Exists> exists = dyn_cast<Exists>(type)) {
        result = rewrite_type(unwrap_exists(exists));
      } else if (ValuePtr<HashableValue> functional = dyn_cast<HashableValue>(type)) {
        result = type_term_rewrite(*this, functional);
      } else {
        pass().error_context().error_throw(type->location(), "Unexpected term type in type lowering");
      }
      
      PSI_ASSERT(!result.empty());
      m_type_map.insert(std::make_pair(type, result));
      return result;
    }
    
    LoweredValue AggregateLoweringPass::ModuleLevelRewriter::rewrite_value(const ValuePtr<>& value) {
      const LoweredValue *lookup = m_value_map.lookup(value);
      if (lookup)
        return *lookup;
      
      if (isa<Global>(value)) {
        PSI_ASSERT(value_cast<Global>(value)->module() != pass().source_module());
        pass().error_context().error_throw(value->location(), "Global from a different module encountered during lowering");
      } else if (isa<FunctionType>(value)) {
        pass().error_context().error_throw(value->location(), "Function type encountered in computed expression");
      }
      
      LoweredValue result;
      if (ValuePtr<Exists> exists = dyn_cast<Exists>(value)) {
        result = rewrite_value(unwrap_exists(exists));
      } else if (ValuePtr<FunctionalValue> functional = dyn_cast<FunctionalValue>(value)) {
        result = hashable_term_rewrite(*this, functional);
      } else {
        pass().error_context().error_throw(value->location(), "Non-functional value encountered in global expression: probably instruction or block which has not been inserted into a function");
      }
      
      PSI_ASSERT(!result.empty());
      m_value_map.insert(std::make_pair(value, result));
      return result;
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
    pointer_arithmetic_to_bytes(false),
    flatten_globals(false),
    memcpy_to_bytes(false) {
    }
    
    AggregateLoweringPass::~AggregateLoweringPass() {
    }

    /**
     * \brief Get the alignment of a lowered type.
     */
    std::size_t AggregateLoweringPass::lowered_type_alignment(const ValuePtr<>& type) {
      if (ValuePtr<StructType> st = dyn_cast<StructType>(type)) {
        std::size_t a = 1;
        for (unsigned ii = 0, ie = st->n_members(); ii != ie; ++ii)
          a = std::max(a, lowered_type_alignment(st->member_type(ii)));
        return a;
      } else if (ValuePtr<ArrayType> arr = dyn_cast<ArrayType>(type)) {
        return lowered_type_alignment(arr->element_type());
      } else if (ValuePtr<UnionType> un = dyn_cast<UnionType>(type)) {
        std::size_t a = 1;
        for (unsigned ii = 0, ie = un->n_members(); ii != ie; ++ii)
          a = std::max(a, lowered_type_alignment(un->member_type(ii)));
        return a;
      } else if (isa<EmptyType>(type)) {
        return 1;
      } else {
        return target_callback->type_size_alignment(type, type->location()).alignment;
      }
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
    
    /**
     * \brief Generate the type used to store a global variable.
     * 
     * \return First entry is the type, second is its alignment.
     */
    std::pair<ValuePtr<>, ValuePtr<> > AggregateLoweringPass::build_global_type(const ValuePtr<>& type, const SourceLocation& location) {
      std::vector<ExplodeEntry> entries;
      std::size_t offset = 0;
      LoweredType lowered_type = m_global_rewriter.rewrite_type(type);
      explode_lowered_type(lowered_type, entries, offset, location);
      ValuePtr<> global_type;
      if (entries.size() == 1) {
        global_type = entries.front().value;
      } else {
        std::vector<ValuePtr<> > type_entries;
        offset = 0;
        for (std::vector<ExplodeEntry>::const_iterator ji = entries.begin(), je = entries.end(); ji != je; ++ji) {
          std::size_t delta = (ji->offset - offset) / ji->tsa.alignment;
          if (delta >= 1) {
            std::pair<ValuePtr<>, std::size_t> pad_type = target_callback->type_from_size(context(), ji->tsa.alignment, location);
            std::size_t pad_delta = (ji->offset - offset) / pad_type.second;
            type_entries.insert(type_entries.end(), pad_delta, pad_type.first);
          }
          type_entries.push_back(ji->value);
          offset += ji->tsa.size;
        }
        global_type = FunctionalBuilder::struct_type(context(), type_entries, location);
      }
      
      return std::make_pair(global_type, lowered_type.alignment());
    }
    
    ValuePtr<> AggregateLoweringPass::build_global_value(const ValuePtr<>& source_value, const SourceLocation& location) {
      std::vector<ExplodeEntry> entries;
      std::size_t offset = 0;
      explode_lowered_value(m_global_rewriter.rewrite_value(source_value), entries, offset, location, false);
      
      ValuePtr<> target_value;
      if (entries.size() == 1) {
        target_value = entries.front().value;
      } else {
        std::vector<ValuePtr<> > value_entries;
        offset = 0;
        for (std::vector<ExplodeEntry>::const_iterator ji = entries.begin(), je = entries.end(); ji != je; ++ji) {
          std::size_t delta = (ji->offset - offset) / ji->tsa.alignment;
          if (delta >= 1) {
            std::pair<ValuePtr<>, std::size_t> pad_type = target_callback->type_from_size(context(), ji->tsa.alignment, location);
            std::size_t pad_delta = (ji->offset - offset) / pad_type.second;
            ValuePtr<> pad_value = FunctionalBuilder::undef(pad_type.first, location);
            value_entries.insert(value_entries.end(), pad_delta, pad_value);
          }
          value_entries.push_back(ji->value);
          offset += ji->tsa.size;
        }
        target_value = FunctionalBuilder::struct_value(context(), value_entries, location);
      }
      
      return target_value;
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
          std::pair<ValuePtr<>, ValuePtr<> > type_alignment = build_global_type(old_var->value_type(), term->location());
          ValuePtr<GlobalVariable> new_var = target_module()->new_global_variable(old_var->name(), type_alignment.first, term->location());
          new_var->set_constant(old_var->constant());
          new_var->set_merge(old_var->merge());
          new_var->set_linkage(old_var->linkage());
          
          if (old_var->alignment()) {
            LoweredValueSimple old_align = m_global_rewriter.rewrite_value_register(old_var->alignment());
            PSI_ASSERT(old_align.global);
            new_var->set_alignment(FunctionalBuilder::max(type_alignment.second, old_align.value, term->location()));
          } else {
            new_var->set_alignment(type_alignment.second);
          }

          ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(new_var, byte_type, term->location());
          global_rewriter().m_value_map.insert(std::make_pair(old_var, LoweredValue::register_(pointer_type(), true, cast_ptr)));
          rewrite_globals.push_back(std::make_pair(old_var, new_var));
        } else {
          ValuePtr<Function> old_function = value_cast<Function>(term);
          boost::shared_ptr<FunctionRunner> runner(new FunctionRunner(this, old_function));
          ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(runner->new_function(), byte_type, term->location());
          runner->new_function()->set_linkage(old_function->linkage());
          runner->new_function()->set_alignment(old_function->alignment());
          global_rewriter().m_value_map.insert(std::make_pair(old_function, LoweredValue::register_(pointer_type(), true, cast_ptr)));
          rewrite_functions.push_back(std::make_pair(old_function, runner));
        }
      }
      
      for (std::vector<std::pair<ValuePtr<GlobalVariable>, ValuePtr<GlobalVariable> > >::iterator
           i = rewrite_globals.begin(), e = rewrite_globals.end(); i != e; ++i) {
        ValuePtr<GlobalVariable> source = i->first, target = i->second;
        if (source->value())
          target->set_value(build_global_value(source->value(), source->value()->location()));
        
        global_map_put(i->first, i->second);
      }
      
      for (std::vector<std::pair<ValuePtr<Function>, boost::shared_ptr<FunctionRunner> > >::iterator
           i = rewrite_functions.begin(), e = rewrite_functions.end(); i != e; ++i) {
        i->second->run();
        global_map_put(i->first, i->second->new_function());
      }
    }
    
    void AggregateLoweringPass::explode_lowered_type(const LoweredType& type, std::vector<ExplodeEntry>& entries, std::size_t& offset, const SourceLocation& location) {
      TypeSizeAlignment tsa = type.size_alignment_const();
      offset = (offset + tsa.alignment - 1) & ~(tsa.alignment - 1);
      PSI_ASSERT((tsa.alignment != 0) && ((tsa.alignment & (tsa.alignment - 1)) == 0)); // Power of 2 check
      
      if (tsa.size == 0)
        return;

      switch (type.mode()) {
      case LoweredType::mode_register: {
        ExplodeEntry entry = {offset, tsa, type.register_type()};
        entries.push_back(entry);
        offset += tsa.size;
        break;
      }
      
      case LoweredType::mode_split:
        for (LoweredType::EntryVector::const_iterator ii = type.split_entries().begin(), ie = type.split_entries().end(); ii != ie; ++ii)
          explode_lowered_type(*ii, entries, offset, location);
        break;
        
      default: PSI_FAIL("Unknown LoweredType mode");
      }

      offset = (offset + tsa.alignment - 1) & ~(tsa.alignment - 1);
    }
    
    void AggregateLoweringPass::explode_constant_value(const ValuePtr<>& value, std::vector<ExplodeEntry>& entries, std::size_t& offset, const SourceLocation& location, bool expand_aggregates) {
      TypeSizeAlignment tsa = m_global_rewriter.rewrite_type(value->type()).size_alignment_const();
      offset = (offset + tsa.alignment - 1) & ~(tsa.alignment - 1);
      PSI_ASSERT((tsa.alignment != 0) && ((tsa.alignment & (tsa.alignment - 1)) == 0)); // Power of 2 check

      if (tsa.size == 0)
        return;

      if (!expand_aggregates || isa<IntegerType>(value->type()) || isa<PointerType>(value->type()) || isa<FloatType>(value->type())) {
        ExplodeEntry entry = {offset, tsa, value};
        entries.push_back(entry);
        offset += tsa.size;
      } else if (ValuePtr<StructValue> struct_val = dyn_cast<StructValue>(value)) {
        for (unsigned ii = 0, ie = struct_val->n_members(); ii != ie; ++ii)
          explode_constant_value(struct_val->member_value(ii), entries, offset, location, expand_aggregates);
        offset = (offset + tsa.alignment - 1) & ~(tsa.alignment - 1);
      } else if (ValuePtr<ArrayValue> array_val = dyn_cast<ArrayValue>(value)) {
        for (unsigned ii = 0, ie = array_val->length(); ii != ie; ++ii)
          explode_constant_value(array_val->value(ii), entries, offset, location, expand_aggregates);
        offset = (offset + tsa.alignment - 1) & ~(tsa.alignment - 1);
      } else {
        PSI_FAIL("Unexpected tree type in constant explosion (unions should be handled by UnionValue operation lowering)");
      }
    }
    
    struct AggregateLoweringPass::ExplodeCompareStart {
      bool operator () (const ExplodeEntry& a, const ExplodeEntry& b) const {
        return a.offset < b.offset;
      }
    };

    struct AggregateLoweringPass::ExplodeCompareEnd {
      bool operator () (const ExplodeEntry& a, const ExplodeEntry& b) const {
        return a.offset + a.tsa.size < b.offset + b.tsa.size;
      }
    };
    
    ValuePtr<> AggregateLoweringPass::implode_constant_value(const ValuePtr<>& type, const std::vector<ExplodeEntry>& entries, std::size_t& offset, const SourceLocation& location) {
      TypeSizeAlignment tsa = m_global_rewriter.rewrite_type(type).size_alignment_const();
      offset = (offset + tsa.alignment - 1) & ~(tsa.alignment - 1);
      PSI_ASSERT((tsa.alignment != 0) && ((tsa.alignment & (tsa.alignment - 1)) == 0)); // Power of 2 check

      if (isa<IntegerType>(type) || isa<PointerType>(type) || isa<FloatType>(type)) {
        ExplodeEntry start = {offset, TypeSizeAlignment(0,0), ValuePtr<>()}, end = {offset + tsa.size, TypeSizeAlignment(0,0), ValuePtr<>()};
        std::vector<ExplodeEntry>::const_iterator first = std::upper_bound(entries.begin(), entries.end(), start, ExplodeCompareEnd());
        std::vector<ExplodeEntry>::const_iterator last = std::lower_bound(entries.begin(), entries.end(), end, ExplodeCompareStart());
    
        ValuePtr<> result;
        if ((std::distance(first, last) == 1) && (first->tsa.size == tsa.size) && (first->offset == offset)) {
          result = FunctionalBuilder::bit_cast(first->value, type, location);
        } else {
          std::pair<ValuePtr<>, std::size_t> integer_type = target_callback->type_from_size(context(), tsa.alignment, location);
          PSI_ASSERT(tsa.size == integer_type.second);
          result = FunctionalBuilder::zero(integer_type.first, location);
          for (; first != last; ++first) {
            ValuePtr<> shifted = target_callback->byte_shift(first->value, integer_type.first, offset - first->offset, location);
            result = FunctionalBuilder::bit_or(result, shifted, location);
          }
          result = FunctionalBuilder::bit_cast(result, type, location);
        }
        
        offset += tsa.size;
        return result;
      } else if (ValuePtr<StructType> struct_ty = dyn_cast<StructType>(type)) {
        std::vector<ValuePtr<> > members;
        for (unsigned ii = 0, ie = struct_ty->n_members(); ii != ie; ++ii)
          members.push_back(implode_constant_value(struct_ty->member_type(ii), entries, offset, location));
        offset = (offset + tsa.alignment - 1) & ~(tsa.alignment - 1);
        return FunctionalBuilder::struct_value(context(), members, location);
      } else if (ValuePtr<ArrayType> array_ty = dyn_cast<ArrayType>(type)) {
        ValuePtr<IntegerValue> length_val = dyn_cast<IntegerValue>(array_ty->length());
        if (!length_val)
          error_context().error_throw(location, "Array length not constant in value implosion");
        std::vector<ValuePtr<> > elements;
        for (unsigned ii = 0, ie = length_val->value().unsigned_value_checked(error_context().bind(location)); ii != ie; ++ii)
          elements.push_back(implode_constant_value(array_ty->element_type(), entries, offset, location));
        offset = (offset + tsa.alignment - 1) & ~(tsa.alignment - 1);
        return FunctionalBuilder::array_value(array_ty->element_type(), elements, location);
      } else {
        PSI_FAIL("Unexpected tree type in constant implosion (unions should be handled by UnionValue operation lowering)");
      }
    }
    
    void AggregateLoweringPass::explode_lowered_value(const LoweredValue& value, std::vector<ExplodeEntry>& entries, std::size_t& offset, const SourceLocation& location, bool expand_aggregates) {
      TypeSizeAlignment tsa = value.type().size_alignment_const();
      PSI_ASSERT((tsa.alignment != 0) && ((tsa.alignment & (tsa.alignment - 1)) == 0)); // Power of 2 check
      offset = (offset + tsa.alignment - 1) & ~(tsa.alignment - 1);
      
      if (tsa.size == 0)
        return;
      
      switch (value.mode()) {
      case LoweredValue::mode_register:
        explode_constant_value(value.register_value(), entries, offset, location, expand_aggregates);
        return;
      
      case LoweredValue::mode_split:
        for (LoweredValue::EntryVector::const_iterator ii = value.split_entries().begin(), ie = value.split_entries().end(); ii != ie; ++ii)
          explode_lowered_value(*ii, entries, offset, location, expand_aggregates);
        offset = (offset + tsa.alignment - 1) & ~(tsa.alignment - 1);
        return;
      
      default: PSI_FAIL("unexpected LoweredValue mode");
      }
    }
    
    LoweredValue AggregateLoweringPass::implode_lowered_value(const LoweredType& type, const std::vector<ExplodeEntry>& entries, std::size_t& offset, const SourceLocation& location) {
      TypeSizeAlignment tsa = type.size_alignment_const();
      PSI_ASSERT((tsa.alignment != 0) && ((tsa.alignment & (tsa.alignment - 1)) == 0)); // Power of 2 check
      offset = (offset + tsa.alignment - 1) & ~(tsa.alignment - 1);

      switch (type.mode()) {
      case LoweredType::mode_register:
        return LoweredValue::register_(type, true, implode_constant_value(type.register_type(), entries, offset, location));
      
      case LoweredType::mode_split: {
        LoweredValue::EntryVector value_entries;
        for (LoweredType::EntryVector::const_iterator ii = type.split_entries().begin(), ie = type.split_entries().end(); ii != ie; ++ii)
          value_entries.push_back(implode_lowered_value(*ii, entries, offset, location));
        offset = (offset + tsa.alignment - 1) & ~(tsa.alignment - 1);
        return LoweredValue::split(type, value_entries);
      }
      
      default: PSI_FAIL("unexpected LoweredType mode");
      }
    }
  }
}
