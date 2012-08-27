#include "Target.hpp"

#include "../Aggregate.hpp"
#include "../Number.hpp"
#include "../FunctionalBuilder.hpp"

#include <boost/make_shared.hpp>

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      /**
       * \brief Target specific fixes for X86-64 on platforms using
       * the AMD64 ABI.
       *
       * In practise, this means every OS except Windows.
       *
       * There's no point really in trying to reverse-engineer
       * everything LLVM is doing. Just implement most of the ABI
       * right here.
       *
       * \see <a
       * href="http://x86-64.org/documentation/abi.pdf">System V
       * Application Binary Interface AMD64 Architecture Processor
       * Supplement</a>
       */
      class TargetFixes_AMD64_AggregateLowering : public TargetCommon {
        /**
         * Used to classify how each parameter should be passed (or
         * returned).
         */
        enum AMD64_Class {
          amd64_integer,
          amd64_sse,
          //amd64_sse_up,
          amd64_x87,
          //amd64_x87_up,
          amd64_no_class,
          amd64_memory
        };

        /**
         * Get the parameter class resulting from two separate
         * classes. Described on page 19 of the ABI.
         */
        static AMD64_Class merge_amd64_class(AMD64_Class left, AMD64_Class right) {
          if (left == right) {
            return left;
          } else if (left == amd64_no_class) {
            return right;
          } else if (right == amd64_no_class) {
            return left;
          } else if ((left == amd64_memory) || (right == amd64_memory)) {
            return amd64_memory;
          } else if ((left == amd64_integer) || (right == amd64_integer)) {
            return amd64_integer;
          } else {
            return amd64_sse;
          }
        }

        struct ElementTypeInfo {
          ElementTypeInfo(TargetParameterCategory category_, AMD64_Class amd64_class_, uint64_t size_, uint64_t align_, unsigned n_elements_)
            : category(category_), amd64_class(amd64_class_), size(size_), align(align_), n_elements(n_elements_) {}

          TargetParameterCategory category;
          AMD64_Class amd64_class;
          uint64_t size;
          uint64_t align;
          unsigned n_elements;
        };

        /**
         * Return the smallest value greater than \c size which is a
         * multiple of \c align, which must be a power of two.
         */
        uint64_t align_to(uint64_t size, uint64_t align) {
          PSI_ASSERT(align && !(align & (align - 1)));
          return (size + align - 1) & ~(align - 1);
        }

        /**
         * Get the type used to pass a parameter of a given class with a
         * given size in bytes.
         */
        ValuePtr<> type_from_amd64_class_and_size(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, AMD64_Class amd64_class, uint64_t size, const SourceLocation& location) {
          switch (amd64_class) {
          case amd64_sse: {
            FloatType::Width width;
            switch (size) {
            case 4:  width = FloatType::fp32; break;
            case 8:  width = FloatType::fp64; break;
            case 16: width = FloatType::fp128; break;
            default: PSI_FAIL("unknown SSE floating point type width");
            }
            return FunctionalBuilder::float_type(rewriter.context(), width, location);
          }

          case amd64_x87:
            PSI_ASSERT(size == 16);
            return FunctionalBuilder::float_type(rewriter.context(), FloatType::fp_x86_80, location);

          case amd64_integer: {
            IntegerType::Width width;
            switch (size) {
            case 1:  width = IntegerType::i8; break;
            case 2:  width = IntegerType::i16; break;
            case 4:  width = IntegerType::i32; break;
            case 8:  width = IntegerType::i64; break;
            case 16: width = IntegerType::i128; break;
            default: PSI_FAIL("unknown integer width in AMD64 parameter passing");
            }
            return FunctionalBuilder::int_type(rewriter.context(), width, false, location);
          }

          default:
            PSI_FAIL("unexpected amd64 parameter class here");
          }
        }

        /**
         * Construct an ElementTypeInfo object for a type which is a
         * single EVT in LLVM, and is accurately represented by this
         * type.
         */
        ElementTypeInfo primitive_element_info(const ValuePtr<>& type, AMD64_Class amd_class) {
          TargetCommon::TypeSizeAlignmentLiteral size_align = type_size_alignment_literal(type);
          return ElementTypeInfo(TargetParameterCategory::simple, amd_class, size_align.size, size_align.alignment, 1);
        }

        /**
         * Compute element type info for a sub-part of the object.
         */
        ElementTypeInfo get_element_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<>& element) {
          if (ValuePtr<StructType> struct_ty = dyn_cast<StructType>(element)) {
            TargetParameterCategory category = TargetParameterCategory::simple;
            uint64_t size = 0, align = 1;
            unsigned n_elements = 0;
            AMD64_Class amd64_class = amd64_no_class;
            for (unsigned i = 0, e = struct_ty->n_members(); i != e; ++i) {
              ElementTypeInfo child = get_element_info(rewriter, struct_ty->member_type(i));
              n_elements += child.n_elements;
              size = align_to(size, child.align);
              size += child.size;
              align = std::max(align, child.align);
              amd64_class = merge_amd64_class(amd64_class, child.amd64_class);
              category = TargetParameterCategory::merge(category, child.category);
            }

            size = align_to(size, align);
            return ElementTypeInfo(category, amd64_class, size, align, n_elements);
          } else if (ValuePtr<ArrayType> array_ty = dyn_cast<ArrayType>(element)) {
            ElementTypeInfo child = get_element_info(rewriter, array_ty->element_type());
            ValuePtr<IntegerValue> length = value_cast<IntegerValue>(rewriter.rewrite_value_stack(array_ty->length()));
            boost::optional<unsigned> length_val = length->value().unsigned_value();
            if (!length_val)
              throw BuildError("array length value out of range");
            child.size *= *length_val;
            child.n_elements *= *length_val;
            return child;
          } else if (ValuePtr<UnionType> union_ty = dyn_cast<UnionType>(element)) {
            TargetParameterCategory category = TargetParameterCategory::altered;
            uint64_t size = 0, align = 1;
            unsigned n_elements = 0;
            AMD64_Class amd64_class = amd64_no_class;
            for (unsigned i = 0, e = union_ty->n_members(); i != e; ++i) {
              ElementTypeInfo child = get_element_info(rewriter, union_ty->member_type(i));
              n_elements = std::max(n_elements, child.n_elements);
              size = std::max(size, child.size);
              align = std::max(align, child.align);
              amd64_class = merge_amd64_class(amd64_class, child.amd64_class);
              category = TargetParameterCategory::merge(category, child.category);
            }

            size = align_to(size, align);
            return ElementTypeInfo(category, amd64_class, size, align, n_elements);
          } else if (isa<PointerType>(element) || isa<BooleanType>(element) || isa<IntegerType>(element)) {
            return primitive_element_info(element, amd64_integer);
          } else if (ValuePtr<FloatType> float_ty = dyn_cast<FloatType>(element)) {
            return primitive_element_info(element, (float_ty->width() != FloatType::fp_x86_80) ? amd64_sse : amd64_x87);
          } else if (isa<EmptyType>(element)) {
            return ElementTypeInfo(TargetParameterCategory::simple, amd64_no_class, 0, 1, 0);
          } else if (isa<Metatype>(element)) {
            ValuePtr<> size_type = FunctionalBuilder::size_type(element->context(), element->location());
            ValuePtr<> metatype_struct = FunctionalBuilder::struct_type(element->context(), std::vector<ValuePtr<> >(2, size_type), element->location());
            return get_element_info(rewriter, metatype_struct);
          } else {
            PSI_ASSERT_MSG(!dyn_cast<ParameterPlaceholder>(element) && !dyn_cast<FunctionParameter>(element),
                           "low-level parameter type should not depend on function type parameters");
            PSI_FAIL("unknown type");
          }
        }

        ElementTypeInfo get_parameter_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<>& type) {
          ElementTypeInfo result = get_element_info(rewriter, type);

          switch (result.amd64_class) {
          case amd64_sse:
          case amd64_x87:
            if (result.n_elements > 1)
              result.amd64_class = amd64_memory;
            break;

          case amd64_integer:
            if (result.size > 16) {
              // LLVM should handle this fine, so just set the AMD64 class
              result.amd64_class = amd64_memory;
            } else if (result.n_elements > 2) {
              // more than two elements means that it will not be passed
              // as 2xi64 in two integer registers, so we must re-pack it.
              result.category = TargetParameterCategory::altered;
            } else if ((result.n_elements == 2) && (result.size < 16)) {
              PSI_ASSERT(result.size <= 8);
              // In this case there are two elements, but they fit
              // into one 64-bit register so must be packed.
              result.category = TargetParameterCategory::altered;
            } else {
              PSI_ASSERT(result.category != TargetParameterCategory::force_ptr);
            }
            break;

          case amd64_memory:
            break;

          case amd64_no_class:
            PSI_ASSERT(!result.size && !result.n_elements);
            break;
          }

          return result;
        }

        struct FunctionCallCommonCallback : TargetCommon::Callback {
          TargetFixes_AMD64_AggregateLowering *self;
          FunctionCallCommonCallback(TargetFixes_AMD64_AggregateLowering *self_) : self(self_) {}

          /**
           * Special handling is required in the following cases:
           *
           * <ul>
           *
           * <li>Unions with both float and integer elements in the same
           * eightbyte must be passed in memory but the LLVM type system
           * does not support having this explained to it.</li>
           *
           * <li>Union types which can be passed as parameters must be
           * mapped to a type of equivalent size and alignment since LLVM
           * does not understand unions.</li>
           *
           * <li>Eightbytes with only integer elements should be packed into
           * integer registers regardless of exactly what types those
           * elements are; LLVM considers each one as a separate
           * parameter.</li>
           *
           * </ul>
           */
          virtual boost::shared_ptr<ParameterHandler> parameter_type_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, CallingConvention cconv, const ValuePtr<>& type) const {
            ElementTypeInfo info = self->get_parameter_info(rewriter, type);
            switch (info.category) {
            case TargetParameterCategory::simple:
              return TargetCommon::parameter_handler_simple(rewriter, type, cconv);

            case TargetParameterCategory::altered: {
              ValuePtr<> lowered_type = self->type_from_amd64_class_and_size(rewriter, info.amd64_class, info.size, type->location());
              return TargetCommon::parameter_handler_change_type_by_memory(type, lowered_type, cconv);
            }

            case TargetParameterCategory::force_ptr:
              return TargetCommon::parameter_handler_force_ptr(rewriter.context(), type, cconv);

            default:
              PSI_FAIL("unknown parameter category");
            }
          }

          /**
           * Whether the convention is supported on X86-64. Currently this
           * is the C calling convention only, other calling conventions
           * will probably require different custom code. Note that this
           * does not count x86-specific conventions, assuming that they are
           * 32-bit.
           */
          virtual bool convention_supported(CallingConvention id) const {
            return id == cconv_c;
          }
        };

        FunctionCallCommonCallback m_function_call_callback;
        boost::shared_ptr<llvm::TargetMachine> m_target_machine;

      public:
        TargetFixes_AMD64_AggregateLowering(llvm::LLVMContext *context, const boost::shared_ptr<llvm::TargetMachine>& target_machine)
        : TargetCommon(&m_function_call_callback, context, target_machine->getTargetData()),
        m_function_call_callback(this),
        m_target_machine(target_machine) {
        }
      };
      
      class TargetFixes_AMD64 : public TargetCallback {
        TargetFixes_AMD64_AggregateLowering m_aggregate_lowering_callback;
        
      public:
        TargetFixes_AMD64(llvm::LLVMContext *context, const boost::shared_ptr<llvm::TargetMachine>& target_machine)
        : m_aggregate_lowering_callback(context, target_machine) {
        }

        virtual AggregateLoweringPass::TargetCallback* aggregate_lowering_callback() {
          return &m_aggregate_lowering_callback;
        }
        
        virtual llvm::Function* exception_personality_routine(llvm::Module *module, const std::string& basename) {
          return target_exception_personality_linux(module, basename);
        }
      };

      /**
       * \brief Create TargetFixes instance for the AMD64 platform.
       *
       * \see TargetFixes_AMD64
       */
      boost::shared_ptr<TargetCallback> create_target_fixes_amd64(llvm::LLVMContext *context, const boost::shared_ptr<llvm::TargetMachine>& target_machine) {
        return boost::make_shared<TargetFixes_AMD64>(context, target_machine);
      }
    }
  }
}
