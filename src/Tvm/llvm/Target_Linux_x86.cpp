#include "Target.hpp"

#include "../Aggregate.hpp"
#include "../Number.hpp"
#include "../Recursive.hpp"
#include "../FunctionalBuilder.hpp"

#include <boost/make_shared.hpp>

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      /**
       * X86 calling convention with GCC seems to work in a somewhat similar
       * way to X86-64, so I've cloned that code as a start.
       */
      class TargetFixes_Linux_X86_AggregateLowering : public TargetCommon {
        /**
         * Used to classify how each parameter should be passed (or
         * returned).
         */
        enum X86_Class {
          x86_integer,
          x86_sse,
          //x86_sse_up,
          x86_x87,
          //x86_x87_up,
          x86_no_class,
          x86_memory
        };

        static X86_Class merge_x86_class(X86_Class left, X86_Class right) {
          if (left == right) {
            return left;
          } else if (left == x86_no_class) {
            return right;
          } else if (right == x86_no_class) {
            return left;
          } else if ((left == x86_memory) || (right == x86_memory)) {
            return x86_memory;
          } else if ((left == x86_integer) || (right == x86_integer)) {
            return x86_integer;
          } else {
            return x86_sse;
          }
        }

        struct ElementTypeInfo {
          ElementTypeInfo(TargetParameterCategory category_, X86_Class x86_class_, uint64_t size_, uint64_t align_, unsigned n_elements_)
            : category(category_), x86_class(x86_class_), size(size_), align(align_), n_elements(n_elements_) {}

          TargetParameterCategory category;
          X86_Class x86_class;
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
        ValuePtr<> type_from_x86_class_and_size(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, X86_Class x86_class, uint64_t size, const SourceLocation& location) {
          switch (x86_class) {
          case x86_sse: {
            FloatType::Width width;
            switch (size) {
            case 4:  width = FloatType::fp32; break;
            case 8:  width = FloatType::fp64; break;
            case 16: width = FloatType::fp128; break;
            default: PSI_FAIL("unknown SSE floating point type width");
            }
            return FunctionalBuilder::float_type(rewriter.context(), width, location);
          }

          case x86_x87:
            PSI_ASSERT(size == 16);
            return FunctionalBuilder::float_type(rewriter.context(), FloatType::fp_x86_80, location);

          case x86_integer: {
            IntegerType::Width width;
            unsigned count = 1;
            switch (size) {
            case 1:  width = IntegerType::i8; break;
            case 2:  width = IntegerType::i16; break;
            case 4:  width = IntegerType::i32; break;
            case 8:  width = IntegerType::i32; count = 2; break;
            case 16: width = IntegerType::i32; count = 4; break;
            default: PSI_FAIL("unknown integer width in X86 parameter passing");
            }
            ValuePtr<> ty = FunctionalBuilder::int_type(rewriter.context(), width, false, location);
            if (count > 1)
              ty = FunctionalBuilder::array_type(ty, count, location);
            return ty;
          }

          default:
            PSI_FAIL("unexpected x86 parameter class here");
          }
        }

        /**
         * Construct an ElementTypeInfo object for a type which is a
         * single EVT in LLVM, and is accurately represented by this
         * type.
         */
        ElementTypeInfo primitive_element_info(const ValuePtr<>& type, X86_Class x86_class) {
          TypeSizeAlignment size_align = type_size_alignment(type);
          return ElementTypeInfo(TargetParameterCategory::simple, x86_class, size_align.size, size_align.alignment, 1);
        }

        /**
         * Compute element type info for a sub-part of the object.
         */
        ElementTypeInfo get_element_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<>& element_base) {
          ValuePtr<> element = rewriter.simplify_argument_type(element_base);
          
          if (ValuePtr<StructType> struct_ty = dyn_cast<StructType>(element)) {
            TargetParameterCategory category = TargetParameterCategory::simple;
            uint64_t size = 0, align = 1;
            unsigned n_elements = 0;
            X86_Class x86_class = x86_no_class;
            for (unsigned i = 0, e = struct_ty->n_members(); i != e; ++i) {
              ElementTypeInfo child = get_element_info(rewriter, struct_ty->member_type(i));
              n_elements += child.n_elements;
              size = align_to(size, child.align);
              size += child.size;
              align = std::max(align, child.align);
              x86_class = merge_x86_class(x86_class, child.x86_class);
              category = TargetParameterCategory::merge(category, child.category);
            }

            size = align_to(size, align);
            return ElementTypeInfo(category, x86_class, size, align, n_elements);
          } else if (ValuePtr<ArrayType> array_ty = dyn_cast<ArrayType>(element)) {
            ElementTypeInfo child = get_element_info(rewriter, array_ty->element_type());
            ValuePtr<IntegerValue> length = value_cast<IntegerValue>(rewriter.rewrite_value_register(array_ty->length()).value);
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
            X86_Class x86_class = x86_no_class;
            for (unsigned i = 0, e = union_ty->n_members(); i != e; ++i) {
              ElementTypeInfo child = get_element_info(rewriter, union_ty->member_type(i));
              n_elements = std::max(n_elements, child.n_elements);
              size = std::max(size, child.size);
              align = std::max(align, child.align);
              x86_class = merge_x86_class(x86_class, child.x86_class);
              category = TargetParameterCategory::merge(category, child.category);
            }

            size = align_to(size, align);
            return ElementTypeInfo(category, x86_class, size, align, n_elements);
          } else if (isa<PointerType>(element) || isa<BooleanType>(element) || isa<IntegerType>(element)) {
            return primitive_element_info(element, x86_integer);
          } else if (ValuePtr<FloatType> float_ty = dyn_cast<FloatType>(element)) {
            return primitive_element_info(element, (float_ty->width() != FloatType::fp_x86_80) ? x86_sse : x86_x87);
          } else {
            PSI_ASSERT_MSG(!dyn_cast<ParameterPlaceholder>(element) && !dyn_cast<FunctionParameter>(element),
                           "low-level parameter type should not depend on function type parameters");
            PSI_FAIL("unknown type");
          }
        }

        ElementTypeInfo get_parameter_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<>& type) {
          ElementTypeInfo result = get_element_info(rewriter, type);

          switch (result.x86_class) {
          case x86_sse:
          case x86_x87:
            if (result.n_elements > 1)
              result.x86_class = x86_memory;
            break;

          case x86_integer:
            if (result.size > 8) {
              // LLVM should handle this fine, so just set the X86 class
              result.x86_class = x86_memory;
              if (result.category == TargetParameterCategory::altered)
                result.category = TargetParameterCategory::force_ptr;
            } else if (result.n_elements > 2) {
              result.category = TargetParameterCategory::altered;
            } else if ((result.n_elements == 2) && (result.size < 8)) {
              PSI_ASSERT(result.size <= 4);
              // In this case there are two elements, but they fit
              // into one 32-bit register so must be packed.
              result.category = TargetParameterCategory::altered;
            } else {
              PSI_ASSERT(result.category != TargetParameterCategory::force_ptr);
            }
            break;

          case x86_memory:
            break;

          case x86_no_class:
            PSI_ASSERT(!result.size && !result.n_elements);
            break;
          }

          return result;
        }

        struct FunctionCallCommonCallback : TargetCommon::Callback {
          TargetFixes_Linux_X86_AggregateLowering *self;
          FunctionCallCommonCallback(TargetFixes_Linux_X86_AggregateLowering *self_) : self(self_) {}

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
          virtual boost::shared_ptr<ParameterHandler> parameter_type_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, CallingConvention, const ValuePtr<>& type) const {
            ElementTypeInfo info = self->get_parameter_info(rewriter, type);
            switch (info.category) {
            case TargetParameterCategory::simple:
              return TargetCommon::parameter_handler_simple(rewriter, type);

            case TargetParameterCategory::altered: {
              ValuePtr<> lowered_type = self->type_from_x86_class_and_size(rewriter, info.x86_class, info.size, type->location());
              return TargetCommon::parameter_handler_change_type_by_memory(rewriter, type, lowered_type);
            }

            case TargetParameterCategory::force_ptr:
              return TargetCommon::parameter_handler_force_ptr(rewriter, type);

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
        TargetFixes_Linux_X86_AggregateLowering(llvm::LLVMContext *context, const boost::shared_ptr<llvm::TargetMachine>& target_machine)
        : TargetCommon(&m_function_call_callback, context, target_machine->getDataLayout()),
        m_function_call_callback(this),
        m_target_machine(target_machine) {
        }
      };
      
      class TargetFixes_Linux_X86 : public TargetCallback {
        TargetFixes_Linux_X86_AggregateLowering m_aggregate_lowering_callback;
        
      public:
        TargetFixes_Linux_X86(llvm::LLVMContext *context, const boost::shared_ptr<llvm::TargetMachine>& target_machine)
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
       * \brief Create TargetFixes instance for the Linux_x86 platform.
       *
       * \see TargetFixes_Linux_x86
       */
      boost::shared_ptr<TargetCallback> create_target_fixes_linux_x86(llvm::LLVMContext *context, const boost::shared_ptr<llvm::TargetMachine>& target_machine) {
        return boost::make_shared<TargetFixes_Linux_X86>(context, target_machine);
      }
    }
  }
}

