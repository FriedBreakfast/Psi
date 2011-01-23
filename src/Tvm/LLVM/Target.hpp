#ifndef HPP_PSI_TVM_LLVM_TARGET
#define HPP_PSI_TVM_LLVM_TARGET

#include "Builder.hpp"
#include "../Core.hpp"
#include "../AggregateLowering.hpp"

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
      class TargetCommon : public AggregateLoweringPass::TargetCallback {
      public:
        struct TypeSizeAlignmentLiteral {
          unsigned size;
          unsigned alignment;
        };

	/**
	 * \brief Base class for a handler for a particular parameter
	 * type on a particular target.
	 */
	class ParameterHandler {
          Term *m_type;
          Term *m_lowered_type;
          CallingConvention m_calling_convention;

	public:
	  ParameterHandler(Term *type, Term *lowered_type, CallingConvention calling_convention);

	  /// The type of term that this object was created to pass.
	  Term *type() const {return m_type;}
	  
	  /// Type used to pass this parameter.
	  Term *lowered_type() const {return m_lowered_type;}

	  /// The calling convention this parameter type was built for.
	  CallingConvention calling_convention() const {return m_calling_convention;}

	  /// \brief Whether this type should be returned via an extra sret
	  /// parameter, which must be inserted manually since LLVM will not
	  /// handle this case correctly.
	  virtual bool return_by_sret() const = 0;

	  /// \brief Convert a parameter to the correct type for passing.
	  virtual Term* pack(AggregateLoweringPass::FunctionRunner& builder, Term *source_value) const = 0;

	  /// \brief Convert a parameter from the passed type.
	  virtual void unpack(AggregateLoweringPass::FunctionRunner& builder, Term *source_value, Term *target_value) const = 0;

	  /**
	   * \brief Prepare for a call which returns by a custom sret.
	   *
	   * This should return NULL if this parameter type does not force
	   * an sret return, that is if return_by_sret returns false,
	   * otherwise it should always return a non-NULL value giving the
	   * memory to use to store the sret return.
	   */
	  virtual Term* return_by_sret_setup(AggregateLoweringPass::FunctionRunner& builder) const = 0;

	  /// \brief Generate code for returning a value from a function.
	  virtual InstructionTerm* return_pack(AggregateLoweringPass::FunctionRunner& builder, Term *value) const = 0;

	  /**
	   * \brief Decode a value returned by a called function.
	   *
	   * If return_by_sret_setup returned a non-NULL value, this
	   * will be passed in the last parameter. It is safe to
	   * assume that the return value from return_by_sret_setup
	   * will always be passed as the third parameter so it is not
	   * necessary to check whether it is NULL.
	   */
	  virtual void return_unpack(AggregateLoweringPass::FunctionRunner& builder, Term *sret_addr, Term *source_value, Term *target_value) const = 0;
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
	  virtual boost::shared_ptr<ParameterHandler> parameter_type_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, CallingConvention cconv, Term *type) const = 0;

	  /// \brief Checks whether a given calling convention actually
	  /// makes sense for a given platform.
	  virtual bool convention_supported(CallingConvention id) const = 0;
	};

      private:
	const Callback *m_callback;
        llvm::LLVMContext *m_context;
        const llvm::TargetData *m_target_data;
        
        struct LowerFunctionHelperResult {
          FunctionTypeTerm *lowered_type;
          bool sret;
          boost::shared_ptr<ParameterHandler> return_handler;
          std::vector<boost::shared_ptr<ParameterHandler> > parameter_handlers;
          std::size_t n_phantom;
          std::size_t n_passed_parameters;
        };
        
        LowerFunctionHelperResult lower_function_helper(AggregateLoweringPass::AggregateLoweringRewriter&,  FunctionTypeTerm*);
        TypeSizeAlignmentLiteral type_size_alignment_simple(const llvm::Type*);

      public:
	TargetCommon(const Callback*, llvm::LLVMContext*, const llvm::TargetData*);

	static boost::shared_ptr<ParameterHandler> parameter_handler_simple(AggregateLoweringPass::AggregateLoweringRewriter&, Term *, CallingConvention);
	static boost::shared_ptr<ParameterHandler> parameter_handler_change_type_by_memory(Term*, Term*, CallingConvention);
	static boost::shared_ptr<ParameterHandler> parameter_handler_force_ptr(Context&, Term*, CallingConvention);

        static llvm::CallingConv::ID map_calling_convention(CallingConvention conv);
        
        llvm::LLVMContext& context() const {return *m_context;}
        
        TypeSizeAlignmentLiteral type_size_alignment_literal(Term*);

        virtual void lower_function_call(AggregateLoweringPass::FunctionRunner&, FunctionCall::Ptr);
        virtual InstructionTerm* lower_return(AggregateLoweringPass::FunctionRunner&, Term*);
        virtual FunctionTerm* lower_function(AggregateLoweringPass&, FunctionTerm*);
        virtual void lower_function_entry(AggregateLoweringPass::FunctionRunner&, FunctionTerm*, FunctionTerm*);
        virtual Term* convert_value(Term*, Term*);
        virtual AggregateLoweringPass::TypeSizeAlignment type_size_alignment(Term*);
        virtual std::pair<Term*,Term*> type_from_alignment(Term*);
      };

      boost::shared_ptr<AggregateLoweringPass::TargetCallback> create_target_fixes_amd64(llvm::LLVMContext*, const boost::shared_ptr<llvm::TargetMachine>&);
    }
  }
}

#endif
