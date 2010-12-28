#include "Builder.hpp"

#include "../Aggregate.hpp"

#include <boost/make_shared.hpp>
#include <boost/optional.hpp>
#include <boost/ref.hpp>

#include <llvm/Function.h>
#include <llvm/Target/TargetRegistry.h>

using namespace Psi;
using namespace Psi::Tvm;
using namespace Psi::Tvm::LLVM;

namespace {
  class ParameterCategory {
  public:
    enum Value {
      /// \brief This parameter is simple - it can be mapped to an
      /// LLVM type and LLVM handles passing this correctly.
      simple,
      /// \brief This parameter needs some platform specific work to
      /// be passed correctly.
      altered,
      /// \brief This parameter should be passed as a pointer, using
      /// space from alloca() and the normal mechanism for loading and
      /// storing types to and from memory.
      force_ptr
    };

    ParameterCategory(Value value) : m_value(value) {}
    operator Value () const {return m_value;}
    Value value() const {return m_value;}

  private:
    Value m_value;
  };

  /// Merge two parameter categories so the resulting category would
  /// correctly handle both input categories.
  ParameterCategory merge_category(ParameterCategory left, ParameterCategory right) {
    return std::max(left.value(), right.value());
  }

  class ParameterHandler {
  public:
    ParameterHandler(Term *type, const llvm::Type *llvm_type, llvm::CallingConv::ID calling_convention)
      : m_type(type), m_llvm_type(llvm_type), m_calling_convention(calling_convention) {
    }

    /// The type of term that this object was created to pass.
    Term *type() const {return m_type;}

    /// Type used to pass this parameter.
    const llvm::Type *llvm_type() const {return m_llvm_type;}

    /// The calling convention this parameter type was built for.
    llvm::CallingConv::ID calling_convention() const {return m_calling_convention;}

    /// \brief Whether this type should be returned via an extra sret
    /// parameter, which must be inserted manually since LLVM will not
    /// handle this case correctly.
    virtual bool return_by_sret() const = 0;

    /// \brief Convert a parameter to the correct type for passing.
    virtual llvm::Value* pack(FunctionBuilder& builder, Term *value) const = 0;

    /// \brief Convert a parameter from the passed type.
    virtual BuiltValue* unpack(FunctionBuilder& builder, llvm::Value *value) const = 0;

    /**
     * \brief Prepare for a call which returns by a custom sret.
     *
     * This should return NULL if this parameter type does not force
     * an sret return, that is if return_by_sret returns false,
     * otherwise it should always return a non-NULL value giving the
     * memory to use to store the sret return.
     */
    virtual llvm::Value* return_by_sret_setup(FunctionBuilder& builder) const = 0;

    /// \brief Generate code for returning a value from a function.
    virtual void return_pack(FunctionBuilder& builder, llvm::Function *llvm_function, Term *value) const = 0;

    /**
     * \brief Decode a value returned by a called function.
     * If
     * return_by_sret_setup returned a non-NULL value, this will be
     * passed in the last parameter. It is safe to assume that the
     * return value from return_by_sret_setup will always be passed
     * as the third parameter so it is not necessary to check whether
     * it is NULL.
     */
    virtual BuiltValue* return_unpack(FunctionBuilder& builder, llvm::Value *value, llvm::Value *sret_addr) const = 0;

  private:
    Term *m_type;
    const llvm::Type *m_llvm_type;
    llvm::CallingConv::ID m_calling_convention;
  };

  /**
   * A simple handler which just uses the LLVM default mechanism to pass each parameter.
   */
  class ParameterSimpleHandler : public ParameterHandler {
  public:
    ParameterSimpleHandler(ConstantBuilder& builder, Term *type, llvm::CallingConv::ID calling_convention)
      : ParameterHandler(type, builder.build_type(type), calling_convention) {
    }

    virtual bool return_by_sret() const {
      return false;
    }

