#include "AggregateLowering.hpp"
#include "FunctionalBuilder.hpp"
#include "TermOperationMap.hpp"
#include "Recursive.hpp"

namespace Psi {
  namespace Tvm {
    struct AggregateLoweringPass::TypeTermRewriter {
      static LoweredType array_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ArrayType>& term) {
        LoweredValueSimple length = rewriter.rewrite_value_register(term->length());
        LoweredType element_type = rewriter.rewrite_type(term->element_type());
        ValuePtr<> size = FunctionalBuilder::mul(length.value, element_type.size(), term->location());
        ValuePtr<> alignment = element_type.alignment();
        
        if (element_type.global() && isa<IntegerValue>(length.value)) {
          PSI_ASSERT(length.global);
          
          if (!rewriter.pass().split_arrays && (element_type.mode() == LoweredType::mode_register)) {
            ValuePtr<> register_type = FunctionalBuilder::array_type(element_type.register_type(), length.value, term->location());
            return LoweredType::register_(term, size, alignment, register_type);
          } else {
            LoweredType::EntryVector entries(size_to_unsigned(length.value), element_type);
            return LoweredType::split(term, size, alignment, entries);
          }
        } else {
          return LoweredType::blob(term, size, alignment);
        }
      }

      static LoweredType struct_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<StructType>& term) {
        ValuePtr<> size = FunctionalBuilder::size_value(rewriter.context(), 0, term->location());
        ValuePtr<> alignment = FunctionalBuilder::size_value(rewriter.context(), 1, term->location());
        
        std::vector<ValuePtr<> > register_members;
        LoweredType::EntryVector entries;
        bool all_register = true, global = true;
        for (unsigned ii = 0, ie = term->n_members(); ii != ie; ++ii) {
          LoweredType member_type = rewriter.rewrite_type(term->member_type(ii));
          entries.push_back(member_type);
          global = global && member_type.global();

          if (member_type.mode() == LoweredType::mode_register) {
            if (all_register)
              register_members.push_back(member_type.register_type());
          } else {
            all_register = false;
            register_members.clear();
          }
          
          ValuePtr<> aligned_size = FunctionalBuilder::align_to(size, member_type.alignment(), term->location());
          size = FunctionalBuilder::add(aligned_size, member_type.size(), term->location());
          alignment = FunctionalBuilder::max(alignment, member_type.alignment(), term->location());
        }
        
        if (!rewriter.pass().split_structs && all_register) {
          ValuePtr<> register_type = FunctionalBuilder::struct_type(rewriter.context(), register_members, term->location());
          return LoweredType::register_(term, size, alignment, register_type);
        } else if (global) {
          return LoweredType::split(term, size, alignment, entries);
        } else {
          return LoweredType::blob(term, size, alignment);
        }
      }

