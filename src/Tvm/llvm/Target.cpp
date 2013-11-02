#include "Builder.hpp"
#include "CallingConventions.hpp"
#include "../FunctionalBuilder.hpp"

#include <sstream>

#include <boost/make_shared.hpp>
#include <boost/optional.hpp>
#include <boost/ref.hpp>

#include "LLVMPushWarnings.hpp"
#include <llvm/ADT/Triple.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetRegistry.h>
#include "LLVMPopWarnings.hpp"

namespace Psi {
  namespace Tvm {
    namespace LLVM {
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
      class AggregateTargetCallbackLLVM : public AggregateLoweringPass::TargetCallback {
        llvm::LLVMContext *m_context;
        llvm::Triple m_target_triple;
        boost::shared_ptr<llvm::TargetMachine> m_target_machine;

        TypeSizeAlignment type_size_alignment_simple(llvm::Type*);

      public:
        AggregateTargetCallbackLLVM(llvm::LLVMContext *context, const llvm::Triple& target_triple, const boost::shared_ptr<llvm::TargetMachine>& target_machine);
        
        const llvm::Triple& target_triple() {return m_target_triple;}
        const llvm::DataLayout *target_data_layout() {return m_target_machine->getDataLayout();}

        llvm::LLVMContext& context() const {return *m_context;}
        
        virtual void lower_function_call(AggregateLoweringPass::FunctionRunner&, const ValuePtr<Call>&);
        virtual ValuePtr<Instruction> lower_return(AggregateLoweringPass::FunctionRunner&, const ValuePtr<>&, const SourceLocation&);
        virtual ValuePtr<Function> lower_function(AggregateLoweringPass&, const ValuePtr<Function>&);
        virtual void lower_function_entry(AggregateLoweringPass::FunctionRunner&, const ValuePtr<Function>&, const ValuePtr<Function>&);
        virtual TypeSizeAlignment type_size_alignment(const ValuePtr<>&, const SourceLocation& location);
        virtual std::pair<ValuePtr<>, std::size_t> type_from_size(Context& context, std::size_t size, const SourceLocation& location);
        virtual std::pair<ValuePtr<>, std::size_t> type_from_alignment(Context& context, std::size_t alignment, const SourceLocation& location);
        virtual ValuePtr<> byte_shift(const ValuePtr<>& value, const ValuePtr<>& result_type, int offset, const SourceLocation& location);
      };
      
      AggregateTargetCallbackLLVM::AggregateTargetCallbackLLVM(llvm::LLVMContext *context, const llvm::Triple& target_triple, const boost::shared_ptr<llvm::TargetMachine>& target_machine)
      : m_context(context), m_target_triple(target_triple), m_target_machine(target_machine) {
      }

      void AggregateTargetCallbackLLVM::lower_function_call(AggregateLoweringPass::FunctionRunner& runner, const ValuePtr<Call>& term) {
        UniquePtr<CallingConventionHandler> handler;
        calling_convention_handler(runner.error_context().bind(term->location()), target_triple(), term->target_function_type()->calling_convention(), handler);
        handler->lower_function_call(runner, term);
      }

      ValuePtr<Instruction> AggregateTargetCallbackLLVM::lower_return(AggregateLoweringPass::FunctionRunner& runner, const ValuePtr<>& value, const SourceLocation& location) {
        UniquePtr<CallingConventionHandler> handler;
        calling_convention_handler(runner.error_context().bind(location), target_triple(), runner.old_function()->function_type()->calling_convention(), handler);
        return handler->lower_return(runner, value, location);
      }

      ValuePtr<Function> AggregateTargetCallbackLLVM::lower_function(AggregateLoweringPass& pass, const ValuePtr<Function>& function) {
        UniquePtr<CallingConventionHandler> handler;
        calling_convention_handler(pass.error_context().bind(function->location()), target_triple(), function->function_type()->calling_convention(), handler);
        return handler->lower_function(pass, function);
      }
      
