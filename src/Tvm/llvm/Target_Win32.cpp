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
       * Win32 calling conventions.
       *
       * Presently, only __cdecl is implemented.
       */
      class TargetFixes_Win32_AggregateLowering : public TargetCommon {
        struct FunctionCallCommonCallback : TargetCommon::Callback {
          TargetFixes_Win32_AggregateLowering *self;
          FunctionCallCommonCallback(TargetFixes_Win32_AggregateLowering *self_) : self(self_) {}

          virtual boost::shared_ptr<ParameterHandler> parameter_type_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, CallingConvention, const ParameterType& type) const {
            return TargetCommon::parameter_handler_simple(rewriter, type.value);
          }

          virtual boost::shared_ptr<ReturnHandler> return_type_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, CallingConvention, const ParameterType& type) const {
            return TargetCommon::return_handler_simple(rewriter, type.value);
          }

          /**
           * Whether the convention is supported on Win32. Currently this
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
        TargetFixes_Win32_AggregateLowering(llvm::LLVMContext *context, const boost::shared_ptr<llvm::TargetMachine>& target_machine)
        : TargetCommon(&m_function_call_callback, context, target_machine->getDataLayout()),
        m_function_call_callback(this),
        m_target_machine(target_machine) {
        }
      };
      
      class TargetFixes_Win32 : public TargetCallback {
        TargetFixes_Win32_AggregateLowering m_aggregate_lowering_callback;
        
      public:
        TargetFixes_Win32(llvm::LLVMContext *context, const boost::shared_ptr<llvm::TargetMachine>& target_machine)
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
       * \brief Create TargetFixes instance for the Win32-x86 platform.
       *
       * \see TargetFixes_Win32
       */
      boost::shared_ptr<TargetCallback> create_target_fixes_win32(llvm::LLVMContext *context, const boost::shared_ptr<llvm::TargetMachine>& target_machine) {
        return boost::make_shared<TargetFixes_Win32>(context, target_machine);
      }
    }
  }
}

