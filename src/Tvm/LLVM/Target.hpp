#ifndef HPP_PSI_TVM_LLVM_TARGET
#define HPP_PSI_TVM_LLVM_TARGET

#include "Builder.hpp"
#include "../Core.hpp"

#include <boost/shared_ptr.hpp>

#include <llvm/Function.h>

/**
 * \file
 *
 * Contains common helper classes for target-specific code, plus
 * definitions of functions to create target-specific classes.
 */

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      /**
       * Describes the general target-specific information about this
       * parameter. This is not used by the core classes in any way,
       * but is probably useful in several platform-specific classes
       * so it's here with the common code.
       */
      class TargetParameterCategory {
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

	TargetParameterCategory(Value value) : m_value(value) {}
	operator Value () const {return m_value;}
	Value value() const {return m_value;}

	/// Merge two parameter categories so the resulting category would
	/// correctly handle both input categories.
	static TargetParameterCategory merge(TargetParameterCategory lhs, TargetParameterCategory rhs) {
	  return std::max(lhs.value(), rhs.value());
	}

      private:
	Value m_value;
      };

      /**
       * If target fixes can be handled entirely on a per-parameter
       * basis, this handles the general management of function
       * calls. To use this class, create an instance of it inside a
       * TargetFixes implementation and forward calls to the function
       * call handling code to it.
       *
       * Note that this also relies on LLVM handling sret parameters
       * (hidden parameters to functions which point to memory to
       * write the result to) correctly.
       */
      class TargetFunctionCallCommon {
      public:
	/**
	 * \brief Base class for a handler for a particular parameter
	 * type on a particular target.
	 */
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

      private:
	class ParameterHandlerSimple;
	class ParameterHandlerChangeTypeByMemory;
	class ParameterHandlerForcePtr;

      public:
	/**
	 * \brief Functions which must be supplied by the user to use
	 * TargetFunctionCallCommon.
	 */
	struct Callback {
	  /// \brief Return information about how to pass this parameter.
	  virtual boost::shared_ptr<ParameterHandler> parameter_type_info(ConstantBuilder& builder, llvm::CallingConv::ID cconv, Term *type) const = 0;
    
	  /// \brief Checks whether a given calling convention actually
	  /// makes sense for a given platform.
	  virtual bool convention_supported(llvm::CallingConv::ID id) const = 0;
	};

      private:
	Callback *m_callback;

      public:
	TargetFunctionCallCommon(Callback *callback);

	static boost::shared_ptr<ParameterHandler> parameter_handler_simple(ConstantBuilder& builder, Term *type, llvm::CallingConv::ID cconv);
	static boost::shared_ptr<ParameterHandler> parameter_handler_change_type_by_memory(Term *type, const llvm::Type *llvm_type, llvm::CallingConv::ID calling_convention);
	static boost::shared_ptr<ParameterHandler> parameter_handler_force_ptr(ConstantBuilder& builder, Term *type, llvm::CallingConv::ID cconv);

	static bool convention_always_supported(llvm::CallingConv::ID id);
	llvm::CallingConv::ID map_calling_convention(CallingConvention conv);
	const llvm::FunctionType* function_type(ConstantBuilder& builder, FunctionTypeTerm *term);
	BuiltValue* function_call(FunctionBuilder& builder, llvm::Value *target, FunctionTypeTerm *target_type, FunctionCall::Ptr insn);
	void function_parameters_unpack(FunctionBuilder& builder, FunctionTerm *function,
					llvm::Function *llvm_function, llvm::SmallVectorImpl<BuiltValue*>& result);
	void function_return(FunctionBuilder& builder, FunctionTypeTerm *function_type, llvm::Function *llvm_function, Term *value);
      };

      /**
       * Macro to generate forwarding code to TargetFunctionCallCommon
       * inside a TargetFixes implementation.
       *
       * \param var The memeber variables which is an instance of
       * TargetFunctionCallCommon.
       */
#define PSI_TVM_LLVM_TARGET_FUNCTION_CALL_COMMON_FORWARD(var)		\
      virtual const llvm::FunctionType* function_type(ConstantBuilder& builder, FunctionTypeTerm *term) {return var.function_type(builder, term);} \
      virtual BuiltValue* function_call(FunctionBuilder& builder, llvm::Value *target, FunctionTypeTerm *target_type, FunctionCall::Ptr insn) {return var.function_call(builder, target, target_type, insn);} \
      virtual void function_parameters_unpack(FunctionBuilder& builder, FunctionTerm *function, llvm::Function *llvm_function, llvm::SmallVectorImpl<BuiltValue*>& result) {return var.function_parameters_unpack(builder, function, llvm_function, result);} \
      virtual void function_return(FunctionBuilder& builder, FunctionTypeTerm *function_type, llvm::Function *llvm_function, Term *value) {return var.function_return(builder, function_type, llvm_function, value);}

      boost::shared_ptr<TargetFixes> create_target_fixes_amd64();
    }
  }
}

#endif