      void AggregateTargetCallbackLLVM::lower_function_entry(AggregateLoweringPass::FunctionRunner& runner, const ValuePtr<Function>& source_function, const ValuePtr<Function>& target_function) {
        UniquePtr<CallingConventionHandler> handler;
        calling_convention_handler(runner.error_context().bind(source_function->location()), target_triple(), source_function->function_type()->calling_convention(), handler);
        return handler->lower_function_entry(runner, source_function, target_function);
      }
      
      TypeSizeAlignment AggregateTargetCallbackLLVM::type_size_alignment_simple(llvm::Type *llvm_type) {
        TypeSizeAlignment result;
        result.size = target_data_layout()->getTypeAllocSize(llvm_type);
        result.alignment = target_data_layout()->getABITypeAlignment(llvm_type);
        return result;
      }
      
      TypeSizeAlignment AggregateTargetCallbackLLVM::type_size_alignment(const ValuePtr<>& type, const SourceLocation& location) {
        if (isa<PointerType>(type)) {
          TypeSizeAlignment result;
          result.size = target_data_layout()->getPointerSize();
          result.alignment = target_data_layout()->getPointerABIAlignment();
          return result;
        } else if (isa<BooleanType>(type)) {
          return  type_size_alignment_simple(llvm::Type::getInt1Ty(context()));
        } else if (isa<ByteType>(type)) {
          return type_size_alignment_simple(llvm::Type::getInt8Ty(context()));
        } else if (ValuePtr<IntegerType> int_ty = dyn_cast<IntegerType>(type)) {
          return type_size_alignment_simple(integer_type(context(), target_data_layout(), int_ty->width()));
        } else if (ValuePtr<FloatType> float_ty = dyn_cast<FloatType>(type)) {
          return type_size_alignment_simple(float_type(context(), float_ty->width()));
        } else if (isa<EmptyType>(type) || isa<BlockType>(type)) {
          TypeSizeAlignment result;
          result.size = 0;
          result.alignment = 1;
          return result;
        } else {
          type->context().error_context().error_throw(location, "type not recognised by LLVM backend during aggregate lowering");
        }
      }
      
      std::pair<ValuePtr<>, std::size_t> AggregateTargetCallbackLLVM::type_from_size(Context& context, std::size_t size, const SourceLocation& location) {
        std::size_t my_size = std::min(size, std::size_t(16));
        IntegerType::Width width;
        switch (my_size) {
        case 1: width = IntegerType::i8; break;
        case 2: width = IntegerType::i16; break;
        case 4: width = IntegerType::i32; break;
        case 8: width = IntegerType::i64; break;
        case 16: width = IntegerType::i128; break;
        default: PSI_FAIL("type_from_size argument was not a power of two");
        }
        
        ValuePtr<> int_type = FunctionalBuilder::int_type(context, width, false, location);
        return std::make_pair(int_type, my_size);
      }
      
      std::pair<ValuePtr<>, std::size_t> AggregateTargetCallbackLLVM::type_from_alignment(Context& context, std::size_t alignment, const SourceLocation& location) {
        unsigned real_alignment = std::min(alignment, std::size_t(16));
        for (; real_alignment > 1; real_alignment /= 2) {
          unsigned abi_alignment = target_data_layout()->getABIIntegerTypeAlignment(real_alignment*8);
          if (abi_alignment == real_alignment)
            break;
        }
        return type_from_size(context, real_alignment, location);
      }

      ValuePtr<> AggregateTargetCallbackLLVM::byte_shift(const ValuePtr<>& value, const ValuePtr<>& result_type, int offset, const SourceLocation& location) {
        TypeSizeAlignment value_size_align = type_size_alignment(value->type(), location);
        TypeSizeAlignment result_size_align = type_size_alignment(result_type, location);
        std::size_t max_size = std::max(value_size_align.size, result_size_align.size);
        std::pair<ValuePtr<>, std::size_t> value_int_type = type_from_size(value->context(), max_size, location);
        PSI_ASSERT(value_int_type.second == max_size);
        ValuePtr<> value_cast = FunctionalBuilder::bit_cast(value, value_int_type.first, location);
        int byte_offset;
        if (target_data_layout()->isBigEndian())
          byte_offset = offset + result_size_align.size - value_size_align.size;
        else
          byte_offset = -offset;
        PSI_ASSERT(unsigned(std::abs(byte_offset)) < max_size);
        ValuePtr<> value_shifted = FunctionalBuilder::bit_shift(value_cast, byte_offset*8, location);
        return FunctionalBuilder::bit_cast(value_shifted, result_type, location);
      }
      
