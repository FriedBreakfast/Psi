#include "AggregateLowering.hpp"
#include "Aggregate.hpp"
#include "Instructions.hpp"
#include "InstructionBuilder.hpp"
#include "FunctionalBuilder.hpp"
#include "TermOperationMap.hpp"
#include "Utility.hpp"

#include <boost/next_prior.hpp>
#include <string.h>

namespace Psi {
  namespace Tvm {
    struct AggregateLoweringPass::TypeTermRewriter {
      static Type array_type_rewrite(FunctionRunner& rewriter, ArrayType::Ptr term) {
        if (!rewriter.pass().remove_only_unknown)
          return Type();

        Term *length = rewriter.rewrite_value_stack(term->length());
        Type element_type = rewriter.rewrite_type(term->element_type());
        Term *stack_type = 0, *heap_type = 0;
        if (element_type.stack_type() && !rewriter.pass().remove_stack_arrays)
          stack_type = FunctionalBuilder::array_type(element_type.stack_type(), length);
        if (element_type.heap_type())
          heap_type = FunctionalBuilder::array_type(element_type.heap_type(), length);
        return Type(stack_type, heap_type);
      }

      static Type struct_type_rewrite(FunctionRunner& rewriter, StructType::Ptr term) {
        if (!rewriter.pass().remove_only_unknown)
          return Type();
        
        ScopedArray<Term*> stack_members(term->n_members()), heap_members(term->n_members());
        bool stack_simple = true, heap_simple = true;
        for (unsigned i = 0; i != stack_members.size(); ++i) {
          Type member_type = rewriter.rewrite_type(term->member_type(i));
          stack_members[i] = member_type.stack_type();
          stack_simple = stack_simple && member_type.stack_type();
          heap_members[i] = member_type.heap_type();
          heap_simple = heap_simple && member_type.heap_type();
        }
        
        Term *stack_type = stack_simple ? FunctionalBuilder::struct_type(rewriter.context(), stack_members) : 0;
        Term *heap_type = heap_simple ? FunctionalBuilder::struct_type(rewriter.context(), heap_members) : 0;
        
        return Type(stack_type, heap_type);
      }

      static Type union_type_rewrite(FunctionRunner& rewriter, UnionType::Ptr term) {
        if (!rewriter.pass().remove_only_unknown || rewriter.pass().remove_all_unions)
          return Type();
        
        ScopedArray<Term*> stack_members(term->n_members()), heap_members(term->n_members());
        bool stack_simple = true, heap_simple = true;
        for (unsigned i = 0; i != stack_members.size(); ++i) {
          Type member_type = rewriter.rewrite_type(term->member_type(i));
          stack_members[i] = member_type.stack_type();
          stack_simple = stack_simple && member_type.stack_type();
          heap_members[i] = member_type.heap_type();
          heap_simple = heap_simple && member_type.heap_type();
        }
        
        Term *stack_type = stack_simple ? FunctionalBuilder::union_type(rewriter.context(), stack_members) : 0;
        Term *heap_type = heap_simple ? FunctionalBuilder::union_type(rewriter.context(), heap_members) : 0;
        
        return Type(stack_type, heap_type);
      }
      
      static Type unknown_type_rewrite(FunctionRunner&, MetatypeValue::Ptr) {
        return Type();
      }
      
      static Type default_rewrite(FunctionRunner&, FunctionalTerm *term) {
        PSI_ASSERT(term->is_type());
        PSI_ASSERT(term->global());
        return Type(term);
      }
      
      typedef TermOperationMap<FunctionalTerm, Type, FunctionRunner> CallbackMap;
      static CallbackMap callback_map;
      
      static CallbackMap callback_map_initializer() {
        return CallbackMap::initializer(default_rewrite)
          .add<ArrayType>(array_type_rewrite)
          .add<StructType>(struct_type_rewrite)
          .add<UnionType>(union_type_rewrite)
          .add<MetatypeValue>(unknown_type_rewrite);
      }
    };
    
    AggregateLoweringPass::TypeTermRewriter::CallbackMap
      AggregateLoweringPass::TypeTermRewriter::callback_map =
      AggregateLoweringPass::TypeTermRewriter::callback_map_initializer();
      
    struct AggregateLoweringPass::FunctionalTermRewriter {
      static Value aggregate_type_rewrite(FunctionRunner& rewriter, Term *term) {
        Type ty = rewriter.rewrite_type(term);
        Term *result = ty.heap_type();
        if (!result) {
          Term *size = rewriter.rewrite_value_stack(FunctionalBuilder::type_size(term));
          Term *alignment = rewriter.rewrite_value_stack(FunctionalBuilder::type_alignment(term));
          result = FunctionalBuilder::type_value(size, alignment);
        }
        return Value(result, true);
      }

