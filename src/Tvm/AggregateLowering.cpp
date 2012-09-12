#include "AggregateLowering.hpp"
#include "Aggregate.hpp"
#include "Recursive.hpp"
#include "Instructions.hpp"
#include "InstructionBuilder.hpp"
#include "FunctionalBuilder.hpp"
#include "TermOperationMap.hpp"
#include "Utility.hpp"

#include <boost/next_prior.hpp>
#include <set>
#include <map>
#include <utility>

namespace Psi {
  namespace Tvm {
    struct AggregateLoweringPass::TypeTermRewriter {
      static LoweredType array_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ArrayType>& term) {
        ValuePtr<> length = rewriter.rewrite_value_stack(term->length());
        LoweredType element_type = rewriter.rewrite_type(term->element_type());
        ValuePtr<> size = FunctionalBuilder::mul(length, element_type.size(), term->location());
        ValuePtr<> alignment = element_type.alignment();
        
        ValuePtr<> stack_type, heap_type;
        if (rewriter.pass().remove_only_unknown) {
          if (element_type.stack_type() && !rewriter.pass().remove_stack_arrays)
            stack_type = FunctionalBuilder::array_type(element_type.stack_type(), length, term->location());
          
          if (element_type.heap_type()) {
            heap_type = FunctionalBuilder::array_type(element_type.heap_type(), length, term->location());
            
            if (!rewriter.pass().remove_sizeof) {
              size = FunctionalBuilder::type_size(heap_type, term->location());
              alignment = FunctionalBuilder::type_alignment(heap_type, term->location());
            }
          }
        }
        
        return LoweredType(size, alignment, stack_type, heap_type);
      }

      static LoweredType struct_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<StructType>& term) {
        ValuePtr<> size = FunctionalBuilder::size_value(rewriter.context(), 0, term->location());
        ValuePtr<> alignment = FunctionalBuilder::size_value(rewriter.context(), 1, term->location());
        
        std::vector<ValuePtr<> > stack_members(term->n_members()), heap_members(term->n_members());
        bool stack_simple = true, heap_simple = true;
        for (unsigned i = 0; i != stack_members.size(); ++i) {
          LoweredType member_type = rewriter.rewrite_type(term->member_type(i));
          stack_members[i] = member_type.stack_type();
          stack_simple = stack_simple && member_type.stack_type();
          heap_members[i] = member_type.heap_type();
          heap_simple = heap_simple && member_type.heap_type();
          
          ValuePtr<> aligned_size = FunctionalBuilder::align_to(size, member_type.alignment(), term->location());
          size = FunctionalBuilder::add(aligned_size, member_type.size(), term->location());
          alignment = FunctionalBuilder::max(alignment, member_type.alignment(), term->location());
        }
        
        ValuePtr<> stack_type, heap_type;
        if (rewriter.pass().remove_only_unknown) {
          if (stack_simple)
            stack_type = FunctionalBuilder::struct_type(rewriter.context(), stack_members, term->location());
          
          if (heap_simple) {
            heap_type = FunctionalBuilder::struct_type(rewriter.context(), heap_members, term->location());
            
            if (!rewriter.pass().remove_sizeof) {
              size = FunctionalBuilder::type_size(heap_type, term->location());
              alignment = FunctionalBuilder::type_alignment(heap_type, term->location());
            }
          }
        }
        
        return LoweredType(size, alignment, stack_type, heap_type);
      }

