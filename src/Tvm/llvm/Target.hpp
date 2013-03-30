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
        /**
         * \brief Base class for a handler for a particular parameter
         * type on a particular target.
         */
        class ParameterHandler {
          ValuePtr<> m_lowered_type;

        public:
          ParameterHandler(const ValuePtr<>& lowered_type);
          
          /// Type used to pass this parameter.
          const ValuePtr<>& lowered_type() const {return m_lowered_type;}

          /// \brief Convert a parameter to the correct type for passing.
          virtual ValuePtr<> pack(AggregateLoweringPass::FunctionRunner& builder, const ValuePtr<>& source_value, const SourceLocation& location) const = 0;
          
          /// \brief Clean up any memory allocation during \c pack
          virtual void pack_cleanup(AggregateLoweringPass::FunctionRunner& builder, const ValuePtr<>& param_value, const SourceLocation& location) const = 0;

          /// \brief Convert a parameter from the passed type.
          virtual void unpack(AggregateLoweringPass::FunctionRunner& builder, const ValuePtr<>& source_value, const ValuePtr<>& target_value, const SourceLocation& location) const = 0;
        };
        
        /**
         * \brief Base class for a handler for returning a particular type
         * on a particular target.
         */
        class ReturnHandler {
          ValuePtr<> m_lowered_type;

        public:
          ReturnHandler(const ValuePtr<>& lowered_type);

          /// Type used to pass this parameter.
          const ValuePtr<>& lowered_type() const {return m_lowered_type;}
          
          /// \brief Whether this type should be returned via an extra sret
          /// parameter, which must be inserted manually since LLVM will not
          /// handle this case correctly.
          virtual bool return_by_sret() const = 0;

          /**
           * \brief Prepare for a call which returns by a custom sret.
           *
           * This should return NULL if this parameter type does not force
           * an sret return, that is if return_by_sret returns false,
           * otherwise it should always return a non-NULL value giving the
           * memory to use to store the sret return.
           */
          virtual ValuePtr<> return_by_sret_setup(AggregateLoweringPass::FunctionRunner& builder, const SourceLocation& location) const = 0;

          /// \brief Generate code for returning a value from a function.
          virtual ValuePtr<Instruction> return_pack(AggregateLoweringPass::FunctionRunner& builder, const ValuePtr<>& value, const SourceLocation& location) const = 0;

          /**
           * \brief Decode a value returned by a called function.
           *
           * If return_by_sret_setup returned a non-NULL value, this
           * will be passed in the last parameter. It is safe to
           * assume that the return value from return_by_sret_setup
           * will always be passed as the third parameter so it is not
           * necessary to check whether it is NULL.
           */
          virtual void return_unpack(AggregateLoweringPass::FunctionRunner& builder, const ValuePtr<>& sret_addr, const ValuePtr<>& source_value, const ValuePtr<>& target_value, const SourceLocation& location) const = 0;
        };

      private:
        static ValuePtr<> change_by_memory_in(AggregateLoweringPass::FunctionRunner& builder, const ValuePtr<>& source_value, const ValuePtr<>& target_type, const SourceLocation& location);
        static LoweredValue change_by_memory_out(AggregateLoweringPass::FunctionRunner& builder, const ValuePtr<>& source_value, const LoweredType& target_type, const SourceLocation& location);
          
        class ParameterHandlerSimple;
        class ReturnHandlerSimple;
        class ParameterHandlerChangeTypeByMemory;
        class ReturnHandlerChangeTypeByMemory;
        class ParameterHandlerForcePtr;
        class ReturnHandlerForcePtr;

      public:
        /**
         * \brief Functions which must be supplied by the user to use
         * TargetFunctionCallCommon.
         */
        struct Callback {
          /// \brief Return information about how to pass this parameter.
          virtual boost::shared_ptr<ParameterHandler> parameter_type_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, CallingConvention cconv, const ValuePtr<>& type) const = 0;

          /// \brief Return information about how to return this type.
          virtual boost::shared_ptr<ReturnHandler> return_type_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, CallingConvention cconv, const ValuePtr<>& type) const = 0;

          /// \brief Checks whether a given calling convention actually
          /// makes sense for a given platform.
          virtual bool convention_supported(CallingConvention id) const = 0;
        };

      private:
        const Callback *m_callback;
        llvm::LLVMContext *m_context;
        const llvm::DataLayout *m_target_data;
        
        struct LowerFunctionHelperResult {
          ValuePtr<FunctionType> lowered_type;
          boost::shared_ptr<ReturnHandler> return_handler;
          std::vector<boost::shared_ptr<ParameterHandler> > parameter_handlers;
        };
        
        LowerFunctionHelperResult lower_function_helper(AggregateLoweringPass::AggregateLoweringRewriter&,  const ValuePtr<FunctionType>&);
        TypeSizeAlignment type_size_alignment_simple(llvm::Type*);

      public:
        TargetCommon(const Callback*, llvm::LLVMContext*, const llvm::DataLayout*);

        static boost::shared_ptr<ParameterHandler> parameter_handler_simple(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<>& type);
        static boost::shared_ptr<ReturnHandler> return_handler_simple(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<>& type);
        static boost::shared_ptr<ParameterHandler> parameter_handler_change_type_by_memory(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<>& type, const ValuePtr<>& lowered_type);
        static boost::shared_ptr<ReturnHandler> return_handler_change_type_by_memory(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<>& type, const ValuePtr<>& lowered_type);
        static boost::shared_ptr<ParameterHandler> parameter_handler_force_ptr(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<>& type);
        static boost::shared_ptr<ReturnHandler> return_handler_force_ptr(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<>& type);

        static llvm::CallingConv::ID map_calling_convention(CallingConvention conv);
        
        llvm::LLVMContext& context() const {return *m_context;}
        
        virtual void lower_function_call(AggregateLoweringPass::FunctionRunner&, const ValuePtr<Call>&);
        virtual ValuePtr<Instruction> lower_return(AggregateLoweringPass::FunctionRunner&, const ValuePtr<>&, const SourceLocation&);
        virtual ValuePtr<Function> lower_function(AggregateLoweringPass&, const ValuePtr<Function>&);
        virtual void lower_function_entry(AggregateLoweringPass::FunctionRunner&, const ValuePtr<Function>&, const ValuePtr<Function>&);
        virtual TypeSizeAlignment type_size_alignment(const ValuePtr<>&);
        virtual std::pair<ValuePtr<>, std::size_t> type_from_size(Context& context, std::size_t size, const SourceLocation& location);
        virtual std::pair<ValuePtr<>, std::size_t> type_from_alignment(Context& context, std::size_t alignment, const SourceLocation& location);
        virtual ValuePtr<> byte_shift(const ValuePtr<>& value, const ValuePtr<>& result_type, int offset, const SourceLocation& location);
      };
      
      llvm::Function* target_exception_personality_linux(llvm::Module*, const std::string&);

      boost::shared_ptr<TargetCallback> create_target_fixes_amd64(llvm::LLVMContext*, const boost::shared_ptr<llvm::TargetMachine>&);
      boost::shared_ptr<TargetCallback> create_target_fixes_linux_x86(llvm::LLVMContext*, const boost::shared_ptr<llvm::TargetMachine>&);
      boost::shared_ptr<TargetCallback> create_target_fixes_win32(llvm::LLVMContext*, const boost::shared_ptr<llvm::TargetMachine>&);
    }
  }
}

#endif