      static Value default_rewrite(FunctionRunner& rewriter, FunctionalTerm *term) {
        PSI_ASSERT(rewriter.rewrite_type(term->type()).stack_type());
        unsigned n_parameters = term->n_parameters();
        ScopedArray<Term*> parameters(n_parameters);
        for (unsigned i = 0; i != n_parameters; ++i)
          parameters[i] = rewriter.rewrite_value_stack(term->parameter(i));
        return Value(term->rewrite(parameters), true);
      }
      
      static Value aggregate_value_rewrite(FunctionRunner& rewriter, FunctionalTerm *term) {
        Type term_type = rewriter.rewrite_type(term->type());
        if (term_type.stack_type()) {
          return default_rewrite(rewriter, term);
        } else {
          Term *term_ptr = 0;
          if (term_type.heap_type()) {
            term_ptr = rewriter.builder().alloca_(term_type.heap_type());
          } else {
            if (ArrayValue::Ptr array_val = dyn_cast<ArrayValue>(term)) {
              Type element_type = rewriter.rewrite_type(array_val->element_type());
              if (element_type.heap_type())
                term_ptr = rewriter.builder().alloca_(element_type.heap_type(), array_val->length());
            }

            if (!term_ptr) {
              Term *type_size = rewriter.rewrite_value_stack(FunctionalBuilder::type_size(term->type()));
              Term *type_alignment = rewriter.rewrite_value_stack(FunctionalBuilder::type_alignment(term->type()));
              term_ptr = rewriter.builder().alloca_(FunctionalBuilder::byte_type(rewriter.context()),
                                                    type_size, type_alignment);
            }
          }
          rewriter.store_value(term, term_ptr);
          return Value(term_ptr, false);
        }
      }
      
      static Term* array_ptr_offset(FunctionRunner& rewriter, ArrayType::Ptr array_ty, Term *base, Term *index) {
        Type element_ty = rewriter.rewrite_type(array_ty->element_type());
        if (element_ty.heap_type()) {
          Term *cast_ptr = FunctionalBuilder::pointer_cast(base, element_ty.heap_type());
          return FunctionalBuilder::pointer_offset(cast_ptr, index);
        }

        Term *element_size = rewriter.rewrite_value_stack(FunctionalBuilder::type_size(array_ty->element_type()));
        Term *offset = FunctionalBuilder::mul(element_size, index);
        PSI_ASSERT(base->type() == FunctionalBuilder::byte_pointer_type(rewriter.context()));
        return FunctionalBuilder::pointer_offset(base, offset);
      }

      static Value array_element_rewrite(FunctionRunner& rewriter, ArrayElement::Ptr term) {
        Term *index = rewriter.rewrite_value_stack(term->aggregate());
        Value array_val = rewriter.rewrite_value(term->aggregate());
        if (array_val.on_stack()) {
          return Value(FunctionalBuilder::array_element(array_val.value(), index), true);
        } else {
          Term *element_ptr = array_ptr_offset(rewriter, term->aggregate_type(), array_val.value(), index);
          return rewriter.load_value(term, element_ptr, false);
        }
      }

      static Value array_element_ptr_rewrite(FunctionRunner& rewriter, ArrayElementPtr::Ptr term) {
        Term *array_ptr = rewriter.rewrite_value_stack(term->aggregate_ptr());
        Term *index = rewriter.rewrite_value_stack(term->index());
        
        Type array_ty = rewriter.rewrite_type(term->aggregate_type());
        if (array_ty.heap_type())
          return Value(FunctionalBuilder::array_element_ptr(array_ptr, index), true);
        
        return Value(array_ptr_offset(rewriter, term->aggregate_type(), array_ptr, index), true);
      }
      
      static Term* struct_ptr_offset(FunctionRunner& rewriter, StructType::Ptr struct_ty, Term *base, unsigned index) {
        Type struct_ty_rewritten = rewriter.rewrite_type(struct_ty);
        if (struct_ty_rewritten.heap_type()) {
          Term *cast_ptr = FunctionalBuilder::pointer_cast(base, struct_ty);
          return FunctionalBuilder::struct_element_ptr(cast_ptr, index);
        }
        
        PSI_ASSERT(base->type() == FunctionalBuilder::byte_pointer_type(rewriter.context()));
        Term *offset = rewriter.rewrite_value_stack(FunctionalBuilder::struct_element_offset(struct_ty, index));
        return FunctionalBuilder::pointer_offset(base, offset);
      }
      