    virtual llvm::Value* pack(FunctionBuilder& builder, Term *value) const {
      return builder.build_value_simple(value);
    }

    virtual BuiltValue* unpack(FunctionBuilder& builder, llvm::Value *value) const {
      return builder.new_function_value_simple(type(), value);
    }

    virtual llvm::Value* return_by_sret_setup(FunctionBuilder&) const {
      return NULL;
    }

    virtual void return_pack(FunctionBuilder& builder, llvm::Function*, Term *value) const {
      llvm::Value *llvm_value = builder.build_value_simple(value);
      builder.irbuilder().CreateRet(llvm_value);
    }

    virtual BuiltValue* return_unpack(FunctionBuilder& builder, llvm::Value *value, llvm::Value*) const {
      return builder.new_function_value_simple(type(), value);
    }
  };

  /**
   * A handler which converts the Tvm value to an LLVM value of a
   * specific type by writing it to memory on the stack and reading it
   * back.
   */
  class ParameterChangeTypeByMemoryHandler : public ParameterHandler {
  public:
    ParameterChangeTypeByMemoryHandler(Term *type, const llvm::Type *llvm_type, llvm::CallingConv::ID calling_convention)
      : ParameterHandler(type, llvm_type, calling_convention) {
    }

    virtual bool return_by_sret() const {
      return false;
    }

    virtual llvm::Value* pack(FunctionBuilder& builder, Term *value) const {
      PSI_FAIL("not implemented");
    }

    virtual BuiltValue* unpack(FunctionBuilder& builder, llvm::Value *value) const {
      PSI_FAIL("not implemented");
    }

    virtual llvm::Value* return_by_sret_setup(FunctionBuilder&) const {
      return NULL;
    }

    virtual void return_pack(FunctionBuilder& builder, llvm::Function *llvm_function, Term *value) const {
      PSI_FAIL("not implemented");
    }

    virtual BuiltValue* return_unpack(FunctionBuilder& builder, llvm::Value *value, llvm::Value*) const {
      PSI_FAIL("not implemented");
    }
  };

  /**
   * A handler which always passes the parameter as a pointer,
   * allocating storage when passing the parameter using alloca, and
   * returning by writing to the pointer in the first function
   * parameter.
   */
  class ParameterForcePtrHandler : public ParameterHandler {
  public:
    ParameterForcePtrHandler(ConstantBuilder& builder, Term *type, llvm::CallingConv::ID calling_convention)
      : ParameterHandler(type, llvm::Type::getInt8PtrTy(builder.llvm_context()), calling_convention) {
    }

    virtual bool return_by_sret() const {
      return true;
    }

    virtual llvm::Value* pack(FunctionBuilder& builder, Term *value) const {
      PSI_FAIL("not implemented");
    }

    virtual BuiltValue* unpack(FunctionBuilder& builder, llvm::Value *value) const {
      PSI_FAIL("not implemented");
    }

    virtual llvm::Value* return_by_sret_setup(FunctionBuilder& builder) const {
      PSI_FAIL("not implemented");
    }

    virtual void return_pack(FunctionBuilder& builder, llvm::Function*, Term *value) const {
      PSI_FAIL("not implemented");
    }

    virtual BuiltValue* return_unpack(FunctionBuilder& builder, llvm::Value *value, llvm::Value* sret_addr) const {
      PSI_FAIL("not implemented");
    }
  };

  /**
   * If target fixes can be handled entirely on a per-parameter
   * basis, this handles the general management of function
   * calls. Subclasses of this should implement different virtual
   * methods to those that subclass TargetFixes directly.
   *
   * Note that this also relies on LLVM handling sret parameters
   * (hidden parameters to functions which point to memory to
   * write the result to) correctly.
   */
  class TargetFixes_SimpleBase : public TargetFixes {
  protected:
  private:
    /// \brief Return information about how to pass this parameter.
    virtual boost::shared_ptr<ParameterHandler> parameter_type_info(ConstantBuilder& builder, llvm::CallingConv::ID cconv, Term *type) const = 0;
    
