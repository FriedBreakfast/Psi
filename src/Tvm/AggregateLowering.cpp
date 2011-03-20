#include "AggregateLowering.hpp"
#include "Aggregate.hpp"
#include "Instructions.hpp"
#include "InstructionBuilder.hpp"
#include "FunctionalBuilder.hpp"
#include "TermOperationMap.hpp"
#include "Utility.hpp"

#include <boost/next_prior.hpp>
#include <set>

namespace Psi {
  namespace Tvm {
    struct AggregateLoweringPass::TypeTermRewriter {
      static Type array_type_rewrite(AggregateLoweringRewriter& rewriter, ArrayType::Ptr term) {
        Term *length = rewriter.rewrite_value_stack(term->length());
        Type element_type = rewriter.rewrite_type(term->element_type());
        Term *size = FunctionalBuilder::mul(length, element_type.size());
        Term *alignment = element_type.alignment();
        
        Term *stack_type = 0, *heap_type = 0;
        if (rewriter.pass().remove_only_unknown) {
          if (element_type.stack_type() && !rewriter.pass().remove_stack_arrays)
            stack_type = FunctionalBuilder::array_type(element_type.stack_type(), length);
          
          if (element_type.heap_type()) {
            heap_type = FunctionalBuilder::array_type(element_type.heap_type(), length);
            
            if (!rewriter.pass().remove_sizeof) {
              size = FunctionalBuilder::type_size(heap_type);
              alignment = FunctionalBuilder::type_alignment(heap_type);
            }
          }
        }
        
        return Type(size, alignment, stack_type, heap_type);
      }

      static Type struct_type_rewrite(AggregateLoweringRewriter& rewriter, StructType::Ptr term) {
        Term *size = FunctionalBuilder::size_value(rewriter.context(), 0);
        Term *alignment = FunctionalBuilder::size_value(rewriter.context(), 1);
        
        SmallArray<Term*,8> stack_members(term->n_members()), heap_members(term->n_members());
        bool stack_simple = true, heap_simple = true;
        for (unsigned i = 0; i != stack_members.size(); ++i) {
          Type member_type = rewriter.rewrite_type(term->member_type(i));
          stack_members[i] = member_type.stack_type();
          stack_simple = stack_simple && member_type.stack_type();
          heap_members[i] = member_type.heap_type();
          heap_simple = heap_simple && member_type.heap_type();
          
          size = FunctionalBuilder::add(FunctionalBuilder::align_to(size, member_type.alignment()), member_type.size());
          alignment = FunctionalBuilder::max(alignment, member_type.alignment());
        }
        
        Term *stack_type = 0, *heap_type = 0;
        if (rewriter.pass().remove_only_unknown) {
          if (stack_simple)
            stack_type = FunctionalBuilder::struct_type(rewriter.context(), stack_members);
          
          if (heap_simple) {
            heap_type = FunctionalBuilder::struct_type(rewriter.context(), heap_members);
            
            if (!rewriter.pass().remove_sizeof) {
              size = FunctionalBuilder::type_size(heap_type);
              alignment = FunctionalBuilder::type_alignment(heap_type);
            }
          }
        }
        
        return Type(size, alignment, stack_type, heap_type);
      }

      static Type union_type_rewrite(AggregateLoweringRewriter& rewriter, UnionType::Ptr term) {
        Term *size = FunctionalBuilder::size_value(rewriter.context(), 0);
        Term *alignment = FunctionalBuilder::size_value(rewriter.context(), 1);
        
        SmallArray<Term*,8> stack_members(term->n_members()), heap_members(term->n_members());
        bool stack_simple = true, heap_simple = true;
        for (unsigned i = 0; i != stack_members.size(); ++i) {
          Type member_type = rewriter.rewrite_type(term->member_type(i));
          stack_members[i] = member_type.stack_type();
          stack_simple = stack_simple && member_type.stack_type();
          heap_members[i] = member_type.heap_type();
          heap_simple = heap_simple && member_type.heap_type();
          
          size = FunctionalBuilder::max(size, member_type.size());
          alignment = FunctionalBuilder::max(alignment, member_type.alignment());
        }
        
        Term *stack_type = 0, *heap_type = 0;
        if (rewriter.pass().remove_only_unknown && !rewriter.pass().remove_all_unions) {
          if (stack_simple)
            stack_type = FunctionalBuilder::union_type(rewriter.context(), stack_members);
          
          if (heap_simple) {
            heap_type = FunctionalBuilder::union_type(rewriter.context(), heap_members);
            
            if (!rewriter.pass().remove_sizeof) {
              size = FunctionalBuilder::type_size(heap_type);
              alignment = FunctionalBuilder::type_alignment(heap_type);
            }
          }
        }
        
        return Type(size, alignment, stack_type, heap_type);
      }
      
      static Type simple_type_helper(AggregateLoweringRewriter& rewriter, Term *rewritten_type) {
        TypeSizeAlignment size_align = rewriter.pass().target_callback->type_size_alignment(rewritten_type);
        return Type(size_align.size, size_align.alignment, rewritten_type);
      }
      
      static Type pointer_type_rewrite(AggregateLoweringRewriter& rewriter, PointerType::Ptr) {
        return simple_type_helper(rewriter, FunctionalBuilder::byte_pointer_type(rewriter.context()));
      }
      
