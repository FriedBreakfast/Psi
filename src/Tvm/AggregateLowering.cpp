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
    /**
     * \brief Computes offsets and alignment of struct-like (and thereby also array-like) data structures.
     */
    class AggregateLoweringPass::ElementOffsetGenerator {
      AggregateLoweringRewriter *m_rewriter;
      SourceLocation m_location;
      bool m_global;
      ValuePtr<> m_offset, m_size, m_alignment;
      
    public:
      ElementOffsetGenerator(AggregateLoweringRewriter *rewriter, const SourceLocation& location)
      : m_rewriter(rewriter), m_location(location), m_global(true) {
        m_offset = m_size = FunctionalBuilder::size_value(rewriter->context(), 0, location);
        m_alignment = FunctionalBuilder::size_value(rewriter->context(), 1, location);
      }
      
      /// \brief Are size(), offset() and alignment() all global values?
      bool global() const {return m_global;}
      /// \brief Offset of last element inserted
      const ValuePtr<>& offset() const {return m_offset;}
      /// \brief Current total size of all elements (may not be a multiple of alignment until finish() is called)
      const ValuePtr<>& size() const {return m_size;}
      /// \brief Current alignment of all elements
      const ValuePtr<>& alignment() const {return m_alignment;}
      
      /**
       * \brief Append an element to the data list.
       */
      void next(bool global, const ValuePtr<>& el_size, const ValuePtr<>& el_alignment) {
        if (!size_equals_constant(m_size, 0) && !size_equals_constant(el_alignment, 1))
          m_offset = FunctionalBuilder::align_to(m_size, el_alignment, m_location);
        m_size = FunctionalBuilder::add(m_offset, el_size, m_location);
        m_global = m_global && global;
      }
      
      /// \brief Ensure \c size if a multiple of \c alignment
      void finish() {m_size = FunctionalBuilder::align_to(m_size, m_alignment, m_location);}
      
      /// \copydoc ElementOffsetGenerator::next(const ValuePtr<>&,const ValuePtr<>&)
      void next(const LoweredType& type) {next(type.global(), type.size(), type.alignment());}
      
      /// \copydoc ElementOffsetGenerator::next(const LoweredType&)
      void next(const ValuePtr<>& type) {next(m_rewriter->rewrite_type(type));}
    };
    
    struct AggregateLoweringPass::TypeTermRewriter {
      static LoweredType array_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ArrayType>& term) {
        LoweredValueRegister length = rewriter.rewrite_value_register(term->length());
        LoweredType element_type = rewriter.rewrite_type(term->element_type());
        ValuePtr<> size = FunctionalBuilder::mul(length.value, element_type.size(), term->location());
        ValuePtr<> alignment = element_type.alignment();
        
        if (rewriter.pass().remove_only_unknown && isa<IntegerValue>(length.value)) {
          PSI_ASSERT(length.global);
          
          if (element_type.primitive() && !rewriter.pass().remove_register_arrays) {
            ValuePtr<> register_type = FunctionalBuilder::array_type(element_type.register_type(), length.value, term->location());

            if (!rewriter.pass().remove_sizeof) {
              size = FunctionalBuilder::type_size(register_type, term->location());
              alignment = FunctionalBuilder::type_alignment(register_type, term->location());
            }
            
            return LoweredType(term, element_type.global(), size, alignment, register_type);
          } else if (element_type.split()) {
            LoweredType::EntryVector entries(size_to_unsigned(length.value), element_type);
            return LoweredType(term, element_type.global(), size, alignment, entries);
          }
        } else {
          return LoweredType(term, element_type.global() && length.global, size, alignment);
        }
      }

      static LoweredType struct_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<StructType>& term) {
        ValuePtr<> size = FunctionalBuilder::size_value(rewriter.context(), 0, term->location());
        ValuePtr<> alignment = FunctionalBuilder::size_value(rewriter.context(), 1, term->location());
        
        std::vector<ValuePtr<> > register_members;
        LoweredType::EntryVector entries;
        bool has_register = true, global = true;
        for (unsigned i = 0; i != register_members.size(); ++i) {
          LoweredType member_type = rewriter.rewrite_type(term->member_type(i));
          entries.push_back(member_type);
          register_members.push_back(member_type.register_type());
          
          has_register = has_register && member_type.register_type();
          global = global && member_type.global();
          
          ValuePtr<> aligned_size = FunctionalBuilder::align_to(size, member_type.alignment(), term->location());
          size = FunctionalBuilder::add(aligned_size, member_type.size(), term->location());
          alignment = FunctionalBuilder::max(alignment, member_type.alignment(), term->location());
        }
        
        if (rewriter.pass().remove_only_unknown && has_register) {
          ValuePtr<> register_type = FunctionalBuilder::struct_type(rewriter.context(), register_members, term->location());

          if (!rewriter.pass().remove_sizeof) {
            size = FunctionalBuilder::type_size(register_type, term->location());
            alignment = FunctionalBuilder::type_alignment(register_type, term->location());
          }
          
          return LoweredType(term, global, size, alignment, register_type);
        } else {
          return LoweredType(term, global, size, alignment, entries);
        }
      }

      static LoweredType union_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<UnionType>& term) {
        ValuePtr<> size = FunctionalBuilder::size_value(rewriter.context(), 0, term->location());
        ValuePtr<> alignment = FunctionalBuilder::size_value(rewriter.context(), 1, term->location());
        
        std::vector<ValuePtr<> > register_members;
        bool has_register = true, global = true;
        for (unsigned i = 0; i != register_members.size(); ++i) {
          LoweredType member_type = rewriter.rewrite_type(term->member_type(i));
          register_members.push_back(member_type.register_type());
          has_register = has_register && member_type.register_type();
          global = global && member_type.global();
          
          size = FunctionalBuilder::max(size, member_type.size(), term->location());
          alignment = FunctionalBuilder::max(alignment, member_type.alignment(), term->location());
        }
        
        if (has_register && rewriter.pass().remove_only_unknown && !rewriter.pass().remove_all_unions) {
          ValuePtr<> register_type = FunctionalBuilder::union_type(rewriter.context(), register_members, term->location());
          
          if (!rewriter.pass().remove_sizeof) {
            size = FunctionalBuilder::type_size(register_type, term->location());
            alignment = FunctionalBuilder::type_alignment(register_type, term->location());
          }
          
          return LoweredType(term, global, size, alignment, register_type);
        } else {
          return LoweredType(term, global, size, alignment);
        }
      }
      
      static LoweredType simple_type_helper(AggregateLoweringRewriter& rewriter, const ValuePtr<>& origin, const ValuePtr<>& rewritten_type, const SourceLocation& location) {
        ValuePtr<> size, alignment;
        if (rewriter.pass().remove_sizeof) {
          TypeSizeAlignment size_align = rewriter.pass().target_callback->type_size_alignment(rewritten_type);
          size = size_align.size;
          alignment = size_align.alignment;
        } else {
          size = FunctionalBuilder::type_size(rewritten_type, location);
          alignment = FunctionalBuilder::type_alignment(rewritten_type, location);
        }
        return LoweredType(origin, true, size, alignment, rewritten_type);
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
        LoweredValueRegister size = rewriter.rewrite_value_register(term->size());
        LoweredValueRegister alignment = rewriter.rewrite_value_register(term->alignment());
        return LoweredType(term, size.global && alignment.global, size.value, alignment.value);
      }
      
      static LoweredType parameter_type_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<>& type) {
        bool global = false;
        ValuePtr<> size, alignment;
        if (rewriter.pass().remove_only_unknown) {
          LoweredValueRegister rewritten = rewriter.rewrite_value_register(type);
          global = rewritten.global;
          size = FunctionalBuilder::element_value(rewritten.value, 0, type->location());
          alignment = FunctionalBuilder::element_value(rewritten.value, 1, type->location());
        } else {
          LoweredValueRegister size_reg = rewriter.lookup_value_register(FunctionalBuilder::type_size(type, type->location()));
          LoweredValueRegister alignment_reg = rewriter.lookup_value_register(FunctionalBuilder::type_alignment(type, type->location()));
          global = size_reg.global && alignment_reg.global;
          size = size_reg.value;
          alignment = alignment_reg.value;
        }
        return LoweredType(type, global, size, alignment);
      }
      
      typedef TermOperationMap<FunctionalValue, LoweredType, AggregateLoweringRewriter&> CallbackMap;
      static CallbackMap callback_map;
      
      static CallbackMap::Initializer callback_map_initializer() {
        return CallbackMap::initializer(parameter_type_rewrite)
          .add<ArrayType>(array_type_rewrite)
          .add<StructType>(struct_type_rewrite)
          .add<UnionType>(union_type_rewrite)
          .add<Metatype>(metatype_rewrite)
          .add<MetatypeValue>(unknown_type_rewrite)
          .add<PointerType>(pointer_type_rewrite)
          .add<BooleanType>(primitive_type_rewrite)
          .add<ByteType>(primitive_type_rewrite)
          .add<EmptyType>(primitive_type_rewrite)
          .add<FloatType>(primitive_type_rewrite)
          .add<IntegerType>(primitive_type_rewrite);
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
          return LoweredValue::register_(term->type(), ty.global(), FunctionalBuilder::struct_value(rewriter.context(), members, term->location()));
        } else {
          LoweredValue::EntryVector members;
          members.push_back(LoweredValue::primitive(ty.global(), ty.size()));
          members.push_back(LoweredValue::primitive(ty.global(), ty.alignment()));
          return LoweredValue(term->type(), ty.global(), members);
        }
      }

      static LoweredValue default_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionalValue>& term) {
        PSI_ASSERT(rewriter.rewrite_type(term->type()).register_type());

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
            LoweredValueRegister x = m_rewriter->rewrite_value_register(value);
            m_global = m_global && x.global;
            return x.value;
          }
          
          bool global() const {return m_global;}
        } callback(rewriter);
        
        ValuePtr<> rewritten = term->rewrite(callback);
        return LoweredValue::register_(term->type(), callback.global(), rewritten);
      }
      
      static LoweredValue array_value_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ArrayValue>& term) {
        bool global = true;
        LoweredType el_type = rewriter.rewrite_type(term->element_type());
        LoweredValue::EntryVector entries;
        for (std::size_t ii = 0, ie = term->length(); ii != ie; ++ii) {
          LoweredValue c = rewriter.rewrite_value(term->value(ii));
          entries.push_back(c);
          global = global && c.global();
        }
          
        if (el_type.register_type()) {
          std::vector<ValuePtr<> > values;
          for (LoweredValue::EntryVector::const_iterator ii = entries.begin(), ie = entries.end(); ii != ie; ++ii)
            values.push_back(ii->register_value());
          return LoweredValue::register_(term->type(), global, FunctionalBuilder::array_value(el_type.register_type(), values, term->location()));
        } else {
          return LoweredValue(term->type(), global, entries);
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
        
        if (st_type.register_type()) {
          std::vector<ValuePtr<> > values;
          for (LoweredValue::EntryVector::const_iterator ii = entries.begin(), ie = entries.end(); ii != ie; ++ii)
            values.push_back(ii->register_value());
          return LoweredValue::register_(term->type(), global, FunctionalBuilder::struct_value(rewriter.context(), values, term->location()));
        } else {
          return LoweredValue(term->type(), global, entries);
        }
      }
      
      static LoweredValue union_value_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<UnionValue>& term) {
        LoweredType type = rewriter.rewrite_type(term->type());
        LoweredValue inner = rewriter.rewrite_value(term->value());
        if (type.register_type()) {
          return LoweredValue::register_(term->type(), inner.global() && type.global(),
                                         FunctionalBuilder::union_value(type.register_type(), inner.register_value(), term->location()));
        } else {
          return LoweredValue(term->type(), inner.global() && type.global(), std::vector<LoweredValue>(1, inner));
        }
      }
      
      static LoweredValue outer_ptr_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<OuterPtr>& term) {
        ValuePtr<PointerType> ptr_ty = value_cast<PointerType>(term->pointer()->type());
        LoweredValueRegister inner_ptr = rewriter.rewrite_value_register(term->pointer());
        ValuePtr<> base = FunctionalBuilder::pointer_cast(inner_ptr.value,
                                                          FunctionalBuilder::byte_type(rewriter.context(), term->location()),
                                                          term->location());
        
        ValuePtr<UpwardReference> up = dyn_unrecurse<UpwardReference>(ptr_ty->upref());
        ValuePtr<> offset;
        bool global = inner_ptr.global;
        if (ValuePtr<StructType> struct_ty = dyn_unrecurse<StructType>(up->outer_type())) {
          LoweredValueRegister st_offset = rewriter.rewrite_value_register(FunctionalBuilder::struct_element_offset(struct_ty, size_to_unsigned(up->index()), term->location()));
          offset = st_offset.value;
          global = global && st_offset.global;
        } else if (ValuePtr<ArrayType> array_ty = dyn_unrecurse<ArrayType>(up)) {
          LoweredValueRegister idx = rewriter.rewrite_value_register(up->index());
          LoweredType el_type = rewriter.rewrite_type(array_ty->element_type());
          offset = FunctionalBuilder::mul(idx.value, el_type.size(), term->location());
          global = global && idx.global && el_type.global();
        } else if (ValuePtr<UnionType> union_ty = dyn_unrecurse<UnionType>(up)) {
          return LoweredValue::register_(term->type(), global, base);
        } else {
          throw TvmInternalError("Upward reference cannot be unfolded");
        }
        
        offset = FunctionalBuilder::neg(offset, term->location());
        return LoweredValue::register_(term->type(), global, FunctionalBuilder::pointer_offset(base, offset, term->location()));
      }

      /**
       * Get a pointer to an array element from an array pointer.
       * 
       * This is common to both array_element_rewrite() and element_ptr_rewrite().
       * 
       * \param unchecked_array_ty Array type. This must not have been lowered.
       * \param base Pointer to array. This must have already been lowered.
       */
      static LoweredValueRegister array_ptr_offset(AggregateLoweringRewriter& rewriter, const ValuePtr<>& unchecked_array_ty, const LoweredValueRegister& base, const LoweredValueRegister& index, const SourceLocation& location) {
        ValuePtr<ArrayType> array_ty = dyn_unrecurse<ArrayType>(unchecked_array_ty);
        if (!array_ty)
          throw TvmUserError("array type argument did not evaluate to an array type");

        LoweredType array_ty_l = rewriter.rewrite_type(array_ty);
        if (array_ty_l.register_type()) {
          ValuePtr<> array_ptr = FunctionalBuilder::pointer_cast(base.value, array_ty_l.register_type(), location);
          return LoweredValueRegister(base.global && index.global && array_ty_l.global(),
                                      FunctionalBuilder::element_ptr(array_ptr, index.value, location));
        }

        LoweredType element_ty = rewriter.rewrite_type(array_ty->element_type());
        if (element_ty.register_type()) {
          ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(base.value, element_ty.register_type(), location);
          return LoweredValueRegister(base.global && index.global && element_ty.global(),
                                      FunctionalBuilder::pointer_offset(cast_ptr, index.value, location));
        }

        LoweredValueRegister element_size = rewriter.rewrite_value_register(FunctionalBuilder::type_size(array_ty->element_type(), location));
        ValuePtr<> offset = FunctionalBuilder::mul(element_size.value, index.value, location);
        PSI_ASSERT(base.value->type() == FunctionalBuilder::byte_pointer_type(rewriter.context(), location));
        return LoweredValueRegister(base.global && index.global && element_size.global,
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
      
      static LoweredValue array_element_rewrite_split(AggregateLoweringRewriter& rewriter, const LoweredValueRegister& index, const LoweredValue::EntryVector& entries, const SourceLocation& location) {
        LoweredType ty = rewriter.rewrite_type(entries.front().type());
        if (ty.register_type()) {
          std::map<unsigned, ValuePtr<> > values;
          bool global = index.global;
          ValuePtr<> zero;
          for (std::size_t ii = 0, ie = entries.size(); ii != ie; ++ii) {
            const LoweredValue& entry = entries[ii];
            global = global && entry.global();
            switch (entry.mode()) {
            case LoweredValue::mode_register:
              values[ii] = entry.register_value();
              break;
              
            case LoweredValue::mode_zero:
              if (!zero)
                zero = FunctionalBuilder::zero(ty.register_type(), location);
              values[ii] = zero;
              break;
              
            case LoweredValue::mode_undefined:
              break;
              
            default: PSI_FAIL("unexpected enum value");
            }
          }
          ValuePtr<> undef_value = FunctionalBuilder::undef(ty.register_type(), location);
          return LoweredValue::register_(entries.front().type(), global,
                                         array_element_select(rewriter, index.value, undef_value, values, location));
        } else if (ty.split()) {
          bool global = index.global;
          LoweredValue::EntryVector split_result;
          std::vector<LoweredValue> component_entries;
          for (std::size_t ii = 0, ie = entries.front().entries().size(); ii != ie; ++ii) {
            component_entries.clear();
            for (LoweredValue::EntryVector::const_iterator ji = entries.begin(), je = entries.end(); ji != je; ++ji) {
              PSI_ASSERT(ii < ji->entries().size());
              component_entries.push_back(ji->entries()[ii]);
            }
            split_result.push_back(array_element_rewrite_split(rewriter, index, component_entries, location));
            global = global && split_result.back().global();
          }
          return LoweredValue(entries.front().type(), global, split_result);
        } else {
          // Single values on the stack
          std::map<unsigned, ValuePtr<> > values;
          ValuePtr<> zero;
          bool global = index.global;
          for (std::size_t ii = 0, ie = entries.size(); ii != ie; ++ii) {
            const LoweredValue& entry = entries[ii];
            global = global && entry.global();
            switch (entry.mode()) {
            case LoweredValue::mode_stack:
              values[ii] = entry.register_value();
              break;
              
            case LoweredValue::mode_zero:
              if (!zero)
                zero = rewriter.store_value(FunctionalBuilder::zero(entries.front().type(), location), location);
              values[ii] = zero;
              break;
              
            case LoweredValue::mode_undefined:
              break;
              
            default: PSI_FAIL("unexpected enum value");
            }
          }
          ValuePtr<> undef_value = zero ? zero : values[0];
          return LoweredValue::stack(entries.front().type(), global, array_element_select(rewriter, index.value, undef_value, values, location));
        }
      }

      static LoweredValue array_element_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementValue>& term) {
        LoweredValueRegister index = rewriter.rewrite_value_register(term->aggregate());
        LoweredValue array_val = rewriter.rewrite_value(term->aggregate());
        LoweredType el_type = rewriter.rewrite_type(term->type());
        switch (array_val.mode()) {
        case LoweredValue::mode_register:
          return LoweredValue::register_(term->type(), index.global && array_val.global(),
                                         FunctionalBuilder::element_value(array_val.register_value(), index.value, term->location()));
          
        case LoweredValue::mode_stack: {
          LoweredValueRegister element_ptr = array_ptr_offset(rewriter, term->aggregate()->type(),
                                                              LoweredValueRegister(array_val.global(), array_val.stack_value()),
                                                              index, term->location());
          return rewriter.load_value(el_type, element_ptr, term->location());
        }
        
        case LoweredValue::mode_split: {
          switch (array_val.entries().size()) {
          case 0: return LoweredValue::undefined(el_type);
          case 1: return array_val.entries().front();
          default: return array_element_rewrite_split(rewriter, index, array_val.entries(), term->location());
          }
        }
        
        case LoweredValue::mode_zero: return LoweredValue::zero(el_type);
        case LoweredValue::mode_undefined: return LoweredValue::undefined(el_type);
          
        default: PSI_FAIL("unexpected enum value");
        }
      }
      
      static LoweredValue array_element_ptr_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementPtr>& term) {
        LoweredValueRegister array_ptr = rewriter.rewrite_value_register(term->aggregate_ptr());
        LoweredValueRegister index = rewriter.rewrite_value_register(term->index());
        
        ValuePtr<PointerType> pointer_type = dyn_unrecurse<PointerType>(term->aggregate_ptr()->type());
        if (!pointer_type)
          throw TvmUserError("array_ep argument did not evaluate to a pointer");
        
        LoweredValueRegister result = array_ptr_offset(rewriter, pointer_type->target_type(), array_ptr, index, term->location());
        return LoweredValue::register_(term->type(), result.global, result.value);
      }
      
      /**
       * Get a pointer to an struct element from a struct pointer.
       * 
       * This is common to both struct_element_rewrite() and element_ptr_rewrite().
       * 
       * \param base Pointer to struct. This must have already been lowered.
       */
      static LoweredValueRegister struct_ptr_offset(AggregateLoweringRewriter& rewriter, const ValuePtr<>& unchecked_struct_ty, const LoweredValueRegister& base, unsigned index, const SourceLocation& location) {
        ValuePtr<StructType> struct_ty = dyn_unrecurse<StructType>(unchecked_struct_ty);
        if (!struct_ty)
          throw TvmInternalError("struct type value did not evaluate to a struct type");

        LoweredType struct_ty_rewritten = rewriter.rewrite_type(struct_ty);
        if (struct_ty_rewritten.register_type()) {
          ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(base.value, struct_ty_rewritten.register_type(), location);
          return LoweredValueRegister(base.global && struct_ty_rewritten.global(), FunctionalBuilder::element_ptr(cast_ptr, index, location));
        }
        
        PSI_ASSERT(base.value->type() == FunctionalBuilder::byte_pointer_type(rewriter.context(), location));
        LoweredValueRegister offset = rewriter.rewrite_value_register(FunctionalBuilder::struct_element_offset(struct_ty, index, location));
        return LoweredValueRegister(base.global && offset.global, FunctionalBuilder::pointer_offset(base.value, offset.value, location));
      }
      
      static LoweredValue struct_element_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementValue>& term) {
        LoweredValue struct_val = rewriter.rewrite_value(term->aggregate());
        unsigned idx = size_to_unsigned(term->index());
        switch (struct_val.mode()) {
        case LoweredValue::mode_register:
          return LoweredValue::register_(term->type(), struct_val.global(),
                                         FunctionalBuilder::element_value(struct_val.register_value(), idx, term->location()));
          
        case LoweredValue::mode_stack: {
          LoweredValueRegister result = struct_ptr_offset(rewriter, term->aggregate()->type(),
                                                          LoweredValueRegister(struct_val.global(), struct_val.stack_value()),
                                                          idx, term->location());
          return LoweredValue::stack(term->type(), result.global, result.value);
        }
        
        case LoweredValue::mode_split:
          return struct_val.entries()[idx];
          
        default:
          PSI_FAIL("unexpected enum value");
        }
      }

      static LoweredValue struct_element_ptr_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementPtr>& term) {
        LoweredValueRegister struct_ptr = rewriter.rewrite_value_register(term->aggregate_ptr());
        
        ValuePtr<PointerType> pointer_type = dyn_unrecurse<PointerType>(term->aggregate_ptr()->type());
        if (!pointer_type)
          throw TvmUserError("struct_ep argument did not evaluate to a pointer");
        
        LoweredValueRegister result = struct_ptr_offset(rewriter, pointer_type->target_type(), struct_ptr, size_to_unsigned(term->index()), term->location());
        return LoweredValue::register_(term->type(), result.global, result.value);
      }
      
      static LoweredValue struct_element_offset_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<StructElementOffset>& term) {
        ValuePtr<StructType> struct_ty = dyn_unrecurse<StructType>(term->struct_type());
        if (!struct_ty)
          throw TvmUserError("struct_eo argument did not evaluate to a struct type");

        ElementOffsetGenerator gen(&rewriter, term->location());
        for (unsigned ii = 0, ie = term->index(); ii != ie; ++ii)
          gen.next(struct_ty->member_type(ii));
        
        return LoweredValue::primitive(gen.global(), gen.offset());
      }

      static LoweredValue union_element_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementValue>& term) {
        LoweredValue union_val = rewriter.rewrite_value(term->aggregate());
        switch (union_val.mode()) {
        case LoweredValue::mode_register:
          return LoweredValue::register_(term->type(), union_val.global(),
                                         FunctionalBuilder::element_value(union_val.register_value(), term->index(), term->location()));
          
        case LoweredValue::mode_stack:
          return LoweredValue::stack(term->type(), union_val.global(), union_val.stack_value());
          
        case LoweredValue::mode_split: {
          const LoweredValue& element = union_val.entries().front();
          
          // If we're getting the existing element value, then just
          if (element.type() == term->type())
            return element;
          
          if (element.mode() == LoweredValue::mode_stack) {
            /**
             * \todo Is pointer adjustment necessary here? There's no guarantee that the alignment
             * of the union and the inner value are the same.
             */
            return LoweredValue::stack(term->type(), union_val.global(), union_val.stack_value());
          } else {
            return rewriter.bitcast(term->type(), element, term->location());
          }
        }
          
        default: PSI_FAIL("unexpected enum value");
        }
      }

      static LoweredValue union_element_ptr_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementPtr>& term) {
        LoweredValueRegister result = rewriter.rewrite_value_register(term->aggregate_ptr());
        return LoweredValue::register_(term->type(), result.global, result.value);
      }

      static LoweredValue metatype_size_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<MetatypeSize>& term) {
        LoweredType t = rewriter.rewrite_type(term->parameter());
        return LoweredValue::primitive(t.global(), t.size());
      }
      
      static LoweredValue metatype_alignment_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<MetatypeAlignment>& term) {
        LoweredType t = rewriter.rewrite_type(term->parameter());
        return LoweredValue::primitive(t.global(), t.alignment());
      }
      
      static LoweredValue pointer_offset_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<PointerOffset>& term) {
        LoweredValueRegister base_value = rewriter.rewrite_value_register(term->pointer());
        LoweredValueRegister offset = rewriter.rewrite_value_register(term->offset());
        
        LoweredType ty = rewriter.rewrite_type(term->pointer_type()->target_type());
        if (ty.register_type() && !rewriter.pass().pointer_arithmetic_to_bytes) {
          ValuePtr<> cast_base = FunctionalBuilder::pointer_cast(base_value.value, ty.register_type(), term->location());
          ValuePtr<> ptr = FunctionalBuilder::pointer_offset(cast_base, offset.value, term->location());
          ValuePtr<> result = FunctionalBuilder::pointer_cast(ptr, FunctionalBuilder::byte_type(rewriter.context(), term->location()), term->location());
          return LoweredValue::register_(term->type(), ty.global() && base_value.global && offset.global, result);
        } else {
          ValuePtr<> new_offset = FunctionalBuilder::mul(ty.size(), offset.value, term->location());
          ValuePtr<> result = FunctionalBuilder::pointer_offset(base_value.value, new_offset, term->location());
          return LoweredValue::register_(term->type(), ty.global() && base_value.global && offset.global, result);
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

      static LoweredValue zero_value_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ZeroValue>& term) {
        return LoweredValue::zero(rewriter.rewrite_type(term->type()));
      }
      
      static LoweredValue undefined_value_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<UndefinedValue>& term) {
        return LoweredValue::undefined(rewriter.rewrite_type(term->type()));
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
          .add<ArrayValue>(array_value_rewrite)
          .add<StructValue>(struct_value_rewrite)
          .add<UnionValue>(union_value_rewrite)
          .add<StructElementOffset>(struct_element_offset_rewrite)
          .add<MetatypeSize>(metatype_size_rewrite)
          .add<MetatypeAlignment>(metatype_alignment_rewrite)
          .add<PointerOffset>(pointer_offset_rewrite)
          .add<PointerCast>(pointer_cast_rewrite)
          .add<Unwrap>(unwrap_rewrite)
          .add<ApplyValue>(apply_rewrite)
          .add<ElementValue>(element_value_rewrite)
          .add<ElementPtr>(element_ptr_rewrite)
          .add<ZeroValue>(zero_value_rewrite)
          .add<UndefinedValue>(undefined_value_rewrite);
      }
    };

    AggregateLoweringPass::FunctionalTermRewriter::CallbackMap
      AggregateLoweringPass::FunctionalTermRewriter::callback_map(AggregateLoweringPass::FunctionalTermRewriter::callback_map_initializer());

    struct AggregateLoweringPass::InstructionTermRewriter {
      static LoweredValue return_rewrite(FunctionRunner& runner, const ValuePtr<Return>& term) {
        return LoweredValue::primitive(false, runner.pass().target_callback->lower_return(runner, term->value, term->location()));
      }

      static LoweredValue br_rewrite(FunctionRunner& runner, const ValuePtr<UnconditionalBranch>& term) {
        ValuePtr<Block> target = runner.rewrite_block(term->target);
        return LoweredValue::primitive(false, runner.builder().br(target, term->location()));
      }

      static LoweredValue cond_br_rewrite(FunctionRunner& runner, const ValuePtr<ConditionalBranch>& term) {
        ValuePtr<> cond = runner.rewrite_value_register(term->condition).value;
        ValuePtr<> true_target = runner.rewrite_value_register(term->true_target).value;
        ValuePtr<> false_target = runner.rewrite_value_register(term->false_target).value;
        return LoweredValue::primitive(false, runner.builder().cond_br(cond, value_cast<Block>(true_target), value_cast<Block>(false_target), term->location()));
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
        if (type.register_type()) {
          stack_ptr = runner.builder().alloca_(type.register_type(), count, alignment, term->location());
        } else {
          ValuePtr<> total_size = runner.rewrite_value_register(FunctionalBuilder::type_size(term->element_type, term->location())).value;
          if (count)
            total_size = FunctionalBuilder::mul(count, total_size, term->location());
          ValuePtr<> total_alignment = runner.rewrite_value_register(FunctionalBuilder::type_alignment(term->element_type, term->location())).value;
          if (alignment)
            total_alignment = FunctionalBuilder::max(total_alignment, alignment, term->location());
          stack_ptr = runner.builder().alloca_(FunctionalBuilder::byte_type(runner.context(), term->location()), total_size, total_alignment, term->location());
        }
        ValuePtr<> cast_stack_ptr = FunctionalBuilder::pointer_cast(stack_ptr, FunctionalBuilder::byte_type(runner.context(), term->location()), term->location());
        return LoweredValue::primitive(false, cast_stack_ptr);
      }
      
      static LoweredValue load_rewrite(FunctionRunner& runner, const ValuePtr<Load>& term) {
        LoweredType ty = runner.rewrite_type(term->type());
        LoweredValueRegister ptr = runner.rewrite_value_register(term->target);
        return runner.load_value(ty, ptr, term->location());
      }
      
      static LoweredValue store_rewrite(FunctionRunner& runner, const ValuePtr<Store>& term) {
        ValuePtr<> ptr = runner.rewrite_value_register(term->target).value;
        runner.store_value(term->value, ptr, term->location());
        return LoweredValue();
      }
      
      static LoweredValue memcpy_rewrite(FunctionRunner& runner, const ValuePtr<MemCpy>& term) {
        ValuePtr<> dest = runner.rewrite_value_register(term->dest).value;
        ValuePtr<> src = runner.rewrite_value_register(term->src).value;
        ValuePtr<> count = runner.rewrite_value_register(term->count).value;
        ValuePtr<> alignment = runner.rewrite_value_register(term->alignment).value;
        
        ValuePtr<> original_element_type = value_cast<PointerType>(term->dest->type())->target_type();
        LoweredType element_type = runner.rewrite_type(original_element_type);
        if (element_type.register_type()) {
          ValuePtr<> dest_cast = FunctionalBuilder::pointer_cast(dest, element_type.register_type(), term->location());
          ValuePtr<> src_cast = FunctionalBuilder::pointer_cast(dest, element_type.register_type(), term->location());
          return LoweredValue::primitive(false, runner.builder().memcpy(dest_cast, src_cast, count, alignment, term->location()));
        } else {
          PSI_ASSERT(dest->type() == FunctionalBuilder::byte_pointer_type(runner.context(), term->location()));
          ValuePtr<> type_size = runner.rewrite_value_register(FunctionalBuilder::type_size(original_element_type, term->location())).value;
          ValuePtr<> type_alignment = runner.rewrite_value_register(FunctionalBuilder::type_alignment(original_element_type, term->location())).value;
          ValuePtr<> bytes = FunctionalBuilder::mul(count, type_size, term->location());
          ValuePtr<> max_alignment = FunctionalBuilder::max(alignment, type_alignment, term->location());
          return LoweredValue::primitive(false, runner.builder().memcpy(dest, src, bytes, max_alignment, term->location()));
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
    LoweredValueRegister AggregateLoweringPass::AggregateLoweringRewriter::rewrite_value_register(const ValuePtr<>& value) {
      LoweredValue v = rewrite_value(value);
      PSI_ASSERT((v.mode() == LoweredValue::mode_register) && v.register_value());
      return LoweredValueRegister(v.global(), v.register_value());
    }
    
    /**
     * Utility function which runs rewrite_value and asserts that the resulting
     * value is on the stack and is non-NULL.
     */
    LoweredValueRegister AggregateLoweringPass::AggregateLoweringRewriter::rewrite_value_ptr(const ValuePtr<>& value) {
      LoweredValue v = rewrite_value(value);
      PSI_ASSERT((v.mode() == LoweredValue::mode_stack) && v.stack_value());
      return LoweredValueRegister(v.global(), v.stack_value());
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
    LoweredValueRegister AggregateLoweringPass::AggregateLoweringRewriter::lookup_value_register(const ValuePtr<>& value) {
      LoweredValue v = lookup_value(value);
      PSI_ASSERT((v.mode() == LoweredValue::mode_register) && v.register_value());
      return LoweredValueRegister(v.global(), v.register_value());
    }
    
    /**
     * Utility function which runs lookup_value and asserts that the resulting
     * value is not on the stack and is non-NULL.
     */
    LoweredValueRegister AggregateLoweringPass::AggregateLoweringRewriter::lookup_value_ptr(const ValuePtr<>& value) {
      LoweredValue v = lookup_value(value);
      PSI_ASSERT(!v.empty());
      PSI_ASSERT((v.mode() == LoweredValue::mode_stack) && v.stack_value());
      return LoweredValueRegister(v.global(), v.stack_value());
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
      if (metatype.register_type()) {
        ptr = builder().alloca_(metatype.register_type(), location);
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
    void AggregateLoweringPass::FunctionRunner::store_value(const ValuePtr<>& value, const ValuePtr<>& ptr, const SourceLocation& location) {
      if (isa<UndefinedValue>(value))
        return;
      
      LoweredType value_type = rewrite_type(value->type());
      if (value_type.register_type()) {
        ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(ptr, value_type.register_type(), location);
        ValuePtr<> stack_value = rewrite_value_register(value).value;
        builder().store(stack_value, cast_ptr, location);
      }
      
      if (isa<ZeroValue>(value)) {
        builder().memzero(ptr, value_type.size(), location);
        return;
      }

      if (ValuePtr<ArrayValue> array_val = dyn_cast<ArrayValue>(value)) {
        LoweredType element_type = rewrite_type(array_val->element_type());
        if (element_type.register_type()) {
          ValuePtr<> base_ptr = FunctionalBuilder::pointer_cast(ptr, element_type.register_type(), location);
          for (unsigned i = 0, e = array_val->length(); i != e; ++i) {
            ValuePtr<> element_ptr = FunctionalBuilder::pointer_offset(base_ptr, i, location);
            store_value(array_val->value(i), element_ptr, location);
          }
        } else {
          PSI_ASSERT(ptr->type() == FunctionalBuilder::byte_pointer_type(context(), location));
          ValuePtr<> element_size = rewrite_value_register(FunctionalBuilder::type_size(array_val->element_type(), location)).value;
          ValuePtr<> element_ptr = ptr;
          for (unsigned i = 0, e = array_val->length(); i != e; ++i) {
            store_value(array_val->value(i), element_ptr, location);
            element_ptr = FunctionalBuilder::pointer_offset(element_ptr, element_size, location);
          }
        }
      } else if (ValuePtr<UnionValue> union_val = dyn_cast<UnionValue>(value)) {
        return store_value(union_val->value(), ptr, location);
      }
      
      if (ValuePtr<StructType> struct_ty = dyn_cast<StructType>(value->type())) {
        PSI_ASSERT(ptr->type() == FunctionalBuilder::byte_pointer_type(context(), location));
        for (unsigned i = 0, e = struct_ty->n_members(); i != e; ++i) {
          ValuePtr<> offset = rewrite_value_register(FunctionalBuilder::struct_element_offset(struct_ty, i, location)).value;
          ValuePtr<> member_ptr = FunctionalBuilder::pointer_offset(ptr, offset, location);
          store_value(FunctionalBuilder::element_value(value, i, location), member_ptr, location);
        }
      }

      if (ValuePtr<ArrayType> array_ty = dyn_cast<ArrayType>(value->type())) {
        LoweredType element_type = rewrite_type(array_ty->element_type());
        if (element_type.register_type()) {
          ValuePtr<> value_ptr = rewrite_value_ptr(value).value;
          ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(ptr, element_type.register_type(), location);
          builder().memcpy(cast_ptr, value_ptr, array_ty->length(), location);
        }
      }

      PSI_ASSERT(ptr->type() == FunctionalBuilder::byte_pointer_type(context(), location));
      ValuePtr<> value_ptr = rewrite_value_ptr(value).value;
      PSI_ASSERT(value_ptr->type() == FunctionalBuilder::byte_pointer_type(context(), location));
      ValuePtr<> value_size = rewrite_value_register(FunctionalBuilder::type_size(value->type(), location)).value;
      ValuePtr<> value_alignment = rewrite_value_register(FunctionalBuilder::type_alignment(value->type(), location)).value;
      builder().memcpy(ptr, value_ptr, value_size, value_alignment, location);
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
        m_value_map.insert(std::make_pair(*ii, LoweredValue::primitive(false, new_block)));
      }
      
      // Jump from prolog block to entry block
      InstructionBuilder(prolog_block).br(rewrite_block(old_function()->blocks().front()), prolog_block->location());
      
      // Generate PHI nodes and convert instructions!
      for (std::vector<std::pair<ValuePtr<Block>, ValuePtr<Block> > >::iterator ii = sorted_blocks.begin(), ie = sorted_blocks.end(); ii != ie; ++ii) {
        const ValuePtr<Block>& old_block = ii->first;
        const ValuePtr<Block>& new_block = ii->second;

        // Generate PHI nodes
        for (Block::PhiList::const_iterator ji = old_block->phi_nodes().begin(), je = old_block->phi_nodes().end(); ji != je; ++ji)
          create_phi_node(new_block, rewrite_type((*ji)->type()), (*ji)->location());

        // Create instructions
        m_builder.set_insert_point(new_block);
        for (Block::InstructionList::const_iterator ji = old_block->instructions().begin(), je = old_block->instructions().end(); ji != je; ++ji) {
          const ValuePtr<Instruction>& insn = *ji;
          LoweredValue value = InstructionTermRewriter::callback_map.call(*this, insn);
          if (!value.empty())
            m_value_map.insert(std::make_pair(insn, value));
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
      if (new_type.register_type()) {
        ValuePtr<> alloca_insn = builder().alloca_(new_type.register_type(), location);
        return FunctionalBuilder::pointer_cast(alloca_insn, byte_type, location);
      }
      
      if (ValuePtr<ArrayType> array_ty = dyn_cast<ArrayType>(type)) {
        LoweredType element_type = rewrite_type(array_ty->element_type());
        if (element_type.register_type()) {
          ValuePtr<> alloca_insn = builder().alloca_(element_type.register_type(), array_ty->length(), location);
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
#if 0
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
#else
      PSI_NOT_IMPLEMENTED();
#endif
    }
    
    LoweredValue AggregateLoweringPass::FunctionRunner::load_value(const LoweredType& type, const LoweredValueRegister& ptr, const SourceLocation& location) {
      if (type.global() && ptr.global)
        return pass().global_rewriter().load_value(type, ptr, location);
      else
        return load_value(type, ptr.value, location);
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
      if (type.primitive()) {
        ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(ptr, type.register_type(), location);
        ValuePtr<> load_insn = builder().load(cast_ptr, location);
        return LoweredValue::primitive(false, load_insn);
      } else if (type.split()) {
        const LoweredType::EntryVector& ty_entries = type.entries();
        LoweredValue::EntryVector val_entries;
        ElementOffsetGenerator gen(this, location);
        ValuePtr<> byte_ptr = FunctionalBuilder::pointer_cast(ptr, FunctionalBuilder::byte_pointer_type(context(), location), location);
        for (LoweredType::EntryVector::const_iterator ii = ty_entries.begin(), ie = ty_entries.end(); ii != ie; ++ii) {
          gen.next(*ii);
          val_entries.push_back(load_value(*ii, FunctionalBuilder::pointer_offset(byte_ptr, gen.offset(), location), location));
        }
        return LoweredValue(type.origin(), false, val_entries);
      } else {
        // So this type cannot be loaded: memcpy it to the stack
        ValuePtr<> target_ptr = create_storage(type, location);
        builder().memcpy(target_ptr, ptr, type.size(), type.alignment(), location);
        return LoweredValue::stack(type.origin(), false, target_ptr);
      }
    }
    
    LoweredType AggregateLoweringPass::FunctionRunner::rewrite_type(const ValuePtr<>& type) {
      LoweredType global_lookup = pass().global_rewriter().lookup_type(type);
      if (!global_lookup.empty())
        return global_lookup;
      
      const LoweredType *lookup = m_type_map.lookup(type);
      if (lookup)
        return *lookup;
      
      LoweredType result;
      if (ValuePtr<FunctionalValue> func_type = dyn_cast<FunctionalValue>(type)) {
        result = TypeTermRewriter::callback_map.call(*this, func_type);
      } else {
        result = TypeTermRewriter::parameter_type_rewrite(*this, type);
      }
      
      if (result.global()) {
        pass().global_rewriter().m_type_map.insert(std::make_pair(type, result));
      } else {
        PSI_ASSERT(!result.empty());
        m_type_map.insert(std::make_pair(type, result));
        return result;
      }
    }
    
    LoweredValue AggregateLoweringPass::FunctionRunner::rewrite_value(const ValuePtr<>& value_orig) {
      LoweredValue global_lookup = pass().global_rewriter().lookup_value(value_orig);
      if (!global_lookup.empty())
        return global_lookup;

      ValuePtr<> value = unrecurse(value_orig);
      
      const LoweredValue *lookup = m_value_map.lookup(value);
      if (lookup) {
        // Not all values in the value map are necessarily valid - instructions which do not
        // produce a value have NULL entries. However, if the value is used, it must be valid.
        PSI_ASSERT(!lookup->empty());
        return *lookup;
      }

      LoweredValue result = FunctionalTermRewriter::callback_map.call(*this, value_cast<FunctionalValue>(value));
      
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
      if (type.primitive()) {
        ValuePtr<Phi> new_phi = block->insert_phi(type.register_type(), location);
        return LoweredValue::register_(type.origin(), false, new_phi);
      } else if (type.split()) {
        const LoweredType::EntryVector& entries = type.entries();
        LoweredValue::EntryVector value_entries;
        for (LoweredType::EntryVector::const_iterator ii = entries.begin(), ie = entries.end(); ii != ie; ++ii)
          value_entries.push_back(create_phi_node(block, *ii, location));
        return LoweredValue(type.origin(), false, value_entries);
      } else {
        ValuePtr<Phi> new_phi = block->insert_phi(FunctionalBuilder::byte_pointer_type(context(), location), location);
        m_generated_phi_terms[type.origin()][block].user.push_back(new_phi);
        return LoweredValue::stack(type.origin(), false, new_phi);
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
      if (type.register_type()) {
        ValuePtr<Phi> new_phi = value_cast<Phi>(lookup_value_register(phi_term).value);
        for (unsigned ii = 0, ie = incoming_edges.size(); ii != ie; ++ii)
          new_phi->add_edge(incoming_edges[ii].block, rewrite_value_register(incoming_edges[ii].value).value);
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
        ValuePtr<Phi> new_phi = value_cast<Phi>(lookup_value_ptr(phi_term).value);
        for (unsigned ii = 0, ie = incoming_edges.size(); ii != ie; ++ii)
          new_phi->add_edge(incoming_edges[ii].block, rewrite_value_ptr(incoming_edges[ii].value).value);
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
        if (new_type.register_type()) {
          for (unsigned ji = 0, je = entry_data.free_.size(); ji != je; ++ji)
            entry_data.free_[ji] = FunctionalBuilder::pointer_cast(builder().alloca_(new_type.register_type(), type_block->location()), byte_type, type_block->location());
          goto slots_created;
        }
        
        if (ValuePtr<ArrayType> array_ty = dyn_cast<ArrayType>(ii->first)) {
          LoweredType element_type = rewrite_type(array_ty->element_type());
          if (element_type.register_type()) {
            for (unsigned ji = 0, je = entry_data.free_.size(); ji != je; ++ji)
              entry_data.free_[ji] = FunctionalBuilder::pointer_cast(builder().alloca_(element_type.register_type(), array_ty->length(), type_block->location()), byte_type, type_block->location());
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
    
    LoweredValue AggregateLoweringPass::ModuleLevelRewriter::load_value(const ValuePtr<>& load_term, const LoweredValueRegister& ptr, const SourceLocation& location) {
      PSI_ASSERT(ptr.global);

      ValuePtr<> origin = ptr.value;
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
      const LoweredType *lookup = m_type_map.lookup(type);
      if (lookup)
        return *lookup;
      
      if (ValuePtr<Exists> exists = dyn_cast<Exists>(type)) {
        if (!isa<PointerType>(exists->result()))
          throw TvmUserError("Value of type exists is not a pointer");
        return rewrite_type(FunctionalBuilder::byte_pointer_type(context(), exists->location()));
      }
      
      LoweredType result = TypeTermRewriter::callback_map.call(*this, value_cast<FunctionalValue>(type));
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
        throw TvmUserError("Global from a different module encountered during lowering");
      } else if (isa<FunctionType>(value)) {
        throw TvmUserError("Function type encountered in computed expression");
      } else if (!isa<FunctionalValue>(value)) {
        throw TvmUserError("Non-functional value encountered in global expression: probably instruction or block which has not been inserted into a function");
      }
      
      LoweredValue result = FunctionalTermRewriter::callback_map.call(*this, value_cast<FunctionalValue>(value));
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
      std::pair<ValuePtr<>,ValuePtr<> > padding_type = target_callback->type_from_alignment(alignment);
      ValuePtr<> count = FunctionalBuilder::div(FunctionalBuilder::sub(size, status.size, location), padding_type.second, location);
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
      if (value_ty.register_type())
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
      if (value_ty.register_type()) {
        LoweredValueRegister rewritten_value = m_global_rewriter.rewrite_value_register(value);
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
    remove_only_unknown(false),
    remove_all_unions(false),
    remove_register_arrays(false),
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
          
          if (old_var->alignment()) {
            LoweredValueRegister old_align = m_global_rewriter.rewrite_value_register(old_var->alignment());
            PSI_ASSERT(old_align.global);
            new_var->set_alignment(FunctionalBuilder::max(status.alignment, old_align.value, term->location()));
          } else {
            new_var->set_alignment(status.alignment);
          }

          ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(new_var, byte_type, term->location());
          global_rewriter().m_value_map.insert(std::make_pair(old_var, LoweredValue::primitive(true, cast_ptr)));
          rewrite_globals.push_back(std::make_pair(old_var, new_var));
        } else {
          ValuePtr<Function> old_function = value_cast<Function>(term);
          boost::shared_ptr<FunctionRunner> runner(new FunctionRunner(this, old_function));
          ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(runner->new_function(), byte_type, term->location());
          global_rewriter().m_value_map.insert(std::make_pair(old_function, LoweredValue::primitive(true, cast_ptr)));
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