    /// \brief Checks whether a given calling convention actually
    /// makes sense for a given platform.
    virtual bool convention_supported(llvm::CallingConv::ID id) const = 0;

    /// Map from a Tvm calling convention identifier to an LLVM one.
    llvm::CallingConv::ID map_calling_convention(CallingConvention conv) const {
      llvm::CallingConv::ID id;
      switch (conv) {
      case cconv_c: id = llvm::CallingConv::C; break;
      case cconv_x86_stdcall: id = llvm::CallingConv::X86_StdCall; break;
      case cconv_x86_thiscall: id = llvm::CallingConv::X86_ThisCall; break;
      case cconv_x86_fastcall: id = llvm::CallingConv::X86_FastCall; break;

      default:
        throw BuildError("Unsupported calling convention");
      }

      if (!convention_supported(id))
        throw BuildError("Calling convention does not make sense for target platform");

      return id;
    }

  protected:
    /// \brief Check whether LLVM supported this convention on all
    /// platforms.
    bool convention_always_supported(llvm::CallingConv::ID id) const {
      switch (id) {
      case llvm::CallingConv::C:
      case llvm::CallingConv::Fast:
      case llvm::CallingConv::Cold:
      case llvm::CallingConv::GHC:
        return true;

      default:
        return false;
      }
    }

  public:
    virtual const llvm::FunctionType* function_type(ConstantBuilder& builder, FunctionTypeTerm *term) const {
      llvm::CallingConv::ID cconv = map_calling_convention(term->calling_convention());

      std::size_t n_phantom = term->n_phantom_parameters();
      std::size_t n_passed_parameters = term->n_parameters() - n_phantom;
      std::vector<const llvm::Type*> parameter_types;

      boost::shared_ptr<ParameterHandler> return_handler = parameter_type_info(builder, cconv, term->result_type());
      const llvm::Type *return_type;
      if (return_handler->return_by_sret()) {
	return_type = llvm::Type::getVoidTy(builder.llvm_context());
	parameter_types.push_back(return_handler->llvm_type());
      } else {
	return_type = return_handler->llvm_type();
      }

      for (std::size_t i = 0; i != n_passed_parameters; ++i) {
	boost::shared_ptr<ParameterHandler> handler = parameter_type_info(builder, cconv, term->parameter(i+n_phantom)->type());
	parameter_types.push_back(handler->llvm_type());
      }

      return llvm::FunctionType::get(return_type, parameter_types, false);
    }

    virtual BuiltValue* function_call(FunctionBuilder& builder, llvm::Value *target, FunctionTypeTerm *target_type, FunctionCall::Ptr insn) const {
      llvm::CallingConv::ID cconv = map_calling_convention(target_type->calling_convention());

      std::size_t n_phantom = target_type->n_phantom_parameters();
      std::size_t n_passed_parameters = target_type->n_parameters() - n_phantom;
      std::vector<const llvm::Type*> parameter_types;
      llvm::SmallVector<llvm::Value*, 4> parameters;

      boost::shared_ptr<ParameterHandler> return_handler = parameter_type_info(builder, cconv, target_type->result_type());

      const llvm::Type *return_type;
      llvm::Value *sret_addr = return_handler->return_by_sret_setup(builder);
      if (sret_addr) {
	return_type = llvm::Type::getVoidTy(builder.llvm_context());
	parameter_types.push_back(return_handler->llvm_type());
	parameters.push_back(sret_addr);
      } else {
	return_type = return_handler->llvm_type();
      }

      for (std::size_t i = 0; i != n_passed_parameters; ++i) {
	Term *param = insn->parameter(i + n_phantom);
	boost::shared_ptr<ParameterHandler> handler = parameter_type_info(builder, cconv, param->type());
	llvm::Value *value = handler->pack(builder, param);
	parameters.push_back(value);
	parameter_types.push_back(value->getType());
      }

      llvm::FunctionType *llvm_function_type = llvm::FunctionType::get(return_type, parameter_types, false);
      llvm::Value *cast_target = builder.irbuilder().CreateBitCast(target, llvm_function_type->getPointerTo());
      llvm::CallInst *call_insn = builder.irbuilder().CreateCall(cast_target, parameters.begin(), parameters.end());
      call_insn->setCallingConv(cconv);

      return return_handler->return_unpack(builder, call_insn, sret_addr);
    }

