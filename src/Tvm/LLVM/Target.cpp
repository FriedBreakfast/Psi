#include "Builder.hpp"
#include "Target.hpp"

#include <boost/make_shared.hpp>
#include <boost/optional.hpp>
#include <boost/ref.hpp>

#include <llvm/Function.h>
#include <llvm/ADT/Triple.h>

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      TargetFunctionCallCommon::TargetFunctionCallCommon(const Callback *callback) : m_callback(callback) {
      }

      /**
       * \brief Map from a Tvm calling convention identifier to an LLVM one.
       *
       * This will raise an exception if the given calling convention
       * is not supported on the target platform.
       */
      llvm::CallingConv::ID TargetFunctionCallCommon::map_calling_convention(CallingConvention conv) {
	llvm::CallingConv::ID id;
	switch (conv) {
	case cconv_c: id = llvm::CallingConv::C; break;
	case cconv_x86_stdcall: id = llvm::CallingConv::X86_StdCall; break;
	case cconv_x86_thiscall: id = llvm::CallingConv::X86_ThisCall; break;
	case cconv_x86_fastcall: id = llvm::CallingConv::X86_FastCall; break;

	default:
	  throw BuildError("Unsupported calling convention");
	}

	if (!m_callback->convention_supported(id))
	  throw BuildError("Calling convention does not make sense for target platform");

	return id;
      }

      /// \brief Check whether LLVM supports this convention on all
      /// platforms.
      bool TargetFunctionCallCommon::convention_always_supported(llvm::CallingConv::ID id) {
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

      /// \copydoc TargetFixes::function_type
      const llvm::FunctionType* TargetFunctionCallCommon::function_type(ConstantBuilder& builder, FunctionTypeTerm *term) {
	llvm::CallingConv::ID cconv = map_calling_convention(term->calling_convention());

	std::size_t n_phantom = term->n_phantom_parameters();
	std::size_t n_passed_parameters = term->n_parameters() - n_phantom;
	std::vector<const llvm::Type*> parameter_types;

	boost::shared_ptr<ParameterHandler> return_handler = m_callback->parameter_type_info(builder, cconv, term->result_type());
	const llvm::Type *return_type;
	if (return_handler->return_by_sret()) {
	  return_type = llvm::Type::getVoidTy(builder.llvm_context());
	  parameter_types.push_back(return_handler->llvm_type());
	} else {
	  return_type = return_handler->llvm_type();
	}

	for (std::size_t i = 0; i != n_passed_parameters; ++i) {
	  boost::shared_ptr<ParameterHandler> handler = m_callback->parameter_type_info(builder, cconv, term->parameter_type(i+n_phantom));
	  parameter_types.push_back(handler->llvm_type());
	}

	return llvm::FunctionType::get(return_type, parameter_types, false);
      }

      /// \copydoc TargetFixes::function_call
      BuiltValue* TargetFunctionCallCommon::function_call(FunctionBuilder& builder, llvm::Value *target, FunctionTypeTerm *target_type, FunctionCall::Ptr insn) {
	llvm::CallingConv::ID cconv = map_calling_convention(target_type->calling_convention());

	std::size_t n_phantom = target_type->n_phantom_parameters();
	std::size_t n_passed_parameters = target_type->n_parameters() - n_phantom;
	std::vector<const llvm::Type*> parameter_types;
	llvm::SmallVector<llvm::Value*, 4> parameters;

	boost::shared_ptr<ParameterHandler> return_handler = m_callback->parameter_type_info(builder, cconv, target_type->result_type());

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
	  boost::shared_ptr<ParameterHandler> handler = m_callback->parameter_type_info(builder, cconv, param->type());
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

      /// \copydoc TargetFixes::function_parameters_unpack
      void TargetFunctionCallCommon::function_parameters_unpack(FunctionBuilder& builder, FunctionTerm *function,
								llvm::Function *llvm_function, llvm::SmallVectorImpl<BuiltValue*>& result) {
	llvm::CallingConv::ID cconv = map_calling_convention(function->function_type()->calling_convention());

	std::size_t n_phantom = function->function_type()->n_phantom_parameters();
	std::size_t n_passed_parameters = function->function_type()->n_parameters() - n_phantom;

	result.resize(n_passed_parameters);
	llvm::Function::arg_iterator jt = llvm_function->arg_begin();

	// Need to check if the first parameter is an sret.
	boost::shared_ptr<ParameterHandler> return_handler = m_callback->parameter_type_info(builder, cconv, function->function_type()->result_type());
	if (return_handler->return_by_sret())
	  ++jt;

	PSI_ASSERT(n_passed_parameters + (return_handler->return_by_sret() ? 1 : 0) == llvm_function->getFunctionType()->getNumParams());
	for (std::size_t i = 0; i != n_passed_parameters; ++i, ++jt) {
	  boost::shared_ptr<ParameterHandler> handler = m_callback->parameter_type_info(builder, cconv, function->parameter(i + n_phantom)->type());
	  result[i] = handler->unpack(builder, &*jt);
	}
      }

      /// \copydoc TargetFixes::function_return
      void TargetFunctionCallCommon::function_return(FunctionBuilder& builder, FunctionTypeTerm *function_type, llvm::Function *llvm_function, Term *value) {
	llvm::CallingConv::ID cconv = map_calling_convention(function_type->calling_convention());
	boost::shared_ptr<ParameterHandler> return_handler = m_callback->parameter_type_info(builder, cconv, function_type->result_type());
	return_handler->return_pack(builder, llvm_function, value);
      }

      /**
       * A simple handler which just uses the LLVM default mechanism to pass each parameter.
       */
      class TargetFunctionCallCommon::ParameterHandlerSimple : public ParameterHandler {
      public:
	ParameterHandlerSimple(ConstantBuilder& builder, Term *type, llvm::CallingConv::ID calling_convention)
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
       * Create an instance of ParameterHandlerSimple, which handles a
       * parameter type by assuming that LLVM already has the correct
       * behaviour.
       */
      boost::shared_ptr<TargetFunctionCallCommon::ParameterHandler> TargetFunctionCallCommon::parameter_handler_simple(ConstantBuilder& builder, Term *type, llvm::CallingConv::ID cconv) {
	return boost::make_shared<ParameterHandlerSimple>(boost::ref(builder), type, cconv);
      }

      /**
       * A handler which converts the Tvm value to an LLVM value of a
       * specific type by writing it to memory on the stack and reading it
       * back.
       */
      class TargetFunctionCallCommon::ParameterHandlerChangeTypeByMemory : public ParameterHandler {

      public:
	ParameterHandlerChangeTypeByMemory(Term *type, const llvm::Type *llvm_type, llvm::CallingConv::ID calling_convention)
	  : ParameterHandler(type, llvm_type, calling_convention) {
	}

	virtual bool return_by_sret() const {
	  return false;
	}

	virtual llvm::Value* pack(FunctionBuilder& builder, Term *value) const {
	  llvm::Value *ptr_value = builder.build_value(value)->raw_value();
	  llvm::Value *cast_ptr = builder.irbuilder().CreateBitCast(ptr_value, llvm_type()->getPointerTo());
	  return builder.irbuilder().CreateLoad(cast_ptr);
	}

	virtual BuiltValue* unpack(FunctionBuilder& builder, llvm::Value *value) const {
	  llvm::AllocaInst *alloca_ptr = builder.irbuilder().CreateAlloca(value->getType());
	  alloca_ptr->setAlignment(builder.constant_type_alignment(type()));
	  builder.irbuilder().CreateStore(value, alloca_ptr);
	  llvm::Value *byte_ptr = builder.irbuilder().CreateBitCast(alloca_ptr, builder.get_pointer_type());
	  return builder.new_function_value_raw(type(), byte_ptr);
	}

	virtual llvm::Value* return_by_sret_setup(FunctionBuilder&) const {
	  return NULL;
	}

	virtual void return_pack(FunctionBuilder& builder, llvm::Function*, Term *value) const {
	  builder.irbuilder().CreateRet(pack(builder, value));
	}

	virtual BuiltValue* return_unpack(FunctionBuilder& builder, llvm::Value *value, llvm::Value*) const {
	  return unpack(builder, value);
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
      boost::shared_ptr<TargetFunctionCallCommon::ParameterHandler> TargetFunctionCallCommon::parameter_handler_change_type_by_memory(Term *type, const llvm::Type *llvm_type, llvm::CallingConv::ID calling_convention) {
	return boost::make_shared<ParameterHandlerChangeTypeByMemory>(type, llvm_type, calling_convention);
      }

      /**
       * A handler which always passes the parameter as a pointer,
       * allocating storage when passing the parameter using alloca, and
       * returning by writing to the pointer in the first function
       * parameter.
       */
      class TargetFunctionCallCommon::ParameterHandlerForcePtr : public ParameterHandler {
      public:
	ParameterHandlerForcePtr(ConstantBuilder& builder, Term *type, llvm::CallingConv::ID calling_convention)
	  : ParameterHandler(type, builder.get_pointer_type(), calling_convention) {
	}

	virtual bool return_by_sret() const {
	  return true;
	}

	virtual llvm::Value* pack(FunctionBuilder& builder, Term *value) const {
	  return builder.build_value(value)->raw_value();
	}

	virtual BuiltValue* unpack(FunctionBuilder& builder, llvm::Value *value) const {
	  return builder.new_function_value_raw(type(), value);
	}

	virtual llvm::Value* return_by_sret_setup(FunctionBuilder& builder) const {
	  uint64_t alloca_size = builder.constant_type_size(type());
	  llvm::Value *alloca_size_value = llvm::ConstantInt::get(builder.get_intptr_type(), alloca_size);
	  llvm::AllocaInst *alloca_ptr = builder.irbuilder().CreateAlloca(builder.get_byte_type(), alloca_size_value);
	  alloca_ptr->setAlignment(builder.constant_type_alignment(type()));
	  return alloca_ptr;
	}

	virtual void return_pack(FunctionBuilder& builder, llvm::Function* function, Term *value) const {
	  llvm::Value *sret_parameter = &function->getArgumentList().front();
	  PSI_FAIL("not implemented");
	}

	virtual BuiltValue* return_unpack(FunctionBuilder& builder, llvm::Value*, llvm::Value* sret_addr) const {
	  return builder.new_function_value_raw(type(), sret_addr);
	}
      };

      /**
       * Return a ParameterHandler which forces LLVM to pass the
       * parameter using a pointer to its value. This should only be
       * used when such a "by-reference" strategy will not be
       * correctly handled by LLVM.
       */
      boost::shared_ptr<TargetFunctionCallCommon::ParameterHandler> TargetFunctionCallCommon::parameter_handler_force_ptr(ConstantBuilder& builder, Term *type, llvm::CallingConv::ID cconv) {
	return boost::make_shared<ParameterHandlerForcePtr>(boost::ref(builder), type, cconv);
      }

      /**
       * Simple default implementation - this assumes that everything
       * works correctly in LLVM.
       */
      struct TargetFixes_Default : TargetFixes {
	struct Callback : TargetFunctionCallCommon::Callback {
	  virtual boost::shared_ptr<TargetFunctionCallCommon::ParameterHandler> parameter_type_info(ConstantBuilder& builder, llvm::CallingConv::ID cconv, Term *type) const {
	    return TargetFunctionCallCommon::parameter_handler_simple(builder, type, cconv);
	  }

	  virtual bool convention_supported(llvm::CallingConv::ID) const {
	    return true;
	  }
	};
      };

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
	  case llvm::Triple::Linux: return create_target_fixes_amd64();
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