      static Value struct_element_rewrite(FunctionRunner& rewriter, StructElement::Ptr term) {
        Value struct_val = rewriter.rewrite_value(term->aggregate());
        if (struct_val.on_stack()) {
          return Value(FunctionalBuilder::struct_element(struct_val.value(), term->index()), true);
        } else {
          Term *member_ptr = struct_ptr_offset(rewriter, term->aggregate_type(), struct_val.value(), term->index());
          return rewriter.load_value(term, member_ptr, false);
        }
      }

      static Value struct_element_ptr_rewrite(FunctionRunner& rewriter, StructElementPtr::Ptr term) {
        Term *struct_ptr = rewriter.rewrite_value_stack(term->aggregate_ptr());
        
        Type struct_ty = rewriter.rewrite_type(term->aggregate_type());
        if (struct_ty.heap_type())
          return Value(FunctionalBuilder::struct_element_ptr(struct_ptr, term->index()), true);
        
        return Value(struct_ptr_offset(rewriter, term->aggregate_type(), struct_ptr, term->index()), true);
      }

      static Value union_element_rewrite(FunctionRunner& rewriter, UnionElement::Ptr term) {
        Value union_val = rewriter.rewrite_value(term->aggregate());
        if (union_val.on_stack()) {
          Term *member_type = rewriter.rewrite_value_stack(term->member_type());
          return Value(FunctionalBuilder::union_element(union_val.value(), member_type), true);
        } else {
          Type member_ty = rewriter.rewrite_type(term->type());
          return rewriter.load_value(term, union_val.value(), false);
        }
      }

      static Value union_element_ptr_rewrite(FunctionRunner& rewriter, UnionElementPtr::Ptr term) {
        Term *union_ptr = rewriter.rewrite_value_stack(term->aggregate_ptr());
        
        Type member_type = rewriter.rewrite_type(term->type());
        Type union_ty = rewriter.rewrite_type(term->aggregate_type());
        if (union_ty.heap_type()) {
          PSI_ASSERT(member_type.heap_type());
          return Value(FunctionalBuilder::union_element_ptr(union_ptr, member_type.heap_type()), true);
        }
        
        if (member_type.heap_type()) {
          return Value(FunctionalBuilder::pointer_cast(union_ptr, member_type.heap_type()), true);
        } else {
          return Value(FunctionalBuilder::pointer_cast(union_ptr, FunctionalBuilder::byte_type(rewriter.context())), true);
        }
      }
      
      typedef TermOperationMap<FunctionalTerm, Value, FunctionRunner> CallbackMap;
      static CallbackMap callback_map;
      
      static CallbackMap callback_map_initializer() {
        return CallbackMap::initializer(default_rewrite)
          .add<ArrayType>(aggregate_type_rewrite)
          .add<StructType>(aggregate_type_rewrite)
          .add<UnionType>(aggregate_type_rewrite)
          .add<ArrayValue>(aggregate_value_rewrite)
          .add<StructValue>(aggregate_value_rewrite)
          .add<UnionValue>(aggregate_value_rewrite)
          .add<ArrayElement>(array_element_rewrite)
          .add<StructElement>(struct_element_rewrite)
          .add<UnionElement>(union_element_rewrite)
          .add<ArrayElementPtr>(array_element_ptr_rewrite)
          .add<StructElementPtr>(struct_element_ptr_rewrite)
          .add<UnionElementPtr>(union_element_ptr_rewrite);
      }
    };

    AggregateLoweringPass::FunctionalTermRewriter::CallbackMap
      AggregateLoweringPass::FunctionalTermRewriter::callback_map =
      AggregateLoweringPass::FunctionalTermRewriter::callback_map_initializer();

    struct AggregateLoweringPass::InstructionTermRewriter {
      static Value return_rewrite(FunctionRunner& runner, Return::Ptr term) {
        return Value(runner.pass().target_callback->lower_return(runner, term->value()), true);
      }

      static Value br_rewrite(FunctionRunner& runner, UnconditionalBranch::Ptr term) {
        Term *target = runner.rewrite_value_stack(term->target());
        return Value(runner.builder().br(target), true);
      }

      static Value cond_br_rewrite(FunctionRunner& runner, ConditionalBranch::Ptr term) {
        Term *cond = runner.rewrite_value_stack(term->condition());
        Term *true_target = runner.rewrite_value_stack(term->true_target());
        Term *false_target = runner.rewrite_value_stack(term->false_target());
        return Value(runner.builder().cond_br(cond, true_target, false_target), true);
      }

      static Value call_rewrite(FunctionRunner& runner, FunctionCall::Ptr term) {
        return runner.pass().target_callback->lower_function_call(runner, term);
      }