      /**
       * Target constructor.
       * 
       * Checks that the target is supported by the back end, otherwise it throws.
       * 
       * \param triple An LLVM target triple, which will be parsed
       * using the llvm::Triple class.
       */
      TargetCallback::TargetCallback(const CompileErrorPair& error_loc, llvm::LLVMContext *context, const boost::shared_ptr<llvm::TargetMachine>& target_machine, const std::string& triple)
      : m_triple(triple) {
        bool accept = false;
        switch (m_triple.getArch()) {
        case llvm::Triple::x86_64:
          switch (m_triple.getOS()) {
          case llvm::Triple::FreeBSD:
          case llvm::Triple::Linux: accept = true; break;
          default: break;
          }
          break;
          
        case llvm::Triple::x86:
          switch (m_triple.getOS()) {
          case llvm::Triple::FreeBSD:
          case llvm::Triple::Linux:
          case llvm::Triple::MinGW32:
          case llvm::Triple::Win32: accept = true; break;
          default: break;
          }
          break;

        case llvm::Triple::arm:
          switch (m_triple.getOS()) {
          case llvm::Triple::Linux:
            switch (m_triple.getEnvironment()) {
            case llvm::Triple::GNUEABIHF:
            case llvm::Triple::GNUEABI:
            case llvm::Triple::Android: accept = true; break;
            default: break;
            }
          default: break;
          }
          break;
        
        default:
          break;
        }
        
        if (!accept)
          error_loc.error_throw("Target " + triple + " not supported");
        
        m_aggregate_lowering_callback.reset(new AggregateTargetCallbackLLVM(context, m_triple, target_machine));
      }
      
      /**
       * Perform any necessary adjustments to the target triple for JIT compilation.
       */
      llvm::Triple TargetCallback::jit_triple() {
        llvm::Triple result(llvm::sys::getProcessTriple());
        switch (result.getOS()) {
        case llvm::Triple::Cygwin:
        case llvm::Triple::Win32:
        case llvm::Triple::MinGW32:
          result.setEnvironment(llvm::Triple::ELF);
          break;
          
        default: break;
        }
        
        return result;
      }

      /**
       * \brief Exception personality routine set up for Linux with DWARF2 unwinding.
       */
      llvm::Function* target_exception_personality_linux(llvm::Module *module, const std::string& basename) {
        std::ostringstream ss;
        ss << "__" << basename << "_personality_v0";
        std::string fullname = ss.str();
        
        if (llvm::Function *f = module->getFunction(fullname))
          return f;
        
        llvm::LLVMContext& c = module->getContext();
        llvm::Type *i32 = llvm::Type::getInt32Ty(c);
        llvm::Type *i8ptr = llvm::Type::getInt8PtrTy(c);
        llvm::Type *args[] = {
          i32,
          i32,
          llvm::Type::getInt64Ty(c),
          i8ptr,
          i8ptr
        };

        llvm::FunctionType *ft = llvm::FunctionType::get(i32, args, false);
        return llvm::Function::Create(ft, llvm::GlobalValue::ExternalLinkage, fullname, module);
      }
      
      /**
       * \brief Set up or get the exception personality routine with the specified name.
       * 
       * \param module Module to set up the handler for.
       * 
       * \param basename Name of the personality to use. Interpretation of this name is
       * platform specific.
       */
      llvm::Function* TargetCallback::exception_personality_routine(llvm::Module *module, const std::string& basename) {
        switch (m_triple.getOS()) {
        case llvm::Triple::Linux: return target_exception_personality_linux(module, basename);
        default: PSI_FAIL("Exception handling not implemented on this platform");
        }
      }
    }
  }
}
