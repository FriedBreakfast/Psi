#include "Target.hpp"

#include "../Aggregate.hpp"
#include "../Number.hpp"

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
      class TargetFixes_AMD64 : public TargetFixes {
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
	static uint64_t align_to(uint64_t size, uint64_t align) {
	  PSI_ASSERT(align && !(align & (align - 1)));
	  return (size + align - 1) & ~(align - 1);
	}

	/**
	 * Get the type used to pass a parameter of a given class with a
	 * given size in bytes.
	 */
	static const llvm::Type* type_from_amd64_class_and_size(ConstantBuilder& builder, AMD64_Class amd64_class, uint64_t size) {
	  switch (amd64_class) {
	  case amd64_sse:
	    switch (size) {
	    case 4:  return llvm::Type::getFloatTy(builder.llvm_context());
	    case 8:  return llvm::Type::getDoubleTy(builder.llvm_context());
	    case 16: return llvm::Type::getFP128Ty(builder.llvm_context());
	    default: PSI_FAIL("unknown SSE floating point type width");
	    }

	  case amd64_x87:
	    PSI_ASSERT(size == 16);
	    return llvm::Type::getX86_FP80Ty(builder.llvm_context());

	  case amd64_integer:
	    // check size is a power of two
	    PSI_ASSERT((size > 0) && (size <= 16) && !(size & (size - 1)));
	    return llvm::IntegerType::get(builder.llvm_context(), size*8);

	  default:
	    PSI_FAIL("unexpected amd64 parameter class here");
	  }
	}

	/**
	 * Construct an ElementTypeInfo object for a type which is a
	 * single EVT in LLVM, and is accurately represented by this
	 * type.
	 */
	static ElementTypeInfo primitive_element_info(ConstantBuilder& builder, const llvm::Type *ty, AMD64_Class amd_class) {
	  return ElementTypeInfo(TargetParameterCategory::simple, amd_class, builder.type_size(ty), builder.type_alignment(ty), 1);
	}

	/**
	 * Compute element type info for a sub-part of the object.
	 */
	static ElementTypeInfo get_element_info(ConstantBuilder& builder, Term *element) {
	  if (StructType::Ptr struct_ty = dyn_cast<StructType>(element)) {
	    std::vector<const llvm::Type*> child_types;
	    bool child_types_valid = true;
	    TargetParameterCategory category = TargetParameterCategory::simple;
	    uint64_t size = 0, align = 1;
	    unsigned n_elements = 0;
	    AMD64_Class amd64_class = amd64_no_class;
	    for (unsigned i = 0, e = struct_ty->n_members(); i != e; ++i) {
	      ElementTypeInfo child = get_element_info(builder, struct_ty->member_type(i));
	      n_elements += child.n_elements;
	      size = align_to(size, child.align);
	      size += child.size;
	      align = std::max(align, child.align);
	      amd64_class = merge_amd64_class(amd64_class, child.amd64_class);
	      category = TargetParameterCategory::merge(category, child.category);
	    }

	    const llvm::Type *type = NULL;
	    if (child_types_valid)
	      type = llvm::StructType::get(builder.llvm_context(), child_types);

	    size = align_to(size, align);
	    return ElementTypeInfo(category, amd64_class, size, align, n_elements);
	  } else if (ArrayType::Ptr array_ty = dyn_cast<ArrayType>(element)) {
	    ElementTypeInfo child = get_element_info(builder, array_ty->element_type());
	    uint64_t length = builder.build_constant_integer(array_ty->length()).getZExtValue();
	    child.size *= length;
	    child.n_elements *= length;
	    return child;
	  } else if (UnionType::Ptr union_ty = dyn_cast<UnionType>(element)) {
	    TargetParameterCategory category = TargetParameterCategory::altered;
	    uint64_t size = 0, align = 1;
	    unsigned n_elements = 0;
	    AMD64_Class amd64_class = amd64_no_class;
	    for (unsigned i = 0, e = union_ty->n_members(); i != e; ++i) {
	      ElementTypeInfo child = get_element_info(builder, union_ty->member_type(i));
	      n_elements = std::max(n_elements, child.n_elements);
	      size = std::max(size, child.size);
	      align = std::max(align, child.align);
	      amd64_class = merge_amd64_class(amd64_class, child.amd64_class);
	      category = TargetParameterCategory::merge(category, child.category);
	    }

	    size = align_to(size, align);
	    return ElementTypeInfo(category, amd64_class, size, align, n_elements);
	  } else if (PointerType::Ptr ptr_ty = dyn_cast<PointerType>(element)) {
	    return primitive_element_info(builder, builder.get_pointer_type(), amd64_integer);
	  } else if (FloatType::Ptr float_ty = dyn_cast<FloatType>(element)) {
	    return primitive_element_info(builder, builder.get_float_type(float_ty->width()), amd64_integer);
	  } else if (BooleanType::Ptr bool_ty = dyn_cast<BooleanType>(element)) {
	    return primitive_element_info(builder, builder.get_boolean_type(), amd64_integer);
	  } else if (IntegerType::Ptr int_ty = dyn_cast<IntegerType>(element)) {
	    return primitive_element_info(builder, builder.get_integer_type(int_ty->width()), amd64_integer);
	  } else {
	    PSI_ASSERT_MSG(!dyn_cast<FunctionTypeParameterTerm>(element) && !dyn_cast<FunctionParameterTerm>(element),
			   "low-level parameter type should not depend on function type parameters");
	    PSI_FAIL("unknown type");
	  }
	}

	static ElementTypeInfo get_parameter_info(ConstantBuilder& builder, Term *type) {
	  ElementTypeInfo result = get_element_info(builder, type);

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
	  virtual boost::shared_ptr<TargetCommon::ParameterHandler> parameter_type_info(ConstantBuilder& builder, llvm::CallingConv::ID cconv, Term *type) const {
	    ElementTypeInfo info = get_parameter_info(builder, type);
	    switch (info.category) {
	    case TargetParameterCategory::simple:
	      return TargetCommon::parameter_handler_simple(builder, type, cconv);

	    case TargetParameterCategory::altered: {
	      const llvm::Type *llvm_type = type_from_amd64_class_and_size(builder, info.amd64_class, info.size);
	      return TargetCommon::parameter_handler_change_type_by_memory(type, llvm_type, cconv);
	    }

	    case TargetParameterCategory::force_ptr:
	      return TargetCommon::parameter_handler_force_ptr(builder, type, cconv);

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
	  virtual bool convention_supported(llvm::CallingConv::ID id) const {
	    return TargetCommon::convention_always_supported(id);
	  }
	};

	FunctionCallCommonCallback m_function_call_callback;
	TargetCommon m_function_call_common;

      public:
	TargetFixes_AMD64() : m_function_call_common(&m_function_call_callback) {
	}

	PSI_TVM_LLVM_TARGET_FUNCTION_CALL_COMMON_FORWARD(m_function_call_common)
      };

      /**
       * \brief Create TargetFixes instance for the AMD64 platform.
       *
       * \see TargetFixes_AMD64
       */
      boost::shared_ptr<TargetFixes> create_target_fixes_amd64() {
	return boost::make_shared<TargetFixes_AMD64>();
      }
    }
  }
}
