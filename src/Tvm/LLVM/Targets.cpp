#include "Builder.hpp"

#include <boost/make_shared.hpp>

#include <llvm/Function.h>
#include <llvm/Target/TargetRegistry.h>

using namespace Psi;
using namespace Psi::Tvm;
using namespace Psi::Tvm::LLVM;

namespace {
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
    /** \brief Whether we need to add an extra pointer parameter at
     * the start for the result value.
     *
     * This should only return true if the regular LLVM sret mechanism
     * cannot handle this type.
     */
    virtual bool return_by_sret(ConstantBuilder& builder, llvm::CallingConv::ID cconv, Term *type) const = 0;

    /// \brief Return the type used to pass a parameter.
    virtual const llvm::Type* parameter_type(ConstantBuilder& builder, llvm::CallingConv::ID cconv, Term *type) const = 0;

    /// \brief Convert a parameter to the correct type for passing.
    virtual llvm::Value* parameter_pack(FunctionBuilder& builder, llvm::CallingConv::ID cconv, Term *term) const = 0;

    /// \brief Convert a parameter from the passed type.
    virtual BuiltValue* parameter_unpack(FunctionBuilder& builder, llvm::CallingConv::ID cconv, Term *type, llvm::Value *value) const = 0;

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
      const llvm::Type *llvm_result_type = parameter_type(builder, cconv, term->result_type());

      std::size_t n_phantom = term->n_phantom_parameters();
      std::size_t n_passed_parameters = term->n_parameters() - n_phantom;
      std::vector<const llvm::Type*> llvm_parameter_types(n_passed_parameters);
      for (std::size_t i = 0; i != n_passed_parameters; ++i)
        llvm_parameter_types[i] = parameter_type(builder, cconv, term->parameter(i+n_phantom)->type());

