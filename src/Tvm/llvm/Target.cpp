#include "Builder.hpp"
#include "Target.hpp"
#include "../FunctionalBuilder.hpp"

#include <sstream>

#include <boost/make_shared.hpp>
#include <boost/optional.hpp>
#include <boost/ref.hpp>

#include <llvm/Function.h>
#include <llvm/Module.h>
#include <llvm/Target/TargetData.h>
#include <llvm/ADT/Triple.h>
#include <llvm/Support/TargetRegistry.h>

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      TargetCommon::TargetCommon(const Callback *callback, llvm::LLVMContext *context, const llvm::TargetData *target_data)
      : m_callback(callback), m_context(context), m_target_data(target_data) {
      }

      /**
       * \brief Map from a Tvm calling convention identifier to an LLVM one.
       *
       * This will raise an exception if the given calling convention
       * is not supported on the target platform.
       */
      llvm::CallingConv::ID TargetCommon::map_calling_convention(CallingConvention conv) {
        llvm::CallingConv::ID id;
        switch (conv) {
        case cconv_c: id = llvm::CallingConv::C; break;
        case cconv_x86_stdcall: id = llvm::CallingConv::X86_StdCall; break;
        case cconv_x86_thiscall: id = llvm::CallingConv::X86_ThisCall; break;
        case cconv_x86_fastcall: id = llvm::CallingConv::X86_FastCall; break;

        default:
          throw BuildError("Unsupported calling convention");
        }

        return id;
      }

      TargetCommon::LowerFunctionHelperResult
      TargetCommon::lower_function_helper(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionType>& function_type) {
        if (!m_callback->convention_supported(function_type->calling_convention()))
          throw BuildError("Calling convention is not supported on this platform");
        
        LowerFunctionHelperResult result;
        std::vector<ValuePtr<> > parameter_types;

        result.return_handler =
          m_callback->parameter_type_info(rewriter, function_type->calling_convention(), function_type->result_type());
        ValuePtr<> return_type = result.return_handler->lowered_type();
        result.sret = result.return_handler->return_by_sret();
        if (result.sret)
          parameter_types.push_back(return_type);

        result.n_phantom = function_type->n_phantom();
        result.n_passed_parameters = function_type->parameter_types().size() - result.n_phantom;
        for (std::size_t i = 0; i != result.n_passed_parameters; ++i) {
          boost::shared_ptr<ParameterHandler> handler = m_callback->parameter_type_info
            (rewriter, function_type->calling_convention(), function_type->parameter_types()[i+result.n_phantom]);
          result.parameter_handlers.push_back(handler);
          parameter_types.push_back(handler->lowered_type());
        }
        
        result.lowered_type = FunctionalBuilder::function_type
          (function_type->calling_convention(), return_type, parameter_types, 0, function_type->sret(), function_type->location());
          
        return result;
      }

      void TargetCommon::lower_function_call(AggregateLoweringPass::FunctionRunner& runner, const ValuePtr<Call>& term) {
        LowerFunctionHelperResult helper_result = lower_function_helper(runner, term->target_function_type());
        
        int sret = helper_result.sret ? 1 : 0;
        std::vector<ValuePtr<> > parameters(sret + helper_result.n_passed_parameters);

        ValuePtr<> sret_addr;
        if (helper_result.sret) {
          sret_addr = helper_result.return_handler->return_by_sret_setup(runner, term->location());
          parameters[0] = sret_addr;
        }
        
        for (std::size_t i = 0; i != helper_result.n_passed_parameters; ++i)
          parameters[i+sret] = helper_result.parameter_handlers[i]->pack(runner, term->parameters[i+helper_result.n_phantom], term->location());
        
        ValuePtr<> lowered_target = runner.rewrite_value_register(term->target).value;
        ValuePtr<> cast_target = FunctionalBuilder::pointer_cast(lowered_target, helper_result.lowered_type, term->location());
        ValuePtr<> result = runner.builder().call(cast_target, parameters, term->location());
        
        helper_result.return_handler->return_unpack(runner, sret_addr, term, result, term->location());
      }

      ValuePtr<Instruction> TargetCommon::lower_return(AggregateLoweringPass::FunctionRunner& runner, const ValuePtr<>& value, const SourceLocation& location) {
        ValuePtr<FunctionType> function_type = runner.old_function()->function_type();
        boost::shared_ptr<ParameterHandler> return_handler =
          m_callback->parameter_type_info(runner, function_type->calling_convention(), function_type->result_type());
          
        return return_handler->return_pack(runner, value, location);
      }

      ValuePtr<Function> TargetCommon::lower_function(AggregateLoweringPass& pass, const ValuePtr<Function>& function) {
        LowerFunctionHelperResult helper_result = lower_function_helper(pass.global_rewriter(), function->function_type());
        return pass.target_module()->new_function(function->name(), helper_result.lowered_type, function->location());
      }
      
      void TargetCommon::lower_function_entry(AggregateLoweringPass::FunctionRunner& runner, const ValuePtr<Function>& source_function, const ValuePtr<Function>& target_function) {
        LowerFunctionHelperResult helper_result = lower_function_helper(runner, source_function->function_type());
        int sret = helper_result.sret ? 1 : 0;
        for (std::size_t i = 0; i != helper_result.n_passed_parameters; ++i)
          helper_result.parameter_handlers[i]->unpack(runner, source_function->parameters().at(i+helper_result.n_phantom), target_function->parameters().at(i + sret), target_function->location());
      }
      
      ValuePtr<> TargetCommon::convert_value(const ValuePtr<>& value, const ValuePtr<>& type) {
        PSI_NOT_IMPLEMENTED();
      }
      
      TargetCommon::TypeSizeAlignmentLiteral TargetCommon::type_size_alignment_simple(llvm::Type *llvm_type) {
        TypeSizeAlignmentLiteral result;
        result.size = m_target_data->getTypeAllocSize(llvm_type);
        result.alignment = m_target_data->getABITypeAlignment(llvm_type);
        return result;
      }
      
      TargetCommon::TypeSizeAlignmentLiteral TargetCommon::type_size_alignment_literal(const ValuePtr<>& type) {
        if (ValuePtr<PointerType> pointer_ty = dyn_cast<PointerType>(type)) {
          TypeSizeAlignmentLiteral result;
          result.size = m_target_data->getPointerSize();
          result.alignment = m_target_data->getPointerABIAlignment();
          return result;
        } else if (isa<BooleanType>(type)) {
          return  type_size_alignment_simple(llvm::Type::getInt1Ty(context()));
        } else if (isa<ByteType>(type)) {
          return type_size_alignment_simple(llvm::Type::getInt8Ty(context()));
        } else if (ValuePtr<IntegerType> int_ty = dyn_cast<IntegerType>(type)) {
          return type_size_alignment_simple(integer_type(context(), m_target_data, int_ty->width()));
        } else if (ValuePtr<FloatType> float_ty = dyn_cast<FloatType>(type)) {
          return type_size_alignment_simple(float_type(context(), float_ty->width()));
        } else if (isa<EmptyType>(type) || isa<BlockType>(type)) {
          TypeSizeAlignmentLiteral result;
          result.size = 0;
          result.alignment = 1;
          return result;
        } else {
          throw BuildError("type not recognised by LLVM backend during aggregate lowering");
        }
      }
      
      AggregateLoweringPass::TypeSizeAlignment TargetCommon::type_size_alignment(const ValuePtr<>& type) {
        TypeSizeAlignmentLiteral literal = type_size_alignment_literal(type);
        AggregateLoweringPass::TypeSizeAlignment result;
        result.size = FunctionalBuilder::size_value(type->context(), literal.size, type->location());
        result.alignment = FunctionalBuilder::size_value(type->context(), literal.alignment, type->location());
        return result;
      }
      
      std::pair<ValuePtr<>, ValuePtr<> > TargetCommon::type_from_alignment(const ValuePtr<>& alignment) {
        if (ValuePtr<IntegerValue> alignment_int_val = dyn_cast<IntegerValue>(alignment)) {
          boost::optional<unsigned> alignment_val = alignment_int_val->value().unsigned_value();
          if (alignment_val) {
            unsigned real_alignment = std::min(*alignment_val, 16u);
            for (; real_alignment > 1; real_alignment /= 2) {
              unsigned abi_alignment = m_target_data->getABIIntegerTypeAlignment(real_alignment*8);
              if (abi_alignment == real_alignment)
                break;
            }
            
            IntegerType::Width width;
            switch (real_alignment) {
            case 1: width = IntegerType::i8; break;
            case 2: width = IntegerType::i16; break;
            case 4: width = IntegerType::i32; break;
            case 8: width = IntegerType::i64; break;
            case 16: width = IntegerType::i128; break;
            default: PSI_FAIL("should not reach here");
            }
            
            ValuePtr<> int_type = FunctionalBuilder::int_type(alignment->context(), width, false, alignment->location());
            return std::make_pair(int_type, FunctionalBuilder::size_value(alignment->context(), real_alignment, alignment->location()));
          }
        }
        
        return std::make_pair(FunctionalBuilder::byte_type(alignment->context(), alignment->location()),
                              FunctionalBuilder::size_value(alignment->context(), 1, alignment->location()));
      }

      TargetCommon::ParameterHandler::ParameterHandler(const ValuePtr<>& lowered_type)
      : m_lowered_type(lowered_type) {
        PSI_ASSERT(lowered_type);
      }

      /**
       * A simple handler which just uses the LLVM default mechanism to pass each parameter.
       */
      class TargetCommon::ParameterHandlerSimple : public ParameterHandler {
        LoweredType m_type;
        
      public:
        ParameterHandlerSimple(const LoweredType& type)
        : ParameterHandler(type.register_type()),
        m_type(type) {
        }

        virtual bool return_by_sret() const {
          return false;
        }

        virtual ValuePtr<> pack(AggregateLoweringPass::FunctionRunner& builder, const ValuePtr<>& source_value, const SourceLocation&) const {
          return builder.rewrite_value_register(source_value).value;
        }

        virtual void unpack(AggregateLoweringPass::FunctionRunner& runner, const ValuePtr<>& source_value, const ValuePtr<>& target_value, const SourceLocation&) const {
          runner.add_mapping(source_value, LoweredValue::register_(m_type, false, target_value));
        }

        virtual ValuePtr<> return_by_sret_setup(AggregateLoweringPass::FunctionRunner&, const SourceLocation&) const {
          return ValuePtr<>();
        }

        virtual ValuePtr<Instruction> return_pack(AggregateLoweringPass::FunctionRunner& builder, const ValuePtr<>& value, const SourceLocation& location) const {
          ValuePtr<> lowered_value = builder.rewrite_value_register(value).value;
          return builder.builder().return_(lowered_value, location);
        }

        virtual void return_unpack(AggregateLoweringPass::FunctionRunner& runner, const ValuePtr<>&, const ValuePtr<>& source_value, const ValuePtr<>& target_value, const SourceLocation&) const {
          runner.add_mapping(source_value, LoweredValue::register_(m_type, false, target_value));
        }
      };

      /**
       * Create an instance of ParameterHandlerSimple, which handles a
       * parameter type by assuming that LLVM already has the correct
       * behaviour.
       */
      boost::shared_ptr<TargetCommon::ParameterHandler> TargetCommon::parameter_handler_simple(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<>& type) {
        return boost::make_shared<ParameterHandlerSimple>(rewriter.rewrite_type(type));
      }

      /**
       * A handler which converts the Tvm value to an LLVM value of a
       * specific type by writing it to memory on the stack and reading it
       * back.
       */
      class TargetCommon::ParameterHandlerChangeTypeByMemory : public ParameterHandler {
        LoweredType m_type;
        
      public:
        ParameterHandlerChangeTypeByMemory(const LoweredType& type, const ValuePtr<>& lowered_type)
        : ParameterHandler(lowered_type),
        m_type(type) {
        }

        virtual bool return_by_sret() const {
          return false;
        }

        virtual ValuePtr<> pack(AggregateLoweringPass::FunctionRunner& builder, const ValuePtr<>& source_value, const SourceLocation& location) const {
          LoweredValue value = builder.rewrite_value(source_value);
          ValuePtr<> ptr = builder.alloca_(m_type, location);
          builder.store_value(value, ptr, location);
          ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(ptr, lowered_type(), location);
          return builder.builder().load(cast_ptr, location);
        }

        virtual void unpack(AggregateLoweringPass::FunctionRunner& runner, const ValuePtr<>& source_value, const ValuePtr<>& target_value, const SourceLocation& location) const {
          ValuePtr<> ptr = runner.builder().alloca_(lowered_type(), location);
          runner.builder().store(target_value, ptr, location);
          LoweredValue loaded = runner.load_value(m_type, ptr, location);
          runner.add_mapping(source_value, loaded);
        }

        virtual ValuePtr<> return_by_sret_setup(AggregateLoweringPass::FunctionRunner&, const SourceLocation&) const {
          return ValuePtr<>();
        }

        virtual ValuePtr<Instruction> return_pack(AggregateLoweringPass::FunctionRunner& builder, const ValuePtr<>& value, const SourceLocation& location) const {
          ValuePtr<> packed_value = pack(builder, value, location);
          return builder.builder().return_(packed_value, location);
        }

        virtual void return_unpack(AggregateLoweringPass::FunctionRunner& builder, const ValuePtr<>&, const ValuePtr<>& source_value, const ValuePtr<>& target_value, const SourceLocation& location) const {
          unpack(builder, source_value, target_value, location);
        }
      };

      /**
       * Return a ParameterHandler which changes the LLVM type used by
       * writing the value out to memory on the stack and reading it
       * back as a different type.
       *
       * \param type The original type of the parameter.
       * \param llvm_type The type LLVM will use for the parameter.
       */
      boost::shared_ptr<TargetCommon::ParameterHandler> TargetCommon::parameter_handler_change_type_by_memory(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<>& type, const ValuePtr<>& lowered_type) {
        return boost::make_shared<ParameterHandlerChangeTypeByMemory>(rewriter.rewrite_type(type), lowered_type);
      }

      /**
       * A handler which always passes the parameter as a pointer,
       * allocating storage when passing the parameter using alloca, and
       * returning by writing to the pointer in the first function
       * parameter.
       */
      class TargetCommon::ParameterHandlerForcePtr : public ParameterHandler {
        LoweredType m_type;
        
      public:
        ParameterHandlerForcePtr(Context& context, const LoweredType& type, const SourceLocation& location)
        : ParameterHandler(FunctionalBuilder::byte_pointer_type(context, location)),
        m_type(type) {
        }

        virtual bool return_by_sret() const {
          return true;
        }

        virtual ValuePtr<> pack(AggregateLoweringPass::FunctionRunner& builder, const ValuePtr<>& source_value, const SourceLocation& location) const {
          LoweredValue val = builder.rewrite_value(source_value);
          ValuePtr<> ptr = builder.alloca_(val.type(), location);
          builder.store_value(val, ptr, location);
          return ptr;
        }

        virtual void unpack(AggregateLoweringPass::FunctionRunner& runner, const ValuePtr<>& source_value, const ValuePtr<>& target_value, const SourceLocation& location) const {
          LoweredValue val = runner.load_value(m_type, target_value, location);
          runner.add_mapping(source_value, val);
        }

        virtual ValuePtr<> return_by_sret_setup(AggregateLoweringPass::FunctionRunner& runner, const SourceLocation& location) const {
          return runner.alloca_(m_type, location);
        }

        virtual ValuePtr<Instruction> return_pack(AggregateLoweringPass::FunctionRunner& builder, const ValuePtr<>& value, const SourceLocation& location) const {
          ValuePtr<> sret_parameter = builder.new_function()->parameters().at(0);
          LoweredValue rewritten = builder.rewrite_value(value);
          builder.store_value(rewritten, sret_parameter, location);
          return builder.builder().return_(sret_parameter, location);
        }

        virtual void return_unpack(AggregateLoweringPass::FunctionRunner& builder, const ValuePtr<>& sret_addr, const ValuePtr<>& source_value, const ValuePtr<>&, const SourceLocation& location) const {
          LoweredValue loaded = builder.load_value(m_type, sret_addr, location);
          builder.add_mapping(source_value, loaded);
        }
      };

      /**
       * Return a ParameterHandler which forces LLVM to pass the
       * parameter using a pointer to its value. This should only be
       * used when such a "by-reference" strategy will not be
       * correctly handled by LLVM.
       */
      boost::shared_ptr<TargetCommon::ParameterHandler> TargetCommon::parameter_handler_force_ptr(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<>& type) {
        return boost::make_shared<ParameterHandlerForcePtr>(boost::ref(rewriter.context()), rewriter.rewrite_type(type), type->location());
      }

      /**
       * Simple default implementation - this assumes that everything
       * works correctly in LLVM.
       */
      class TargetDefault : public TargetCommon {
      private:
        struct Callback : TargetCommon::Callback {
          virtual boost::shared_ptr<ParameterHandler> parameter_type_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, CallingConvention cconv, const ValuePtr<>& type) const {
            return TargetCommon::parameter_handler_simple(rewriter, type);
          }

          virtual bool convention_supported(CallingConvention) const {
            return true;
          }
        };
        
        Callback m_callback;
        boost::shared_ptr<llvm::TargetMachine> m_target_machine;

      public:
        TargetDefault(llvm::LLVMContext *context, const boost::shared_ptr<llvm::TargetMachine>& target_machine)
        : TargetCommon(&m_callback, context, target_machine->getTargetData()), m_target_machine(target_machine) {}
      };

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
       * Get the machine-specific set of LLVM workarounds for a given
       * machine. If no such workaround are available, this returns a
       * dummy class, but that may well break in some cases.
       *
       * \param triple An LLVM target triple, which will be parsed
       * using the llvm::Triple class.
       */
      boost::shared_ptr<TargetCallback> create_target_fixes(llvm::LLVMContext *context, const boost::shared_ptr<llvm::TargetMachine>& target_machine, const std::string& triple) {
        llvm::Triple parsed_triple(triple);

        switch (parsed_triple.getArch()) {
        case llvm::Triple::x86_64:
          switch (parsed_triple.getOS()) {
          case llvm::Triple::Linux: return create_target_fixes_amd64(context, target_machine);
          default: break;
          }
          break;

        default:
          break;
        }

#if 0
        return boost::make_shared<TargetDefault>(target_machine);
#else
        throw BuildError("Target " + triple + " not supported");
#endif
      }
    }
  }
}