      static LoweredType union_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<UnionType>& term) {
        ValuePtr<> size = FunctionalBuilder::size_value(rewriter.context(), 0, term->location());
        ValuePtr<> alignment = FunctionalBuilder::size_value(rewriter.context(), 1, term->location());
        
        std::vector<ValuePtr<> > stack_members(term->n_members()), heap_members(term->n_members());
        bool stack_simple = true, heap_simple = true;
        for (unsigned i = 0; i != stack_members.size(); ++i) {
          LoweredType member_type = rewriter.rewrite_type(term->member_type(i));
          stack_members[i] = member_type.stack_type();
          stack_simple = stack_simple && member_type.stack_type();
          heap_members[i] = member_type.heap_type();
          heap_simple = heap_simple && member_type.heap_type();
          
          size = FunctionalBuilder::max(size, member_type.size(), term->location());
          alignment = FunctionalBuilder::max(alignment, member_type.alignment(), term->location());
        }
        
        ValuePtr<> stack_type, heap_type;
        if (rewriter.pass().remove_only_unknown && !rewriter.pass().remove_all_unions) {
          if (stack_simple)
            stack_type = FunctionalBuilder::union_type(rewriter.context(), stack_members, term->location());
          
          if (heap_simple) {
            heap_type = FunctionalBuilder::union_type(rewriter.context(), heap_members, term->location());
            
            if (!rewriter.pass().remove_sizeof) {
              size = FunctionalBuilder::type_size(heap_type, term->location());
              alignment = FunctionalBuilder::type_alignment(heap_type, term->location());
            }
          }
        }
        
        return LoweredType(size, alignment, stack_type, heap_type);
      }
      
      static LoweredType simple_type_helper(AggregateLoweringRewriter& rewriter, const ValuePtr<>& rewritten_type) {
        TypeSizeAlignment size_align = rewriter.pass().target_callback->type_size_alignment(rewritten_type);
        return LoweredType(size_align.size, size_align.alignment, rewritten_type);
      }
      
      static LoweredType pointer_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<PointerType>& term) {
        return simple_type_helper(rewriter, FunctionalBuilder::byte_pointer_type(rewriter.context(), term->location()));
      }
      
      static LoweredType primitive_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionalValue>& type) {
        PSI_ASSERT(!type->source());
        PSI_ASSERT(type->is_type());
        
        class TrivialRewriteCallback : public RewriteCallback {
        public:
          TrivialRewriteCallback(Context& context) : RewriteCallback(context) {}
          virtual ValuePtr<> rewrite(const ValuePtr<>&) {
            PSI_FAIL("Primitive type should not require internal rewriting.");
          }
        } callback(rewriter.context());
        
        return simple_type_helper(rewriter, type->rewrite(callback));
      }
      
      static LoweredType metatype_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<Metatype>& term) {
        ValuePtr<> size = FunctionalBuilder::size_type(term->context(), term->location());
        std::vector<ValuePtr<> > members(2, size);
        ValuePtr<> metatype_struct = FunctionalBuilder::struct_type(term->context(), members, term->location());
        return rewriter.rewrite_type(metatype_struct);
      }
      
      static LoweredType unknown_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<MetatypeValue>& term) {
        ValuePtr<> size = rewriter.rewrite_value_stack(term->size());
        ValuePtr<> alignment = rewriter.rewrite_value_stack(term->alignment());
        return LoweredType(size, alignment);
      }
      
      static LoweredType parameter_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<>& type) {
        PSI_ASSERT(type->source() && isa<FunctionParameter>(type->source()));
        ValuePtr<> size, alignment;
        if (rewriter.pass().remove_only_unknown) {
          ValuePtr<> rewritten = rewriter.rewrite_value_stack(type);
          size = FunctionalBuilder::element_value(rewritten, 0, type->location());
          alignment = FunctionalBuilder::element_value(rewritten, 1, type->location());
        } else {
          size = rewriter.lookup_value_stack(FunctionalBuilder::type_size(type, type->location()));
          alignment = rewriter.lookup_value_stack(FunctionalBuilder::type_alignment(type, type->location()));
        }
        return LoweredType(size, alignment);
      }
      
      static LoweredType default_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionalValue>& type) {
        PSI_ASSERT(type);
        if (type->source()) {
          return parameter_type_rewrite(rewriter, type);
        } else {
          return primitive_type_rewrite(rewriter, type);
        }
      }
      
      typedef TermOperationMap<FunctionalValue, LoweredType, AggregateLoweringRewriter&> CallbackMap;
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
      static LoweredValue type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<>& term) {
        LoweredType ty = rewriter.rewrite_type(term);
        if (rewriter.pass().remove_only_unknown) {
          std::vector<ValuePtr<> > members;
          members.push_back(ty.size());
          members.push_back(ty.alignment());
          return LoweredValue(FunctionalBuilder::struct_value(rewriter.context(), members, term->location()), true);
        } else {
          return LoweredValue(rewriter.store_type(ty.size(), ty.alignment(), term->location()), false);
        }
      }

      static LoweredValue default_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionalValue>& term) {
        PSI_ASSERT(rewriter.rewrite_type(term->type()).stack_type());

        class Callback : public RewriteCallback {
          AggregateLoweringRewriter *m_rewriter;
        public:
          Callback(AggregateLoweringRewriter& rewriter)
          : RewriteCallback(rewriter.context()),
          m_rewriter(&rewriter) {
          }

          virtual ValuePtr<> rewrite(const ValuePtr<>& value) {
            return m_rewriter->rewrite_value_stack(value);
          }
        } callback(rewriter);
        
        return LoweredValue(term->rewrite(callback), true);
      }
      
      static LoweredValue aggregate_value_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionalValue>& term) {
        LoweredType term_type = rewriter.rewrite_type(term->type());
        if (term_type.stack_type()) {
          return default_rewrite(rewriter, term);
        } else {
          return LoweredValue(rewriter.store_value(term, term->location()), false);
        }
      }
      
      static LoweredValue outer_ptr_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<OuterPtr>& term) {
        ValuePtr<PointerType> ptr_ty = value_cast<PointerType>(term->pointer()->type());
        ValuePtr<> base = FunctionalBuilder::pointer_cast(rewriter.rewrite_value_stack(term->pointer()),
                                                          FunctionalBuilder::byte_type(rewriter.context(), term->location()),
                                                          term->location());
        
        ValuePtr<UpwardReference> up = dyn_unrecurse<UpwardReference>(ptr_ty->upref());
        ValuePtr<> offset;
        if (ValuePtr<StructType> struct_ty = dyn_unrecurse<StructType>(up->outer_type())) {
          offset = rewriter.rewrite_value_stack(FunctionalBuilder::struct_element_offset(struct_ty, size_to_unsigned(up->index()), term->location()));
        } else if (ValuePtr<ArrayType> array_ty = dyn_unrecurse<ArrayType>(up)) {
          offset = FunctionalBuilder::mul(rewriter.rewrite_value_stack(up->index()),
                                          rewriter.rewrite_type(array_ty->element_type()).size(),
                                          term->location());
        } else if (ValuePtr<UnionType> union_ty = dyn_unrecurse<UnionType>(up)) {
          return LoweredValue(base, true);
        } else {
          throw TvmInternalError("Upward reference cannot be unfolded");
        }
        
        offset = FunctionalBuilder::neg(offset, term->location());
        return LoweredValue(FunctionalBuilder::pointer_offset(base, offset, term->location()), true);
      }

      /**
       * Get a pointer to an array element from an array pointer.
       * 
       * This is common to both array_element_rewrite() and element_ptr_rewrite().
       * 
       * \param unchecked_array_ty Array type. This must not have been lowered.
       * \param base Pointer to array. This must have already been lowered.
       */
      static ValuePtr<> array_ptr_offset(AggregateLoweringRewriter& rewriter, const ValuePtr<>& unchecked_array_ty, const ValuePtr<>& base, const ValuePtr<>& index, const SourceLocation& location) {
        ValuePtr<ArrayType> array_ty = dyn_unrecurse<ArrayType>(unchecked_array_ty);
        if (!array_ty)
          throw TvmUserError("array type argument did not evaluate to an array type");

        LoweredType array_ty_l = rewriter.rewrite_type(array_ty);
        if (array_ty_l.heap_type()) {
          ValuePtr<> array_ptr = FunctionalBuilder::pointer_cast(base, array_ty_l.heap_type(), location);
          return FunctionalBuilder::element_ptr(array_ptr, index, location);
        }

        LoweredType element_ty = rewriter.rewrite_type(array_ty->element_type());
        if (element_ty.heap_type()) {
          ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(base, element_ty.heap_type(), location);
          return FunctionalBuilder::pointer_offset(cast_ptr, index, location);
        }

        ValuePtr<> element_size = rewriter.rewrite_value_stack(FunctionalBuilder::type_size(array_ty->element_type(), location));
        ValuePtr<> offset = FunctionalBuilder::mul(element_size, index, location);
        PSI_ASSERT(base->type() == FunctionalBuilder::byte_pointer_type(rewriter.context(), location));
        return FunctionalBuilder::pointer_offset(base, offset, location);
      }

      static LoweredValue array_element_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementValue>& term) {
        ValuePtr<> index = rewriter.rewrite_value_stack(term->aggregate());
        LoweredValue array_val = rewriter.rewrite_value(term->aggregate());
        if (array_val.on_stack()) {
          return LoweredValue(FunctionalBuilder::element_value(array_val.value(), index, term->location()), true);
        } else {
          ValuePtr<> element_ptr = array_ptr_offset(rewriter, term->aggregate()->type(), array_val.value(), index, term->location());
          return rewriter.load_value(term, element_ptr, term->location());
        }
      }
      
      static LoweredValue array_element_ptr_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementPtr>& term) {
        ValuePtr<> array_ptr = rewriter.rewrite_value_stack(term->aggregate_ptr());
        ValuePtr<> index = rewriter.rewrite_value_stack(term->index());
        
        ValuePtr<PointerType> pointer_type = dyn_unrecurse<PointerType>(term->aggregate_ptr()->type());
        if (!pointer_type)
          throw TvmUserError("array_ep argument did not evaluate to a pointer");
        
        return LoweredValue(array_ptr_offset(rewriter, pointer_type->target_type(), array_ptr, index, term->location()), true);
      }
      
      /**
       * Get a pointer to an array element from an array pointer.
       * 
       * This is common to both struct_element_rewrite() and element_ptr_rewrite().
       * 
       * \param base Pointer to struct. This must have already been lowered.
       */
      static ValuePtr<> struct_ptr_offset(AggregateLoweringRewriter& rewriter, const ValuePtr<>& unchecked_struct_ty, const ValuePtr<>& base, unsigned index, const SourceLocation& location) {
        ValuePtr<StructType> struct_ty = dyn_unrecurse<StructType>(unchecked_struct_ty);
        if (!struct_ty)
          throw TvmInternalError("struct type value did not evaluate to a struct type");

        LoweredType struct_ty_rewritten = rewriter.rewrite_type(struct_ty);
        if (struct_ty_rewritten.heap_type()) {
          ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(base, struct_ty_rewritten.heap_type(), location);
          return FunctionalBuilder::element_ptr(cast_ptr, index, location);
        }
        
        PSI_ASSERT(base->type() == FunctionalBuilder::byte_pointer_type(rewriter.context(), location));
        ValuePtr<> offset = rewriter.rewrite_value_stack(FunctionalBuilder::struct_element_offset(struct_ty, index, location));
        return FunctionalBuilder::pointer_offset(base, offset, location);
      }
      
      static LoweredValue struct_element_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementValue>& term) {
        LoweredValue struct_val = rewriter.rewrite_value(term->aggregate());
        if (struct_val.on_stack()) {
          return LoweredValue(FunctionalBuilder::element_value(struct_val.value(), size_to_unsigned(term->index()), term->location()), true);
        } else {
          ValuePtr<> member_ptr = struct_ptr_offset(rewriter, term->aggregate()->type(), struct_val.value(), size_to_unsigned(term->index()), term->location());
          return rewriter.load_value(term, member_ptr, term->location());
        }
      }

      static LoweredValue struct_element_ptr_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementPtr>& term) {
        ValuePtr<> struct_ptr = rewriter.rewrite_value_stack(term->aggregate_ptr());
        
        ValuePtr<PointerType> pointer_type = dyn_unrecurse<PointerType>(term->aggregate_ptr()->type());
        if (!pointer_type)
          throw TvmUserError("struct_ep argument did not evaluate to a pointer");
        
        return LoweredValue(struct_ptr_offset(rewriter, pointer_type->target_type(), struct_ptr, size_to_unsigned(term->index()), term->location()), true);
      }

      static LoweredValue struct_element_offset_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<StructElementOffset>& term) {
        ValuePtr<StructType> struct_ty = dyn_unrecurse<StructType>(term->struct_type());
        if (!struct_ty)
          throw TvmUserError("struct_eo argument did not evaluate to a struct type");

        ValuePtr<> offset = FunctionalBuilder::size_value(rewriter.context(), 0, term->location());
        
        for (unsigned ii = 0, ie = term->index(); ; ++ii) {
          ValuePtr<> member_type = struct_ty->member_type(ii);
          ValuePtr<> member_alignment = rewriter.rewrite_value_stack(FunctionalBuilder::type_alignment(member_type, term->location()));
          offset = FunctionalBuilder::align_to(offset, member_alignment, term->location());
          if (ii == ie)
            break;

          ValuePtr<> member_size = rewriter.rewrite_value_stack(FunctionalBuilder::type_size(member_type, term->location()));
          offset = FunctionalBuilder::add(offset, member_size, term->location());
        }
        
        return LoweredValue(offset, true);
      }

      static LoweredValue union_element_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementValue>& term) {
        LoweredValue union_val = rewriter.rewrite_value(term->aggregate());
        if (union_val.on_stack()) {
          return LoweredValue(FunctionalBuilder::element_value(union_val.value(), term->index(), term->location()), true);
        } else {
          LoweredType member_ty = rewriter.rewrite_type(term->type());
          return rewriter.load_value(term, union_val.value(), term->location());
        }
      }

      static LoweredValue union_element_ptr_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementPtr>& term) {
        return LoweredValue(rewriter.rewrite_value_stack(term->aggregate_ptr()), true);
      }

      static LoweredValue metatype_size_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<MetatypeSize>& term) {
        return LoweredValue(rewriter.rewrite_type(term->parameter()).size(), true);
      }
      
      static LoweredValue metatype_alignment_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<MetatypeAlignment>& term) {
        return LoweredValue(rewriter.rewrite_type(term->parameter()).alignment(), true);
      }
      
      static LoweredValue pointer_offset_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<PointerOffset>& term) {
        ValuePtr<> base_value = rewriter.rewrite_value_stack(term->pointer());
        ValuePtr<> offset = rewriter.rewrite_value_stack(term->offset());
        
        LoweredType ty = rewriter.rewrite_type(term->pointer_type()->target_type());
        if (ty.heap_type() && !rewriter.pass().pointer_arithmetic_to_bytes) {
          ValuePtr<> cast_base = FunctionalBuilder::pointer_cast(base_value, ty.heap_type(), term->location());
          ValuePtr<> ptr = FunctionalBuilder::pointer_offset(cast_base, offset, term->location());
          ValuePtr<> result = FunctionalBuilder::pointer_cast(ptr, FunctionalBuilder::byte_type(rewriter.context(), term->location()), term->location());
          return LoweredValue(result, true);
        } else {
          ValuePtr<> new_offset = FunctionalBuilder::mul(ty.size(), offset, term->location());
          ValuePtr<> result = FunctionalBuilder::pointer_offset(base_value, new_offset, term->location());
          return LoweredValue(result, true);
        }
      }
      
      static LoweredValue pointer_cast_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<PointerCast>& term) {
        return rewriter.rewrite_value(term->pointer());
      }
      
      static LoweredValue unwrap_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<Unwrap>& term) {
        return rewriter.rewrite_value(term->value());
      }
      
      static LoweredValue apply_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ApplyValue>& term) {
        return rewriter.rewrite_value(term->unpack());
      }
      
      static LoweredValue element_value_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementValue>& term) {
        ValuePtr<> ty = term->aggregate()->type();
        if (dyn_unrecurse<StructType>(ty))
          return struct_element_rewrite(rewriter, term);
        else if (dyn_unrecurse<ArrayType>(ty))
          return array_element_rewrite(rewriter, term);
        else if (dyn_unrecurse<UnionType>(ty))
          return union_element_rewrite(rewriter, term);
        else
          throw TvmUserError("element_value aggregate argument is not an aggregate type");
      }
      
      static LoweredValue element_ptr_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementPtr>& term) {
        ValuePtr<PointerType> ptr_ty = dyn_unrecurse<PointerType>(term->aggregate_ptr()->type());
        if (!ptr_ty)
          throw TvmUserError("element_ptr aggregate argument is not a pointer");

        ValuePtr<> ty = ptr_ty->target_type();
        if (dyn_unrecurse<StructType>(ty))
          return struct_element_ptr_rewrite(rewriter, term);
        else if (dyn_unrecurse<ArrayType>(ty))
          return array_element_ptr_rewrite(rewriter, term);
        else if (dyn_unrecurse<UnionType>(ty))
          return union_element_ptr_rewrite(rewriter, term);
        else
          throw TvmUserError("element_value aggregate argument is not an aggregate type");
      }

      typedef TermOperationMap<FunctionalValue, LoweredValue, AggregateLoweringRewriter&> CallbackMap;
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
          .add<OuterPtr>(outer_ptr_rewrite)
          .add<ArrayValue>(aggregate_value_rewrite)
          .add<StructValue>(aggregate_value_rewrite)
          .add<UnionValue>(aggregate_value_rewrite)
          .add<StructElementOffset>(struct_element_offset_rewrite)
          .add<MetatypeSize>(metatype_size_rewrite)
          .add<MetatypeAlignment>(metatype_alignment_rewrite)
          .add<PointerOffset>(pointer_offset_rewrite)
          .add<PointerCast>(pointer_cast_rewrite)
          .add<Unwrap>(unwrap_rewrite)
          .add<ApplyValue>(apply_rewrite)
          .add<ElementValue>(element_value_rewrite)
          .add<ElementPtr>(element_ptr_rewrite);
      }
    };

    AggregateLoweringPass::FunctionalTermRewriter::CallbackMap
      AggregateLoweringPass::FunctionalTermRewriter::callback_map(AggregateLoweringPass::FunctionalTermRewriter::callback_map_initializer());

    struct AggregateLoweringPass::InstructionTermRewriter {
      static LoweredValue return_rewrite(FunctionRunner& runner, const ValuePtr<Return>& term) {
        return LoweredValue(runner.pass().target_callback->lower_return(runner, term->value, term->location()), true);
      }

      static LoweredValue br_rewrite(FunctionRunner& runner, const ValuePtr<UnconditionalBranch>& term) {
        ValuePtr<> target = runner.rewrite_value_stack(term->target);
        return LoweredValue(runner.builder().br(value_cast<Block>(target), term->location()), true);
      }

      static LoweredValue cond_br_rewrite(FunctionRunner& runner, const ValuePtr<ConditionalBranch>& term) {
        ValuePtr<> cond = runner.rewrite_value_stack(term->condition);
        ValuePtr<> true_target = runner.rewrite_value_stack(term->true_target);
        ValuePtr<> false_target = runner.rewrite_value_stack(term->false_target);
        return LoweredValue(runner.builder().cond_br(cond, value_cast<Block>(true_target), value_cast<Block>(false_target), term->location()), true);
      }

      static LoweredValue call_rewrite(FunctionRunner& runner, const ValuePtr<Call>& term) {
        runner.pass().target_callback->lower_function_call(runner, term);
        return LoweredValue();
      }

      static LoweredValue alloca_rewrite(FunctionRunner& runner, const ValuePtr<Alloca>& term) {
        LoweredType type = runner.rewrite_type(term->element_type);
        ValuePtr<> count = runner.rewrite_value_stack(term->count);
        ValuePtr<> alignment = runner.rewrite_value_stack(term->alignment);
        ValuePtr<> stack_ptr;
        if (type.heap_type()) {
          stack_ptr = runner.builder().alloca_(type.heap_type(), count, alignment, term->location());
        } else {
          ValuePtr<> type_size = runner.rewrite_value_stack(FunctionalBuilder::type_size(term->element_type, term->location()));
          ValuePtr<> type_alignment = runner.rewrite_value_stack(FunctionalBuilder::type_alignment(term->element_type, term->location()));
          ValuePtr<> total_size = FunctionalBuilder::mul(count, type_size, term->location());
          stack_ptr = runner.builder().alloca_(FunctionalBuilder::byte_type(runner.context(), term->location()), total_size, type_alignment, term->location());
        }
        ValuePtr<> cast_stack_ptr = FunctionalBuilder::pointer_cast(stack_ptr, FunctionalBuilder::byte_type(runner.context(), term->location()), term->location());
        return LoweredValue(cast_stack_ptr, true);
      }
      
      static LoweredValue load_rewrite(FunctionRunner& runner, const ValuePtr<Load>& term) {
        ValuePtr<> ptr = runner.rewrite_value_stack(term->target);
        return runner.load_value(term, ptr, term->location());
      }
      
      static LoweredValue store_rewrite(FunctionRunner& runner, const ValuePtr<Store>& term) {
        ValuePtr<> ptr = runner.rewrite_value_stack(term->target);
        runner.store_value(term->value, ptr, term->location());
        return LoweredValue();
      }
      
      static LoweredValue memcpy_rewrite(FunctionRunner& runner, const ValuePtr<MemCpy>& term) {
        ValuePtr<> dest = runner.rewrite_value_stack(term->dest);
        ValuePtr<> src = runner.rewrite_value_stack(term->src);
        ValuePtr<> count = runner.rewrite_value_stack(term->count);
        ValuePtr<> alignment = runner.rewrite_value_stack(term->alignment);
        
        ValuePtr<> original_element_type = value_cast<PointerType>(term->dest->type())->target_type();
        LoweredType element_type = runner.rewrite_type(original_element_type);
        if (element_type.heap_type()) {
          ValuePtr<> dest_cast = FunctionalBuilder::pointer_cast(dest, element_type.heap_type(), term->location());
          ValuePtr<> src_cast = FunctionalBuilder::pointer_cast(dest, element_type.heap_type(), term->location());
          return LoweredValue(runner.builder().memcpy(dest_cast, src_cast, count, alignment, term->location()), true);
        } else {
          PSI_ASSERT(dest->type() == FunctionalBuilder::byte_pointer_type(runner.context(), term->location()));
          ValuePtr<> type_size = runner.rewrite_value_stack(FunctionalBuilder::type_size(original_element_type, term->location()));
          ValuePtr<> type_alignment = runner.rewrite_value_stack(FunctionalBuilder::type_alignment(original_element_type, term->location()));
          ValuePtr<> bytes = FunctionalBuilder::mul(count, type_size, term->location());
          ValuePtr<> max_alignment = FunctionalBuilder::max(alignment, type_alignment, term->location());
          return LoweredValue(runner.builder().memcpy(dest, src, bytes, max_alignment, term->location()), true);
        }
      }
      
      typedef TermOperationMap<Instruction, LoweredValue, FunctionRunner&> CallbackMap;
      static CallbackMap callback_map;
      
      static CallbackMap::Initializer callback_map_initializer() {
        return CallbackMap::initializer()
          .add<Return>(return_rewrite)
          .add<UnconditionalBranch>(br_rewrite)
          .add<ConditionalBranch>(cond_br_rewrite)
          .add<Call>(call_rewrite)
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
    ValuePtr<> AggregateLoweringPass::AggregateLoweringRewriter::rewrite_value_stack(const ValuePtr<>& value) {
      LoweredValue v = rewrite_value(value);
      PSI_ASSERT(v.on_stack() && v.value());
      return v.value();
    }
    
    /**
     * Utility function which runs rewrite_value and asserts that the resulting
     * value is not on the stack and is non-NULL.
     */
    ValuePtr<> AggregateLoweringPass::AggregateLoweringRewriter::rewrite_value_ptr(const ValuePtr<>& value) {
      LoweredValue v = rewrite_value(value);
      PSI_ASSERT(!v.on_stack() && v.value());
      return v.value();
    }
    
    /**
     * \brief Get a value which must already have been rewritten.
     */
    LoweredValue AggregateLoweringPass::AggregateLoweringRewriter::lookup_value(const ValuePtr<>& value) {
      ValueMapType::iterator it = m_value_map.find(value);
      PSI_ASSERT(it != m_value_map.end());
      return it->second;
    }

    /**
     * Utility function which runs lookup_value and asserts that the resulting
     * value is on the stack and is non-NULL.
     */
    ValuePtr<> AggregateLoweringPass::AggregateLoweringRewriter::lookup_value_stack(const ValuePtr<>& value) {
      LoweredValue v = lookup_value(value);
      PSI_ASSERT(v.on_stack() && v.value());
      return v.value();
    }
    
    /**
     * Utility function which runs lookup_value and asserts that the resulting
     * value is not on the stack and is non-NULL.
     */
    ValuePtr<> AggregateLoweringPass::AggregateLoweringRewriter::lookup_value_ptr(const ValuePtr<>& value) {
      LoweredValue v = lookup_value(value);
      PSI_ASSERT(!v.on_stack() && v.value());
      return v.value();
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
    void AggregateLoweringPass::FunctionRunner::add_mapping(const ValuePtr<>& source, const ValuePtr<>& target, bool on_stack) {
      PSI_ASSERT(&source->context() == &old_function()->context());
      PSI_ASSERT(&target->context() == &new_function()->context());
      m_value_map[source] = LoweredValue(target, on_stack);
    }
    
    /**
     * \brief Map a block from the old function to the new one.
     */
    ValuePtr<Block> AggregateLoweringPass::FunctionRunner::rewrite_block(const ValuePtr<Block>& block) {
      return value_cast<Block>(lookup_value_stack(block));
    }

    /**
     * Stores a value onto the stack. The type of \c value is used to determine where to place the
     * \c alloca instruction, so that the pointer will be available at all PHI nodes that it can possibly
     * reach as a value.
     * 
     * \copydoc AggregateLoweringPass::AggregateLoweringRewriter::store_value
     */
    ValuePtr<> AggregateLoweringPass::FunctionRunner::store_value(const ValuePtr<>& value, const SourceLocation& location) {
      ValuePtr<> ptr = create_storage(value->type(), location);
      store_value(value, ptr, location);
      return ptr;
    }
    
    ValuePtr<> AggregateLoweringPass::FunctionRunner::store_type(const ValuePtr<>& size, const ValuePtr<>& alignment, const SourceLocation& location) {
      ValuePtr<> byte_type = FunctionalBuilder::byte_type(context(), location);
      ValuePtr<> size_type = FunctionalBuilder::size_type(context(), location);
      
      LoweredType metatype = rewrite_type(FunctionalBuilder::type_type(pass().source_module()->context(), location));

      /*
       * Note that we should not need to change the insert point because this function is called by
       * the functional operation code generator, so the insert point should already be set to the
       * appropriate place for this op.
       * 
       * In cases involving PHI nodes however, I doubt this is true.
       */
      PSI_FAIL("This will fail when PHI nodes get involved");
      ValuePtr<> ptr;
      if (metatype.heap_type()) {
        ptr = builder().alloca_(metatype.heap_type(), location);
        ptr = FunctionalBuilder::pointer_cast(ptr, size_type, location);
      } else {
        ptr = builder().alloca_(size_type, 2, location);
      }
      
      builder().store(size, ptr, location);
      ValuePtr<> alignment_ptr = FunctionalBuilder::pointer_offset(ptr, FunctionalBuilder::size_value(context(), 1, location), location);
      builder().store(alignment, alignment_ptr, location);
      
      return FunctionalBuilder::pointer_cast(ptr, byte_type, location);
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
    ValuePtr<> AggregateLoweringPass::FunctionRunner::store_value(const ValuePtr<>& value, const ValuePtr<>& ptr, const SourceLocation& location) {
      LoweredType value_type = rewrite_type(value->type());
      if (value_type.stack_type()) {
        ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(ptr, value_type.stack_type(), location);
        ValuePtr<> stack_value = rewrite_value_stack(value);
        return builder().store(stack_value, cast_ptr, location);
      }

      if (ValuePtr<ArrayValue> array_val = dyn_cast<ArrayValue>(value)) {
        ValuePtr<> result;
        LoweredType element_type = rewrite_type(array_val->element_type());
        if (element_type.heap_type()) {
          ValuePtr<> base_ptr = FunctionalBuilder::pointer_cast(ptr, element_type.heap_type(), location);
          for (unsigned i = 0, e = array_val->length(); i != e; ++i) {
            ValuePtr<> element_ptr = FunctionalBuilder::pointer_offset(base_ptr, i, location);
            result = store_value(array_val->value(i), element_ptr, location);
          }
        } else {
          PSI_ASSERT(ptr->type() == FunctionalBuilder::byte_pointer_type(context(), location));
          ValuePtr<> element_size = rewrite_value_stack(FunctionalBuilder::type_size(array_val->element_type(), location));
          ValuePtr<> element_ptr = ptr;
          for (unsigned i = 0, e = array_val->length(); i != e; ++i) {
            result = store_value(array_val->value(i), element_ptr, location);
            element_ptr = FunctionalBuilder::pointer_offset(element_ptr, element_size, location);
          }
        }
        return result;
      } else if (ValuePtr<UnionValue> union_val = dyn_cast<UnionValue>(value)) {
        return store_value(union_val->value(), ptr, location);
      }
      
      if (ValuePtr<StructType> struct_ty = dyn_cast<StructType>(value->type())) {
        PSI_ASSERT(ptr->type() == FunctionalBuilder::byte_pointer_type(context(), location));
        ValuePtr<> result;
        for (unsigned i = 0, e = struct_ty->n_members(); i != e; ++i) {
          ValuePtr<> offset = rewrite_value_stack(FunctionalBuilder::struct_element_offset(struct_ty, i, location));
          ValuePtr<> member_ptr = FunctionalBuilder::pointer_offset(ptr, offset, location);
          result = store_value(FunctionalBuilder::element_value(value, i, location), member_ptr, location);
        }
        return result;
      }

      if (ValuePtr<ArrayType> array_ty = dyn_cast<ArrayType>(value->type())) {
        LoweredType element_type = rewrite_type(array_ty->element_type());
        if (element_type.heap_type()) {
          ValuePtr<> value_ptr = rewrite_value_ptr(value);
          ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(ptr, element_type.heap_type(), location);
          return builder().memcpy(cast_ptr, value_ptr, array_ty->length(), location);
        }
      }

      PSI_ASSERT(ptr->type() == FunctionalBuilder::byte_pointer_type(context(), location));
      ValuePtr<> value_ptr = rewrite_value_ptr(value);
      PSI_ASSERT(value_ptr->type() == FunctionalBuilder::byte_pointer_type(context(), location));
      ValuePtr<> value_size = rewrite_value_stack(FunctionalBuilder::type_size(value->type(), location));
      ValuePtr<> value_alignment = rewrite_value_stack(FunctionalBuilder::type_alignment(value->type(), location));
      return builder().memcpy(ptr, value_ptr, value_size, value_alignment, location);
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
      ValuePtr<Block> prolog_block = new_function()->blocks().front();
      if (!prolog_block)
        return; // This is an external function

      std::vector<std::pair<ValuePtr<Block>, ValuePtr<Block> > > sorted_blocks;
      
      // Set up block mapping for all blocks except the entry block,
      // which has already been handled
      for (Function::BlockList::const_iterator ii = old_function()->blocks().begin(), ie = old_function()->blocks().end(); ii != ie; ++ii) {
        ValuePtr<Block> dominator = (*ii)->dominator() ? rewrite_block((*ii)->dominator()) : prolog_block;
        ValuePtr<Block> new_block = new_function()->new_block((*ii)->location(), dominator);
        sorted_blocks.push_back(std::make_pair(*ii, new_block));
        m_value_map.insert(std::make_pair(*ii, LoweredValue(new_block, true)));
      }
      
      // Jump from prolog block to entry block
      InstructionBuilder(prolog_block).br(rewrite_block(old_function()->blocks().front()), prolog_block->location());
      
      // Generate PHI nodes and convert instructions!
      for (std::vector<std::pair<ValuePtr<Block>, ValuePtr<Block> > >::iterator ii = sorted_blocks.begin(), ie = sorted_blocks.end(); ii != ie; ++ii) {
        const ValuePtr<Block>& old_block = ii->first;
        const ValuePtr<Block>& new_block = ii->second;

        // Generate PHI nodes
        for (Block::PhiList::const_iterator ji = old_block->phi_nodes().begin(), je = old_block->phi_nodes().end(); ji != je; ++ji)
          create_phi_node(new_block, *ji);

        // Create instructions
        m_builder.set_insert_point(new_block);
        for (Block::InstructionList::const_iterator ji = old_block->instructions().begin(), je = old_block->instructions().end(); ji != je; ++ji) {
          const ValuePtr<Instruction>& insn = *ji;
          LoweredValue value = InstructionTermRewriter::callback_map.call(*this, insn);
          if (value.value())
            m_value_map[insn] = value;
        }
      }
      
      // Populate preexisting PHI nodes with values
      for (std::vector<std::pair<ValuePtr<Block>, ValuePtr<Block> > >::iterator ii = sorted_blocks.begin(), ie = sorted_blocks.end(); ii != ie; ++ii) {
        ValuePtr<Block> old_block = ii->first;
        for (Block::PhiList::const_iterator ji = old_block->phi_nodes().begin(), je = old_block->phi_nodes().end(); ji != je; ++ji) {
          const ValuePtr<Phi>& phi_node = *ji;

          std::vector<PhiEdge> edges;
          for (unsigned ki = 0, ke = phi_node->edges().size(); ki != ke; ++ki) {
            const PhiEdge& e = phi_node->edges()[ki];
            PhiEdge ne;
            ne.block = rewrite_block(e.block);
            ne.value = e.value;
            edges.push_back(ne);
          }
          
          populate_phi_node(phi_node, edges);
        }
      }
      
      create_phi_alloca_terms(sorted_blocks);
    }
    
    /**
     * Create suitable alloca'd storage for the given type.
     */
    ValuePtr<> AggregateLoweringPass::FunctionRunner::create_alloca(const ValuePtr<>& type, const SourceLocation& location) {
      ValuePtr<> byte_type = FunctionalBuilder::byte_type(context(), location);
      
      LoweredType new_type = rewrite_type(type);
      if (new_type.heap_type()) {
        ValuePtr<> alloca_insn = builder().alloca_(new_type.heap_type(), location);
        return FunctionalBuilder::pointer_cast(alloca_insn, byte_type, location);
      }
      
      if (ValuePtr<ArrayType> array_ty = dyn_cast<ArrayType>(type)) {
        LoweredType element_type = rewrite_type(array_ty->element_type());
        if (element_type.heap_type()) {
          ValuePtr<> alloca_insn = builder().alloca_(element_type.heap_type(), array_ty->length(), location);
          return FunctionalBuilder::pointer_cast(alloca_insn, byte_type, location);
        }
      }
      
      return builder().alloca_(byte_type, new_type.size(), new_type.alignment(), location);
    }

    /**
     * Create storage for an unknown type.
     * 
     * \param type Type to create storage for.
     */
    ValuePtr<> AggregateLoweringPass::FunctionRunner::create_storage(const ValuePtr<>& type, const SourceLocation& location) {
      if (Value *source = type->source()) {
        ValuePtr<Block> block;
        switch(source->term_type()) {
        case term_instruction: block = rewrite_block(value_cast<Instruction>(source)->block()); break;
        case term_phi: block = rewrite_block(value_cast<Phi>(source)->block()); break;
        default: break;
        }

        if (block == builder().insert_point().block())
          return create_alloca(type, location);
      }
      
      ValuePtr<Block> block = builder().insert_point().block();
      ValuePtr<Phi> phi = block->insert_phi(FunctionalBuilder::byte_pointer_type(context(), location), location);
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
    LoweredValue AggregateLoweringPass::FunctionRunner::load_value(const ValuePtr<>& load_term, const ValuePtr<>& ptr, const SourceLocation& location) {
      LoweredType load_type = rewrite_type(load_term->type());
      if (load_type.stack_type()) {
        ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(ptr, load_type.stack_type(), location);
        ValuePtr<> load_insn = builder().load(cast_ptr, location);
        return (m_value_map[load_term] = LoweredValue(load_insn, true));
      }
      
      if (ValuePtr<StructType> struct_ty = dyn_cast<StructType>(load_term->type())) {
        for (unsigned i = 0, e = struct_ty->n_members(); i != e; ++i) {
          load_value(FunctionalBuilder::element_value(load_term, i, location),
                     FunctionalTermRewriter::struct_ptr_offset(*this, struct_ty, ptr, i, location), location);
        }
        // struct loads have no value because they should not be accessed directly
        return LoweredValue();
      } else if (isa<Metatype>(load_term->type())) {
        ValuePtr<> size_type = FunctionalBuilder::size_type(load_term->context(), location);
        ValuePtr<StructType> metatype_ty = value_cast<StructType>(FunctionalBuilder::struct_type(load_term->context(), std::vector<ValuePtr<> >(2, size_type), location));
        load_value(FunctionalBuilder::type_size(load_term, location), FunctionalTermRewriter::struct_ptr_offset(*this, metatype_ty, ptr, 0, location), location);
        load_value(FunctionalBuilder::type_alignment(load_term, location), FunctionalTermRewriter::struct_ptr_offset(*this, metatype_ty, ptr, 1, location), location);
        return LoweredValue();
      }
      
      // So this type cannot be loaded: memcpy it to the stack
      ValuePtr<> target_ptr = create_storage(load_term->type(), location);
      LoweredValue result(target_ptr, false);
      m_value_map[load_term] = result;

      if (load_type.heap_type()) {
        ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(ptr, load_type.heap_type(), location);
        ValuePtr<> cast_target_ptr = FunctionalBuilder::pointer_cast(target_ptr, load_type.heap_type(), location);
        builder().memcpy(cast_target_ptr, cast_ptr, 1, location);
        return result;
      }

      if (ValuePtr<ArrayType> array_ty = dyn_cast<ArrayType>(load_term->type())) {
        LoweredType element_ty = rewrite_type(array_ty->element_type());
        if (element_ty.heap_type()) {
          ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(ptr, element_ty.heap_type(), location);
          ValuePtr<> cast_target_ptr = FunctionalBuilder::pointer_cast(target_ptr, element_ty.heap_type(), location);
          ValuePtr<> length = rewrite_value_stack(array_ty->length());
          builder().memcpy(cast_target_ptr, cast_ptr, length, location);
          return result;
        }
      }

      builder().memcpy(target_ptr, ptr, load_type.size(), load_type.alignment(), location);
      return result;
    }
    
    LoweredType AggregateLoweringPass::FunctionRunner::rewrite_type(const ValuePtr<>& type) {
      // Forward to parent if applicable.
      if (!type->source() || isa<Global>(type->source()))
        return pass().global_rewriter().rewrite_type(type);
      
      TypeMapType::iterator type_it = m_type_map.find(type);
      if (type_it != m_type_map.end())
        return type_it->second;
      
      LoweredType result;
      if (ValuePtr<FunctionalValue> func_type = dyn_cast<FunctionalValue>(type)) {
        result = TypeTermRewriter::callback_map.call(*this, func_type);
      } else {
        result = TypeTermRewriter::parameter_type_rewrite(*this, type);
      }
      
      PSI_ASSERT(result.valid());
      m_type_map[type] = result;
      return result;
    }
    
    LoweredValue AggregateLoweringPass::FunctionRunner::rewrite_value(const ValuePtr<>& value_orig) {
      ValuePtr<> value = unrecurse(value_orig);

      // Forward to parent if applicable.
      if (!value->source() || isa<Global>(value->source()))
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
      PSI_ASSERT(isa<FunctionalValue>(value));
      
      ValuePtr<Block> insert_block;
      switch (value->source()->term_type()) {
      case term_instruction: insert_block = value_cast<Instruction>(value->source())->block(); break;
      case term_phi: insert_block = value_cast<Phi>(value->source())->block(); break;
      case term_block: insert_block.reset(value_cast<Block>(value->source())); break;
      case term_function_parameter: insert_block = value_cast<FunctionParameter>(value->source())->function()->blocks().front(); break;
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
      LoweredValue result = FunctionalTermRewriter::callback_map.call(*this, value_cast<FunctionalValue>(value));
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
    void AggregateLoweringPass::FunctionRunner::create_phi_node(const ValuePtr<Block>& block, const ValuePtr<>& phi_term) {
      LoweredType type = rewrite_type(phi_term->type());
      if (type.stack_type()) {
        ValuePtr<Phi> new_phi = block->insert_phi(type.stack_type(), phi_term->location());
        m_value_map[phi_term] = LoweredValue(new_phi, true);
        return;
      }
      
      if (ValuePtr<StructType> struct_ty = dyn_cast<StructType>(phi_term->type())) {
        for (unsigned i = 0, e = struct_ty->n_members(); i != e; ++i)
          create_phi_node(block, FunctionalBuilder::element_value(phi_term, i, phi_term->location()));
      } else if (isa<Metatype>(phi_term->type())) {
        create_phi_node(block, FunctionalBuilder::type_size(phi_term, phi_term->location()));
        create_phi_node(block, FunctionalBuilder::type_alignment(phi_term, phi_term->location()));
      } else {
        ValuePtr<Phi> new_phi = block->insert_phi(FunctionalBuilder::byte_pointer_type(context(), phi_term->location()), phi_term->location());
        m_value_map[phi_term] = LoweredValue(new_phi, false);
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
    void AggregateLoweringPass::FunctionRunner::populate_phi_node(const ValuePtr<>& phi_term, const std::vector<PhiEdge>& incoming_edges) {
      LoweredType type = rewrite_type(phi_term->type());
      if (type.stack_type()) {
        ValuePtr<Phi> new_phi = value_cast<Phi>(lookup_value_stack(phi_term));
        for (unsigned ii = 0, ie = incoming_edges.size(); ii != ie; ++ii)
          new_phi->add_edge(incoming_edges[ii].block, rewrite_value_stack(incoming_edges[ii].value));
        return;
      }

      if (ValuePtr<StructType> struct_ty = dyn_cast<StructType>(phi_term->type())) {
        std::vector<PhiEdge> child_incoming_edges(incoming_edges.size());
        for (unsigned ii = 0, ie = struct_ty->n_members(); ii != ie; ++ii) {
          for (unsigned ji = 0, je = incoming_edges.size(); ji != je; ++ji) {
            PhiEdge& child_edge = child_incoming_edges[ji];
            child_edge.block = incoming_edges[ji].block;
            child_edge.value = FunctionalBuilder::element_value(incoming_edges[ji].value, ii, incoming_edges[ji].value->location());
          }
          populate_phi_node(FunctionalBuilder::element_value(phi_term, ii, phi_term->location()), child_incoming_edges);
        }
      } else if (isa<Metatype>(phi_term->type())) {
        std::vector<PhiEdge> child_incoming_edges(incoming_edges.size());
        for (unsigned ji = 0, je = incoming_edges.size(); ji != je; ++ji)
          child_incoming_edges[ji].block = incoming_edges[ji].block;
        
        for (unsigned ji = 0, je = incoming_edges.size(); ji != je; ++ji)
          child_incoming_edges[ji].value = FunctionalBuilder::type_size(incoming_edges[ji].value, incoming_edges[ji].value->location());
        populate_phi_node(FunctionalBuilder::type_size(phi_term, phi_term->location()), child_incoming_edges);
          
        for (unsigned ji = 0, je = incoming_edges.size(); ji != je; ++ji)
          child_incoming_edges[ji].value = FunctionalBuilder::type_alignment(incoming_edges[ji].value, incoming_edges[ji].value->location());
        populate_phi_node(FunctionalBuilder::type_alignment(phi_term, phi_term->location()), child_incoming_edges);
      } else {
        ValuePtr<Phi> new_phi = value_cast<Phi>(lookup_value_ptr(phi_term));
        for (unsigned ii = 0, ie = incoming_edges.size(); ii != ie; ++ii)
          new_phi->add_edge(incoming_edges[ii].block, rewrite_value_ptr(incoming_edges[ii].value));
      }
    }

    /**
     * \c alloca terms created by rewriting aggregates onto the heap are created as
     * PHI terms since loops can the memory used to have to be different on later
     * passes through a block.
     */
    void AggregateLoweringPass::FunctionRunner::create_phi_alloca_terms(const std::vector<std::pair<ValuePtr<Block>, ValuePtr<Block> > >& sorted_blocks) {
      ValuePtr<> byte_type = FunctionalBuilder::byte_type(context(), new_function()->location());
      ValuePtr<> byte_pointer_type = FunctionalBuilder::byte_pointer_type(context(), new_function()->location());

      // Build a map from a block to blocks it dominates
      typedef std::multimap<ValuePtr<Block>, ValuePtr<Block> > DominatorMapType;
      DominatorMapType dominator_map;
      for (std::vector<std::pair<ValuePtr<Block>, ValuePtr<Block> > >::const_iterator ji = sorted_blocks.begin(), je = sorted_blocks.end(); ji != je; ++ji)
        dominator_map.insert(std::make_pair(ji->second->dominator(), ji->second));
      
      for (TypePhiMapType::iterator ii = m_generated_phi_terms.begin(), ie = m_generated_phi_terms.end(); ii != ie; ++ii) {
        // Find block to create alloca's for current type
        ValuePtr<Block> type_block = new_function()->blocks().front();
        if (Value *source = ii->first->source()) {
          switch (source->term_type()) {
          case term_instruction: type_block = rewrite_block(value_cast<Instruction>(source)->block()); break;
          case term_phi: type_block = rewrite_block(value_cast<Phi>(source)->block()); break;
          default: break;
          }
        }

        // Find blocks dominated by the block the type is created in, recursively
        std::vector<ValuePtr<Block> > dominated_blocks(1, type_block);
        for (unsigned count = 0; count != dominated_blocks.size(); ++count) {
          for (std::pair<DominatorMapType::iterator, DominatorMapType::iterator> pair = dominator_map.equal_range(dominated_blocks[count]);
               pair.first != pair.second; ++pair.first)
            dominated_blocks.push_back(pair.first->second);
        }
        
        // The total number of slots required
        unsigned total_vars = 0;
        
        // Holds the number of variables in scope in the specified block
        std::map<ValuePtr<Block>, unsigned> active_vars;
        active_vars[type_block] = 0;
        for (std::vector<ValuePtr<Block> >::iterator ji = boost::next(dominated_blocks.begin()), je = dominated_blocks.end(); ji != je; ++ji) {
          ValuePtr<Block> block = *ji;
          BlockPhiData& data = ii->second[block];
          unsigned block_vars = active_vars[block->dominator()] + data.user.size() + data.alloca_.size();
          active_vars[block] = block_vars;
          total_vars = std::max(block_vars, total_vars);
        }
        
        // Create PHI nodes in each block to track the used/free list
        for (std::vector<ValuePtr<Block> >::iterator ji = boost::next(dominated_blocks.begin()), je = dominated_blocks.end(); ji != je; ++ji) {
          ValuePtr<Block> block = *ji;
          BlockPhiData& data = ii->second[*ji];
          unsigned used_vars = active_vars[block];
          unsigned free_vars = total_vars - used_vars;
          for (unsigned ki = 0, ke = data.user.size(); ki != ke; ++ki)
            data.used.push_back(block->insert_phi(byte_pointer_type, block->location()));
          for (unsigned k = 0; k != free_vars; ++k)
            data.free_.push_back(block->insert_phi(byte_pointer_type, block->location()));
        }
        
        // Create memory slots
        m_builder.set_insert_point(type_block->instructions().back());
        
        BlockPhiData& entry_data = ii->second[type_block];
        entry_data.free_.resize(total_vars);
        LoweredType new_type = rewrite_type(ii->first);
        if (new_type.heap_type()) {
          for (unsigned ji = 0, je = entry_data.free_.size(); ji != je; ++ji)
            entry_data.free_[ji] = FunctionalBuilder::pointer_cast(builder().alloca_(new_type.heap_type(), type_block->location()), byte_type, type_block->location());
          goto slots_created;
        }
        
        if (ValuePtr<ArrayType> array_ty = dyn_cast<ArrayType>(ii->first)) {
          LoweredType element_type = rewrite_type(array_ty->element_type());
          if (element_type.heap_type()) {
            for (unsigned ji = 0, je = entry_data.free_.size(); ji != je; ++ji)
              entry_data.free_[ji] = FunctionalBuilder::pointer_cast(builder().alloca_(element_type.heap_type(), array_ty->length(), type_block->location()), byte_type, type_block->location());
            goto slots_created;
          }
        }
        
        // Default mechanism suitable for any type
        for (unsigned ji = 0, je = entry_data.free_.size(); ji != je; ++ji)
          entry_data.free_[ji] = builder().alloca_(byte_type, new_type.size(), new_type.alignment(), type_block->location());
        
      slots_created:
        for (std::vector<ValuePtr<Block> >::iterator ji = dominated_blocks.begin(), je = dominated_blocks.end(); ji != je; ++ji) {
          ValuePtr<Block> source_block = *ji;
          BlockPhiData& source_data = ii->second[source_block];
          
          const std::vector<ValuePtr<Block> >& successors = source_block->successors();
          for (std::vector<ValuePtr<Block> >::const_iterator ki = successors.begin(), ke = successors.end(); ki != ke; ++ki) {
            ValuePtr<Block> target_block = *ki;
            ValuePtr<Block> common_dominator = Block::common_dominator(source_block, target_block->dominator());
            
            if (!common_dominator->dominated_by(type_block))
              continue;
            
            BlockPhiData& target_data = ii->second[target_block];
            
            // Find all slots newly used between common_dominator and source_block
            std::set<ValuePtr<> > free_slots_set;
            for (ValuePtr<Block> parent = source_block; parent != common_dominator; parent = parent->dominator()) {
              BlockPhiData& parent_data = ii->second[parent];
              free_slots_set.insert(parent_data.alloca_.begin(), parent_data.alloca_.end());
              free_slots_set.insert(parent_data.used.begin(), parent_data.used.end());
            }
            
            std::set<ValuePtr<> > used_slots_set;
            for (std::vector<ValuePtr<Phi> >::iterator li = target_data.user.begin(), le = target_data.user.end(); li != le; ++li) {
              ValuePtr<> incoming = (*li)->incoming_value_from(source_block);
              // Filter out values which are user-specified
              if (free_slots_set.erase(incoming))
                used_slots_set.insert(incoming);
            }
              
            std::vector<ValuePtr<> > used_slots(used_slots_set.begin(), used_slots_set.end());
            std::vector<ValuePtr<> > free_slots = source_data.free_;
            free_slots.insert(free_slots.end(), free_slots_set.begin(), free_slots_set.end());
            
            PSI_ASSERT(free_slots.size() >= target_data.free_.size() + target_data.alloca_.size());
            PSI_ASSERT(free_slots.size() + used_slots.size() == target_data.used.size() + target_data.free_.size() + target_data.alloca_.size());
            
            unsigned free_to_used_transfer = target_data.used.size() - used_slots.size();
            used_slots.insert(used_slots.end(), free_slots.end() - free_to_used_transfer, free_slots.end());
            free_slots.erase(free_slots.end() - free_to_used_transfer, free_slots.end());
            
            PSI_ASSERT(used_slots.size() == target_data.used.size());
            PSI_ASSERT(free_slots.size() == target_data.free_.size() + target_data.alloca_.size());
            
            for (unsigned li = 0, le = used_slots.size(); li != le; ++li)
              value_cast<Phi>(target_data.used[li])->add_edge(source_block, used_slots[li]);
            for (unsigned li = 0, le = target_data.alloca_.size(); li != le; ++li)
              target_data.alloca_[li]->add_edge(source_block, free_slots[li]);
            for (unsigned li = 0, le = target_data.free_.size(), offset = target_data.alloca_.size(); li != le; ++li)
              value_cast<Phi>(target_data.free_[li])->add_edge(source_block, free_slots[li+offset]);
          }
        }
      }
    }
    
    AggregateLoweringPass::ModuleLevelRewriter::ModuleLevelRewriter(AggregateLoweringPass *pass)
    : AggregateLoweringRewriter(pass) {
    }
    
    LoweredValue AggregateLoweringPass::ModuleLevelRewriter::load_value(const ValuePtr<>& load_term, const ValuePtr<>& ptr, const SourceLocation& location) {
      ValuePtr<> origin = ptr;
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
        if (ValuePtr<PointerOffset> ptr_offset = dyn_cast<PointerOffset>(origin)) {
          origin = ptr_offset->pointer();
          //offset += pass().target_callback->type_size_alignment() * rewrite_value_integer();
        } else if (ValuePtr<PointerCast> ptr_cast = dyn_cast<PointerCast>(origin)) {
          origin = ptr_cast->pointer();
        } else if (ValuePtr<ElementPtr> element_ptr = dyn_cast<ElementPtr>(origin)) {
          origin = element_ptr->aggregate_ptr();
        } else if (ValuePtr<OuterPtr> outer_ptr = dyn_cast<OuterPtr>(origin)) {
          origin = outer_ptr->pointer();
        } else if (isa<GlobalVariable>(origin)) {
          break;
        } else {
          PSI_FAIL("unexpected term type in global pointer expression");
        }
      }
      
      PSI_NOT_IMPLEMENTED();
    }
    
    ValuePtr<> AggregateLoweringPass::ModuleLevelRewriter::store_value(const ValuePtr<>& value, const SourceLocation& location) {
      PSI_NOT_IMPLEMENTED();
    }
    
    ValuePtr<> AggregateLoweringPass::ModuleLevelRewriter::store_type(const ValuePtr<>& size, const ValuePtr<>& alignment, const SourceLocation& location) {
      PSI_NOT_IMPLEMENTED();
    }
    
    LoweredType AggregateLoweringPass::ModuleLevelRewriter::rewrite_type(const ValuePtr<>& type) {
      TypeMapType::iterator type_it = m_type_map.find(type);
      if (type_it != m_type_map.end())
        return type_it->second;
      
      if (ValuePtr<Exists> exists = dyn_cast<Exists>(type)) {
        if (!isa<PointerType>(exists->result()))
          throw TvmUserError("Value of type exists is not a pointer");
        return rewrite_type(FunctionalBuilder::byte_pointer_type(context(), exists->location()));
      }
      
      LoweredType result = TypeTermRewriter::callback_map.call(*this, value_cast<FunctionalValue>(type));
      PSI_ASSERT(result.valid());
      m_type_map[type] = result;
      return result;
    }
    
    LoweredValue AggregateLoweringPass::ModuleLevelRewriter::rewrite_value(const ValuePtr<>& value) {
      ValueMapType::iterator value_it = m_value_map.find(value);
      if (value_it != m_value_map.end())
        return value_it->second;
      
      LoweredValue result = FunctionalTermRewriter::callback_map.call(*this, value_cast<FunctionalValue>(value));
      PSI_ASSERT(result.value());
      m_value_map[value] = result;
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
      std::pair<ValuePtr<>,ValuePtr<> > padding_type = target_callback->type_from_alignment(alignment);
      ValuePtr<> count = FunctionalBuilder::div(FunctionalBuilder::sub(size, status.size, location), padding_type.second, location);
      if (ValuePtr<IntegerValue> count_value = dyn_cast<IntegerValue>(count)) {
        boost::optional<unsigned> count_value_int = count_value->value().unsigned_value();
        if (!count_value_int)
          throw TvmInternalError("cannot create internal global variable padding due to size overflow");
        ValuePtr<> padding_term = is_value ? FunctionalBuilder::undef(padding_type.first, location) : padding_type.first;
        status.elements.insert(status.elements.end(), *count_value_int, padding_term);
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
      if (value_ty.stack_type())
        return GlobalBuildStatus(value_ty.heap_type(), value_ty.size(), value_ty.alignment(), value_ty.size(), value_ty.alignment());

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
      if (value_ty.stack_type()) {
        ValuePtr<> rewritten_value = m_global_rewriter.rewrite_value_stack(value);
        return GlobalBuildStatus(rewritten_value, value_ty.size(), value_ty.alignment(), value_ty.size(), value_ty.alignment());
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
          
          if (old_var->alignment())
            new_var->set_alignment(FunctionalBuilder::max(status.alignment, m_global_rewriter.rewrite_value_stack(old_var->alignment()), term->location()));
          else
            new_var->set_alignment(status.alignment);

          ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(new_var, byte_type, term->location());
          global_rewriter().m_value_map[old_var] = LoweredValue(cast_ptr, true);
          rewrite_globals.push_back(std::make_pair(old_var, new_var));
        } else {
          ValuePtr<Function> old_function = value_cast<Function>(term);
          boost::shared_ptr<FunctionRunner> runner(new FunctionRunner(this, old_function));
          ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(runner->new_function(), byte_type, term->location());
          global_rewriter().m_value_map[old_function] = LoweredValue(cast_ptr, true);
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