    virtual void function_parameters_unpack(FunctionBuilder& builder, FunctionTerm *function,
                                            llvm::Function *llvm_function, llvm::SmallVectorImpl<BuiltValue*>& result) const {
      llvm::CallingConv::ID cconv = map_calling_convention(function->function_type()->calling_convention());

      std::size_t n_phantom = function->function_type()->n_phantom_parameters();
      std::size_t n_passed_parameters = function->function_type()->n_parameters() - n_phantom;

      result.resize(n_passed_parameters);
      llvm::Function::arg_iterator jt = llvm_function->arg_begin();

      // Need to check if the first parameter is an sret.
      boost::shared_ptr<ParameterHandler> return_handler = parameter_type_info(builder, cconv, function->function_type()->result_type());
      if (return_handler->return_by_sret())
	++jt;

      PSI_ASSERT(n_passed_parameters + (return_handler->return_by_sret() ? 1 : 0) == llvm_function->getFunctionType()->getNumParams());
      for (std::size_t i = 0; i != n_passed_parameters; ++i, ++jt) {
	boost::shared_ptr<ParameterHandler> handler = parameter_type_info(builder, cconv, function->parameter(i + n_phantom)->type());
	result[i] = handler->unpack(builder, &*jt);
      }
    }

    virtual void function_return(FunctionBuilder& builder, FunctionTypeTerm *function_type, llvm::Function *llvm_function, Term *value) const {
      llvm::CallingConv::ID cconv = map_calling_convention(function_type->calling_convention());
      boost::shared_ptr<ParameterHandler> return_handler = parameter_type_info(builder, cconv, function_type->result_type());
      return_handler->return_pack(builder, llvm_function, value);
    }
  };

  /**
   * Simple default implementation - this assumes that everything
   * works correctly in LLVM.
   */
  class TargetFixes_Default : public TargetFixes_SimpleBase {
    virtual boost::shared_ptr<ParameterHandler> parameter_type_info(ConstantBuilder& builder, llvm::CallingConv::ID cconv, Term *type) const {
      return boost::make_shared<ParameterSimpleHandler>(boost::ref(builder), type, cconv);
    }

    virtual bool convention_supported(llvm::CallingConv::ID) const {
      return true;
    }
  };

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
  class TargetFixes_AMD64 : public TargetFixes_SimpleBase {
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
      ElementTypeInfo(ParameterCategory category_, AMD64_Class amd64_class_, uint64_t size_, uint64_t align_, unsigned n_elements_)
	: category(category_), amd64_class(amd64_class_), size(size_), align(align_), n_elements(n_elements_) {}