      static Value alloca_rewrite(FunctionRunner& runner, Alloca::Ptr term) {
        Type type = runner.rewrite_type(term->stored_type());
        Term *count = runner.rewrite_value_stack(term->count());
        Term *alignment = runner.rewrite_value_stack(term->alignment());
        if (type.heap_type()) {
          return Value(runner.builder().alloca_(type.heap_type(), count, alignment), true);
        } else {
          Term *type_size = runner.rewrite_value_stack(FunctionalBuilder::type_size(term->stored_type()));
          Term *type_alignment = runner.rewrite_value_stack(FunctionalBuilder::type_alignment(term->stored_type()));
          Term *total_size = FunctionalBuilder::mul(count, type_size);
          return Value(runner.builder().alloca_(FunctionalBuilder::byte_type(runner.context()), total_size, type_alignment), true);
        }
      }
      
      static Value load_rewrite(FunctionRunner& runner, Load::Ptr term) {
        Term *ptr = runner.rewrite_value_stack(term->target());
        return runner.load_value(term, ptr);
      }
      
      static Value store_rewrite(FunctionRunner& runner, Store::Ptr term) {
        Term *ptr = runner.rewrite_value_stack(term->target());
        return runner.store_value(term->value(), ptr);
      }
      
      static Value memcpy_rewrite(FunctionRunner& runner, MemCpy::Ptr term) {
        Term *dest = runner.rewrite_value_stack(term->dest());
        Term *src = runner.rewrite_value_stack(term->src());
        Term *count = runner.rewrite_value_stack(term->count());
        Term *alignment = runner.rewrite_value_stack(term->alignment());
        
        Term *original_element_type = cast<PointerType>(term->dest()->type())->target_type();
        Type element_type = runner.rewrite_type(original_element_type);
        if (element_type.heap_type()) {
          Term *dest_cast = FunctionalBuilder::pointer_cast(dest, element_type.heap_type());
          Term *src_cast = FunctionalBuilder::pointer_cast(dest, element_type.heap_type());
          return Value(runner.builder().memcpy(dest_cast, src_cast, count, alignment), true);
        } else {
          PSI_ASSERT(dest->type() == FunctionalBuilder::byte_pointer_type(runner.context()));
          Term *type_size = runner.rewrite_value_stack(FunctionalBuilder::type_size(original_element_type));
          Term *type_alignment = runner.rewrite_value_stack(FunctionalBuilder::type_alignment(original_element_type));
          Term *bytes = FunctionalBuilder::mul(count, type_size);
          Term *max_alignment = FunctionalBuilder::max(alignment, type_alignment);
          return Value(runner.builder().memcpy(dest, src, bytes, max_alignment), true);
        }
      }
      
      typedef TermOperationMap<InstructionTerm, Value, FunctionRunner> CallbackMap;
      static CallbackMap callback_map;
      
      static CallbackMap callback_map_initializer() {
        return CallbackMap::initializer()
          .add<Return>(return_rewrite)
          .add<UnconditionalBranch>(br_rewrite)
          .add<ConditionalBranch>(cond_br_rewrite)
          .add<FunctionCall>(call_rewrite)
          .add<Alloca>(alloca_rewrite)
          .add<Store>(store_rewrite)
          .add<Load>(load_rewrite);
      }
    };
    
    AggregateLoweringPass::InstructionTermRewriter::CallbackMap
      AggregateLoweringPass::InstructionTermRewriter::callback_map =
      AggregateLoweringPass::InstructionTermRewriter::callback_map_initializer();