      if (return_by_sret(builder, cconv, term->result_type())) {
        PSI_ASSERT(llvm_result_type->isPointer());
        llvm_parameter_types.push_front(llvm_result_type);
        const llvm::Type *void_ty = llvm::Type::getVoidTy(builder.llvm_context());
        return llvm::FunctionType::get(void_ty, llvm_parameter_types, false);
      } else {
        return llvm::FunctionType::get(llvm_result_type, llvm_parameter_types, false);
      }
    }

    virtual BuiltValue* function_call(FunctionBuilder& builder, llvm::Value *target, FunctionTypeTerm *target_type, FunctionCall::Ptr insn) const {
      llvm::CallingConv::ID cconv = map_calling_convention(target_type->calling_convention());
      const llvm::Type *llvm_result_type = parameter_type(builder, cconv, target_type->result_type());

      std::size_t n_phantom = target_type->n_phantom_parameters();
      std::size_t n_passed_parameters = target_type->n_parameters() - n_phantom;
      std::vector<const llvm::Type*> llvm_parameter_types(n_passed_parameters);
      llvm::SmallVector<llvm::Value*, 4> llvm_parameters(n_passed_parameters);
      for (std::size_t i = 0; i != n_passed_parameters; ++i) {
        llvm_parameters[i] = parameter_pack(builder, cconv, insn->parameter(i + n_phantom));
        llvm_parameter_types[i] = llvm_parameters[i]->getType();
      }

      llvm::FunctionType *llvm_function_type = llvm::FunctionType::get(llvm_result_type, llvm_parameter_types, false);
      llvm::Value *cast_target = builder.irbuilder().CreateBitCast(target, llvm_function_type->getPointerTo());
      llvm::CallInst *call_insn = builder.irbuilder().CreateCall(cast_target, llvm_parameters.begin(), llvm_parameters.end());
      call_insn->setCallingConv(cconv);
      return parameter_unpack(builder, cconv, target_type->result_type(), call_insn);
    }

    virtual void function_parameters_unpack(FunctionBuilder& builder, FunctionTerm *function,
                                            llvm::Function *llvm_function, llvm::SmallVectorImpl<BuiltValue*>& result) const {
      llvm::CallingConv::ID cconv = map_calling_convention(function->function_type()->calling_convention());

      std::size_t n_phantom = function->function_type()->n_phantom_parameters();
      std::size_t n_passed_parameters = function->function_type()->n_parameters() - n_phantom;
      result.resize(n_passed_parameters);
      llvm::Function::arg_iterator jt = llvm_function->arg_begin();
      PSI_ASSERT(n_passed_parameters == llvm_function->getFunctionType()->getNumParams());
      for (std::size_t i = 0; i != n_passed_parameters; ++i, ++jt)
        result[i] = parameter_unpack(builder, cconv, function->parameter(i + n_phantom)->type(), &*jt);
    }

    virtual void function_return(FunctionBuilder& builder, FunctionTypeTerm *function_type, Term *value) const {
      llvm::CallingConv::ID cconv = map_calling_convention(function_type->calling_convention());
      builder.irbuilder().CreateRet(parameter_pack(builder, cconv, value));
    }
  };

  /**
   * Simple default implementation - this assumes that everything
   * works correctly in LLVM.
   */
  class TargetFixes_Default : public TargetFixes_SimpleBase {
    virtual bool return_by_sret(ConstantBuilder& builder, llvm::CallingConv::ID cconv, Term *type) const {
      return false;
    }

    virtual const llvm::Type* parameter_type(ConstantBuilder& builder, llvm::CallingConv::ID, Term *type) const {
      return builder.build_type(type);
    }

    virtual llvm::Value* parameter_pack(FunctionBuilder& builder, llvm::CallingConv::ID, Term *term) const {
      return builder.build_value_simple(term);
    }

    virtual BuiltValue* parameter_unpack(FunctionBuilder& builder, llvm::CallingConv::ID, Term *type, llvm::Value *value) const {
      return builder.new_value_simple(type, value);
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
     * returned). Types related to long double are not here since
     * it is not supported by Tvm.
     */
    enum ParameterClass {
      param_integer,
      param_sse,
      param_sse_up,
      param_no_class,
      param_memory
    };

    /**
     * Lists the basic types available on x86-64. Note that
     * pointers count as type_long. Decimal types have been left
     * out.
     */
    enum BaseType {
      type_char,
      type_short,
      type_int,
      type_long,
      type_int128,
      type_float,
      type_double,
      type_float128,
      type_m64,
      type_m128,
      type_m256
    };

    /**
     * Get the parameter class resulting from two separate
     * classes. Described on page 19 of the ABI.
     */
    static ParameterClass merge_class(ParameterClass left, ParameterClass right) {
      if (left == right) {
        return left;
      } else if (left == param_no_class) {
        return right;
      } else if (right == param_no_class) {
        return left;
      } else if ((left == param_memory) || (right == param_memory)) {
        return param_memory;
      } else if ((left == param_integer) || (right == param_integer)) {
        return param_integer;
      } else {
        return param_sse;
      }
    }

    /**
     * Handmade sret parameters are only required when LLVM would do
     * the wrong thing with any type used - this happens if the type
     * is small enough to be passed in registers, but is not because
     * it is a union of incompatible types.
     */
    virtual bool return_by_sret(ConstantBuilder& builder, llvm::CallingConv::ID cconv, Term *type) const {
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
    virtual const llvm::Type* parameter_type(ConstantBuilder& builder, llvm::CallingConv::ID cconv, Term *type) const {
    }

    virtual llvm::Value* parameter_pack(FunctionBuilder& builder, llvm::CallingConv::ID cconv, Term *term) const {
      
    }

    virtual BuiltValue* parameter_unpack(FunctionBuilder& builder, llvm::CallingConv::ID cconv, Term *type, llvm::Value *value) const {
    }

    /**
     * Whether the convention is supported on X86-64. Currently this
     * is the C calling convention only, other calling conventions
     * will probably require different custom code. Note that this
     * does not count x86-specific conventions, assuming that they are
     * 32-bit.
     */
    virtual bool convention_supported(llvm::CallingConv::ID id) const {
      return id == llvm::CallingConv::C;
    }
  };

  struct HostDescription {
    std::string cpu;
    std::string vendor;
    std::string kernel;
    std::string os;
  };

  HostDescription splitHostTriple(const char *triple) {
    const char *first = std::strchr(triple, '-');
    PSI_ASSERT(first);
    const char *second = std::strchr(first+1, '-');
    PSI_ASSERT(second);
    const char *third = std::strchr(second+1, '-');

    HostDescription result;
    result.cpu.assign(triple, first);
    result.vendor.assign(first+1, second);
    if (third) {
      result.kernel.assign(second+1, third);
      result.os = (third+1);
    } else {
      result.os = (second+1);
    }

    return result;
  }
}

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      /**
       * Get the machine-specific set of LLVM workarounds for a given
       * machine. If no such workaround are available, this returns a
       * dummy class, but that may well break in some cases.
       */
      boost::shared_ptr<TargetFixes> create_target_fixes(const llvm::Target& target) {
        HostDescription hd = splitHostTriple(target.getName());

        if (hd.cpu == "x86_64") {
          if (hd.os == "gnu") {
            return boost::make_shared<TargetFixes_AMD64>();
          }
        }

        return boost::make_shared<TargetFixes_Default>();
      }
    }
  }
}
