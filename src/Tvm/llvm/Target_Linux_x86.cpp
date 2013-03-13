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
        struct FunctionCallCommonCallback : TargetCommon::Callback {
          TargetFixes_Linux_X86_AggregateLowering *self;
          FunctionCallCommonCallback(TargetFixes_Linux_X86_AggregateLowering *self_) : self(self_) {}

          /**
           * x86 calling convention is as follows:
           * 
           * <ul>
           * 
           * <li>All aggregate types are passed via pointer or sret parameter.</li>
           * 
           * <li>Integer-like types are passed in EAX or EAX:EDX depending on size
           * and floating point types are passed in registers; I leave this to LLVM</li>
           *
           * </ul>
           */
          virtual boost::shared_ptr<ParameterHandler> parameter_type_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, CallingConvention, const ValuePtr<>& type) const {
            return TargetCommon::parameter_handler_simple(rewriter, type);
          }

          virtual boost::shared_ptr<ReturnHandler> return_type_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, CallingConvention, const ValuePtr<>& type) const {
            ValuePtr<> simple_type = rewriter.simplify_argument_type(type);
            if (isa<StructType>(simple_type) || isa<UnionType>(simple_type) || isa<ArrayType>(simple_type)) {
              return TargetCommon::return_handler_force_ptr(rewriter, type);
            } else {
              return TargetCommon::return_handler_simple(rewriter, type);
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