      ParameterCategory category;
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
     * Compute element type info for a sub-part of the object.
     */
    static ElementTypeInfo get_element_info(ConstantBuilder& builder, Term *element) {
      if (StructType::Ptr struct_ty = dyn_cast<StructType>(element)) {
	std::vector<const llvm::Type*> child_types;
	bool child_types_valid = true;
	ParameterCategory category = ParameterCategory::simple;
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
	  category = merge_category(category, child.category);
	}

	const llvm::Type *type = NULL;
	if (child_types_valid)
	  type = llvm::StructType::get(builder.llvm_context(), child_types);

	size = align_to(size, align);
	return ElementTypeInfo(category, amd64_class, size, align, n_elements);
      } else if (ArrayType::Ptr array_ty = dyn_cast<ArrayType>(element)) {
	ElementTypeInfo child = get_element_info(builder, array_ty->element_type());
	llvm::APInt length = builder.build_constant_integer(array_ty->length());
	child.size *= length.getZExtValue();
	return child;
      } else if (UnionType::Ptr union_ty = dyn_cast<UnionType>(element)) {
	ParameterCategory category = ParameterCategory::altered;
	uint64_t size = 0, align = 1;
	unsigned n_elements = 0;
	AMD64_Class amd64_class = amd64_no_class;
	for (unsigned i = 0, e = union_ty->n_members(); i != e; ++i) {
	  ElementTypeInfo child = get_element_info(builder, union_ty->member_type(i));
	  n_elements = std::max(n_elements, child.n_elements);
	  size = std::max(size, child.size);
	  align = std::max(align, child.align);
	  amd64_class = merge_amd64_class(amd64_class, child.amd64_class);
	  category = merge_category(category, child.category);
	}

	size = align_to(size, align);
	return ElementTypeInfo(category, amd64_class, size, align, n_elements);
      } else if (FloatType::Ptr float_ty = dyn_cast<FloatType>(element)) {
	const llvm::Type *ty = builder.get_float_type(float_ty->width());
	return ElementTypeInfo(ParameterCategory::simple, amd64_sse, builder.type_size(ty), builder.type_alignment(ty), 1);
      } else if (IntegerType::Ptr int_ty = dyn_cast<IntegerType>(element)) {
	const llvm::Type *ty = builder.get_integer_type(int_ty->width());
	return ElementTypeInfo(ParameterCategory::simple, amd64_integer, builder.type_size(ty), builder.type_alignment(ty), 1);
      } else {
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
	PSI_ASSERT(result.category == ParameterCategory::simple);
	if (result.size > 16) {
	  // LLVM should handle this fine, so just set the AMD64 class
	  result.amd64_class = amd64_memory;
	} else if (result.n_elements > 2) {
	  // more than two elements means that it will not be passed
	  // as 2xi8 in two integer registers, so we must re-pack it.
	  result.category = ParameterCategory::altered;
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
    virtual boost::shared_ptr<ParameterHandler> parameter_type_info(ConstantBuilder& builder, llvm::CallingConv::ID cconv, Term *type) const {
      ElementTypeInfo info = get_parameter_info(builder, type);
      switch (info.category) {
      case ParameterCategory::simple:
	return boost::make_shared<ParameterSimpleHandler>(boost::ref(builder), type, cconv);

      case ParameterCategory::altered: {
	const llvm::Type *llvm_type = type_from_amd64_class_and_size(builder, info.amd64_class, info.size);
	return boost::make_shared<ParameterChangeTypeByMemoryHandler>(type, llvm_type, cconv);
      }

      case ParameterCategory::force_ptr:
	return boost::make_shared<ParameterForcePtrHandler>(boost::ref(builder), type, cconv);

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
      return convention_always_supported(id);
    }
  };
}

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      /**
       * Get the machine-specific set of LLVM workarounds for a given
       * machine. If no such workaround are available, this returns a
       * dummy class, but that may well break in some cases.
       *
       * \param triple An LLVM target triple, which will be parsed
       * using the llvm::Triple class.
       */
      boost::shared_ptr<TargetFixes> create_target_fixes(const std::string& triple) {
	llvm::Triple parsed_triple(triple);

	switch (parsed_triple.getArch()) {
	case llvm::Triple::x86_64:
	  switch (parsed_triple.getOS()) {
	  case llvm::Triple::Linux: return boost::make_shared<TargetFixes_AMD64>();
	  default: break;
	  }
	  break;

	default:
	  break;
	}

#if 0
        return boost::make_shared<TargetFixes_Default>();
#else
	throw BuildError("Target " + triple + " not supported");
#endif
      }
    }
  }
}
