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
#include <queue>
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
        LoweredValueSimple length = rewriter.rewrite_value_register(term->length());
        LoweredType element_type = rewriter.rewrite_type(term->element_type());
        ValuePtr<> size = FunctionalBuilder::mul(length.value, element_type.size(), term->location());
        ValuePtr<> alignment = element_type.alignment();
        
        if (element_type.global() && isa<IntegerValue>(length.value)) {
          PSI_ASSERT(length.global);
          
          if (!rewriter.pass().split_arrays && (element_type.mode() == LoweredType::mode_register)) {
            ValuePtr<> register_type = FunctionalBuilder::array_type(element_type.register_type(), length.value, term->location());

            if (!rewriter.pass().remove_sizeof) {
              size = FunctionalBuilder::type_size(register_type, term->location());
              alignment = FunctionalBuilder::type_alignment(register_type, term->location());
            }
            
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

          if (!rewriter.pass().remove_sizeof) {
            size = FunctionalBuilder::type_size(register_type, term->location());
            alignment = FunctionalBuilder::type_alignment(register_type, term->location());
          }
          
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
          register_members.push_back(member_type.register_type());

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
          
          if (!rewriter.pass().remove_sizeof) {
            size = FunctionalBuilder::type_size(register_type, term->location());
            alignment = FunctionalBuilder::type_alignment(register_type, term->location());
          }
          
          return LoweredType::register_(term, size, alignment, register_type);
        } else {
          return LoweredType::union_(term, size, alignment);
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
        return LoweredType::constant_(term, rewriter.rewrite_value(term->value()));
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
          .add<BlockType>(primitive_type_rewrite)
          .add<BooleanType>(primitive_type_rewrite)
          .add<ByteType>(primitive_type_rewrite)
          .add<EmptyType>(primitive_type_rewrite)
          .add<FloatType>(primitive_type_rewrite)
          .add<IntegerType>(primitive_type_rewrite)
          .add<ConstantType>(constant_type_rewrite);
      }
    };
    
    AggregateLoweringPass::TypeTermRewriter::CallbackMap
      AggregateLoweringPass::TypeTermRewriter::callback_map(AggregateLoweringPass::TypeTermRewriter::callback_map_initializer());
      
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

      static LoweredValue default_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionalValue>& term) {

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
          
        if (el_type.register_type()) {
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
        
        if (st_type.register_type()) {
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
        LoweredValue inner = rewriter.rewrite_value(term->value());
        if (type.mode() == LoweredType::mode_register) {
          return LoweredValue::register_(type, inner.global(),
                                         FunctionalBuilder::union_value(type.register_type(), inner.register_value(), term->location()));
        } else {
          PSI_ASSERT(type.mode() == LoweredType::mode_union);
          return LoweredValue::union_(type, inner);
        }
      }
      
      static LoweredValue outer_ptr_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<OuterPtr>& term) {
        ValuePtr<PointerType> inner_ptr_ty = value_cast<PointerType>(term->pointer()->type());
        LoweredValueSimple inner_ptr = rewriter.rewrite_value_register(term->pointer());
        LoweredType outer_ptr_ty = rewriter.rewrite_type(term->type());
        ValuePtr<> base = FunctionalBuilder::pointer_cast(inner_ptr.value,
                                                          FunctionalBuilder::byte_type(rewriter.context(), term->location()),
                                                          term->location());
        
        ValuePtr<UpwardReference> up = dyn_unrecurse<UpwardReference>(inner_ptr_ty->upref());
        ValuePtr<> offset;
        bool global = inner_ptr.global && outer_ptr_ty.global();
        if (ValuePtr<StructType> struct_ty = dyn_unrecurse<StructType>(up->outer_type())) {
          LoweredValueSimple st_offset = rewriter.rewrite_value_register(FunctionalBuilder::struct_element_offset(struct_ty, size_to_unsigned(up->index()), term->location()));
          offset = st_offset.value;
          global = global && st_offset.global;
        } else if (ValuePtr<ArrayType> array_ty = dyn_unrecurse<ArrayType>(up)) {
          LoweredValueSimple idx = rewriter.rewrite_value_register(up->index());
          LoweredType el_type = rewriter.rewrite_type(array_ty->element_type());
          offset = FunctionalBuilder::mul(idx.value, el_type.size(), term->location());
          global = global && idx.global && el_type.global();
        } else if (ValuePtr<UnionType> union_ty = dyn_unrecurse<UnionType>(up)) {
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
        ValuePtr<ArrayType> array_ty = dyn_unrecurse<ArrayType>(unchecked_array_ty);
        if (!array_ty)
          throw TvmUserError("array type argument did not evaluate to an array type");

        LoweredType array_ty_l = rewriter.rewrite_type(array_ty);
        if (array_ty_l.register_type()) {
          ValuePtr<> array_ptr = FunctionalBuilder::pointer_cast(base.value, array_ty_l.register_type(), location);
          return LoweredValueSimple(base.global && index.global && array_ty_l.global(),
                                    FunctionalBuilder::element_ptr(array_ptr, index.value, location));
        }

        LoweredType element_ty = rewriter.rewrite_type(array_ty->element_type());
        if (element_ty.register_type()) {
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
          case 0: return rewriter.rewrite_value(FunctionalBuilder::undef(el_type.origin(), term->location()));
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
        
        ValuePtr<PointerType> pointer_type = dyn_unrecurse<PointerType>(term->aggregate_ptr()->type());
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
        ValuePtr<StructType> struct_ty = dyn_unrecurse<StructType>(unchecked_struct_ty);
        if (!struct_ty)
          throw TvmInternalError("struct type value did not evaluate to a struct type");

        LoweredType struct_ty_rewritten = rewriter.rewrite_type(struct_ty);
        if (struct_ty_rewritten.register_type()) {
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
        
        ValuePtr<PointerType> pointer_type = dyn_unrecurse<PointerType>(term->aggregate_ptr()->type());
        if (!pointer_type)
          throw TvmUserError("struct_ep argument did not evaluate to a pointer");
        
        LoweredValueSimple result = struct_ptr_offset(rewriter, pointer_type->target_type(), struct_ptr, size_to_unsigned(term->index()), term->location());
        return LoweredValue::register_(rewriter.rewrite_type(term->type()), result.global, result.value);
      }
      
      static LoweredValue struct_element_offset_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<StructElementOffset>& term) {
        ValuePtr<StructType> struct_ty = dyn_unrecurse<StructType>(term->struct_type());
        if (!struct_ty)
          throw TvmUserError("struct_eo argument did not evaluate to a struct type");

        ElementOffsetGenerator gen(&rewriter, term->location());
        for (unsigned ii = 0, ie = term->index(); ii != ie; ++ii)
          gen.next(struct_ty->member_type(ii));
        
        return LoweredValue::register_(rewriter.pass().size_type(), gen.global(), gen.offset());
      }

      static LoweredValue union_element_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementValue>& term) {
        LoweredType type = rewriter.rewrite_type(term->type());
        LoweredValue union_val = rewriter.rewrite_value(term->aggregate());
        if (union_val.mode() == LoweredValue::mode_register) {
          return LoweredValue::register_(type, union_val.global(),
                                        FunctionalBuilder::element_value(union_val.register_value(), term->index(), term->location()));
        } else {
          PSI_ASSERT(union_val.mode() == LoweredValue::mode_union);
          
          if (union_val.union_inner().type().origin() == term->type())
            return union_val.union_inner();
          
          return rewriter.bitcast(type, union_val, term->location());
        }
      }

      static LoweredValue union_element_ptr_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ElementPtr>& term) {
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
        if (ty.register_type() && !rewriter.pass().pointer_arithmetic_to_bytes) {
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
      
      static LoweredValue build_select(AggregateLoweringRewriter& rewriter, const LoweredValueSimple& cond, const LoweredValue& true_val, const LoweredValue& false_val, const SourceLocation& location) {
        const LoweredType& ty = true_val.type();
        switch (ty.mode()) {
        case LoweredType::mode_register:
          return LoweredValue::register_(ty, ty.global() && cond.global && true_val.global() && false_val.global(),
                                         FunctionalBuilder::select(cond.value, true_val.register_value(), false_val.register_value(), location));
          
        case LoweredType::mode_constant:
          return LoweredValue::constant(ty);
          
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
      
      static LoweredValue build_zero_undef(const LoweredType& ty, bool is_zero, const SourceLocation& location) {
        switch (ty.mode()) {
        case LoweredType::mode_register:
          return LoweredValue::register_(ty, true, is_zero ? FunctionalBuilder::zero(ty.register_type(), location) : FunctionalBuilder::undef(ty.register_type(), location));
          
        case LoweredType::mode_constant:
          return LoweredValue::constant(ty);
          
        case LoweredType::mode_split: {
          LoweredValue::EntryVector entries;
          for (LoweredType::EntryVector::const_iterator ii = ty.split_entries().begin(), ie = ty.split_entries().end(); ii != ie; ++ii)
            entries.push_back(build_zero_undef(*ii, is_zero, location));
          return LoweredValue::split(ty, entries);
        }
          
        case LoweredType::mode_blob:
          throw TvmUserError("Type unsupported by back-end cannot be used in register");
          
        default: PSI_FAIL("unexpected enum value");
        }
      }

      static LoweredValue zero_value_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<ZeroValue>& term) {
        LoweredType ty = rewriter.rewrite_type(term->type());
        return build_zero_undef(ty, true, term->location());
      }
      
      static LoweredValue undefined_value_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<UndefinedValue>& term) {
        LoweredType ty = rewriter.rewrite_type(term->type());
        return build_zero_undef(ty, false, term->location());
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
          .add<Select>(select_rewrite)
          .add<ZeroValue>(zero_value_rewrite)
          .add<UndefinedValue>(undefined_value_rewrite);
      }
    };

    AggregateLoweringPass::FunctionalTermRewriter::CallbackMap
      AggregateLoweringPass::FunctionalTermRewriter::callback_map(AggregateLoweringPass::FunctionalTermRewriter::callback_map_initializer());

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
        if (element_type.register_type()) {
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
        if (element_type.register_type()) {
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
        ValuePtr<ConstantType> cn = unrecurse_cast<ConstantType>(term->value->type());
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
          .add<Store>(store_rewrite)
          .add<Load>(load_rewrite)
          .add<MemCpy>(memcpy_rewrite)
          .add<MemZero>(memzero_rewrite)
          .add<Solidify>(solidify_rewrite);
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
          PSI_NOT_IMPLEMENTED(); // Update gen to current offset
          ValuePtr<> offset_ptr = FunctionalBuilder::pointer_offset(cast_ptr, gen.offset(), location);
          store_value(*ii, offset_ptr, location);
        }
        return;
      }
      
      case LoweredValue::mode_union: {
        // Known element of union
        store_value(value.union_inner(), ptr, location);
        return;
      }
      
      case LoweredValue::mode_constant: {
        PSI_FAIL("Not implemented");
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
        for (Block::PhiList::const_iterator ji = old_block->phi_nodes().begin(), je = old_block->phi_nodes().end(); ji != je; ++ji)
          create_phi_node(new_block, rewrite_type((*ji)->type()), (*ji)->location());

        // Create instructions
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
      if (type.register_type()) {
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
      
      case LoweredType::mode_union:
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
      }
      
      return result;
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

      LoweredValue result = FunctionalTermRewriter::callback_map.call(*this, unrecurse_cast<FunctionalValue>(value));
      
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
      case LoweredType::mode_constant:
        return LoweredValue::constant(type);
        
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
      case LoweredType::mode_union:
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
      case LoweredType::mode_constant:
        return;
        
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

      case LoweredType::mode_union:
      case LoweredType::mode_blob:
        throw TvmUserError("Phi nodes do not work with types not supported by the back-end");

      default: PSI_FAIL("unrecognised enum value");
      }
    }

    AggregateLoweringPass::ModuleLevelRewriter::ModuleLevelRewriter(AggregateLoweringPass *pass)
    : AggregateLoweringRewriter(pass) {
    }

    LoweredValue AggregateLoweringPass::ModuleLevelRewriter::bitcast(const LoweredType& type, const LoweredValue& input, const SourceLocation& location) {
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
      if (value_ty.register_type()) {
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
