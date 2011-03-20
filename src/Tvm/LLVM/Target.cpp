#include "Builder.hpp"
#include "Target.hpp"
#include "../FunctionalBuilder.hpp"

#include <boost/make_shared.hpp>
#include <boost/optional.hpp>
#include <boost/ref.hpp>

#include <llvm/Function.h>
#include <llvm/Target/TargetData.h>
#include <llvm/ADT/Triple.h>
#include <llvm/Target/TargetRegistry.h>

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
      TargetCommon::lower_function_helper(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, FunctionTypeTerm* function_type) {
        if (!m_callback->convention_supported(function_type->calling_convention()))
          throw BuildError("Calling convention is not supported on this platform");
        
        LowerFunctionHelperResult result;
        std::vector<Term*> parameter_types;

        result.return_handler =
          m_callback->parameter_type_info(rewriter, function_type->calling_convention(), function_type->result_type());
        Term *return_type = result.return_handler->lowered_type();
        result.sret = result.return_handler->return_by_sret();
        if (result.sret)
          parameter_types.push_back(return_type);

        result.n_phantom = function_type->n_phantom_parameters();
        result.n_passed_parameters = function_type->n_parameters() - result.n_phantom;
        for (std::size_t i = 0; i != result.n_passed_parameters; ++i) {
          boost::shared_ptr<ParameterHandler> handler = m_callback->parameter_type_info
            (rewriter, function_type->calling_convention(), function_type->parameter_type(i+result.n_phantom));
          result.parameter_handlers.push_back(handler);
          parameter_types.push_back(handler->lowered_type());
        }
        
        result.lowered_type = rewriter.context().get_function_type_fixed
          (function_type->calling_convention(), return_type, parameter_types);
          
        return result;
      }

      void TargetCommon::lower_function_call(AggregateLoweringPass::FunctionRunner& runner, FunctionCall::Ptr term) {
        LowerFunctionHelperResult helper_result = lower_function_helper(runner, term->target_function_type());
        
        int sret = helper_result.sret ? 1 : 0;
        ScopedArray<Term*> parameters(sret + helper_result.n_passed_parameters);

        Term *sret_addr = 0;
        if (helper_result.sret) {
          sret_addr = helper_result.return_handler->return_by_sret_setup(runner);
          parameters[0] = sret_addr;
        }
        
        for (std::size_t i = 0; i != helper_result.n_passed_parameters; ++i)
          parameters[i+sret] = helper_result.parameter_handlers[i]->pack(runner, term->parameter(i+helper_result.n_phantom));
        
        Term *lowered_target = runner.rewrite_value_stack(term->target());
        Term *cast_target = FunctionalBuilder::pointer_cast(lowered_target, helper_result.lowered_type);
        Term *result = runner.builder().call(cast_target, parameters);
        
        helper_result.return_handler->return_unpack(runner, sret_addr, term, result);
      }

      InstructionTerm* TargetCommon::lower_return(AggregateLoweringPass::FunctionRunner& runner, Term *value) {
        FunctionTypeTerm *function_type = runner.old_function()->function_type();
        boost::shared_ptr<ParameterHandler> return_handler =
          m_callback->parameter_type_info(runner, function_type->calling_convention(), function_type->result_type());
          
        return return_handler->return_pack(runner, value);
      }

      FunctionTerm* TargetCommon::lower_function(AggregateLoweringPass& pass, FunctionTerm *function) {
        LowerFunctionHelperResult helper_result = lower_function_helper(pass.global_rewriter(), function->function_type());
        return pass.target_module()->new_function(function->name(), helper_result.lowered_type);
      }
      
      void TargetCommon::lower_function_entry(AggregateLoweringPass::FunctionRunner& runner, FunctionTerm *source_function, FunctionTerm *target_function) {
        LowerFunctionHelperResult helper_result = lower_function_helper(runner, source_function->function_type());
        int sret = helper_result.sret ? 1 : 0;
        for (std::size_t i = 0; i != helper_result.n_passed_parameters; ++i)
          helper_result.parameter_handlers[i]->unpack(runner, source_function->parameter(i+helper_result.n_phantom), target_function->parameter(i + sret));
      }
      
      Term* TargetCommon::convert_value(Term *value, Term *type) {
        PSI_NOT_IMPLEMENTED();
      }
      
      TargetCommon::TypeSizeAlignmentLiteral TargetCommon::type_size_alignment_simple(const llvm::Type *llvm_type) {
        TypeSizeAlignmentLiteral result;
        result.size = m_target_data->getTypeAllocSize(llvm_type);
        result.alignment = m_target_data->getABITypeAlignment(llvm_type);
        return result;
      }
      
      TargetCommon::TypeSizeAlignmentLiteral TargetCommon::type_size_alignment_literal(Term *type) {
        if (PointerType::Ptr pointer_ty = dyn_cast<PointerType>(type)) {
          TypeSizeAlignmentLiteral result;
          result.size = m_target_data->getPointerSize();
          result.alignment = m_target_data->getPointerABIAlignment();
          return result;
        } else if (isa<BooleanType>(type)) {
          return  type_size_alignment_simple(llvm::Type::getInt1Ty(context()));
        } else if (isa<ByteType>(type)) {
          return type_size_alignment_simple(llvm::Type::getInt8Ty(context()));
        } else if (IntegerType::Ptr int_ty = dyn_cast<IntegerType>(type)) {
          return type_size_alignment_simple(integer_type(context(), m_target_data, int_ty->width()));
        } else if (FloatType::Ptr float_ty = dyn_cast<FloatType>(type)) {
          return type_size_alignment_simple(float_type(context(), float_ty->width()));
        } else if (isa<EmptyType>(type)) {
          TypeSizeAlignmentLiteral result;
          result.size = 0;
          result.alignment = 1;
          return result;
        } else {
          throw BuildError("type not recognised by LLVM backend during aggregate lowering");
        }
      }
      
      AggregateLoweringPass::TypeSizeAlignment TargetCommon::type_size_alignment(Term *type) {
        TypeSizeAlignmentLiteral literal = type_size_alignment_literal(type);
        AggregateLoweringPass::TypeSizeAlignment result;
        result.size = FunctionalBuilder::size_value(type->context(), literal.size);
        result.alignment = FunctionalBuilder::size_value(type->context(), literal.alignment);
        return result;
      }
      
      std::pair<Term*, Term*> TargetCommon::type_from_alignment(Term *alignment) {
        if (IntegerValue::Ptr alignment_int_val = dyn_cast<IntegerValue>(alignment)) {
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
            
            Term *int_type = FunctionalBuilder::int_type(alignment->context(), width, false);
            return std::make_pair(int_type, FunctionalBuilder::size_value(alignment->context(), real_alignment));
          }
        }
        
        return std::make_pair(FunctionalBuilder::byte_type(alignment->context()),
                              FunctionalBuilder::size_value(alignment->context(), 1));
      }

      TargetCommon::ParameterHandler::ParameterHandler(Term *type, Term *lowered_type, CallingConvention calling_convention)
        : m_type(type), m_lowered_type(lowered_type), m_calling_convention(calling_convention) {
        PSI_ASSERT(type);
        PSI_ASSERT(lowered_type);
      }

      /**
       * A simple handler which just uses the LLVM default mechanism to pass each parameter.
       */
      class TargetCommon::ParameterHandlerSimple : public ParameterHandler {
      public:
        ParameterHandlerSimple(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, Term *type, CallingConvention calling_convention)
	  : ParameterHandler(type, rewriter.rewrite_type(type).stack_type(), calling_convention) {
	}

	virtual bool return_by_sret() const {
	  return false;
	}

        virtual Term* pack(AggregateLoweringPass::FunctionRunner& builder, Term *source_value) const {
          return builder.rewrite_value_stack(source_value);
        }

        virtual void unpack(AggregateLoweringPass::FunctionRunner& runner, Term *source_value, Term *target_value) const {
          runner.add_mapping(source_value, target_value, true);
        }

        virtual Term* return_by_sret_setup(AggregateLoweringPass::FunctionRunner&) const {
          return NULL;
        }

        virtual InstructionTerm* return_pack(AggregateLoweringPass::FunctionRunner& builder, Term *value) const {
          Term *lowered_value = builder.rewrite_value_stack(value);
          return builder.builder().return_(lowered_value);
        }

        virtual void return_unpack(AggregateLoweringPass::FunctionRunner& runner, Term*, Term *source_value, Term *target_value) const {
          runner.add_mapping(source_value, target_value, true);
        }
      };

      /**
       * Create an instance of ParameterHandlerSimple, which handles a
       * parameter type by assuming that LLVM already has the correct
       * behaviour.
       */
      boost::shared_ptr<TargetCommon::ParameterHandler> TargetCommon::parameter_handler_simple(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, Term *type, CallingConvention calling_convention) {
	return boost::make_shared<ParameterHandlerSimple>(boost::ref(rewriter), type, calling_convention);
      }

      /**
       * A handler which converts the Tvm value to an LLVM value of a
       * specific type by writing it to memory on the stack and reading it
       * back.
       */
      class TargetCommon::ParameterHandlerChangeTypeByMemory : public ParameterHandler {

      public:
	ParameterHandlerChangeTypeByMemory(Term *type, Term *lowered_type, CallingConvention calling_convention)
	  : ParameterHandler(type, lowered_type, calling_convention) {
	}

	virtual bool return_by_sret() const {
	  return false;
	}

        virtual Term* pack(AggregateLoweringPass::FunctionRunner& builder, Term *source_value) const {
          AggregateLoweringPass::Value value = builder.rewrite_value(source_value);
          
          Term *ptr;
          if (value.on_stack()) {
            ptr = builder.builder().alloca_(value.value()->type());
            builder.builder().store(value.value(), ptr);
          } else {
            ptr = value.value();
          }
          
          Term *cast_ptr = FunctionalBuilder::pointer_cast(ptr, lowered_type());
          return builder.builder().load(cast_ptr);
	}

        virtual void unpack(AggregateLoweringPass::FunctionRunner& runner, Term *source_value, Term *target_value) const {
          Term *ptr = runner.builder().alloca_(lowered_type());
          runner.builder().store(target_value, ptr);
          runner.load_value(source_value, ptr);
	}

        virtual Term* return_by_sret_setup(AggregateLoweringPass::FunctionRunner&) const {
	  return NULL;
	}

        virtual InstructionTerm* return_pack(AggregateLoweringPass::FunctionRunner& builder, Term *value) const {
          Term *packed_value = pack(builder, value);
          return builder.builder().return_(packed_value);
        }

	virtual void return_unpack(AggregateLoweringPass::FunctionRunner& builder, Term*, Term *source_value, Term *target_value) const {
          unpack(builder, source_value, target_value);
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
      boost::shared_ptr<TargetCommon::ParameterHandler> TargetCommon::parameter_handler_change_type_by_memory(Term *type, Term *lowered_type, CallingConvention calling_convention) {
	return boost::make_shared<ParameterHandlerChangeTypeByMemory>(type, lowered_type, calling_convention);
      }

      /**
       * A handler which always passes the parameter as a pointer,
       * allocating storage when passing the parameter using alloca, and
       * returning by writing to the pointer in the first function
       * parameter.
       */
      class TargetCommon::ParameterHandlerForcePtr : public ParameterHandler {
      public:
	ParameterHandlerForcePtr(Context& target_context, Term *type, CallingConvention calling_convention)
	  : ParameterHandler(type, FunctionalBuilder::byte_pointer_type(target_context), calling_convention) {
	}

	virtual bool return_by_sret() const {
	  return true;
	}

        virtual Term* pack(AggregateLoweringPass::FunctionRunner& builder, Term *source_value) const {
          AggregateLoweringPass::Value value = builder.rewrite_value(source_value);
          
          if (value.on_stack()) {
            Term *ptr = builder.builder().alloca_(value.value()->type());
            builder.builder().store(value.value(), ptr);
            return ptr;
          } else {
            return value.value();
          }
        }

        virtual void unpack(AggregateLoweringPass::FunctionRunner& runner, Term *source_value, Term *target_value) const {
          runner.load_value(source_value, target_value);
        }

	virtual Term* return_by_sret_setup(AggregateLoweringPass::FunctionRunner& runner) const {
          AggregateLoweringPass::Type lowered_type = runner.rewrite_type(type());
          if (lowered_type.heap_type())
            return runner.builder().alloca_(lowered_type.heap_type());
          
          if (ArrayType::Ptr array_ty = dyn_cast<ArrayType>(type())) {
            AggregateLoweringPass::Type element_type = runner.rewrite_type(array_ty->element_type());
            if (element_type.heap_type()) {
              Term *length = runner.rewrite_value_stack(array_ty->length());
              return runner.builder().alloca_(element_type.heap_type(), length);
            }
          }
          
          AggregateLoweringPass::TypeSizeAlignment size_align = runner.pass().target_callback->type_size_alignment(type());
          Context& context = runner.new_function()->context();
          return runner.builder().alloca_(FunctionalBuilder::byte_type(context), size_align.size, size_align.alignment);
	}

        virtual InstructionTerm* return_pack(AggregateLoweringPass::FunctionRunner& builder, Term *value) const {
          Term *sret_parameter = builder.new_function()->parameter(0);
          builder.store_value(value, sret_parameter);
          return builder.builder().return_(sret_parameter);
	}

        virtual void return_unpack(AggregateLoweringPass::FunctionRunner& builder, Term *sret_addr, Term *source_value, Term*) const {
          builder.load_value(source_value, sret_addr);
        }
      };

      /**
       * Return a ParameterHandler which forces LLVM to pass the
       * parameter using a pointer to its value. This should only be
       * used when such a "by-reference" strategy will not be
       * correctly handled by LLVM.
       */
      boost::shared_ptr<TargetCommon::ParameterHandler> TargetCommon::parameter_handler_force_ptr(Context& target_context, Term *type, CallingConvention calling_convention) {
	return boost::make_shared<ParameterHandlerForcePtr>(boost::ref(target_context), type, calling_convention);
      }

      /**
       * Simple default implementation - this assumes that everything
       * works correctly in LLVM.
       */
      class TargetDefault : public TargetCommon {
      private:
	struct Callback : TargetCommon::Callback {
          virtual boost::shared_ptr<ParameterHandler> parameter_type_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, CallingConvention cconv, Term *type) const {
            return TargetCommon::parameter_handler_simple(rewriter, type, cconv);
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
       * Get the machine-specific set of LLVM workarounds for a given
       * machine. If no such workaround are available, this returns a
       * dummy class, but that may well break in some cases.
       *
       * \param triple An LLVM target triple, which will be parsed
       * using the llvm::Triple class.
       */
      boost::shared_ptr<AggregateLoweringPass::TargetCallback> create_target_fixes(llvm::LLVMContext *context, const boost::shared_ptr<llvm::TargetMachine>& target_machine, const std::string& triple) {
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