      static LoweredType union_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<UnionType>& term) {
        ValuePtr<> size = FunctionalBuilder::size_value(rewriter.context(), 0, term->location());
        ValuePtr<> alignment = FunctionalBuilder::size_value(rewriter.context(), 1, term->location());
        
        std::vector<ValuePtr<> > register_members;
        bool all_register = true;
        for (unsigned ii = 0, ie = term->n_members(); ii != ie; ++ii) {
          LoweredType member_type = rewriter.rewrite_type(term->member_type(ii));

          if (member_type.mode() == LoweredType::mode_register) {
            if (all_register)
              register_members.push_back(member_type.register_type());
          } else {
            all_register = false;
            register_members.clear();
          }
          
          size = FunctionalBuilder::max(size, member_type.size(), term->location());
          alignment = FunctionalBuilder::max(alignment, member_type.alignment(), term->location());
        }
        
        if (all_register && !rewriter.pass().remove_unions) {
          ValuePtr<> register_type = FunctionalBuilder::union_type(rewriter.context(), register_members, term->location());
          return LoweredType::register_(term, size, alignment, register_type);
        } else if (isa<IntegerValue>(size) && isa<IntegerValue>(alignment)) {
          std::pair<ValuePtr<>, std::size_t> elem_type =
            rewriter.pass().target_callback->type_from_size(rewriter.context(), value_cast<IntegerValue>(alignment)->value().unsigned_value_checked(), term->location());
          std::size_t count = value_cast<IntegerValue>(size)->value().unsigned_value_checked() / elem_type.second;
          PSI_ASSERT(count > 0);
          if (count == 1) {
            return LoweredType::register_(term, size, alignment, elem_type.first);
          } else if (rewriter.pass().split_arrays) {
            ValuePtr<> elem_size = FunctionalBuilder::size_value(rewriter.context(), elem_type.second, term->location());
            std::vector<LoweredType> elements(count, LoweredType::register_(ValuePtr<>(), elem_size, elem_size, elem_type.first));
            return LoweredType::split(term, size, alignment, elements);
          } else {
            ValuePtr<> array_type = FunctionalBuilder::array_type(elem_type.first, count, term->location());
            return LoweredType::register_(term, size, alignment, array_type);
          }
        } else {
          return LoweredType::blob(term, size, alignment);
        }
      }
      
      static LoweredType apply_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ApplyType>& term) {
        LoweredType inner_type = rewriter.rewrite_type(term->unpack());
        
        if (!rewriter.pass().split_structs && (inner_type.mode() == LoweredType::mode_register)) {
          return LoweredType::register_(term, inner_type.size(), inner_type.alignment(), inner_type.register_type());
        } else if (inner_type.global()) {
          return LoweredType::split(term, inner_type.size(), inner_type.alignment(), LoweredType::EntryVector(1, inner_type));
        } else {
          return LoweredType::blob(term, inner_type.size(), inner_type.alignment());
        }
      }
      
      static LoweredType simple_type_helper(AggregateLoweringRewriter& rewriter, const ValuePtr<>& origin, const ValuePtr<>& rewritten_type, const SourceLocation& location) {
        ValuePtr<> size, alignment;
        TypeSizeAlignment size_align = rewriter.pass().target_callback->type_size_alignment(rewritten_type);
        size = FunctionalBuilder::size_value(rewriter.context(), size_align.size, location);
        alignment = FunctionalBuilder::size_value(rewriter.context(), size_align.alignment, location);
        return LoweredType::register_(origin, size, alignment, rewritten_type);
      }
      
      static LoweredType pointer_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<PointerType>& term) {
        return simple_type_helper(rewriter, term, FunctionalBuilder::byte_pointer_type(rewriter.context(), term->location()), term->location());
      }
      
      static LoweredType primitive_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionalValue>& type) {
        PSI_ASSERT(type->is_type());
        
        class TrivialRewriteCallback : public RewriteCallback {
        public:
          TrivialRewriteCallback(Context& context) : RewriteCallback(context) {}
          virtual ValuePtr<> rewrite(const ValuePtr<>&) {
            PSI_FAIL("Primitive type should not require internal rewriting.");
          }
        } callback(rewriter.context());
        
        return simple_type_helper(rewriter, type, type->rewrite(callback), type->location());
      }
      
      static LoweredType metatype_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<Metatype>& term) {
        ValuePtr<> size = FunctionalBuilder::size_type(term->context(), term->location());
        std::vector<ValuePtr<> > members(2, size);
        ValuePtr<> metatype_struct = FunctionalBuilder::struct_type(term->context(), members, term->location());
        return rewriter.rewrite_type(metatype_struct);
      }
      
      static LoweredType unknown_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<MetatypeValue>& term) {
        LoweredValueSimple size = rewriter.rewrite_value_register(term->size());
        LoweredValueSimple alignment = rewriter.rewrite_value_register(term->alignment());
        return LoweredType::blob(term, size.value, alignment.value);
      }
      
      static LoweredType constant_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ConstantType>& term) {
        return rewriter.rewrite_type(term->value()->type()).with_origin(term);
      }
      
      static LoweredType upref_type_rewrite(AggregateLoweringRewriter&, const ValuePtr<UpwardReferenceType>&) {
        throw TvmUserError("Upward reference types should not be encountered during lowering");
      }
      
      static LoweredType parameter_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<>& type) {
        ValuePtr<> size, alignment;
        LoweredValue rewritten = rewriter.rewrite_value(type);
        if (rewritten.mode() == LoweredValue::mode_register) {
          size = FunctionalBuilder::element_value(rewritten.register_value(), 0, type->location());
          alignment = FunctionalBuilder::element_value(rewritten.register_value(), 1, type->location());
        } else {
          PSI_ASSERT(rewritten.mode() == LoweredValue::mode_split);
          size = rewritten.split_entries()[0].register_value();
          alignment = rewritten.split_entries()[1].register_value();
        }
        return LoweredType::blob(type, size, alignment);
      }
      
      typedef TermOperationMap<HashableValue, LoweredType, AggregateLoweringRewriter&> CallbackMap;
      static CallbackMap callback_map;
      
      static CallbackMap::Initializer callback_map_initializer() {
        return CallbackMap::initializer(parameter_type_rewrite)
          .add<ArrayType>(array_type_rewrite)
          .add<StructType>(struct_type_rewrite)
          .add<UnionType>(union_type_rewrite)
          .add<ApplyType>(apply_type_rewrite)
          .add<Metatype>(metatype_rewrite)
          .add<MetatypeValue>(unknown_type_rewrite)
          .add<PointerType>(pointer_type_rewrite)
          .add<BlockType>(primitive_type_rewrite)
          .add<BooleanType>(primitive_type_rewrite)
          .add<ByteType>(primitive_type_rewrite)
          .add<EmptyType>(primitive_type_rewrite)
          .add<FloatType>(primitive_type_rewrite)
          .add<IntegerType>(primitive_type_rewrite)
          .add<ConstantType>(constant_type_rewrite)
          .add<UpwardReferenceType>(upref_type_rewrite);
      }
    };
    
    AggregateLoweringPass::TypeTermRewriter::CallbackMap
      AggregateLoweringPass::TypeTermRewriter::callback_map(AggregateLoweringPass::TypeTermRewriter::callback_map_initializer());

    /**
     * \internal
     * Wrapper around TypeTermRewriter to eliminate dependency between AggregateLowering.cpp and AggregateLoweringOperations.cpp.
     */
    LoweredType AggregateLoweringPass::type_term_rewrite(AggregateLoweringRewriter& runner, const ValuePtr<HashableValue>& type) {
      return TypeTermRewriter::callback_map.call(runner, type);
    }
    
    /**
     * \internal
     * Wrapper around TypeTermRewriter::parameter_type_rewrite to eliminate dependency between AggregateLowering.cpp and AggregateLoweringOperations.cpp.
     */
    LoweredType AggregateLoweringPass::type_term_rewrite_parameter(AggregateLoweringRewriter& rewriter, const ValuePtr<>& type) {
      return TypeTermRewriter::parameter_type_rewrite(rewriter, type);
    }

    struct AggregateLoweringPass::FunctionalTermRewriter {
      static LoweredValue type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<>& term) {
        LoweredType ty = rewriter.rewrite_type(term);
        if (!rewriter.pass().split_structs) {
          std::vector<ValuePtr<> > members;
          members.push_back(ty.size());
          members.push_back(ty.alignment());
          return LoweredValue::register_(ty, ty.global(), FunctionalBuilder::struct_value(rewriter.context(), members, term->location()));
        } else {
          LoweredValue::EntryVector members;
          members.push_back(LoweredValue::register_(rewriter.pass().size_type(), ty.global(), ty.size()));
          members.push_back(LoweredValue::register_(rewriter.pass().size_type(), ty.global(), ty.alignment()));
          return LoweredValue::split(ty, members);
        }
      }

      static LoweredValue default_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<HashableValue>& term) {
        class Callback : public RewriteCallback {
          AggregateLoweringRewriter *m_rewriter;
          bool m_global;
          
        public:
          Callback(AggregateLoweringRewriter& rewriter)
          : RewriteCallback(rewriter.context()),
          m_rewriter(&rewriter),
          m_global(true) {
          }

          virtual ValuePtr<> rewrite(const ValuePtr<>& value) {
            LoweredValueSimple x = m_rewriter->rewrite_value_register(value);
            m_global = m_global && x.global;
            return x.value;
          }
          
          bool global() const {return m_global;}
        } callback(rewriter);
        
        LoweredType type = rewriter.rewrite_type(term->type());
        ValuePtr<> rewritten = term->rewrite(callback);
        return LoweredValue::register_(type, callback.global(), rewritten);
      }
      
      static LoweredValue array_value_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ArrayValue>& term) {
        bool global = true;
        LoweredType arr_type = rewriter.rewrite_type(term->type()), el_type = rewriter.rewrite_type(term->element_type());
        LoweredValue::EntryVector entries;
        for (std::size_t ii = 0, ie = term->length(); ii != ie; ++ii) {
          LoweredValue c = rewriter.rewrite_value(term->value(ii));
          entries.push_back(c);
          global = global && c.global();
        }
          
        if (arr_type.mode() == LoweredType::mode_register) {
          std::vector<ValuePtr<> > values;
          for (LoweredValue::EntryVector::const_iterator ii = entries.begin(), ie = entries.end(); ii != ie; ++ii)
            values.push_back(ii->register_value());
          return LoweredValue::register_(arr_type, global, FunctionalBuilder::array_value(el_type.register_type(), values, term->location()));
        } else {
          return LoweredValue::split(arr_type, entries);
        }
      }
      
      static LoweredValue struct_value_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<StructValue>& term) {
        bool global = true;
        LoweredType st_type = rewriter.rewrite_type(term->type());
        LoweredValue::EntryVector entries;
        for (std::size_t ii = 0, ie = term->n_members(); ii != ie; ++ii) {
          LoweredValue c = rewriter.rewrite_value(term->member_value(ii));
          entries.push_back(c);
          global = global && c.global();
        }
        
        if (st_type.mode() == LoweredType::mode_register) {
          std::vector<ValuePtr<> > values;
          for (LoweredValue::EntryVector::const_iterator ii = entries.begin(), ie = entries.end(); ii != ie; ++ii)
            values.push_back(ii->register_value());
          return LoweredValue::register_(st_type, global, FunctionalBuilder::struct_value(rewriter.context(), values, term->location()));
        } else {
          return LoweredValue::split(st_type, entries);
        }
      }
      
      static LoweredValue union_value_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<UnionValue>& term) {
        LoweredType type = rewriter.rewrite_type(term->type());
        if (type.mode() == LoweredType::mode_register) {
          LoweredValue inner = rewriter.rewrite_value(term->value());
          if (isa<UnionType>(type.register_type())) {
            return LoweredValue::register_(type, inner.global(),
                                          FunctionalBuilder::union_value(type.register_type(), inner.register_value(), term->location()));
          } else {
            return rewriter.bitcast(type, inner, term->location());
          }
        } else {
          throw TvmUserError("Cannot create union value of unknown size");
        }
      }
      
      static LoweredValue apply_value_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ApplyValue>& term) {
        LoweredType type = rewriter.rewrite_type(term->type());
        LoweredValue inner = rewriter.rewrite_value(term->value());
        if (inner.mode() == LoweredValue::mode_register) {
          PSI_ASSERT(type.mode() == LoweredType::mode_register);
          return LoweredValue::register_(type, inner.global(), inner.register_value());
        } else {
          PSI_ASSERT(type.mode() == LoweredType::mode_split);
          return LoweredValue::split(type, LoweredValue::EntryVector(1, inner));
        }
      }
      
      static LoweredValue outer_ptr_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<OuterPtr>& term) {
        ValuePtr<PointerType> inner_ptr_ty = value_cast<PointerType>(term->pointer()->type());
        LoweredValueSimple inner_ptr = rewriter.rewrite_value_register(term->pointer());
        LoweredType outer_ptr_ty = rewriter.rewrite_type(term->type());
        ValuePtr<> base = FunctionalBuilder::pointer_cast(inner_ptr.value,
                                                          FunctionalBuilder::byte_type(rewriter.context(), term->location()),
                                                          term->location());
        
        ValuePtr<UpwardReference> up = dyn_cast<UpwardReference>(inner_ptr_ty->upref());
        ValuePtr<> outer_type = up->outer_type();
        ValuePtr<> offset;
        bool global = inner_ptr.global && outer_ptr_ty.global();
        if (ValuePtr<StructType> struct_ty = dyn_cast<StructType>(outer_type)) {
          LoweredValueSimple st_offset = rewriter.rewrite_value_register(FunctionalBuilder::struct_element_offset(struct_ty, size_to_unsigned(up->index()), term->location()));
          offset = st_offset.value;
          global = global && st_offset.global;
        } else if (ValuePtr<ArrayType> array_ty = dyn_cast<ArrayType>(outer_type)) {
          LoweredValueSimple idx = rewriter.rewrite_value_register(up->index());
          LoweredType el_type = rewriter.rewrite_type(array_ty->element_type());
          offset = FunctionalBuilder::mul(idx.value, el_type.size(), term->location());
          global = global && idx.global && el_type.global();
        } else if (isa<UnionType>(outer_type) || isa<ApplyType>(outer_type)) {
          return LoweredValue::register_(outer_ptr_ty, global, base);
        } else {
          throw TvmInternalError("Upward reference cannot be unfolded");
        }
        
        offset = FunctionalBuilder::neg(offset, term->location());
        return LoweredValue::register_(outer_ptr_ty, global, FunctionalBuilder::pointer_offset(base, offset, term->location()));
      }

      /**
       * Get a pointer to an array element from an array pointer.
       * 
       * This is common to both array_element_rewrite() and element_ptr_rewrite().
       * 
       * \param unchecked_array_ty Array type. This must not have been lowered.
       * \param base Pointer to array. This must have already been lowered.
       */
      static LoweredValueSimple array_ptr_offset(AggregateLoweringRewriter& rewriter, const ValuePtr<>& unchecked_array_ty, const LoweredValueSimple& base, const LoweredValueSimple& index, const SourceLocation& location) {
        ValuePtr<ArrayType> array_ty = dyn_cast<ArrayType>(unchecked_array_ty);
        if (!array_ty)
          throw TvmUserError("array type argument did not evaluate to an array type");

        LoweredType array_ty_l = rewriter.rewrite_type(array_ty);
        if (array_ty_l.mode() == LoweredType::mode_register) {
          ValuePtr<> array_ptr = FunctionalBuilder::pointer_cast(base.value, array_ty_l.register_type(), location);
          return LoweredValueSimple(base.global && index.global && array_ty_l.global(),
                                    FunctionalBuilder::element_ptr(array_ptr, index.value, location));
        }

        LoweredType element_ty = rewriter.rewrite_type(array_ty->element_type());
        if (element_ty.mode() == LoweredType::mode_register) {
          ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(base.value, element_ty.register_type(), location);
          return LoweredValueSimple(base.global && index.global && element_ty.global(),
                                    FunctionalBuilder::pointer_offset(cast_ptr, index.value, location));
        }

        LoweredValueSimple element_size = rewriter.rewrite_value_register(FunctionalBuilder::type_size(array_ty->element_type(), location));
        ValuePtr<> offset = FunctionalBuilder::mul(element_size.value, index.value, location);
        PSI_ASSERT(base.value->type() == FunctionalBuilder::byte_pointer_type(rewriter.context(), location));
        return LoweredValueSimple(base.global && index.global && element_size.global,
                                  FunctionalBuilder::pointer_offset(base.value, offset, location));
      }
      
      /**
       * \brief Rewrite an indexed select into a series of binary select statements
       * 
       * This currently creates a list type select rather than a binary search type select,
       * because I think that's probably easier to optimize.
       * 
       * \todo Move indexed select lowering into a separate pass by creating an IndexedSelect operation.
       */
      static ValuePtr<> array_element_select(AggregateLoweringRewriter& rewriter, const ValuePtr<>& index, const ValuePtr<>& undef_value,
                                             const std::map<unsigned, ValuePtr<> >& entries, const SourceLocation& location) {
        PSI_ASSERT(entries.size() >= 2);
        ValuePtr<> value = undef_value;
        for (std::map<unsigned, ValuePtr<> >::const_iterator ii = entries.begin(), ie = entries.end(); ii != ie; ++ii) {
          ValuePtr<> current_index = FunctionalBuilder::size_value(rewriter.context(), ii->first, location);
          ValuePtr<> cmp = FunctionalBuilder::cmp_eq(index, current_index, location);
          value = FunctionalBuilder::select(cmp, ii->second, value, location);
        }
        return value;
      }
      
      static LoweredValue array_element_rewrite_split(AggregateLoweringRewriter& rewriter, const LoweredValueSimple& index, const LoweredValue::EntryVector& entries, const SourceLocation& location) {
        const LoweredType& ty = entries.front().type();
        switch (ty.mode()) {
        case LoweredType::mode_register: {
          std::map<unsigned, ValuePtr<> > values;
          bool global = ty.global() && index.global;
          ValuePtr<> zero;
          for (std::size_t ii = 0, ie = entries.size(); ii != ie; ++ii) {
            const LoweredValue& entry = entries[ii];
            global = global && entry.global();
            values[ii] = entry.register_value();
          }
          ValuePtr<> undef_value = FunctionalBuilder::undef(ty.register_type(), location);
          return LoweredValue::register_(ty, global, array_element_select(rewriter, index.value, undef_value, values, location));
        }
        
        case LoweredType::mode_split: {
          LoweredValue::EntryVector split_result;
          std::vector<LoweredValue> component_entries;
          for (std::size_t ii = 0, ie = ty.split_entries().size(); ii != ie; ++ii) {
            component_entries.clear();
            for (LoweredValue::EntryVector::const_iterator ji = entries.begin(), je = entries.end(); ji != je; ++ji) {
              PSI_ASSERT(ii < ji->split_entries().size());
              component_entries.push_back(ji->split_entries()[ii]);
            }
            split_result.push_back(array_element_rewrite_split(rewriter, index, component_entries, location));
          }
          return LoweredValue::split(ty, split_result);
        }
        
        case LoweredType::mode_blob:
          throw TvmUserError("Arrays type not supported by the back-end used in register");
        
        default: PSI_FAIL("unexpected enum value");
        }
      }

      static LoweredValue array_element_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementValue>& term) {
        LoweredValueSimple index = rewriter.rewrite_value_register(term->aggregate());
        LoweredValue array_val = rewriter.rewrite_value(term->aggregate());
        LoweredType el_type = rewriter.rewrite_type(term->type());
        switch (array_val.mode()) {
        case LoweredValue::mode_register:
          return LoweredValue::register_(el_type, index.global && array_val.global(),
                                         FunctionalBuilder::element_value(array_val.register_value(), index.value, term->location()));
        
        case LoweredValue::mode_split: {
          switch (array_val.split_entries().size()) {
          case 0: return rewriter.rewrite_value(FunctionalBuilder::undef(term->type(), term->location()));
          case 1: return array_val.split_entries().front();
          default: return array_element_rewrite_split(rewriter, index, array_val.split_entries(), term->location());
          }
        }
          
        default: PSI_FAIL("unexpected enum value");
        }
      }
      
      static LoweredValue array_element_ptr_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementPtr>& term) {
        LoweredValueSimple array_ptr = rewriter.rewrite_value_register(term->aggregate_ptr());
        LoweredValueSimple index = rewriter.rewrite_value_register(term->index());
        
        ValuePtr<PointerType> pointer_type = dyn_cast<PointerType>(term->aggregate_ptr()->type());
        if (!pointer_type)
          throw TvmUserError("array_ep argument did not evaluate to a pointer");
        
        LoweredType type = rewriter.rewrite_type(term->type());
        LoweredValueSimple result = array_ptr_offset(rewriter, pointer_type->target_type(), array_ptr, index, term->location());
        return LoweredValue::register_(type, result.global, result.value);
      }
      
      /**
       * Get a pointer to an struct element from a struct pointer.
       * 
       * This is common to both struct_element_rewrite() and element_ptr_rewrite().
       * 
       * \param base Pointer to struct. This must have already been lowered.
       */
      static LoweredValueSimple struct_ptr_offset(AggregateLoweringRewriter& rewriter, const ValuePtr<>& unchecked_struct_ty, const LoweredValueSimple& base, unsigned index, const SourceLocation& location) {
        ValuePtr<StructType> struct_ty = dyn_cast<StructType>(unchecked_struct_ty);
        if (!struct_ty)
          throw TvmInternalError("struct type value did not evaluate to a struct type");

        LoweredType struct_ty_rewritten = rewriter.rewrite_type(struct_ty);
        if (struct_ty_rewritten.mode() == LoweredType::mode_register) {
          ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(base.value, struct_ty_rewritten.register_type(), location);
          return LoweredValueSimple(base.global && struct_ty_rewritten.global(), FunctionalBuilder::element_ptr(cast_ptr, index, location));
        }
        
        PSI_ASSERT(base.value->type() == FunctionalBuilder::byte_pointer_type(rewriter.context(), location));
        LoweredValueSimple offset = rewriter.rewrite_value_register(FunctionalBuilder::struct_element_offset(struct_ty, index, location));
        return LoweredValueSimple(base.global && offset.global, FunctionalBuilder::pointer_offset(base.value, offset.value, location));
      }
      
      static LoweredValue struct_element_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementValue>& term) {
        LoweredValue struct_val = rewriter.rewrite_value(term->aggregate());
        unsigned idx = size_to_unsigned(term->index());
        switch (struct_val.mode()) {
        case LoweredValue::mode_register:
          return LoweredValue::register_(rewriter.rewrite_type(term->type()), struct_val.global(),
                                         FunctionalBuilder::element_value(struct_val.register_value(), idx, term->location()));
          
        case LoweredValue::mode_split:
          return struct_val.split_entries()[idx];
          
        default:
          PSI_FAIL("unexpected enum value");
        }
      }

      static LoweredValue struct_element_ptr_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementPtr>& term) {
        LoweredValueSimple struct_ptr = rewriter.rewrite_value_register(term->aggregate_ptr());
        
        ValuePtr<PointerType> pointer_type = dyn_cast<PointerType>(term->aggregate_ptr()->type());
        if (!pointer_type)
          throw TvmUserError("struct_ep argument did not evaluate to a pointer");
        
        LoweredValueSimple result = struct_ptr_offset(rewriter, pointer_type->target_type(), struct_ptr, size_to_unsigned(term->index()), term->location());
        return LoweredValue::register_(rewriter.rewrite_type(term->type()), result.global, result.value);
      }
      
      static LoweredValue struct_element_offset_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<StructElementOffset>& term) {
        ValuePtr<StructType> struct_ty = dyn_cast<StructType>(term->struct_type());
        if (!struct_ty)
          throw TvmUserError("struct_eo argument did not evaluate to a struct type");

        ElementOffsetGenerator gen(&rewriter, term->location());
        for (unsigned ii = 0, ie = term->index(); ii <= ie; ++ii)
          gen.next(struct_ty->member_type(ii));
        
        return LoweredValue::register_(rewriter.pass().size_type(), gen.global(), gen.offset());
      }

      static LoweredValue union_element_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementValue>& term) {
        LoweredType type = rewriter.rewrite_type(term->type());
        if (type.mode() == LoweredType::mode_register) {
          LoweredValue union_val = rewriter.rewrite_value(term->aggregate());
          if (isa<UnionType>(type.register_type())) {
            return LoweredValue::register_(type, union_val.global(),
                                          FunctionalBuilder::element_value(union_val.register_value(), term->index(), term->location()));
          } else {
            return rewriter.bitcast(type, union_val, term->location());
          }
        } else {
          throw TvmUserError("Cannot get element value from union of unknown size");
        }
      }

      static LoweredValue union_element_ptr_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementPtr>& term) {
        LoweredValueSimple result = rewriter.rewrite_value_register(term->aggregate_ptr());
        return LoweredValue::register_(rewriter.rewrite_type(term->type()), result.global, result.value);
      }
      
      static LoweredValue apply_element_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementValue>& term) {
        LoweredValue apply_val = rewriter.rewrite_value(term->aggregate());
        PSI_ASSERT(size_equals_constant(term->index(), 0));
        switch (apply_val.mode()) {
        case LoweredValue::mode_register:
          return LoweredValue::register_(rewriter.rewrite_type(term->type()), apply_val.global(), apply_val.register_value());
          
        case LoweredValue::mode_split:
          return apply_val.split_entries()[0];
          
        default:
          PSI_FAIL("unexpected enum value");
        }
      }
      
      static LoweredValue apply_element_ptr_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementPtr>& term) {
        LoweredValueSimple result = rewriter.rewrite_value_register(term->aggregate_ptr());
        return LoweredValue::register_(rewriter.rewrite_type(term->type()), result.global, result.value);
      }

      static LoweredValue metatype_size_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<MetatypeSize>& term) {
        LoweredType t = rewriter.rewrite_type(term->parameter());
        return LoweredValue::register_(rewriter.pass().size_type(), t.global(), t.size());
      }
      
      static LoweredValue metatype_alignment_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<MetatypeAlignment>& term) {
        LoweredType t = rewriter.rewrite_type(term->parameter());
        return LoweredValue::register_(rewriter.pass().size_type(), t.global(), t.alignment());
      }
      
      static LoweredValue pointer_offset_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<PointerOffset>& term) {
        LoweredValueSimple base_value = rewriter.rewrite_value_register(term->pointer());
        LoweredValueSimple offset = rewriter.rewrite_value_register(term->offset());
        
        LoweredType term_ty = rewriter.rewrite_type(term->type());
        LoweredType ty = rewriter.rewrite_type(term->pointer_type()->target_type());
        if ((ty.mode() == LoweredType::mode_register) && !rewriter.pass().pointer_arithmetic_to_bytes) {
          ValuePtr<> cast_base = FunctionalBuilder::pointer_cast(base_value.value, ty.register_type(), term->location());
          ValuePtr<> ptr = FunctionalBuilder::pointer_offset(cast_base, offset.value, term->location());
          ValuePtr<> result = FunctionalBuilder::pointer_cast(ptr, FunctionalBuilder::byte_type(rewriter.context(), term->location()), term->location());
          return LoweredValue::register_(term_ty, ty.global() && base_value.global && offset.global, result);
        } else {
          ValuePtr<> new_offset = FunctionalBuilder::mul(ty.size(), offset.value, term->location());
          ValuePtr<> result = FunctionalBuilder::pointer_offset(base_value.value, new_offset, term->location());
          return LoweredValue::register_(term_ty, ty.global() && base_value.global && offset.global, result);
        }
      }
      
      static LoweredValue pointer_cast_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<PointerCast>& term) {
        return rewriter.rewrite_value(term->pointer());
      }
      
      static LoweredValue unwrap_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<Unwrap>& term) {
        return rewriter.rewrite_value(term->value());
      }
      
      static LoweredValue element_value_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementValue>& term) {
        ValuePtr<> ty = term->aggregate()->type();
        if (isa<StructType>(ty))
          return struct_element_rewrite(rewriter, term);
        else if (isa<ArrayType>(ty))
          return array_element_rewrite(rewriter, term);
        else if (isa<UnionType>(ty))
          return union_element_rewrite(rewriter, term);
        else if (isa<ApplyType>(ty))
          return apply_element_rewrite(rewriter, term);
        else
          throw TvmUserError("element_value aggregate argument is not an aggregate type");
      }
      
      static LoweredValue element_ptr_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementPtr>& term) {
        ValuePtr<PointerType> ptr_ty = dyn_cast<PointerType>(term->aggregate_ptr()->type());
        if (!ptr_ty)
          throw TvmUserError("element_ptr aggregate argument is not a pointer");

        ValuePtr<> ty = ptr_ty->target_type();
        if (isa<StructType>(ty))
          return struct_element_ptr_rewrite(rewriter, term);
        else if (isa<ArrayType>(ty))
          return array_element_ptr_rewrite(rewriter, term);
        else if (isa<UnionType>(ty))
          return union_element_ptr_rewrite(rewriter, term);
        else if (isa<ApplyType>(ty))
          return apply_element_ptr_rewrite(rewriter, term);
        else
          throw TvmUserError("element_value aggregate argument is not an aggregate type");
      }
      
      static LoweredValue build_select(AggregateLoweringRewriter& rewriter, const LoweredValueSimple& cond, const LoweredValue& true_val, const LoweredValue& false_val, const SourceLocation& location) {
        const LoweredType& ty = true_val.type();
        switch (ty.mode()) {
        case LoweredType::mode_register:
          return LoweredValue::register_(ty, ty.global() && cond.global && true_val.global() && false_val.global(),
                                         FunctionalBuilder::select(cond.value, true_val.register_value(), false_val.register_value(), location));
          
        case LoweredType::mode_split: {
          const LoweredType::EntryVector& ty_entries = ty.split_entries();
          const LoweredValue::EntryVector& true_entries = true_val.split_entries(), &false_entries = false_val.split_entries();
          PSI_ASSERT((ty_entries.size() == true_entries.size()) && (ty_entries.size() == false_entries.size()));
          LoweredValue::EntryVector val_entries;
          for (std::size_t ii = 0, ie = ty_entries.size(); ii != ie; ++ii)
            val_entries.push_back(build_select(rewriter, cond, true_entries[ii], false_entries[ii], location));
          return LoweredValue::split(ty, val_entries);
        }
        
        default: PSI_FAIL("unexpected enum value");
        }
      }
      
      static LoweredValue select_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<Select>& select) {
        LoweredType ty = rewriter.rewrite_type(select->type());
        LoweredValueSimple cond = rewriter.rewrite_value_register(select->condition());
        LoweredValue true_val = rewriter.rewrite_value(select->true_value());
        LoweredValue false_val = rewriter.rewrite_value(select->false_value());
        return build_select(rewriter, cond, true_val, false_val, select->location());
      }
      
      static LoweredValue build_zero_undef(AggregateLoweringRewriter& rewriter, const LoweredType& ty, bool is_zero, const SourceLocation& location) {
        if (is_zero) {
          if (ValuePtr<ConstantType> constant = dyn_cast<ConstantType>(ty.origin()))
            return rewriter.rewrite_value(constant->value());
        }
        
        switch (ty.mode()) {
        case LoweredType::mode_register:
          return LoweredValue::register_(ty, true, is_zero ? FunctionalBuilder::zero(ty.register_type(), location) : FunctionalBuilder::undef(ty.register_type(), location));
          
        case LoweredType::mode_split: {
          LoweredValue::EntryVector entries;
          for (LoweredType::EntryVector::const_iterator ii = ty.split_entries().begin(), ie = ty.split_entries().end(); ii != ie; ++ii)
            entries.push_back(build_zero_undef(rewriter, *ii, is_zero, location));
          return LoweredValue::split(ty, entries);
        }
          
        case LoweredType::mode_blob:
          throw TvmUserError("Type unsupported by back-end cannot be used in register");
          
        default: PSI_FAIL("unexpected enum value");
        }
      }

      static LoweredValue zero_value_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ZeroValue>& term) {
        LoweredType ty = rewriter.rewrite_type(term->type());
        return build_zero_undef(rewriter, ty, true, term->location());
      }
      
      static LoweredValue undefined_value_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<UndefinedValue>& term) {
        LoweredType ty = rewriter.rewrite_type(term->type());
        return build_zero_undef(rewriter, ty, false, term->location());
      }

      typedef TermOperationMap<HashableValue, LoweredValue, AggregateLoweringRewriter&> CallbackMap;
      static CallbackMap callback_map;
      
      static CallbackMap::Initializer callback_map_initializer() {
        return CallbackMap::initializer(default_rewrite)
          .add<ArrayType>(type_rewrite)
          .add<StructType>(type_rewrite)
          .add<UnionType>(type_rewrite)
          .add<ApplyType>(type_rewrite)
          .add<PointerType>(type_rewrite)
          .add<IntegerType>(type_rewrite)
          .add<FloatType>(type_rewrite)
          .add<EmptyType>(type_rewrite)
          .add<ByteType>(type_rewrite)
          .add<UpwardReferenceType>(type_rewrite)
          .add<MetatypeValue>(type_rewrite)
          .add<OuterPtr>(outer_ptr_rewrite)
          .add<ArrayValue>(array_value_rewrite)
          .add<StructValue>(struct_value_rewrite)
          .add<UnionValue>(union_value_rewrite)
          .add<ApplyValue>(apply_value_rewrite)
          .add<StructElementOffset>(struct_element_offset_rewrite)
          .add<MetatypeSize>(metatype_size_rewrite)
          .add<MetatypeAlignment>(metatype_alignment_rewrite)
          .add<PointerOffset>(pointer_offset_rewrite)
          .add<PointerCast>(pointer_cast_rewrite)
          .add<Unwrap>(unwrap_rewrite)
          .add<ElementValue>(element_value_rewrite)
          .add<ElementPtr>(element_ptr_rewrite)
          .add<Select>(select_rewrite)
          .add<ZeroValue>(zero_value_rewrite)
          .add<UndefinedValue>(undefined_value_rewrite);
      }
    };

    AggregateLoweringPass::FunctionalTermRewriter::CallbackMap
      AggregateLoweringPass::FunctionalTermRewriter::callback_map(AggregateLoweringPass::FunctionalTermRewriter::callback_map_initializer());

    /**
     * \internal
     * Wrapper around FunctionalTermRewriter to eliminate dependency between AggregateLowering.cpp and AggregateLoweringOperations.cpp.
     */
    LoweredValue AggregateLoweringPass::hashable_term_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<HashableValue>& term) {
      return FunctionalTermRewriter::callback_map.call(rewriter, term);
    }

    struct AggregateLoweringPass::InstructionTermRewriter {
      static LoweredValue return_rewrite(FunctionRunner& runner, const ValuePtr<Return>& term) {
        runner.pass().target_callback->lower_return(runner, term->value, term->location());
        return LoweredValue();
      }

      static LoweredValue br_rewrite(FunctionRunner& runner, const ValuePtr<UnconditionalBranch>& term) {
        ValuePtr<Block> target = runner.rewrite_block(term->target);
        runner.builder().br(target, term->location());
        return LoweredValue();
      }

      static LoweredValue cond_br_rewrite(FunctionRunner& runner, const ValuePtr<ConditionalBranch>& term) {
        ValuePtr<> cond = runner.rewrite_value_register(term->condition).value;
        ValuePtr<> true_target = runner.rewrite_value_register(term->true_target).value;
        ValuePtr<> false_target = runner.rewrite_value_register(term->false_target).value;
        runner.builder().cond_br(cond, value_cast<Block>(true_target), value_cast<Block>(false_target), term->location());
        return LoweredValue();
      }
      
      static void eval_rewrite_value(FunctionRunner& runner, const LoweredValue& value, const SourceLocation& location) {
        switch (value.mode()) {
        case LoweredValue::mode_empty:
          break;

        case LoweredValue::mode_register:
          runner.builder().eval(value.register_value(), location);
          break;
          
        case LoweredValue::mode_split: {
          const LoweredValue::EntryVector& entries = value.split_entries();
          for (LoweredValue::EntryVector::const_iterator ii = entries.begin(), ie = entries.end(); ii != ie; ++ii)
            eval_rewrite_value(runner, *ii, location);
          break;
        }
          
        default: PSI_FAIL("Unknown LoweredValue mode");
        }
      }
      
      static LoweredValue eval_rewrite(FunctionRunner& runner, const ValuePtr<Evaluate>& term) {
        eval_rewrite_value(runner, runner.rewrite_value(term->value), term->location());
        return LoweredValue();
      }

      static LoweredValue call_rewrite(FunctionRunner& runner, const ValuePtr<Call>& term) {
        runner.pass().target_callback->lower_function_call(runner, term);
        return LoweredValue();
      }

      static LoweredValue alloca_rewrite(FunctionRunner& runner, const ValuePtr<Alloca>& term) {
        LoweredType type = runner.rewrite_type(term->element_type);
        ValuePtr<> count = term->count ? runner.rewrite_value_register(term->count).value : ValuePtr<>();
        ValuePtr<> alignment = term->alignment ? runner.rewrite_value_register(term->alignment).value : ValuePtr<>();
        ValuePtr<> stack_ptr;
        if (type.mode() == LoweredType::mode_register) {
          stack_ptr = runner.builder().alloca_(type.register_type(), count, alignment, term->location());
        } else {
          ValuePtr<> total_size = type.size();
          if (count)
            total_size = FunctionalBuilder::mul(count, total_size, term->location());
          ValuePtr<> total_alignment = type.alignment();
          if (alignment)
            total_alignment = FunctionalBuilder::max(total_alignment, alignment, term->location());
          stack_ptr = runner.builder().alloca_(FunctionalBuilder::byte_type(runner.context(), term->location()), total_size, total_alignment, term->location());
        }
        ValuePtr<> cast_stack_ptr = FunctionalBuilder::pointer_cast(stack_ptr, FunctionalBuilder::byte_type(runner.context(), term->location()), term->location());
        return LoweredValue::register_(runner.pass().pointer_type(), false, cast_stack_ptr);
      }
      
      static LoweredValue alloca_const_rewrite(FunctionRunner& runner, const ValuePtr<AllocaConst>& term) {
        ValuePtr<> value = runner.rewrite_value_register(term->value).value;
        ValuePtr<> stack_ptr = runner.builder().alloca_const(value, term->location());
        ValuePtr<> cast_stack_ptr = FunctionalBuilder::pointer_cast(stack_ptr, FunctionalBuilder::byte_type(runner.context(), term->location()), term->location());
        return LoweredValue::register_(runner.pass().pointer_type(), false, cast_stack_ptr);
      }
      
      static LoweredValue freea_rewrite(FunctionRunner& runner, const ValuePtr<FreeAlloca>& term) {
        ValuePtr<> ptr = runner.rewrite_value_register(term->value).value;
        if (ValuePtr<PointerCast> cast = dyn_cast<PointerCast>(ptr))
          ptr = cast->pointer();
        PSI_ASSERT(isa<Alloca>(ptr) || isa<AllocaConst>(ptr));
        runner.builder().freea(ptr, term->location());
        return LoweredValue();
      }
      
      static LoweredValue load_rewrite(FunctionRunner& runner, const ValuePtr<Load>& term) {
        LoweredType ty = runner.rewrite_type(term->type());
        LoweredValueSimple ptr = runner.rewrite_value_register(term->target);
        return runner.load_value(ty, ptr.value, term->location());
      }
      
      static LoweredValue store_rewrite(FunctionRunner& runner, const ValuePtr<Store>& term) {
        ValuePtr<> ptr = runner.rewrite_value_register(term->target).value;
        LoweredValue val = runner.rewrite_value(term->value);
        runner.store_value(val, ptr, term->location());
        return LoweredValue();
      }
      
      static LoweredValue memcpy_rewrite(FunctionRunner& runner, const ValuePtr<MemCpy>& term) {
        ValuePtr<> dest = runner.rewrite_value_register(term->dest).value;
        ValuePtr<> src = runner.rewrite_value_register(term->src).value;
        ValuePtr<> count = runner.rewrite_value_register(term->count).value;
        ValuePtr<> alignment = runner.rewrite_value_register(term->alignment).value;
        
        ValuePtr<> original_element_type = value_cast<PointerType>(term->dest->type())->target_type();
        LoweredType element_type = runner.rewrite_type(original_element_type);
        if (element_type.mode() == LoweredType::mode_register) {
          ValuePtr<> dest_cast = FunctionalBuilder::pointer_cast(dest, element_type.register_type(), term->location());
          ValuePtr<> src_cast = FunctionalBuilder::pointer_cast(dest, element_type.register_type(), term->location());
          runner.builder().memcpy(dest_cast, src_cast, count, alignment, term->location());
          return LoweredValue();
        } else {
          PSI_ASSERT(dest->type() == FunctionalBuilder::byte_pointer_type(runner.context(), term->location()));
          ValuePtr<> type_size = runner.rewrite_value_register(FunctionalBuilder::type_size(original_element_type, term->location())).value;
          ValuePtr<> type_alignment = runner.rewrite_value_register(FunctionalBuilder::type_alignment(original_element_type, term->location())).value;
          ValuePtr<> bytes = FunctionalBuilder::mul(count, type_size, term->location());
          ValuePtr<> max_alignment = FunctionalBuilder::max(alignment, type_alignment, term->location());
          runner.builder().memcpy(dest, src, bytes, max_alignment, term->location());
          return LoweredValue();
        }
      }
      
      static LoweredValue memzero_rewrite(FunctionRunner& runner, const ValuePtr<MemZero>& term) {
        ValuePtr<> ptr = runner.rewrite_value_register(term->dest).value;
        ValuePtr<> count = runner.rewrite_value_register(term->count).value;
        ValuePtr<> alignment = runner.rewrite_value_register(term->alignment).value;
        
        ValuePtr<> original_element_type = value_cast<PointerType>(term->dest->type())->target_type();
        LoweredType element_type = runner.rewrite_type(original_element_type);
        if (element_type.mode() == LoweredType::mode_register) {
          ValuePtr<> ptr_cast = FunctionalBuilder::pointer_cast(ptr, element_type.register_type(), term->location());
          runner.builder().memzero(ptr_cast, count, alignment, term->location());
          return LoweredValue();
        } else {
          PSI_ASSERT(ptr->type() == FunctionalBuilder::byte_pointer_type(runner.context(), term->location()));
          ValuePtr<> type_size = runner.rewrite_value_register(FunctionalBuilder::type_size(original_element_type, term->location())).value;
          ValuePtr<> type_alignment = runner.rewrite_value_register(FunctionalBuilder::type_alignment(original_element_type, term->location())).value;
          ValuePtr<> bytes = FunctionalBuilder::mul(count, type_size, term->location());
          ValuePtr<> max_alignment = FunctionalBuilder::max(alignment, type_alignment, term->location());
          runner.builder().memzero(ptr, bytes, max_alignment, term->location());
          return LoweredValue();
        }
      }
      
      static LoweredValue solidify_rewrite(FunctionRunner& runner, const ValuePtr<Solidify>& term) {
        ValuePtr<ConstantType> cn = value_cast<ConstantType>(term->value->type());
        LoweredValue cval = runner.rewrite_value(term->value);
        runner.add_mapping(cn->value(), cval);
        return LoweredValue();
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
          .add<AllocaConst>(alloca_const_rewrite)
          .add<FreeAlloca>(freea_rewrite)
          .add<Evaluate>(eval_rewrite)
          .add<Store>(store_rewrite)
          .add<Load>(load_rewrite)
          .add<MemCpy>(memcpy_rewrite)
          .add<MemZero>(memzero_rewrite)
          .add<Solidify>(solidify_rewrite);
      }
    };
    
    AggregateLoweringPass::InstructionTermRewriter::CallbackMap
      AggregateLoweringPass::InstructionTermRewriter::callback_map(AggregateLoweringPass::InstructionTermRewriter::callback_map_initializer());
    
    /**
     * \internal
     * Wrapper around InstructionTermRewriter to eliminate dependency between AggregateLowering.cpp and AggregateLoweringOperations.cpp.
     */
    LoweredValue AggregateLoweringPass::instruction_term_rewrite(FunctionRunner& runner, const ValuePtr<Instruction>& insn) {
      return InstructionTermRewriter::callback_map.call(runner, insn);
    }
  }
}