      static Type primitive_type_rewrite(AggregateLoweringRewriter& rewriter, FunctionalTerm *type) {
        PSI_ASSERT(!type->source());
        PSI_ASSERT(type->n_parameters() == 0);
        PSI_ASSERT(type->is_type());
        return simple_type_helper(rewriter, type->rewrite(rewriter.context(), StaticArray<Term*,0>()));
      }
      
      static Type metatype_rewrite(AggregateLoweringRewriter& rewriter, Metatype::Ptr term) {
        Term *size = FunctionalBuilder::size_type(term->context());
        Term *metatype_struct = FunctionalBuilder::struct_type(term->context(), StaticArray<Term*,2>(size, size));
        return rewriter.rewrite_type(metatype_struct);
      }
      
      static Type unknown_type_rewrite(AggregateLoweringRewriter& rewriter, MetatypeValue::Ptr term) {
        Term *size = rewriter.rewrite_value_stack(term->size());
        Term *alignment = rewriter.rewrite_value_stack(term->alignment());
        return Type(size, alignment);
      }
      
      static Type parameter_type_rewrite(AggregateLoweringRewriter& rewriter, Term *type) {
        PSI_ASSERT(type->source() && isa<FunctionParameterTerm>(type->source()));
        Term *size, *alignment;
        if (rewriter.pass().remove_only_unknown) {
          Term *rewritten = rewriter.rewrite_value_stack(type);
          size = FunctionalBuilder::struct_element(rewritten, 0);
          alignment = FunctionalBuilder::struct_element(rewritten, 1);
        } else {
          size = rewriter.lookup_value_stack(FunctionalBuilder::type_size(type));
          alignment = rewriter.lookup_value_stack(FunctionalBuilder::type_alignment(type));
        }
        return Type(size, alignment);
      }
      
      static Type default_type_rewrite(AggregateLoweringRewriter& rewriter, FunctionalTerm *type) {
        if (type->source()) {
          return parameter_type_rewrite(rewriter, type);
        } else {
          return primitive_type_rewrite(rewriter, type);
        }
      }
      
      typedef TermOperationMap<FunctionalTerm, Type, AggregateLoweringRewriter&> CallbackMap;
      static CallbackMap callback_map;
      
      static CallbackMap::Initializer callback_map_initializer() {
        return CallbackMap::initializer(default_type_rewrite)
          .add<ArrayType>(array_type_rewrite)
          .add<StructType>(struct_type_rewrite)
          .add<UnionType>(union_type_rewrite)
          .add<Metatype>(metatype_rewrite)
          .add<MetatypeValue>(unknown_type_rewrite)
          .add<PointerType>(pointer_type_rewrite);
      }
    };
    
    AggregateLoweringPass::TypeTermRewriter::CallbackMap
      AggregateLoweringPass::TypeTermRewriter::callback_map(AggregateLoweringPass::TypeTermRewriter::callback_map_initializer());
      
    struct AggregateLoweringPass::FunctionalTermRewriter {
      static Value type_rewrite(AggregateLoweringRewriter& rewriter, Term *term) {
        Type ty = rewriter.rewrite_type(term);
        if (rewriter.pass().remove_only_unknown) {
          return Value(FunctionalBuilder::struct_value(rewriter.context(), StaticArray<Term*,2>(ty.size(), ty.alignment())), true);
        } else {
          return Value(rewriter.store_type(ty.size(), ty.alignment()), false);
        }
      }

      static Value default_rewrite(AggregateLoweringRewriter& rewriter, FunctionalTerm *term) {
        PSI_ASSERT(rewriter.rewrite_type(term->type()).stack_type());
        unsigned n_parameters = term->n_parameters();
        SmallArray<Term*,8> parameters(n_parameters);
        for (unsigned i = 0; i != n_parameters; ++i)
          parameters[i] = rewriter.rewrite_value_stack(term->parameter(i));
        return Value(term->rewrite(rewriter.context(), parameters), true);
      }
      
