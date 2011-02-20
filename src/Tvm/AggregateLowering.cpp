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
      static Type array_type_rewrite(AggregateLoweringRewriter& rewriter, ArrayType::Ptr term) {
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

      static Type struct_type_rewrite(AggregateLoweringRewriter& rewriter, StructType::Ptr term) {
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

      static Type union_type_rewrite(AggregateLoweringRewriter& rewriter, UnionType::Ptr term) {
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
      
      static Type pointer_type_rewrite(AggregateLoweringRewriter& rewriter, PointerType::Ptr) {
        Term *ptr_type = FunctionalBuilder::byte_pointer_type(rewriter.context());
        return Type(ptr_type, ptr_type);
      }
      
      static Type unknown_type_rewrite(AggregateLoweringRewriter&, MetatypeValue::Ptr) {
        return Type();
      }
      
      static Type default_rewrite(AggregateLoweringRewriter& rewriter, FunctionalTerm *term) {
        PSI_ASSERT(term->is_type());
        PSI_ASSERT(term->n_parameters() == 0);
        return Type(term->rewrite(rewriter.context(), StaticArray<Term*,0>()));
      }
      
      typedef TermOperationMap<FunctionalTerm, Type, AggregateLoweringRewriter&> CallbackMap;
      static CallbackMap callback_map;
      
      static CallbackMap::Initializer callback_map_initializer() {
        return CallbackMap::initializer(default_rewrite)
          .add<ArrayType>(array_type_rewrite)
          .add<StructType>(struct_type_rewrite)
          .add<UnionType>(union_type_rewrite)
          .add<MetatypeValue>(unknown_type_rewrite)
          .add<PointerType>(pointer_type_rewrite);
      }
    };
    
    AggregateLoweringPass::TypeTermRewriter::CallbackMap
      AggregateLoweringPass::TypeTermRewriter::callback_map(AggregateLoweringPass::TypeTermRewriter::callback_map_initializer());
      
    struct AggregateLoweringPass::FunctionalTermRewriter {
      static Value aggregate_type_rewrite(AggregateLoweringRewriter& rewriter, Term *term) {
        Type ty = rewriter.rewrite_type(term);
        if (ty.heap_type())
          return Value(ty.heap_type(), true);
        
        TypeSizeAlignment size_alignment = rewriter.pass().target_callback->type_size_alignment(term);
        return Value(FunctionalBuilder::type_value(size_alignment.size, size_alignment.alignment), true);
      }

      static Value default_rewrite(AggregateLoweringRewriter& rewriter, FunctionalTerm *term) {
        PSI_ASSERT(rewriter.rewrite_type(term->type()).stack_type());
        unsigned n_parameters = term->n_parameters();
        ScopedArray<Term*> parameters(n_parameters);
        for (unsigned i = 0; i != n_parameters; ++i)
          parameters[i] = rewriter.rewrite_value_stack(term->parameter(i));
        return Value(term->rewrite(rewriter.context(), parameters), true);
      }
      
      static Value aggregate_value_rewrite(AggregateLoweringRewriter& rewriter, FunctionalTerm *term) {
        Type term_type = rewriter.rewrite_type(term->type());
        if (term_type.stack_type()) {
          return default_rewrite(rewriter, term);
        } else {
          Term *alloc_type = 0, *alloc_count, *alloc_alignment;
          
          if (term_type.heap_type()) {
            alloc_type = term_type.heap_type();
            alloc_count = alloc_alignment = FunctionalBuilder::size_value(rewriter.context(), 1);
          } else {
            if (ArrayValue::Ptr array_val = dyn_cast<ArrayValue>(term)) {
              Type element_type = rewriter.rewrite_type(array_val->element_type());
              if (element_type.heap_type()) {
                alloc_type = element_type.heap_type();
                alloc_count = FunctionalBuilder::size_value(rewriter.context(), array_val->length());
                alloc_alignment = FunctionalBuilder::size_value(rewriter.context(), 1);
              }
            }
            
            if (!alloc_type) {
              alloc_type = FunctionalBuilder::byte_type(rewriter.context());
              alloc_count = rewriter.rewrite_value_stack(FunctionalBuilder::type_size(term->type()));
              alloc_alignment = rewriter.rewrite_value_stack(FunctionalBuilder::type_alignment(term->type()));
            }
          }
          
          return Value(rewriter.store_value(term, alloc_type, alloc_count, alloc_alignment), false);
        }
      }

      static Term* array_ptr_offset(AggregateLoweringRewriter& rewriter, ArrayType::Ptr array_ty, Term *base, Term *index) {
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

      static Value array_element_rewrite(AggregateLoweringRewriter& rewriter, ArrayElement::Ptr term) {
        Term *index = rewriter.rewrite_value_stack(term->aggregate());
        Value array_val = rewriter.rewrite_value(term->aggregate());
        if (array_val.on_stack()) {
          return Value(FunctionalBuilder::array_element(array_val.value(), index), true);
        } else {
          Term *element_ptr = array_ptr_offset(rewriter, term->aggregate_type(), array_val.value(), index);
          return rewriter.load_value(term, element_ptr);
        }
      }

      static Value array_element_ptr_rewrite(AggregateLoweringRewriter& rewriter, ArrayElementPtr::Ptr term) {
        Term *array_ptr = rewriter.rewrite_value_stack(term->aggregate_ptr());
        Term *index = rewriter.rewrite_value_stack(term->index());
        
        Type array_ty = rewriter.rewrite_type(term->aggregate_type());
        if (array_ty.heap_type())
          return Value(FunctionalBuilder::array_element_ptr(array_ptr, index), true);
        
        return Value(array_ptr_offset(rewriter, term->aggregate_type(), array_ptr, index), true);
      }
      
      static Term* struct_ptr_offset(AggregateLoweringRewriter& rewriter, StructType::Ptr struct_ty, Term *base, unsigned index) {
        Type struct_ty_rewritten = rewriter.rewrite_type(struct_ty);
        if (struct_ty_rewritten.heap_type()) {
          Term *cast_ptr = FunctionalBuilder::pointer_cast(base, struct_ty);
          return FunctionalBuilder::struct_element_ptr(cast_ptr, index);
        }
        
        PSI_ASSERT(base->type() == FunctionalBuilder::byte_pointer_type(rewriter.context()));
        Term *offset = rewriter.rewrite_value_stack(FunctionalBuilder::struct_element_offset(struct_ty, index));
        return FunctionalBuilder::pointer_offset(base, offset);
      }
      
      static Value struct_element_rewrite(AggregateLoweringRewriter& rewriter, StructElement::Ptr term) {
        Value struct_val = rewriter.rewrite_value(term->aggregate());
        if (struct_val.on_stack()) {
          return Value(FunctionalBuilder::struct_element(struct_val.value(), term->index()), true);
        } else {
          Term *member_ptr = struct_ptr_offset(rewriter, term->aggregate_type(), struct_val.value(), term->index());
          return rewriter.load_value(term, member_ptr);
        }
      }

      static Value struct_element_ptr_rewrite(AggregateLoweringRewriter& rewriter, StructElementPtr::Ptr term) {
        Term *struct_ptr = rewriter.rewrite_value_stack(term->aggregate_ptr());
        
        Type struct_ty = rewriter.rewrite_type(term->aggregate_type());
        if (struct_ty.heap_type())
          return Value(FunctionalBuilder::struct_element_ptr(struct_ptr, term->index()), true);
        
        return Value(struct_ptr_offset(rewriter, term->aggregate_type(), struct_ptr, term->index()), true);
      }
      
      static Value struct_element_offset_rewrite(AggregateLoweringRewriter& rewriter, StructElementOffset::Ptr term) {
        StructType::Ptr struct_ty = term->aggregate_type();

        Term *offset = FunctionalBuilder::size_value(rewriter.context(), 0);
        
        for (unsigned ii = 0, ie = term->index(); ; ++ii) {
          Term *member_type = struct_ty->member_type(ii);
          Term *member_alignment = rewriter.rewrite_value_stack(FunctionalBuilder::type_alignment(member_type));
          offset = FunctionalBuilder::align_to(offset, member_alignment);
          if (ii == ie)
            break;

          Term *member_size = rewriter.rewrite_value_stack(FunctionalBuilder::type_size(member_type));
          offset = FunctionalBuilder::add(offset, member_size);
        }
        
        return Value(offset, true);
      }

      static Value union_element_rewrite(AggregateLoweringRewriter& rewriter, UnionElement::Ptr term) {
        Value union_val = rewriter.rewrite_value(term->aggregate());
        if (union_val.on_stack()) {
          Term *member_type = rewriter.rewrite_value_stack(term->member_type());
          return Value(FunctionalBuilder::union_element(union_val.value(), member_type), true);
        } else {
          Type member_ty = rewriter.rewrite_type(term->type());
          return rewriter.load_value(term, union_val.value());
        }
      }

      static Value union_element_ptr_rewrite(AggregateLoweringRewriter& rewriter, UnionElementPtr::Ptr term) {
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
      
      static Value metatype_size_rewrite(AggregateLoweringRewriter& rewriter, MetatypeSize::Ptr term) {
        Type ty = rewriter.rewrite_type(term->parameter());
        if (ty.heap_type())
          return Value(FunctionalBuilder::type_size(ty.heap_type()), true);
        
        MetatypeValue::Ptr meta = cast<MetatypeValue>(rewriter.rewrite_value_stack(term->parameter()));
        return Value(meta->size(), true);
      }
      
      static Value metatype_alignment_rewrite(AggregateLoweringRewriter& rewriter, MetatypeAlignment::Ptr term) {
        Type ty = rewriter.rewrite_type(term->parameter());
        if (ty.heap_type())
          return Value(FunctionalBuilder::type_alignment(ty.heap_type()), true);
        
        MetatypeValue::Ptr meta = cast<MetatypeValue>(rewriter.rewrite_value_stack(term->parameter()));
        return Value(meta->alignment(), true);
      }

      typedef TermOperationMap<FunctionalTerm, Value, AggregateLoweringRewriter&> CallbackMap;
      static CallbackMap callback_map;
      
      static CallbackMap::Initializer callback_map_initializer() {
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
          .add<UnionElementPtr>(union_element_ptr_rewrite)
          .add<StructElementOffset>(struct_element_offset_rewrite)
          .add<MetatypeSize>(metatype_size_rewrite)
          .add<MetatypeAlignment>(metatype_alignment_rewrite);
      }
    };

    AggregateLoweringPass::FunctionalTermRewriter::CallbackMap
      AggregateLoweringPass::FunctionalTermRewriter::callback_map(AggregateLoweringPass::FunctionalTermRewriter::callback_map_initializer());

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
        runner.pass().target_callback->lower_function_call(runner, term);
        return Value();
      }

      static Value alloca_rewrite(FunctionRunner& runner, Alloca::Ptr term) {
        Type type = runner.rewrite_type(term->stored_type());
        Term *count = runner.rewrite_value_stack(term->count());
        Term *alignment = runner.rewrite_value_stack(term->alignment());
        Term *stack_ptr;
        if (type.heap_type()) {
          stack_ptr = runner.builder().alloca_(type.heap_type(), count, alignment);
        } else {
          Term *type_size = runner.rewrite_value_stack(FunctionalBuilder::type_size(term->stored_type()));
          Term *type_alignment = runner.rewrite_value_stack(FunctionalBuilder::type_alignment(term->stored_type()));
          Term *total_size = FunctionalBuilder::mul(count, type_size);
          stack_ptr = runner.builder().alloca_(FunctionalBuilder::byte_type(runner.context()), total_size, type_alignment);
        }
        Term *cast_stack_ptr = FunctionalBuilder::pointer_cast(stack_ptr, FunctionalBuilder::byte_type(runner.context()));
        return Value(cast_stack_ptr, true);
      }
      
      static Value load_rewrite(FunctionRunner& runner, Load::Ptr term) {
        Term *ptr = runner.rewrite_value_stack(term->target());
        return runner.load_value(term, ptr, true);
      }
      
      static Value store_rewrite(FunctionRunner& runner, Store::Ptr term) {
        Term *ptr = runner.rewrite_value_stack(term->target());
        runner.store_value(term->value(), ptr);
        return Value();
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
      
      typedef TermOperationMap<InstructionTerm, Value, FunctionRunner&> CallbackMap;
      static CallbackMap callback_map;
      
      static CallbackMap::Initializer callback_map_initializer() {
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
      AggregateLoweringPass::InstructionTermRewriter::callback_map(AggregateLoweringPass::InstructionTermRewriter::callback_map_initializer());
      
    AggregateLoweringPass::AggregateLoweringRewriter::AggregateLoweringRewriter(AggregateLoweringPass *pass)
    : m_pass(pass) {
    }

    /**
     * Work out the expected form of a type after this pass.
     */
    AggregateLoweringPass::Type AggregateLoweringPass::AggregateLoweringRewriter::rewrite_type(Term *type) {
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
    AggregateLoweringPass::Value AggregateLoweringPass::AggregateLoweringRewriter::rewrite_value(Term *value) {      
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
    Term* AggregateLoweringPass::AggregateLoweringRewriter::rewrite_value_stack(Term *value) {
      Value v = rewrite_value(value);
      PSI_ASSERT(v.on_stack() && v.value());
      return v.value();
    }
    
    /**
     * Utility function which runs rewrite_value and asserts that the resulting
     * value is not on the stack and is non-NULL.
     */
    Term* AggregateLoweringPass::AggregateLoweringRewriter::rewrite_value_ptr(Term *value) {
      Value v = rewrite_value(value);
      PSI_ASSERT(!v.on_stack() && v.value());
      return v.value();
    }
    
    AggregateLoweringPass::FunctionRunner::FunctionRunner(AggregateLoweringPass* pass, FunctionTerm* old_function)
    : AggregateLoweringRewriter(pass), m_old_function(old_function) {
      m_new_function = pass->target_callback->lower_function(*pass, old_function);
      if (old_function->entry()) {
        BlockTerm *new_entry = new_function()->new_block();
        new_function()->set_entry(new_entry);
        builder().set_insert_point(new_entry);
        pass->target_callback->lower_function_entry(*this, old_function, new_function());
      }
    }

    /**
     * \brief Add a key,value pair to the existing term mapping.
     */
    void AggregateLoweringPass::FunctionRunner::add_mapping(Term *source, Term *target, bool on_stack) {
      PSI_ASSERT(&source->context() == &old_function()->context());
      PSI_ASSERT(&target->context() == &new_function()->context());
      m_value_map[source] = Value(target, on_stack);
    }

    Term* AggregateLoweringPass::FunctionRunner::store_value(Term *value, Term *alloc_type, Term *alloc_count, Term *alloc_alignment) {
      Term *ptr = builder().alloca_(alloc_type, alloc_count, alloc_alignment);
      store_value(value, ptr);
      return ptr;
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
    Term* AggregateLoweringPass::FunctionRunner::store_value(Term *value, Term *ptr) {
      PSI_ASSERT(isa<PointerType>(ptr->type()));
      
      Type value_type = rewrite_type(value->type());
      if (value_type.stack_type()) {
        Term *cast_ptr = FunctionalBuilder::pointer_cast(ptr, value_type.stack_type());
        Term *stack_value = rewrite_value_stack(value);
        return builder().store(stack_value, cast_ptr);
      }

      if (ArrayValue::Ptr array_val = dyn_cast<ArrayValue>(value)) {
        Term *result;
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
        Term *result;
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
          return builder().memcpy(cast_ptr, value_ptr, array_ty->length());
        }
      }

      Term *value_ptr = rewrite_value_ptr(value);
      PSI_ASSERT(value_ptr->type() == FunctionalBuilder::byte_pointer_type(context()));
      Term *value_size = rewrite_value_stack(FunctionalBuilder::type_size(value->type()));
      Term *value_alignment = rewrite_value_stack(FunctionalBuilder::type_alignment(value->type()));
      return builder().memcpy(ptr, value_ptr, value_size, value_alignment);
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
      BlockTerm *prolog_block;
      BlockTerm *entry_block;
      if (new_function()->entry() && !new_function()->entry()->instructions().empty()) {
        prolog_block = new_function()->entry();
        entry_block = new_function()->new_block(prolog_block);
        m_builder.set_insert_point(prolog_block);
        m_builder.br(entry_block);
      } else {
        prolog_block = 0;
        entry_block = new_function()->entry();
      }
      m_value_map[old_function()->entry()] = Value(entry_block, true);

      std::vector<BlockTerm*> sorted_blocks = old_function()->topsort_blocks();
      
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
        m_builder.set_insert_point(new_block);
        for (BlockTerm::InstructionList::iterator jt = insn_list.begin(); jt != insn_list.end(); ++jt) {
          InstructionTerm *insn = &*jt;
          Value value = InstructionTermRewriter::callback_map.call(*this, insn);
          if (value.value())
            m_value_map[insn] = value;
        }
      }
    }

    AggregateLoweringPass::Value AggregateLoweringPass::FunctionRunner::load_value(Term *load_term, Term *ptr) {
      return load_value(load_term, ptr, false);
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
        Term *cast_ptr = FunctionalBuilder::pointer_cast(ptr, load_type.stack_type());
        Term *load_insn = builder().load(cast_ptr);
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
    
    AggregateLoweringPass::Type AggregateLoweringPass::FunctionRunner::rewrite_type(Term *type) {
      if (!type->source() || isa<GlobalTerm>(type->source()))
        return pass().global_rewriter().rewrite_type(type);
      return AggregateLoweringRewriter::rewrite_type(type);
    }
    
    AggregateLoweringPass::Value AggregateLoweringPass::FunctionRunner::rewrite_value(Term *value) {
      if (!value->source() || isa<GlobalTerm>(value->source()))
        return pass().global_rewriter().rewrite_value(value);
      return AggregateLoweringRewriter::rewrite_value(value);
    }
    
    AggregateLoweringPass::ModuleLevelRewriter::ModuleLevelRewriter(AggregateLoweringPass *pass)
    : AggregateLoweringRewriter(pass) {
    }
    
    AggregateLoweringPass::Value AggregateLoweringPass::ModuleLevelRewriter::load_value(Term* load_term, Term *ptr) {
      Term *origin = ptr;
      unsigned offset = 0;
      /*
       * This is somewhat awkward - I have to work out the relationship of ptr to some
       * already existing global variable, and then simulate a load instruction.
       */
      while (true) {
        /* ArrayElementPtr should not occur in these expressions since it is arrays
         * which can cause values to have to be treated as pointers due to the fact
         * that their indices may not be compile-time constants.
         */
        if (PointerOffset::Ptr ptr_offset = dyn_cast<PointerOffset>(origin)) {
          origin = ptr_offset->pointer();
          //offset += pass().target_callback->type_size_alignment() * rewrite_value_integer();
        } else if (PointerCast::Ptr ptr_cast = dyn_cast<PointerCast>(origin)) {
          origin = ptr_cast->pointer();
        } else if (StructElementPtr::Ptr struct_el = dyn_cast<StructElementPtr>(origin)) {
          origin = struct_el->aggregate_ptr();
        } else if (UnionElementPtr::Ptr union_el = dyn_cast<UnionElementPtr>(origin)) {
          origin = union_el->aggregate_ptr();
        } else if (isa<GlobalVariableTerm>(origin)) {
          break;
        } else {
          PSI_FAIL("unexpected term type in global pointer expression");
        }
      }
      PSI_FAIL("not implemented");
    }
    
    Term* AggregateLoweringPass::ModuleLevelRewriter::store_value(Term *value, Term *alloc_type, Term *alloc_count, Term *alloc_alignment) {
      PSI_FAIL("not implemented");
    }

    /**
     * Initialize a global variable build with no elements, zero size and minimum alignment.
     */
    AggregateLoweringPass::GlobalBuildStatus::GlobalBuildStatus(Context& context)
    : elements_size(FunctionalBuilder::size_value(context, 0)),
    size(FunctionalBuilder::size_value(context, 0)),
    alignment(FunctionalBuilder::size_value(context, 1)) {
    }
    
    /**
     * Initialize a global variable build with one element, and the specified sizes and alignment.
     */
    AggregateLoweringPass::GlobalBuildStatus::GlobalBuildStatus(Term *element, Term *elements_size_, Term *size_, Term *alignment_)
    : elements(1, element), elements_size(elements_size_), size(size_), alignment(alignment_) {
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
    void AggregateLoweringPass::global_pad_to_size(GlobalBuildStatus& status, Term *size, Term *alignment, bool is_value) {
      std::pair<Term*,Term*> padding_type = target_callback->type_from_alignment(alignment);
      Term *count = FunctionalBuilder::div(FunctionalBuilder::sub(size, status.size), padding_type.second);
      if (IntegerValue::Ptr count_value = dyn_cast<IntegerValue>(count)) {
        boost::optional<unsigned> count_value_int = count_value->value().unsigned_value();
        if (!count_value_int)
          throw TvmInternalError("cannot create internal global variable padding due to size overflow");
        Term *padding_term = is_value ? FunctionalBuilder::undef(padding_type.first) : padding_type.first;
        status.elements.insert(status.elements.end(), *count_value_int, padding_term);
      } else {
        Term *array_ty = FunctionalBuilder::array_type(padding_type.first, count);
        status.elements.push_back(is_value ? FunctionalBuilder::undef(array_ty) : array_ty);
      }
    }
    
    /**
     * Append the result of building a part of a global variable to the current
     * status of building it.
     */
    void AggregateLoweringPass::global_append(GlobalBuildStatus& status, const GlobalBuildStatus& child, bool is_value) {
      Term *child_start = FunctionalBuilder::align_to(status.size, child.alignment);
      if (!child.elements.empty()) {
        Term *first_alignment = target_callback->type_size_alignment(is_value ? child.elements.front()->type() : child.elements.front()).alignment;
        global_pad_to_size(status, child_start, first_alignment, is_value);
        status.elements.insert(status.elements.end(), child.elements.begin(), child.elements.end());
        status.elements_size = FunctionalBuilder::add(child_start, child.elements_size);
      }

      status.size = FunctionalBuilder::add(child_start, child.size);
      status.alignment = FunctionalBuilder::max(status.alignment, child.alignment);
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
      Type value_ty = global_rewriter().rewrite_type(value->type());
      if (value_ty.heap_type()) {
        TypeSizeAlignment size_align = target_callback->type_size_alignment(value_ty.heap_type());
        return GlobalBuildStatus(value_ty.heap_type(), size_align.size, size_align.size, size_align.alignment);
      }

      if (ArrayValue::Ptr array_val = dyn_cast<ArrayValue>(value)) {
        GlobalBuildStatus status(context());
        for (unsigned i = 0, e = array_val->length(); i != e; ++i)
          global_append(status, rewrite_global_type(array_val->value(i)), false);
        
        if (!flatten_globals) {
          Term *struct_ty = FunctionalBuilder::struct_type(context(), status.elements);
          status.elements.assign(1, struct_ty);
          status.elements_size = target_callback->type_size_alignment(struct_ty).size;
        }
        return status;
      } else if (StructValue::Ptr struct_val = dyn_cast<StructValue>(value)) {
        GlobalBuildStatus status(context());
        for (unsigned i = 0, e = struct_val->n_members(); i != e; ++i)
          global_append(status, rewrite_global_type(struct_val->member_value(i)), false);

        if (!flatten_globals) {
          Term *struct_ty = FunctionalBuilder::struct_type(context(), status.elements);
          status.elements.assign(1, struct_ty);
          status.elements_size = target_callback->type_size_alignment(struct_ty).size;
        }
        return status;
      } else if (UnionValue::Ptr union_val = dyn_cast<UnionValue>(value)) {
        GlobalBuildStatus status = rewrite_global_type(union_val->value());
        TypeSizeAlignment size_alignment = target_callback->type_size_alignment(union_val->type());
        status.size = size_alignment.size;
        status.alignment = size_alignment.alignment;
        return status;
      } else {
        PSI_FAIL("unsupported global element");
      }
    }

    AggregateLoweringPass::GlobalBuildStatus AggregateLoweringPass::rewrite_global_value(Term *value) {
      Type value_ty = global_rewriter().rewrite_type(value->type());
      if (value_ty.heap_type()) {
        TypeSizeAlignment size_align = target_callback->type_size_alignment(value_ty.heap_type());
        Term *rewritten_value = m_global_rewriter.rewrite_value_stack(value);
        return GlobalBuildStatus(rewritten_value, size_align.size, size_align.size, size_align.alignment);
      }

      if (ArrayValue::Ptr array_val = dyn_cast<ArrayValue>(value)) {
        GlobalBuildStatus status(context());
        for (unsigned i = 0, e = array_val->length(); i != e; ++i)
          global_append(status, rewrite_global_value(array_val->value(i)), true);
        
        if (!flatten_globals) {
          Term *struct_val = FunctionalBuilder::struct_value(context(), status.elements);
          status.elements.assign(1, struct_val);
          status.elements_size = target_callback->type_size_alignment(struct_val->type()).size;
        }
        return status;
      } else if (StructValue::Ptr struct_val = dyn_cast<StructValue>(value)) {
        GlobalBuildStatus status(context());
        for (unsigned i = 0, e = struct_val->n_members(); i != e; ++i)
          global_append(status, rewrite_global_value(struct_val->member_value(i)), true);

        if (!flatten_globals) {
          Term *struct_val = FunctionalBuilder::struct_value(context(), status.elements);
          status.elements.assign(1, struct_val);
          status.elements_size = target_callback->type_size_alignment(struct_val->type()).size;
        }
        return status;
      } else if (UnionValue::Ptr union_val = dyn_cast<UnionValue>(value)) {
        GlobalBuildStatus status = rewrite_global_value(union_val->value());
        PSI_FAIL("not implemented - change size and alignment");
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
    remove_only_unknown(false),
    remove_all_unions(false),
    remove_stack_arrays(false),
    flatten_globals(false) {
    }

    void AggregateLoweringPass::update_implementation(bool incremental) {
      if (!incremental)
        m_global_rewriter = ModuleLevelRewriter(this);
      
      std::vector<std::pair<GlobalVariableTerm*, GlobalVariableTerm*> > rewrite_globals;
      std::vector<std::pair<FunctionTerm*, boost::shared_ptr<FunctionRunner> > > rewrite_functions;
        
      for (Module::ModuleMemberList::iterator i = source_module()->members().begin(),
           e = source_module()->members().end(); i != e; ++i) {
        
        GlobalTerm *term = &*i;
        if (global_map_get(term))
          continue;
      
        if (GlobalVariableTerm *old_var = dyn_cast<GlobalVariableTerm>(term)) {
          std::vector<Term*> element_types;
          GlobalBuildStatus status = rewrite_global_type(old_var->value());
          global_pad_to_size(status, status.size, status.alignment, false);
          Term *global_type;
          if (status.elements.empty()) {
            global_type = FunctionalBuilder::empty_type(context());
          } else if (status.elements.size() == 1) {
            global_type = status.elements.front();
          } else {
            global_type = FunctionalBuilder::struct_type(context(), status.elements);
          }
          GlobalVariableTerm *new_var = target_module()->new_global_variable(old_var->name(), global_type);
          new_var->set_constant(old_var->constant());
          
          if (old_var->alignment())
            new_var->set_alignment(FunctionalBuilder::max(status.alignment, m_global_rewriter.rewrite_value_stack(old_var->alignment())));
          else
            new_var->set_alignment(status.alignment);

          global_rewriter().m_value_map[old_var] = Value(new_var, true);
          rewrite_globals.push_back(std::make_pair(old_var, new_var));
        } else {
          FunctionTerm *old_function = cast<FunctionTerm>(term);
          boost::shared_ptr<FunctionRunner> runner(new FunctionRunner(this, old_function));
          global_rewriter().m_value_map[old_function] = Value(runner->new_function(), true);
          rewrite_functions.push_back(std::make_pair(old_function, runner));
        }
      }
      
      for (std::vector<std::pair<GlobalVariableTerm*, GlobalVariableTerm*> >::iterator
           i = rewrite_globals.begin(), e = rewrite_globals.end(); i != e; ++i) {
        GlobalVariableTerm *source = i->first, *target = i->second;
        if (Term *source_value = source->value()) {
          GlobalBuildStatus status = rewrite_global_value(source_value);
          global_pad_to_size(status, status.size, status.alignment, true);
          Term *target_value;
          if (status.elements.empty()) {
            target_value = FunctionalBuilder::empty_value(context());
          } else if (status.elements.size() == 1) {
            target_value = status.elements.front();
          } else {
            target_value = FunctionalBuilder::struct_value(context(), status.elements);
          }
          target->set_value(target_value);
        }
        
        global_map_put(i->first, i->second);
      }
      
      for (std::vector<std::pair<FunctionTerm*, boost::shared_ptr<FunctionRunner> > >::iterator
           i = rewrite_functions.begin(), e = rewrite_functions.end(); i != e; ++i) {
        i->second->run();
        global_map_put(i->first, i->second->new_function());
      }
    }
  }
}