    AggregateLoweringPass::FunctionRunner::FunctionRunner(AggregateLoweringPass* pass, FunctionTerm* old_function)
    : GlobalTermRewriter(), m_pass(pass), m_old_function(old_function) {
      FunctionTypeTerm *old_function_type = old_function->function_type();
      unsigned n_phantom_parameters = old_function_type->n_phantom_parameters();
      unsigned n_real_parameters = old_function_type->n_parameters() - n_phantom_parameters;
      ScopedArray<Value> parameters(n_real_parameters);
      FunctionTerm *new_function = pass->target_callback->lower_function(*this, old_function, parameters);
      PSI_ASSERT(new_function->n_parameters() == n_real_parameters);
      
      // Put parameters into map
      for (unsigned i = 0; i != n_real_parameters; ++i)
        m_value_map.insert(std::make_pair(old_function->parameter(i + n_phantom_parameters), parameters[i]));
      
      pass->m_term_map.m_value_map.insert(std::make_pair(old_function, Value(new_function, true)));
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
    AggregateLoweringPass::Value AggregateLoweringPass::FunctionRunner::store_value(Term *value, Term *ptr) {
      PSI_ASSERT(isa<PointerType>(ptr->type()));
      
      Type value_type = rewrite_type(value->type());
      if (value_type.stack_type()) {
        Term *cast_ptr = FunctionalBuilder::pointer_cast(ptr, value_type.stack_type());
        Term *stack_value = rewrite_value_stack(value);
        return Value(builder().store(stack_value, cast_ptr), true);
      }

      if (ArrayValue::Ptr array_val = dyn_cast<ArrayValue>(value)) {
        Value result;
        Type element_type = rewrite_type(array_val->element_type());
        if (element_type.heap_type()) {
          Term *base_ptr = FunctionalBuilder::pointer_cast(ptr, element_type.heap_type());
          for (unsigned i = 0, e = array_val->length(); i != e; ++i) {
            Term *element_ptr = FunctionalBuilder::pointer_offset(base_ptr, i);
            result = store_value(array_val->value(i), element_ptr);
          }
        } else {
          PSI_ASSERT(ptr->type() == FunctionalBuilder::byte_pointer_type(context()));
          Term *element_size = rewrite_value_stack(FunctionalBuilder::type_size(array_val->element_type()));
          Term *element_ptr = ptr;
          for (unsigned i = 0, e = array_val->length(); i != e; ++i) {
            result = store_value(array_val->value(i), element_ptr);
            element_ptr = FunctionalBuilder::pointer_offset(element_ptr, element_size);
          }
        }
        return result;
      } else if (UnionValue::Ptr union_val = dyn_cast<UnionValue>(value)) {
        return store_value(union_val->value(), ptr);
      }
      
      if (StructType::Ptr struct_ty = dyn_cast<StructType>(value->type())) {
        PSI_ASSERT(ptr->type() == FunctionalBuilder::byte_pointer_type(context()));
        Value result;
        for (unsigned i = 0, e = struct_ty->n_members(); i != e; ++i) {
          Term *offset = rewrite_value_stack(FunctionalBuilder::struct_element_offset(struct_ty, i));
          Term *member_ptr = FunctionalBuilder::pointer_offset(ptr, offset);
          result = store_value(FunctionalBuilder::struct_element(value, i), member_ptr);
        }
        return result;
      }

      if (ArrayType::Ptr array_ty = dyn_cast<ArrayType>(value->type())) {
        Type element_type = rewrite_type(array_ty->element_type());
        if (element_type.heap_type()) {
          Term *value_ptr = rewrite_value_ptr(value);
          Term *cast_ptr = FunctionalBuilder::pointer_cast(ptr, element_type.heap_type());
          return Value(builder().memcpy(cast_ptr, value_ptr, array_ty->length()), true);
        }
      }

      Term *value_ptr = rewrite_value_ptr(value);
      PSI_ASSERT(value_ptr->type() == FunctionalBuilder::byte_pointer_type(context()));
      Term *value_size = rewrite_value_stack(FunctionalBuilder::type_size(value->type()));
      Term *value_alignment = rewrite_value_stack(FunctionalBuilder::type_alignment(value->type()));
      return Value(builder().memcpy(ptr, value_ptr, value_size, value_alignment), true);
    }

    /**
     * Run this pass on a single function.
     */
    void AggregateLoweringPass::FunctionRunner::run() {
      InstructionBuilder insn_builder;

      /*
       * Check whether any instructions were inserted at the beginning of the
       * function and decide whether a new entry block is necessary in case
       * the user jumps back to the start of the function.
       */
      BlockTerm *prolog_block;
      BlockTerm *entry_block;
      if (new_function()->entry() && !new_function()->entry()->instructions().empty()) {
        prolog_block = new_function()->entry();
        entry_block = new_function()->new_block(prolog_block);
        insn_builder.set_insert_point(prolog_block);
        insn_builder.br(entry_block);
      } else {
        prolog_block = 0;
        entry_block = new_function()->entry();
      }
      m_value_map[old_function()->entry()] = Value(entry_block, true);

      std::vector<BlockTerm*> sorted_blocks = topsort_blocks();
      PSI_ASSERT(!sorted_blocks.empty());
      PSI_ASSERT(sorted_blocks[0] == old_function()->entry());
      
      // Set up block mapping for all blocks except the entry block,
      // which has already been handled
      for (std::vector<BlockTerm*>::iterator it = boost::next(sorted_blocks.begin());
           it != sorted_blocks.end(); ++it) {
        BlockTerm *dominator = prolog_block;
        ValueMapType::iterator dominator_it = m_value_map.find((*it)->dominator());
        if (dominator_it != m_value_map.end()) {
          PSI_ASSERT(dominator_it->second.on_stack());
          dominator = cast<BlockTerm>(dominator_it->second.value());
        }
        BlockTerm *new_block = new_function()->new_block(dominator);
        m_value_map.insert(std::make_pair(*it, Value(new_block, true)));
      }
      
      // Convert instructions!
      for (std::vector<BlockTerm*>::iterator it = sorted_blocks.begin(); it != sorted_blocks.end(); ++it) {
        BlockTerm *old_block = *it;
        BlockTerm *new_block = cast<BlockTerm>(m_value_map[*it].value());
        PSI_ASSERT(new_block);
        BlockTerm::InstructionList& insn_list = old_block->instructions();
        for (BlockTerm::InstructionList::iterator jt = insn_list.begin(); jt != insn_list.end(); ++jt) {
          InstructionTerm *insn = &*jt;
          Value value = InstructionTermRewriter::callback_map.call(*this, insn);
          if (value.value())
            m_value_map[insn] = value;
        }
      }
    }

    /**
     * Work out the order in which to build the blocks so all instruction values
     * required to build an instruction are available.
     */
    std::vector<BlockTerm*> AggregateLoweringPass::FunctionRunner::topsort_blocks() {
      // Set up basic blocks
      BlockTerm* entry_block = old_function()->entry();
      std::tr1::unordered_set<BlockTerm*> visited_blocks;
      std::vector<BlockTerm*> block_queue;
      std::vector<BlockTerm*> blocks;
      visited_blocks.insert(entry_block);
      block_queue.push_back(entry_block);
      blocks.push_back(entry_block);

      // find root block set
      while (!block_queue.empty()) {
        BlockTerm *bl = block_queue.back();
        block_queue.pop_back();

        if (!bl->terminated())
          throw TvmUserError("cannot perform aggregate lowering on function with unterminated blocks");

        std::vector<BlockTerm*> successors = bl->successors();
        for (std::vector<BlockTerm*>::iterator it = successors.begin();
              it != successors.end(); ++it) {
          std::pair<std::tr1::unordered_set<BlockTerm*>::iterator, bool> p = visited_blocks.insert(*it);
          if (p.second) {
            block_queue.push_back(*it);
            if (!(*it)->dominator())
              blocks.push_back(*it);
          }
        }
      }

      // get remaining blocks in topological order
      for (std::size_t i = 0; i < blocks.size(); ++i) {
        std::vector<BlockTerm*> dominated = blocks[i]->dominated_blocks();
        for (std::vector<BlockTerm*>::iterator it = dominated.begin(); it != dominated.end(); ++it)
          blocks.push_back(*it);
      }
      
      return blocks;
    }
    
    /**
     * Load instructions require special behaviour. The goal is to load each
     * component of an aggregate separately, but this means that the load instruction
     * itself does not have an equivalent in the generated code.
     * 
     * \param load_term Term to assign the result of this load to.
     * 
     * \param ptr Address to load from (new value).
     * 
     * \param copy Whether the data being loaded needs to be copied if it
     * cannot be loaded to a stack value. This should be set to \c true if
     * the data is on the heap and \c false if it is already on the stack.
     */
    AggregateLoweringPass::Value AggregateLoweringPass::FunctionRunner::load_value(Term *load_term, Term *ptr, bool copy) {
      Type load_type = rewrite_type(load_term->type());
      if (load_type.stack_type()) {
        Term *load_insn = builder().load(ptr);
        return (m_value_map[load_term] = Value(load_insn, true));
      }
      
      if (StructType::Ptr struct_ty = dyn_cast<StructType>(load_term->type())) {
        for (unsigned i = 0, e = struct_ty->n_members(); i != e; ++i) {
          load_value(FunctionalBuilder::struct_element(load_term, i),
                     FunctionalTermRewriter::struct_ptr_offset(*this, struct_ty, ptr, i), copy);
        }
        // struct loads have no value because they should not be accessed directly
        return Value();
      }
      
      // So this type cannot be loaded: memcpy it to the stack
      
      if (!copy) {
        // Value is already on the stack, so just return the existing pointer
        return (m_value_map[load_term] = Value(ptr, false));
      }
      
      if (load_type.heap_type()) {
        Term *cast_ptr = FunctionalBuilder::pointer_cast(ptr, load_type.heap_type());
        Term *alloca_insn = builder().alloca_(load_type.heap_type());
        builder().memcpy(alloca_insn, cast_ptr, 1);
        return (m_value_map[load_term] = Value(alloca_insn, false));
      }

      if (ArrayType::Ptr array_ty = dyn_cast<ArrayType>(load_term->type())) {
        Type element_ty = rewrite_type(array_ty->element_type());
        if (element_ty.heap_type()) {
          Term *cast_ptr = FunctionalBuilder::pointer_cast(ptr, element_ty.heap_type());
          Term *length = rewrite_value_stack(array_ty->length());
          Term *alloca_insn = builder().alloca_(element_ty.heap_type(), length);
          builder().memcpy(alloca_insn, cast_ptr, length);
          return (m_value_map[load_term] = Value(alloca_insn, false));
        }
      }

      PSI_ASSERT(ptr->type() == FunctionalBuilder::byte_type(context()));
      Term *type_size = rewrite_value_stack(FunctionalBuilder::type_size(load_term->type()));
      Term *type_alignment = rewrite_value_stack(FunctionalBuilder::type_alignment(load_term->type()));
      Term *alloca_insn = builder().alloca_(FunctionalBuilder::byte_type(context()),
                                            type_size, type_alignment);
      builder().memcpy(alloca_insn, ptr, type_size, type_alignment);
      return (m_value_map[load_term] = Value(alloca_insn, false));
    }
    
    /**
     * Work out the expected form of a type after this pass.
     */
    AggregateLoweringPass::Type AggregateLoweringPass::FunctionRunner::rewrite_type(Term *type) {
      if (type->global()) {
        PSI_FAIL("not implemented");
      }
      
      TypeMapType::iterator type_it = m_type_map.find(type);
      if (type_it != m_type_map.end())
        return type_it->second;
      
      FunctionalTerm *func_type = dyn_cast<FunctionalTerm>(type);
      if (!func_type)
        return Type();
      
      Type result = TypeTermRewriter::callback_map.call(*this, func_type);
      m_type_map[type] = result;
      return result;
    }
    
    /**
     * Rewrite a value for later passes.
     */
    AggregateLoweringPass::Value AggregateLoweringPass::FunctionRunner::rewrite_value(Term *value) {
      if (value->global()) {
        PSI_FAIL("not implemented");
      }
      
      ValueMapType::iterator value_it = m_value_map.find(value);
      if (value_it != m_value_map.end())
        return value_it->second;
      
      FunctionalTerm *func_value = cast<FunctionalTerm>(value);
      Value result = FunctionalTermRewriter::callback_map.call(*this, func_value);
      PSI_ASSERT(result.value());
      m_value_map[value] = result;
      return result;
    }
    
    /**
     * Utility function which runs rewrite_value and asserts that the resulting
     * value is on the stack and is non-NULL.
     */
    Term* AggregateLoweringPass::FunctionRunner::rewrite_value_stack(Term *value) {
      Value v = rewrite_value(value);
      PSI_ASSERT(v.on_stack() && v.value());
      return v.value();
    }
    
    /**
     * Utility function which runs rewrite_value and asserts that the resulting
     * value is not on the stack and is non-NULL.
     */
    Term* AggregateLoweringPass::FunctionRunner::rewrite_value_ptr(Term *value) {
      Value v = rewrite_value(value);
      PSI_ASSERT(!v.on_stack() && v.value());
      return v.value();
    }
    
    AggregateLoweringPass::GlobalVariableRunner::GlobalVariableRunner(AggregateLoweringPass* pass, GlobalVariableTerm* global)
    : GlobalTermRewriter(), m_pass(pass), m_old_global(global) {
      std::vector<Term*> element_types;
      GlobalBuildStatus status = pass->rewrite_global_type(global->value());
#if 0
      Term *new_type = pass->rewrite_global_type(global->value());
      m_new_term = pass->context().new_global_variable(new_type);
      pass->m_term_map.m_value_map[global] = m_new_term;
#endif
    }
    
    void AggregateLoweringPass::GlobalVariableRunner::run() {
    }
    
    AggregateLoweringPass::PaddingStatus::PaddingStatus()
      : original_size(0), rewrite_size(0) {
    }

    AggregateLoweringPass::PaddingStatus::PaddingStatus(unsigned original_size_, unsigned rewrite_size_)
      : original_size(original_size_), rewrite_size(rewrite_size_) {
      PSI_ASSERT(original_size >= rewrite_size);
    }

#if 0
    /**
      * Return a type which will cause a field of the given type to
      * have the right alignment, or NULL if no padding field is
      * necessary.
      * 
      * \param actual_type Type of the field in the original structure.
      * 
      * \param storage_type Type used to hold data for the field in the
      * global being built.
      * 
      * \param status Size and alignment of the structure being built,
      * excluding the field being added.
      */
    AggregateLoweringPass::PaddingStatus
    AggregateLoweringPass::pad_to_alignment(std::vector<Term*>& members, Term *actual_type, Term *storage_type, unsigned alignment, PaddingStatus status) {
      TypeSizeAlignment storage_size_align = target_callback->type_size_alignment(storage_type);
      PSI_ASSERT(alignment >= storage_size_align.alignment);

      unsigned field_offset = (status.size + alignment - 1) & ~(alignment - 1);
      // Offset from size to correct position
      unsigned padding = field_offset - status.llvm_size;

      PaddingStatus new_status(field_offset + constant_type_size(field_type),
                               field_offset + type_size(llvm_field_type));
      if (padding < natural_alignment)
        return std::make_pair(new_status, static_cast<const llvm::Type*>(0));

      // Bytes of padding needed to get to a position where the natural alignment will work
      uint64_t required_padding = padding - natural_alignment + 1;
      return std::make_pair(new_status, llvm::ArrayType::get(get_byte_type(), required_padding));
    }
#endif

    /**
     * Initialize a global variable build with no elements, zero size and minimum alignment.
     */
    AggregateLoweringPass::GlobalBuildStatus::GlobalBuildStatus()
    : elements_size(0), size(0), alignment(1) {
    }
    
    /**
     * Initialize a global variable build with one element, and the specified sizes and alignment.
     */
    AggregateLoweringPass::GlobalBuildStatus::GlobalBuildStatus(Term *element, unsigned elements_size_, unsigned size_, unsigned alignment_)
    : elements(1, element), elements_size(elements_size_), size(size_), alignment(alignment_) {
    }
    
    /**
     * Append the result of building a part of a global variable to the current
     * status of building it.
     */
    void AggregateLoweringPass::global_append(GlobalBuildStatus& status, const GlobalBuildStatus& child) {
      unsigned child_start = (status.size + child.alignment - 1) & ~child.alignment;

      if (!child.elements.empty()) {
        PSI_ASSERT(child_start >= status.size);
        unsigned padding = child_start - status.size;
        std::pair<Term*, unsigned> padding_type = target_callback->type_from_alignment(child.alignment);
        if (padding >= padding_type.second) {
          unsigned natural_alignment = target_callback->type_size_alignment(child.elements.front()).alignment;
          padding_type = target_callback->type_from_alignment(natural_alignment);
        }
        
        status.elements.insert(status.elements.end(), padding / padding_type.second, padding_type.first);
        status.elements.insert(status.elements.end(), child.elements.begin(), child.elements.end());
      }

      status.size = child_start + child.size;
      status.elements_size = child_start + child.elements_size;
      status.alignment = std::max(status.alignment, child.alignment);
    }

    /**
     * Rewrite the type of a global variable.
     * 
     * \param value Global value being stored.
     * 
     * \param element_types Sequence of elements to be put into
     * the global at the top level.
     */
    AggregateLoweringPass::GlobalBuildStatus AggregateLoweringPass::rewrite_global_type(Term *value) {
      Type value_ty = rewrite_type(value->type());
      if (value_ty.heap_type()) {
        TypeSizeAlignment size_align = target_callback->type_size_alignment(value_ty.heap_type());
        return GlobalBuildStatus(value_ty.heap_type(), size_align.size, size_align.size, size_align.alignment);
      }

      if (ArrayValue::Ptr array_val = dyn_cast<ArrayValue>(value)) {
        GlobalBuildStatus status;
        for (unsigned i = 0, e = array_val->length(); i != e; ++i)
          global_append(status, rewrite_global_type(array_val->value(i)));
        
        if (!flatten_globals) {
          Term *struct_ty = FunctionalBuilder::struct_type(context(), status.elements);
          status.elements.assign(1, struct_ty);
          status.elements_size = target_callback->type_size_alignment(struct_ty).size;
        }
        return status;
      } else if (StructValue::Ptr struct_val = dyn_cast<StructValue>(value)) {
        GlobalBuildStatus status;
        for (unsigned i = 0, e = struct_val->n_members(); i != e; ++i)
          global_append(status, rewrite_global_type(struct_val->member_value(i)));

        if (!flatten_globals) {
          Term *struct_ty = FunctionalBuilder::struct_type(context(), status.elements);
          status.elements.assign(1, struct_ty);
          status.elements_size = target_callback->type_size_alignment(struct_ty).size;
        }
        return status;
      } else if (UnionValue::Ptr union_val = dyn_cast<UnionValue>(value)) {
        GlobalBuildStatus status = rewrite_global_type(union_val->value());
        PSI_FAIL("not implemented - change size and alignment");
        return status;
      } else {
        PSI_FAIL("unsupported global element");
      }
    }

    AggregateLoweringPass::AggregateLoweringPass(Context *context, TargetCallback *target_callback_)
    : m_context(context),
    target_callback(target_callback_),
    remove_only_unknown(false),
    remove_all_unions(false),
    flatten_globals(false) {
    }
    
    boost::shared_ptr<GlobalTermRewriter> AggregateLoweringPass::rewrite_global(GlobalTerm *global) {
      if (global->term_type() == term_function) {
        return boost::make_shared<FunctionRunner>(this, cast<FunctionTerm>(global));
      } else {
        return boost::make_shared<GlobalVariableRunner>(this, cast<GlobalVariableTerm>(global));
      }
    }
  }
}