      static Value aggregate_value_rewrite(AggregateLoweringRewriter& rewriter, FunctionalTerm *term) {
        Type term_type = rewriter.rewrite_type(term->type());
        if (term_type.stack_type()) {
          return default_rewrite(rewriter, term);
        } else {
          return Value(rewriter.store_value(term), false);
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
        return Value(rewriter.rewrite_type(term->parameter()).size(), true);
      }
      
      static Value metatype_alignment_rewrite(AggregateLoweringRewriter& rewriter, MetatypeAlignment::Ptr term) {
        return Value(rewriter.rewrite_type(term->parameter()).alignment(), true);
      }
      
      static Value pointer_offset_rewrite(AggregateLoweringRewriter& rewriter, PointerOffset::Ptr term) {
        Term *base_value = rewriter.rewrite_value_stack(term->pointer());
        Term *offset = rewriter.rewrite_value_stack(term->offset());
        
        Type ty = rewriter.rewrite_type(term->type()->target_type());
        if (ty.heap_type() && !rewriter.pass().pointer_arithmetic_to_bytes) {
          Term *cast_base = FunctionalBuilder::pointer_cast(base_value, ty.heap_type());
          Term *ptr = FunctionalBuilder::pointer_offset(cast_base, offset);
          Term *result = FunctionalBuilder::pointer_cast(ptr, FunctionalBuilder::byte_type(rewriter.context()));
          return Value(result, true);
        } else {
          Term *new_offset = FunctionalBuilder::mul(ty.size(), offset);
          Term *result = FunctionalBuilder::pointer_offset(base_value, new_offset);
          return Value(result, true);
        }
      }
      
      static Value pointer_cast_rewrite(AggregateLoweringRewriter& rewriter, PointerCast::Ptr term) {
        return rewriter.rewrite_value(term->pointer());
      }

      typedef TermOperationMap<FunctionalTerm, Value, AggregateLoweringRewriter&> CallbackMap;
      static CallbackMap callback_map;
      
      static CallbackMap::Initializer callback_map_initializer() {
        return CallbackMap::initializer(default_rewrite)
          .add<ArrayType>(type_rewrite)
          .add<StructType>(type_rewrite)
          .add<UnionType>(type_rewrite)
          .add<PointerType>(type_rewrite)
          .add<IntegerType>(type_rewrite)
          .add<FloatType>(type_rewrite)
          .add<EmptyType>(type_rewrite)
          .add<ByteType>(type_rewrite)
          .add<MetatypeValue>(type_rewrite)
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
          .add<MetatypeAlignment>(metatype_alignment_rewrite)
          .add<PointerOffset>(pointer_offset_rewrite)
          .add<PointerCast>(pointer_cast_rewrite);
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
        return runner.load_value(term, ptr);
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
    
    /**
     * \brief Get a value which must already have been rewritten.
     */
    AggregateLoweringPass::Value AggregateLoweringPass::AggregateLoweringRewriter::lookup_value(Term *value) {
      ValueMapType::iterator it = m_value_map.find(value);
      PSI_ASSERT(it != m_value_map.end());
      return it->second;
    }

    /**
     * Utility function which runs lookup_value and asserts that the resulting
     * value is on the stack and is non-NULL.
     */
    Term* AggregateLoweringPass::AggregateLoweringRewriter::lookup_value_stack(Term *value) {
      Value v = lookup_value(value);
      PSI_ASSERT(v.on_stack() && v.value());
      return v.value();
    }
    
    /**
     * Utility function which runs lookup_value and asserts that the resulting
     * value is not on the stack and is non-NULL.
     */
    Term* AggregateLoweringPass::AggregateLoweringRewriter::lookup_value_ptr(Term *value) {
      Value v = lookup_value(value);
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
    
    /**
     * \brief Map a block from the old function to the new one.
     */
    BlockTerm* AggregateLoweringPass::FunctionRunner::rewrite_block(BlockTerm *block) {
      return cast<BlockTerm>(lookup_value_stack(block));
    }

    /**
     * Stores a value onto the stack. The type of \c value is used to determine where to place the
     * \c alloca instruction, so that the pointer will be available at all PHI nodes that it can possibly
     * reach as a value.
     * 
     * \copydoc AggregateLoweringPass::AggregateLoweringRewriter::store_value
     */
    Term* AggregateLoweringPass::FunctionRunner::store_value(Term *value) {
      Term *ptr = create_storage(value->type());
      store_value(value, ptr);
      return ptr;
    }
    
    Term* AggregateLoweringPass::FunctionRunner::store_type(Term *size, Term *alignment) {
      Term *ptr;
      Term *byte_type = FunctionalBuilder::byte_type(context());
      Term *size_type = FunctionalBuilder::size_type(context());
      
      Type metatype = rewrite_type(FunctionalBuilder::type_type(pass().source_module()->context()));

      /*
       * Note that we should not need to change the insert point because this function is called by
       * the functional operation code generator, so the insert point should already be set to the
       * appropriate place for this op.
       * 
       * In cases involving PHI nodes however, I doubt this is true.
       */
      PSI_FAIL("This will fail when PHI nodes get involved");
      if (metatype.heap_type()) {
        ptr = builder().alloca_(metatype.heap_type());
        ptr = FunctionalBuilder::pointer_cast(ptr, size_type);
      } else {
        ptr = builder().alloca_(size_type, 2);
      }
      
      builder().store(size, ptr);
      Term *alignment_ptr = FunctionalBuilder::pointer_offset(ptr, FunctionalBuilder::size_value(context(), 1));
      builder().store(alignment, alignment_ptr);
      
      return FunctionalBuilder::pointer_cast(ptr, byte_type);
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

      PSI_ASSERT(ptr->type() == FunctionalBuilder::byte_pointer_type(context()));
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
      BlockTerm *prolog_block = new_function()->entry();
      if (!prolog_block)
        return; // This is an external function

      std::vector<BlockTerm*> old_sorted_blocks = old_function()->topsort_blocks();
      std::vector<std::pair<BlockTerm*, BlockTerm*> > sorted_blocks;
      
      // Set up block mapping for all blocks except the entry block,
      // which has already been handled
      for (std::vector<BlockTerm*>::iterator ii = old_sorted_blocks.begin(), ie = old_sorted_blocks.end(); ii != ie; ++ii) {
        BlockTerm *dominator = (*ii)->dominator() ? rewrite_block((*ii)->dominator()) : prolog_block;
        BlockTerm *new_block = new_function()->new_block(dominator);
        sorted_blocks.push_back(std::make_pair(*ii, new_block));
        m_value_map.insert(std::make_pair(*ii, Value(new_block, true)));
      }
      
      // Jump from prolog block to entry block
      InstructionBuilder(prolog_block).br(rewrite_block(old_sorted_blocks.front()));
      
      // Generate PHI nodes and convert instructions!
      for (std::vector<std::pair<BlockTerm*, BlockTerm*> >::iterator ii = sorted_blocks.begin(), ie = sorted_blocks.end(); ii != ie; ++ii) {
        BlockTerm *old_block = ii->first, *new_block = ii->second;

        // Generate PHI nodes
        BlockTerm::PhiList& phi_list = old_block->phi_nodes();
        for (BlockTerm::PhiList::iterator ji = phi_list.begin(), je = phi_list.end(); ji != je; ++ji)
          create_phi_node(new_block, &*ji);

        // Create instructions
        BlockTerm::InstructionList& insn_list = old_block->instructions();
        m_builder.set_insert_point(new_block);
        for (BlockTerm::InstructionList::iterator ji = insn_list.begin(), je = insn_list.end(); ji != je; ++ji) {
          InstructionTerm *insn = &*ji;
          Value value = InstructionTermRewriter::callback_map.call(*this, insn);
          if (value.value())
            m_value_map[insn] = value;
        }
      }
      
      // Populate preexisting PHI nodes with values
      for (std::vector<std::pair<BlockTerm*, BlockTerm*> >::iterator ii = sorted_blocks.begin(), ie = sorted_blocks.end(); ii != ie; ++ii) {
        BlockTerm *old_block = ii->first;
        BlockTerm::PhiList& phi_list = old_block->phi_nodes();
        for (BlockTerm::PhiList::iterator ji = phi_list.begin(), je = phi_list.end(); ji != je; ++ji) {
          PhiTerm *phi_node = &*ji;

          SmallArray<BlockTerm*,4> incoming_edges(phi_node->n_incoming());
          SmallArray<Term*,4> incoming_values(phi_node->n_incoming());
          for (unsigned ki = 0, ke = phi_node->n_incoming(); ki != ke; ++ki) {
            incoming_edges[ki] = rewrite_block(phi_node->incoming_block(ki));
            incoming_values[ki] = phi_node->incoming_value(ki);
          }
          
          populate_phi_node(phi_node, incoming_edges, incoming_values);
        }
      }
      
      create_phi_alloca_terms(sorted_blocks);
    }
    
    /**
     * Create suitable alloca'd storage for the given type.
     */
    Term* AggregateLoweringPass::FunctionRunner::create_alloca(Term *type) {
      Term *byte_type = FunctionalBuilder::byte_type(context());
      
      Type new_type = rewrite_type(type);
      if (new_type.heap_type()) {
        Term *alloca_insn = builder().alloca_(new_type.heap_type());
        return FunctionalBuilder::pointer_cast(alloca_insn, byte_type);
      }
      
      if (ArrayType::Ptr array_ty = dyn_cast<ArrayType>(type)) {
        Type element_type = rewrite_type(array_ty->element_type());
        if (element_type.heap_type()) {
          Term *alloca_insn = builder().alloca_(element_type.heap_type(), array_ty->length());
          return FunctionalBuilder::pointer_cast(alloca_insn, byte_type);
        }
      }
      
      return builder().alloca_(byte_type, new_type.size(), new_type.alignment());
    }

    /**
     * Create storage for an unknown type.
     * 
     * \param type Type to create storage for.
     */
    Term* AggregateLoweringPass::FunctionRunner::create_storage(Term *type) {
      if (Term *source = type->source()) {
        BlockTerm *block;
        switch(source->term_type()) {
        case term_instruction: block = rewrite_block(cast<InstructionTerm>(source)->block()); break;
        case term_phi: block = rewrite_block(cast<PhiTerm>(source)->block()); break;
        default: block = 0; break;
        }

        if (block == builder().insert_point().block())
          return create_alloca(type);
      }
      
      BlockTerm *block = builder().insert_point().block();
      PhiTerm *phi = block->new_phi(FunctionalBuilder::byte_pointer_type(context()));
      m_generated_phi_terms[type][block].alloca_.push_back(phi);
      return phi;
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
    AggregateLoweringPass::Value AggregateLoweringPass::FunctionRunner::load_value(Term *load_term, Term *ptr) {
      Type load_type = rewrite_type(load_term->type());
      if (load_type.stack_type()) {
        Term *cast_ptr = FunctionalBuilder::pointer_cast(ptr, load_type.stack_type());
        Term *load_insn = builder().load(cast_ptr);
        return (m_value_map[load_term] = Value(load_insn, true));
      }
      
      if (StructType::Ptr struct_ty = dyn_cast<StructType>(load_term->type())) {
        for (unsigned i = 0, e = struct_ty->n_members(); i != e; ++i) {
          load_value(FunctionalBuilder::struct_element(load_term, i),
                     FunctionalTermRewriter::struct_ptr_offset(*this, struct_ty, ptr, i));
        }
        // struct loads have no value because they should not be accessed directly
        return Value();
      } else if (isa<Metatype>(load_term->type())) {
        Term *size_type = FunctionalBuilder::size_type(load_term->context());
        StructType::Ptr metatype_ty = cast<StructType>(FunctionalBuilder::struct_type(load_term->context(), StaticArray<Term*,2>(size_type, size_type)));
        load_value(FunctionalBuilder::type_size(load_term), FunctionalTermRewriter::struct_ptr_offset(*this, metatype_ty, ptr, 0));
        load_value(FunctionalBuilder::type_alignment(load_term), FunctionalTermRewriter::struct_ptr_offset(*this, metatype_ty, ptr, 1));
        return Value();
      }
      
      // So this type cannot be loaded: memcpy it to the stack
      Term *target_ptr = create_storage(load_term->type());
      Value result(target_ptr, false);
      m_value_map[load_term] = result;

      if (load_type.heap_type()) {
        Term *cast_ptr = FunctionalBuilder::pointer_cast(ptr, load_type.heap_type());
        Term *cast_target_ptr = FunctionalBuilder::pointer_cast(target_ptr, load_type.heap_type());
        builder().memcpy(cast_target_ptr, cast_ptr, 1);
        return result;
      }

      if (ArrayType::Ptr array_ty = dyn_cast<ArrayType>(load_term->type())) {
        Type element_ty = rewrite_type(array_ty->element_type());
        if (element_ty.heap_type()) {
          Term *cast_ptr = FunctionalBuilder::pointer_cast(ptr, element_ty.heap_type());
          Term *cast_target_ptr = FunctionalBuilder::pointer_cast(target_ptr, element_ty.heap_type());
          Term *length = rewrite_value_stack(array_ty->length());
          builder().memcpy(cast_target_ptr, cast_ptr, length);
          return result;
        }
      }

      builder().memcpy(target_ptr, ptr, load_type.size(), load_type.alignment());
      return result;
    }
    
    AggregateLoweringPass::Type AggregateLoweringPass::FunctionRunner::rewrite_type(Term *type) {
      // Forward to parent if applicable.
      if (!type->source() || isa<GlobalTerm>(type->source()))
        return pass().global_rewriter().rewrite_type(type);
      
      TypeMapType::iterator type_it = m_type_map.find(type);
      if (type_it != m_type_map.end())
        return type_it->second;
      
      Type result;
      if (FunctionalTerm *func_type = dyn_cast<FunctionalTerm>(type)) {
        result = TypeTermRewriter::callback_map.call(*this, func_type);
      } else {
        result = TypeTermRewriter::parameter_type_rewrite(*this, type);
      }
      
      PSI_ASSERT(result.valid());
      m_type_map[type] = result;
      return result;
    }
    
    AggregateLoweringPass::Value AggregateLoweringPass::FunctionRunner::rewrite_value(Term *value) {
      // Forward to parent if applicable.
      if (!value->source() || isa<GlobalTerm>(value->source()))
        return pass().global_rewriter().rewrite_value(value);
      
      ValueMapType::iterator value_it = m_value_map.find(value);
      if (value_it != m_value_map.end()) {
        // Not all values in the value map are necessarily valid - instructions which do not
        // produce a value have NULL entries. However, if the value is used, it must be valid.
        PSI_ASSERT(value_it->second.value());
        return value_it->second;
      }
      
      // If it isn't in the m_value_map, it must be a functional term since all instructions used
      // should have been placed in m_value_map already.
      PSI_ASSERT(isa<FunctionalTerm>(value));
      
      BlockTerm *insert_block;
      switch (value->source()->term_type()) {
      case term_instruction: insert_block = cast<InstructionTerm>(value->source())->block(); break;
      case term_phi: insert_block = cast<PhiTerm>(value->source())->block(); break;
      case term_block: insert_block = cast<BlockTerm>(value->source()); break;
      case term_function_parameter: insert_block = cast<FunctionParameterTerm>(value->source())->function()->entry(); break;
      default: PSI_FAIL("unexpected term type");
      }
      
      insert_block = rewrite_block(insert_block);

      InstructionInsertPoint old_insert_point = m_builder.insert_point();
      /*
       * The aggregate lowering pass expects instruction insertions to always happen at the end of a block,
       * since instructions are recreated in order (and instructions created later cannot depend on the
       * result of earlier ones except through phi nodes which are handled last anyway).
       */
      PSI_ASSERT(!old_insert_point.instruction());

      m_builder.set_insert_point(insert_block);
      Value result = FunctionalTermRewriter::callback_map.call(*this, cast<FunctionalTerm>(value));
      m_builder.set_insert_point(old_insert_point);
      
      PSI_ASSERT(result.value());
      m_value_map[value] = result;
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
    void AggregateLoweringPass::FunctionRunner::create_phi_node(BlockTerm *block, Term *phi_term) {
      Type type = rewrite_type(phi_term->type());
      if (type.stack_type()) {
        PhiTerm *new_phi = block->new_phi(type.stack_type());
        m_value_map[phi_term] = Value(new_phi, true);
        return;
      }
      
      if (StructType::Ptr struct_ty = dyn_cast<StructType>(phi_term->type())) {
        for (unsigned i = 0, e = struct_ty->n_members(); i != e; ++i)
          create_phi_node(block, FunctionalBuilder::struct_element(phi_term, i));
      } else if (isa<Metatype>(phi_term->type())) {
        create_phi_node(block, FunctionalBuilder::type_size(phi_term));
        create_phi_node(block, FunctionalBuilder::type_alignment(phi_term));
      } else {
        PhiTerm *new_phi = block->new_phi(FunctionalBuilder::byte_pointer_type(context()));
        m_value_map[phi_term] = Value(new_phi, false);
        m_generated_phi_terms[phi_term->type()][block].user.push_back(new_phi);
      }
    }
    
    /**
     * \brief Initialize the values used by a PHI node, or a set of PHI nodes representing parts of a single value.
     * 
     * \param incoming_edges Predecessor block associated with each value, in the rewritten function.
     * 
     * \param incoming_values Values associated with each value in the original function.
     */
    void AggregateLoweringPass::FunctionRunner::populate_phi_node(Term *phi_term, ArrayPtr<BlockTerm*> incoming_edges, ArrayPtr<Term*> incoming_values) {
      Type type = rewrite_type(phi_term->type());
      if (type.stack_type()) {
        PhiTerm *new_phi = cast<PhiTerm>(lookup_value_stack(phi_term));
        for (unsigned ii = 0, ie = incoming_values.size(); ii != ie; ++ii)
          new_phi->add_incoming(incoming_edges[ii], rewrite_value_stack(incoming_values[ii]));
        return;
      }

      if (StructType::Ptr struct_ty = dyn_cast<StructType>(phi_term->type())) {
        SmallArray<Term*,4> child_incoming_values(incoming_values.size());
        for (unsigned ii = 0, ie = struct_ty->n_members(); ii != ie; ++ii) {
          for (unsigned ji = 0, je = incoming_values.size(); ji != je; ++ji)
            child_incoming_values[ji] = FunctionalBuilder::struct_element(incoming_values[ji], ii);
          populate_phi_node(FunctionalBuilder::struct_element(phi_term, ii), incoming_edges, child_incoming_values);
        }
      } else if (isa<Metatype>(phi_term->type())) {
        SmallArray<Term*,4> child_incoming_values(incoming_values.size());
        
        for (unsigned ji = 0, je = incoming_values.size(); ji != je; ++ji)
          child_incoming_values[ji] = FunctionalBuilder::type_size(incoming_values[ji]);
        populate_phi_node(FunctionalBuilder::type_size(phi_term), incoming_edges, child_incoming_values);
          
        for (unsigned ji = 0, je = incoming_values.size(); ji != je; ++ji)
          child_incoming_values[ji] = FunctionalBuilder::type_alignment(incoming_values[ji]);
        populate_phi_node(FunctionalBuilder::type_alignment(phi_term), incoming_edges, child_incoming_values);
      } else {
        PhiTerm *new_phi = cast<PhiTerm>(lookup_value_ptr(phi_term));
        for (unsigned ii = 0, ie = incoming_values.size(); ii != ie; ++ii)
          new_phi->add_incoming(incoming_edges[ii], rewrite_value_ptr(incoming_values[ii]));
      }
    }

    /**
     * \c alloca terms created by rewriting aggregates onto the heap are created as
     * PHI terms since loops can the memory used to have to be different on later
     * passes through a block.
     */
    void AggregateLoweringPass::FunctionRunner::create_phi_alloca_terms(const std::vector<std::pair<BlockTerm*, BlockTerm*> >& sorted_blocks) {
      Term *byte_type = FunctionalBuilder::byte_type(context());
      Term *byte_pointer_type = FunctionalBuilder::byte_pointer_type(context());

      // Build a map from a block to blocks it dominates
      typedef std::multimap<BlockTerm*, BlockTerm*> DominatorMapType;
      DominatorMapType dominator_map;
      for (std::vector<std::pair<BlockTerm*, BlockTerm*> >::const_iterator ji = sorted_blocks.begin(), je = sorted_blocks.end(); ji != je; ++ji)
        dominator_map.insert(std::make_pair(ji->second->dominator(), ji->second));
      
      for (TypePhiMapType::iterator ii = m_generated_phi_terms.begin(), ie = m_generated_phi_terms.end(); ii != ie; ++ii) {
        // Find block to create alloca's for current type
        BlockTerm *type_block = new_function()->entry();
        if (Term *source = ii->first->source()) {
          switch (source->term_type()) {
          case term_instruction: type_block = rewrite_block(cast<InstructionTerm>(source)->block()); break;
          case term_phi: type_block = rewrite_block(cast<PhiTerm>(source)->block()); break;
          default: break;
          }
        }

        // Find blocks dominated by the block the type is created in, recursively
        std::vector<BlockTerm*> dominated_blocks(1, type_block);
        for (unsigned count = 0; count != dominated_blocks.size(); ++count) {
          for (std::pair<DominatorMapType::iterator, DominatorMapType::iterator> pair = dominator_map.equal_range(dominated_blocks[count]);
               pair.first != pair.second; ++pair.first)
            dominated_blocks.push_back(pair.first->second);
        }
        
        // The total number of slots required
        unsigned total_vars = 0;
        
        // Holds the number of variables in scope in the specified block
        std::map<BlockTerm*, unsigned> active_vars;
        active_vars[type_block] = 0;
        for (std::vector<BlockTerm*>::iterator ji = boost::next(dominated_blocks.begin()), je = dominated_blocks.end(); ji != je; ++ji) {
          BlockTerm *block = *ji;
          BlockPhiData& data = ii->second[block];
          unsigned block_vars = active_vars[block->dominator()] + data.user.size() + data.alloca_.size();
          active_vars[block] = block_vars;
          total_vars = std::max(block_vars, total_vars);
        }
        
        // Create PHI nodes in each block to track the used/free list
        for (std::vector<BlockTerm*>::iterator ji = boost::next(dominated_blocks.begin()), je = dominated_blocks.end(); ji != je; ++ji) {
          BlockTerm *block = *ji;
          BlockPhiData& data = ii->second[*ji];
          unsigned used_vars = active_vars[block];
          unsigned free_vars = total_vars - used_vars;
          for (unsigned ki = 0, ke = data.user.size(); ki != ke; ++ki)
            data.used.push_back(block->new_phi(byte_pointer_type));
          for (unsigned k = 0; k != free_vars; ++k)
            data.free_.push_back(block->new_phi(byte_pointer_type));
        }
        
        // Create memory slots
        m_builder.set_insert_point(&type_block->instructions().back());
        
        BlockPhiData& entry_data = ii->second[type_block];
        entry_data.free_.resize(total_vars);
        Type new_type = rewrite_type(ii->first);
        if (new_type.heap_type()) {
          for (unsigned ji = 0, je = entry_data.free_.size(); ji != je; ++ji)
            entry_data.free_[ji] = FunctionalBuilder::pointer_cast(builder().alloca_(new_type.heap_type()), byte_type);
          goto slots_created;
        }
        
        if (ArrayType::Ptr array_ty = dyn_cast<ArrayType>(ii->first)) {
          Type element_type = rewrite_type(array_ty->element_type());
          if (element_type.heap_type()) {
            for (unsigned ji = 0, je = entry_data.free_.size(); ji != je; ++ji)
              entry_data.free_[ji] = FunctionalBuilder::pointer_cast(builder().alloca_(element_type.heap_type(), array_ty->length()), byte_type);
            goto slots_created;
          }
        }
        
        // Default mechanism suitable for any type
        for (unsigned ji = 0, je = entry_data.free_.size(); ji != je; ++ji)
          entry_data.free_[ji] = builder().alloca_(byte_type, new_type.size(), new_type.alignment());
        
      slots_created:
        for (std::vector<BlockTerm*>::iterator ji = dominated_blocks.begin(), je = dominated_blocks.end(); ji != je; ++ji) {
          BlockTerm *source_block = *ji;
          BlockPhiData& source_data = ii->second[source_block];
          
          const std::vector<BlockTerm*>& successors = source_block->successors();
          for (std::vector<BlockTerm*>::const_iterator ki = successors.begin(), ke = successors.end(); ki != ke; ++ki) {
            BlockTerm *target_block = *ki;
            BlockTerm *common_dominator = BlockTerm::common_dominator(source_block, target_block->dominator());
            
            if (!common_dominator->dominated_by(type_block))
              continue;
            
            BlockPhiData& target_data = ii->second[target_block];
            
            // Find all slots newly used between common_dominator and source_block
            std::set<Term*> free_slots_set;
            for (BlockTerm *parent = source_block; parent != common_dominator; parent = parent->dominator()) {
              BlockPhiData& parent_data = ii->second[parent];
              free_slots_set.insert(parent_data.alloca_.begin(), parent_data.alloca_.end());
              free_slots_set.insert(parent_data.used.begin(), parent_data.used.end());
            }
            
            std::set<Term*> used_slots_set;
            for (std::vector<PhiTerm*>::iterator li = target_data.user.begin(), le = target_data.user.end(); li != le; ++li) {
              Term *incoming = (*li)->incoming_value_from(source_block);
              // Filter out values which are user-specified
              if (free_slots_set.erase(incoming))
                used_slots_set.insert(incoming);
            }
              
            std::vector<Term*> used_slots(used_slots_set.begin(), used_slots_set.end());
            std::vector<Term*> free_slots = source_data.free_;
            free_slots.insert(free_slots.end(), free_slots_set.begin(), free_slots_set.end());
            
            PSI_ASSERT(free_slots.size() >= target_data.free_.size() + target_data.alloca_.size());
            PSI_ASSERT(free_slots.size() + used_slots.size() == target_data.used.size() + target_data.free_.size() + target_data.alloca_.size());
            
            unsigned free_to_used_transfer = target_data.used.size() - used_slots.size();
            used_slots.insert(used_slots.end(), free_slots.end() - free_to_used_transfer, free_slots.end());
            free_slots.erase(free_slots.end() - free_to_used_transfer, free_slots.end());
            
            PSI_ASSERT(used_slots.size() == target_data.used.size());
            PSI_ASSERT(free_slots.size() == target_data.free_.size() + target_data.alloca_.size());
            
            for (unsigned li = 0, le = used_slots.size(); li != le; ++li)
              cast<PhiTerm>(target_data.used[li])->add_incoming(source_block, used_slots[li]);
            for (unsigned li = 0, le = target_data.alloca_.size(); li != le; ++li)
              target_data.alloca_[li]->add_incoming(source_block, free_slots[li]);
            for (unsigned li = 0, le = target_data.free_.size(), offset = target_data.alloca_.size(); li != le; ++li)
              cast<PhiTerm>(target_data.free_[li])->add_incoming(source_block, free_slots[li+offset]);
          }
        }
      }
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
      
      PSI_NOT_IMPLEMENTED();
    }
    
    Term* AggregateLoweringPass::ModuleLevelRewriter::store_value(Term *value) {
      PSI_NOT_IMPLEMENTED();
    }
    
    Term* AggregateLoweringPass::ModuleLevelRewriter::store_type(Term *size, Term *alignment) {
      PSI_NOT_IMPLEMENTED();
    }
    
    AggregateLoweringPass::Type AggregateLoweringPass::ModuleLevelRewriter::rewrite_type(Term *type) {
      TypeMapType::iterator type_it = m_type_map.find(type);
      if (type_it != m_type_map.end())
        return type_it->second;
      
      Type result = TypeTermRewriter::callback_map.call(*this, cast<FunctionalTerm>(type));
      PSI_ASSERT(result.valid());
      m_type_map[type] = result;
      return result;
    }
    
    AggregateLoweringPass::Value AggregateLoweringPass::ModuleLevelRewriter::rewrite_value(Term *value) {
      ValueMapType::iterator value_it = m_value_map.find(value);
      if (value_it != m_value_map.end())
        return value_it->second;
      
      Value result = FunctionalTermRewriter::callback_map.call(*this, cast<FunctionalTerm>(value));
      PSI_ASSERT(result.value());
      m_value_map[value] = result;
      return result;
    }

    /**
     * Initialize a global variable build with no elements, zero size and minimum alignment.
     */
    AggregateLoweringPass::GlobalBuildStatus::GlobalBuildStatus(Context& context) {
      first_element_alignment = max_element_alignment = alignment = FunctionalBuilder::size_value(context, 1);
      elements_size = size = FunctionalBuilder::size_value(context, 0);
    }
    
    /**
     * Initialize a global variable build with one element, and the specified sizes and alignment.
     */
    AggregateLoweringPass::GlobalBuildStatus::GlobalBuildStatus(Term *element, Term *element_size_, Term *element_alignment_, Term *size_, Term *alignment_)
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
        global_pad_to_size(status, child_start, child.first_element_alignment, is_value);
        status.elements.insert(status.elements.end(), child.elements.begin(), child.elements.end());
        status.elements_size = FunctionalBuilder::add(child_start, child.elements_size);
      }

      status.size = FunctionalBuilder::add(child_start, child.size);
      status.alignment = FunctionalBuilder::max(status.alignment, child.alignment);
      status.max_element_alignment = FunctionalBuilder::max(status.max_element_alignment, child.max_element_alignment);
    }
    
    /**
     * If the appropriate flags are set, rewrite the global build status \c status
     * from a sequence of elements to a single element which is a struct of the
     * previous elements.
     * 
     * \param is_value If \c status represents a value this should be true, otherwise
     * \c status represents a type.
     */
    void AggregateLoweringPass::global_group(GlobalBuildStatus& status, bool is_value) {
      if (!flatten_globals)
        return;
      
      Term *new_element;
      if (is_value)
        new_element = FunctionalBuilder::struct_value(context(), status.elements);
      else
        new_element = FunctionalBuilder::struct_type(context(), status.elements);
      
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
    AggregateLoweringPass::GlobalBuildStatus AggregateLoweringPass::rewrite_global_type(Term *value) {
      Type value_ty = global_rewriter().rewrite_type(value->type());
      if (value_ty.stack_type())
        return GlobalBuildStatus(value_ty.heap_type(), value_ty.size(), value_ty.alignment(), value_ty.size(), value_ty.alignment());

      if (ArrayValue::Ptr array_val = dyn_cast<ArrayValue>(value)) {
        GlobalBuildStatus status(context());
        for (unsigned i = 0, e = array_val->length(); i != e; ++i)
          global_append(status, rewrite_global_type(array_val->value(i)), false);
        global_group(status, false);
        return status;
      } else if (StructValue::Ptr struct_val = dyn_cast<StructValue>(value)) {
        GlobalBuildStatus status(context());
        for (unsigned i = 0, e = struct_val->n_members(); i != e; ++i)
          global_append(status, rewrite_global_type(struct_val->member_value(i)), false);
        global_group(status, false);
        return status;
      } else if (UnionValue::Ptr union_val = dyn_cast<UnionValue>(value)) {
        GlobalBuildStatus status = rewrite_global_type(union_val->value());
        status.size = value_ty.size();
        status.alignment = value_ty.alignment();
        return status;
      } else {
        PSI_FAIL("unsupported global element");
      }
    }

    AggregateLoweringPass::GlobalBuildStatus AggregateLoweringPass::rewrite_global_value(Term *value) {
      Type value_ty = global_rewriter().rewrite_type(value->type());
      if (value_ty.stack_type()) {
        Term *rewritten_value = m_global_rewriter.rewrite_value_stack(value);
        return GlobalBuildStatus(rewritten_value, value_ty.size(), value_ty.alignment(), value_ty.size(), value_ty.alignment());
      }

      if (ArrayValue::Ptr array_val = dyn_cast<ArrayValue>(value)) {
        GlobalBuildStatus status(context());
        for (unsigned i = 0, e = array_val->length(); i != e; ++i)
          global_append(status, rewrite_global_value(array_val->value(i)), true);
        global_group(status, true);
        return status;
      } else if (StructValue::Ptr struct_val = dyn_cast<StructValue>(value)) {
        GlobalBuildStatus status(context());
        for (unsigned i = 0, e = struct_val->n_members(); i != e; ++i)
          global_append(status, rewrite_global_value(struct_val->member_value(i)), true);
        global_group(status, true);
        return status;
      } else if (UnionValue::Ptr union_val = dyn_cast<UnionValue>(value)) {
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
    remove_only_unknown(false),
    remove_all_unions(false),
    remove_stack_arrays(false),
    remove_sizeof(false),
    pointer_arithmetic_to_bytes(false),
    flatten_globals(false) {
    }

    void AggregateLoweringPass::update_implementation(bool incremental) {
      if (!incremental)
        m_global_rewriter = ModuleLevelRewriter(this);
      
      std::vector<std::pair<GlobalVariableTerm*, GlobalVariableTerm*> > rewrite_globals;
      std::vector<std::pair<FunctionTerm*, boost::shared_ptr<FunctionRunner> > > rewrite_functions;
      
      Term *byte_type = FunctionalBuilder::byte_type(context());
        
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

          Term *cast_ptr = FunctionalBuilder::pointer_cast(new_var, byte_type);
          global_rewriter().m_value_map[old_var] = Value(cast_ptr, true);
          rewrite_globals.push_back(std::make_pair(old_var, new_var));
        } else {
          FunctionTerm *old_function = cast<FunctionTerm>(term);
          boost::shared_ptr<FunctionRunner> runner(new FunctionRunner(this, old_function));
          Term *cast_ptr = FunctionalBuilder::pointer_cast(runner->new_function(), byte_type);
          global_rewriter().m_value_map[old_function] = Value(cast_ptr, true);
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
