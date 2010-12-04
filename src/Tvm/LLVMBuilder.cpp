#include "LLVMBuilder.hpp"
#include "Core.hpp"
#include "Derived.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Recursive.hpp"

#include <stdexcept>
#include <boost/next_prior.hpp>

#include <llvm/LLVMContext.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Module.h>
#include <llvm/Support/IRBuilder.h>

namespace Psi {
  namespace Tvm {
    namespace {
      template<typename ValueMap, typename Callback>
      std::pair<typename ValueMap::value_type::second_type, bool>
      build_term(Term *term, ValueMap& values, const Callback& cb) {
	std::pair<typename ValueMap::iterator, bool> itp =
	  values.insert(std::make_pair(term, cb.invalid()));
	if (!itp.second) {
	  if (cb.valid(itp.first->second)) {
	    return std::make_pair(itp.first->second, false);
	  } else {
	    throw std::logic_error("Cyclical term found");
	  }
	}

	typename ValueMap::value_type::second_type r = cb.build(*term);
	if (cb.valid(r)) {
	  itp.first->second = r;
	} else {
	  values.erase(itp.first);
	  throw std::logic_error("LLVM term building failed");
	}

	return std::make_pair(r, true);
      }

      struct TypeBuilderCallback {
	LLVMValueBuilder *self;
	TypeBuilderCallback(LLVMValueBuilder *self_) : self(self_) {}

	LLVMType build(Term& term) const {
	  switch(term.term_type()) {
	  case term_recursive_parameter: {
	    throw std::logic_error("not implemented");
	  }

	  case term_functional: {
	    FunctionalTerm& cast_term = checked_cast<FunctionalTerm&>(term);
	    return cast_term.backend()->llvm_type(*self, cast_term);
	  }

	  case term_apply: {
	    Term* actual = checked_cast<ApplyTerm&>(term).unpack();
	    PSI_ASSERT(actual->term_type() != term_apply);
	    return self->type(actual);
	  }

	  case term_function_type: {
	    FunctionTypeTerm& actual = checked_cast<FunctionTypeTerm&>(term);
	    if (actual.calling_convention() == cconv_tvm) {
	      const llvm::Type* i8ptr = llvm::Type::getInt8PtrTy(self->context());
	      std::vector<const llvm::Type*> params(actual.n_parameters() - actual.n_phantom_parameters() + 1, i8ptr);
	      return LLVMType::known(llvm::FunctionType::get(i8ptr, params, false));
	    } else {
              std::size_t n_phantom = actual.n_phantom_parameters();
	      std::size_t n_parameters = actual.n_parameters() - n_phantom;
	      std::vector<const llvm::Type*> params;
	      for (std::size_t i = 0; i < n_parameters; ++i) {
		LLVMType param_type = self->type(actual.parameter(i+n_phantom)->type());
		if (!param_type.is_known())
		  throw std::logic_error("Only tvm calling convention supports dependent parameters");
		params.push_back(param_type.type());
	      }
	      LLVMType result_type = self->type(actual.result_type());
	      if (!result_type.is_known())
		throw std::logic_error("Only tvm calling convention support dependent result type");
	      return LLVMType::known(llvm::FunctionType::get(result_type.type(), params, false));
	    }
	  }

	  case term_function_parameter:
	    return LLVMType::unknown();

	  default:
	    /**
	     * Only terms which can be the type of a term should
	     * appear here. This restricts us to term_functional,
	     * term_apply, term_function_type and
	     * term_function_parameter.
	     *
	     * term_recursive should only occur inside term_apply.
	     *
	     * term_recursive_parameter should never be encountered
	     * since it should be expanded out by ApplyTerm::apply().
	     */
	    throw std::logic_error("unexpected type term type");
	  }
	}

	LLVMType invalid() const {
	  return LLVMType();
	}

	bool valid(const LLVMType& t) const {
	  return t.is_valid();
	}
      };

      template<typename ValueBuilderType, typename FunctionalCallback>
      struct ValueBuilderCallback {
	ValueBuilderType *self;
	FunctionalCallback functional_callback;
	ValueBuilderCallback(ValueBuilderType *self_) : self(self_) {}

	LLVMValue build(Term& term) const {
	  switch(term.term_type()) {
	  case term_recursive_parameter: {
	    throw std::logic_error("not implemented");
	  }

	  case term_functional: {
	    FunctionalTerm& cast_term = checked_cast<FunctionalTerm&>(term);
	    return functional_callback(*self, cast_term);
	  }

	  case term_apply: {
	    Term* actual = checked_cast<ApplyTerm&>(term).unpack();
	    PSI_ASSERT(actual->term_type() != term_apply);
	    return self->value(actual);
	  }

	  default:
	    /*
	     * term_function_type_parameter should never be passed
	     * here because function types are defined simply by their
	     * parameters.
	     *
	     * term_recursive needs to be nested inside term_apply.
	     *
	     * term_recursive_parameter should never be encountered
	     * since it should be expanded out by ApplyTerm::apply().
	     *
	     * term_function_type should only exist inside a pointer.
	     */
	    throw std::logic_error("unexpected term type");
	  }
	}

	LLVMValue invalid() const {
	  return LLVMValue();
	}

	bool valid(const LLVMValue& t) const {
	  return t.is_valid();
	}
      };

      struct ConstantFunctionalCallback {
	LLVMValue operator () (LLVMValueBuilder& self, FunctionalTerm& term) const {
	  return term.backend()->llvm_value_constant(self, term);
	}
      };

      typedef ValueBuilderCallback<LLVMConstantBuilder, ConstantFunctionalCallback> ConstantBuilderCallback;

      struct InstructionFunctionalCallback {
	LLVMValue operator () (LLVMFunctionBuilder& self, FunctionalTerm& term) const {
	  return term.backend()->llvm_value_instruction(self, term);
	}
      };

      typedef ValueBuilderCallback<LLVMFunctionBuilder, InstructionFunctionalCallback> InstructionBuilderCallback;

      struct GlobalBuilderCallback {
	LLVMValueBuilder *self;
	GlobalBuilderCallback(LLVMValueBuilder *self_) : self(self_) {}

	llvm::GlobalValue* build(Term& term) const {
	  switch (term.term_type()) {
	  case term_global_variable: {
	    GlobalVariableTerm& global = checked_cast<GlobalVariableTerm&>(term);
	    FunctionalTermPtr<PointerType> type = checked_cast_functional<PointerType>(global.type());
	    LLVMType llvm_type = self->type(type.backend().target_type());
	    if (llvm_type.is_known()) {
	      return new llvm::GlobalVariable(self->module(), llvm_type.type(), global.constant(), llvm::GlobalValue::InternalLinkage, NULL, "");
	    } else if (llvm_type.is_empty()) {
	      const llvm::Type *int8ty = llvm::Type::getInt8Ty(self->context());
	      llvm::Constant *init = llvm::ConstantInt::get(int8ty, 0);
	      return new llvm::GlobalVariable(self->module(), int8ty, true, llvm::GlobalValue::InternalLinkage, init, "");
	    } else {
	      PSI_ASSERT(llvm_type.is_unknown());
	      throw std::logic_error("global variable has unknown type");
	    }
	  }

	  case term_function: {
	    FunctionTerm& func = checked_cast<FunctionTerm&>(term);
	    FunctionalTermPtr<PointerType> type = checked_cast_functional<PointerType>(func.type());
	    FunctionTypeTerm* func_type = checked_cast<FunctionTypeTerm*>(type.backend().target_type());
	    LLVMType llvm_ty = self->type(func_type);
	    PSI_ASSERT(llvm_ty.is_known() && llvm_ty.type()->isFunctionTy());
	    return llvm::Function::Create(llvm::cast<llvm::FunctionType>(llvm_ty.type()),
					  llvm::GlobalValue::InternalLinkage, "", &self->module());
	  }

	  default:
	    PSI_FAIL("unexpected global term type");
	  }
	}

	llvm::GlobalValue* invalid() const {
	  return NULL;
	}

	bool valid(llvm::GlobalValue *p) const {
	  return p;
	}
      };
    }
      
    LLVMValueBuilder::LLVMValueBuilder(llvm::LLVMContext *context, llvm::Module *module)
      : m_parent(0), m_context(context), m_module(module), m_debug_stream(0) {
      PSI_ASSERT(m_context && m_module);
    }

    LLVMValueBuilder::LLVMValueBuilder(LLVMValueBuilder *parent)
      : m_parent(parent), m_context(parent->m_context), m_module(parent->m_module), m_debug_stream(0) {
    }

    LLVMValueBuilder::~LLVMValueBuilder() {
    }

    /**
     * Set a debugging stream.
     */
    void LLVMValueBuilder::set_debug(llvm::raw_ostream *stream) {
      m_debug_stream = stream;
    }

    /**
     * \brief Return the type specified by the specified term.
     *
     * Note that this is not the LLVM type of the LLVM value of this
     * term: it is the LLVM type of the LLVM value of terms whose type
     * is this term.
     */
    LLVMType LLVMValueBuilder::type(Term* term) {
      if (term->global() && m_parent) {
	return m_parent->type(term);
      } else {
	return build_term(term, m_type_terms, TypeBuilderCallback(this)).first;
      }
    }

    LLVMValue LLVMValueBuilder::value(Term* term) {
      if (term->global() && m_parent) {
	return m_parent->value(term);
      } else {
	switch (term->term_type()) {
	case term_function:
	case term_global_variable: {
	  llvm::GlobalValue *gv = global(checked_cast<GlobalTerm*>(term));

	  // Need to force global terms to be of type i8* since they are
	  // pointers, but the Global term builder can't do this
	  // because it needs to return a llvm::GlobalVariable
	  const llvm::Type* i8ptr = llvm::Type::getInt8PtrTy(*m_context);
	  return LLVMValue::known(llvm::ConstantExpr::getPointerCast(gv, i8ptr));
	}

        case term_function_parameter:
	case term_instruction:
	case term_phi:
        case term_block: {
	  ValueTermMap::iterator it = m_value_terms.find(term);
          PSI_ASSERT(it != m_value_terms.end());
          return it->second;
	}

        case term_recursive:
        case term_recursive_parameter:
        case term_function_type_parameter:
        case term_function_type_resolver:
          PSI_FAIL("term type should not be encountered by LLVM builder");

	default:
	  return value_impl(term);
	}
      }
    }

    llvm::GlobalValue* LLVMValueBuilder::global(GlobalTerm* term) {
      if ((term->term_type() != term_function) && (term->term_type() != term_global_variable))
	throw std::logic_error("cannot get global value for non-global variable");

      std::pair<llvm::GlobalValue*, bool> gv = build_term(term, m_global_terms, GlobalBuilderCallback(this));

      if (gv.second) {
	if (m_global_build_list.empty()) {
	  m_global_build_list.push_back(std::make_pair(term, gv.first));
	  while (!m_global_build_list.empty()) {
	    const std::pair<Term*, llvm::GlobalValue*>& t = m_global_build_list.front();
	    if (t.first->term_type() == term_function) {
	      FunctionTerm *psi_func = checked_cast<FunctionTerm*>(t.first);
	      llvm::Function *llvm_func = llvm::cast<llvm::Function>(t.second);
	      build_function(psi_func, llvm_func);
	    } else {
	      PSI_ASSERT(t.first->term_type() == term_global_variable);
	      GlobalVariableTerm *psi_var = checked_cast<GlobalVariableTerm*>(t.first);
	      llvm::GlobalVariable *llvm_var = llvm::cast<llvm::GlobalVariable>(t.second);
	      build_global_variable(psi_var, llvm_var);
	    }
	    m_global_build_list.pop_front();
	  }
	} else {
	  m_global_build_list.push_back(std::make_pair(term, gv.first));
	}
      }

      return gv.first;
    }

    /**
     * Set up function entry. This converts function parameters from
     * whatever format the calling convention passes them in.
     */
    llvm::BasicBlock* LLVMValueBuilder::build_function_entry(FunctionTerm *psi_func, llvm::Function *llvm_func, LLVMFunctionBuilder& func_builder) {
      llvm::BasicBlock *entry_block = llvm::BasicBlock::Create(context(), "", llvm_func);
      LLVMFunctionBuilder::IRBuilder irbuilder(entry_block);

      llvm::Function::ArgumentListType& argument_list = llvm_func->getArgumentList();
      llvm::Function::ArgumentListType::const_iterator llvm_it = argument_list.begin();

      CallingConvention calling_convention = psi_func->function_type()->calling_convention();
      std::size_t n_phantom = psi_func->function_type()->n_phantom_parameters();

      if (calling_convention == cconv_tvm) {
	// Skip the first LLVM parameter snce it is the return
	// address.
	++llvm_it;
      }

      for (std::size_t n = n_phantom; llvm_it != argument_list.end(); ++n, ++llvm_it) {
	FunctionParameterTerm* param = psi_func->parameter(n);
	llvm::Argument *llvm_param = const_cast<llvm::Argument*>(&*llvm_it);
	Term* param_type = param->type();
	PSI_ASSERT(param_type);
	LLVMType param_type_llvm = type(param_type);

	if (calling_convention == cconv_tvm) {
	  if (param_type_llvm.is_known()) {
	    if (!param_type_llvm.type()->isPointerTy()) {
	      const llvm::PointerType *ptr_ty = param_type_llvm.type()->getPointerTo();
	      llvm::Value *cast_param = irbuilder.CreateBitCast(llvm_param, ptr_ty);
	      llvm::Value *load = irbuilder.CreateLoad(cast_param);
	      func_builder.m_value_terms.insert(std::make_pair(param, LLVMValue::known(load)));
	    } else {
	      func_builder.m_value_terms.insert(std::make_pair(param, LLVMValue::known(llvm_param)));
	    }
	  } else if (param_type_llvm.is_empty()) {
	    func_builder.m_value_terms.insert(std::make_pair(param, LLVMValue::empty()));
	  } else {
	    func_builder.m_value_terms.insert(std::make_pair(param, LLVMValue::unknown(llvm_param, llvm_param)));
	  }
	} else {
	  PSI_ASSERT(param_type_llvm.is_known());
	  func_builder.m_value_terms.insert(std::make_pair(param, LLVMValue::known(llvm_param)));
	}
      }

      return entry_block;
    }

    void LLVMValueBuilder::build_function(FunctionTerm *psi_func, llvm::Function *llvm_func) {
      LLVMFunctionBuilder::IRBuilder irbuilder(context());
      LLVMFunctionBuilder func_builder(this, llvm_func, &irbuilder, psi_func->function_type()->calling_convention());

      // Set up parameters
      llvm::BasicBlock *llvm_entry_block = build_function_entry(psi_func, llvm_func, func_builder);

      if (!llvm_entry_block->empty()) {
        irbuilder.SetInsertPoint(llvm_entry_block);
	llvm::BasicBlock *new_entry_block = llvm::BasicBlock::Create(context(), "", llvm_func);
        llvm::Value *sp = irbuilder.CreateCall(func_builder.llvm_stacksave());
        irbuilder.CreateBr(new_entry_block);

        irbuilder.SetInsertPoint(new_entry_block);
        irbuilder.CreateCall(func_builder.llvm_stackrestore(), sp);

        llvm_entry_block = new_entry_block;
      }

      // Set up basic blocks
      BlockTerm* entry_block = psi_func->entry();
      std::tr1::unordered_set<BlockTerm*> visited_blocks;
      std::vector<BlockTerm*> block_queue;
      std::vector<std::pair<BlockTerm*, llvm::BasicBlock*> > blocks;
      visited_blocks.insert(entry_block);
      block_queue.push_back(entry_block);
      blocks.push_back(std::make_pair(entry_block, llvm_entry_block));
      func_builder.m_value_terms.insert(std::make_pair(entry_block, LLVMValue::known(llvm_entry_block)));

      // find root block set
      while (!block_queue.empty()) {
        BlockTerm *bl = block_queue.back();
        block_queue.pop_back();

        if (!bl->terminated())
          throw std::logic_error("cannot compile function with unterminated blocks");

        std::vector<BlockTerm*> successors = bl->successors();
        for (std::vector<BlockTerm*>::iterator it = successors.begin();
             it != successors.end(); ++it) {
          std::pair<std::tr1::unordered_set<BlockTerm*>::iterator, bool> p = visited_blocks.insert(*it);
          if (p.second) {
            block_queue.push_back(*it);
            if (!(*it)->dominator())
              blocks.push_back(std::make_pair(*it, static_cast<llvm::BasicBlock*>(0)));
          }
        }
      }

      // reset visited blocks to blocks in the "blocks" variable
      visited_blocks.clear();
      for (std::vector<std::pair<BlockTerm*, llvm::BasicBlock*> >::iterator it = blocks.begin();
           it != blocks.end(); ++it) {
        visited_blocks.insert(it->first);
      }

      // get remaining blocks in topological order
      for (std::size_t i = 0; i < blocks.size(); ++i) {
        BlockTerm *bl = blocks[i].first;
	for (TermIterator<BlockTerm> it = bl->term_users_begin<BlockTerm>();
	     it != bl->term_users_end<BlockTerm>(); ++it) {
          if ((bl == it->dominator()) && (visited_blocks.find(it.get_ptr()) == visited_blocks.end())) {
            blocks.push_back(std::make_pair(it.get_ptr(), static_cast<llvm::BasicBlock*>(0)));
            visited_blocks.insert(it.get_ptr());
          }
        }
      }

      // create llvm blocks
      for (std::vector<std::pair<BlockTerm*, llvm::BasicBlock*> >::iterator it = boost::next(blocks.begin());
           it != blocks.end(); ++it) {
        llvm::BasicBlock *llvm_bb = llvm::BasicBlock::Create(context(), "", llvm_func);
        it->second = llvm_bb;
        std::pair<ValueTermMap::iterator, bool> insert_result =
          func_builder.m_value_terms.insert(std::make_pair(it->first, LLVMValue::known(llvm_bb)));
        PSI_ASSERT(insert_result.second);
      }

      std::tr1::unordered_map<BlockTerm*, llvm::Value*> stack_pointers;

      // Build basic blocks
      for (std::vector<std::pair<BlockTerm*, llvm::BasicBlock*> >::iterator it = blocks.begin();
	   it != blocks.end(); ++it) {
	irbuilder.SetInsertPoint(it->second);

        // Restore stack as it was when dominating block exited, so
        // any values alloca'd since then are removed. This is
        // necessary to allow loops which handle unknown types without
        // unbounded stack growth.
        if (it->first->dominator()) {
          PSI_ASSERT(stack_pointers.find(it->first->dominator()) != stack_pointers.end());
          irbuilder.CreateCall(func_builder.llvm_stackrestore(), stack_pointers[it->first->dominator()]);
        }

	const BlockTerm::InstructionList& insn_list = it->first->instructions();
	for (BlockTerm::InstructionList::const_iterator jt = insn_list.begin(); jt != insn_list.end(); ++jt) {
	  InstructionTerm& insn = const_cast<InstructionTerm&>(*jt);
	  LLVMValue r = insn.backend()->llvm_value_instruction(func_builder, insn);
	  func_builder.m_value_terms.insert(std::make_pair(&insn, r));
	}

        // Save stack pointer - must do this retrospectively before
        // the last instruction in the block
        PSI_ASSERT(stack_pointers.find(it->first) == stack_pointers.end());
        irbuilder.SetInsertPoint(it->second, boost::prior(it->second->end()));
        stack_pointers[it->first] = irbuilder.CreateCall(func_builder.llvm_stacksave());
      }

      if (m_debug_stream)
        llvm_func->print(*m_debug_stream);
    }

    void LLVMValueBuilder::build_global_variable(GlobalVariableTerm *psi_var, llvm::GlobalVariable *llvm_var) {
      Term* init_value = psi_var->value();
      if (init_value) {
	LLVMValue llvm_init_value = value(init_value);
	if (llvm_init_value.is_known()) {
	  llvm_var->setInitializer(llvm::cast<llvm::Constant>(llvm_init_value.value()));
	} else if (!llvm_init_value.is_empty()) {
	  throw std::logic_error("global value initializer is not a known or empty value");
	}
      }
    }

    void LLVMValueBuilder::set_module(llvm::Module *module) {
      m_module = module;
    }

    LLVMConstantBuilder::LLVMConstantBuilder(llvm::LLVMContext *context, llvm::Module *module)
      : LLVMValueBuilder(context, module) {
    }

    LLVMConstantBuilder::~LLVMConstantBuilder() {
    }

    LLVMValue LLVMConstantBuilder::value_impl(Term* term) {
      return build_term(term, m_value_terms, ConstantBuilderCallback(this)).first;
    }

    LLVMFunctionBuilder::LLVMFunctionBuilder(LLVMValueBuilder *parent, llvm::Function *function, IRBuilder *irbuilder, CallingConvention calling_convention) 
      : LLVMValueBuilder(parent),
	m_function(function),
	m_irbuilder(irbuilder),
	m_calling_convention(calling_convention) {
      m_llvm_memcpy = llvm_intrinsic_memcpy(module());
      m_llvm_stacksave = llvm_intrinsic_stacksave(module());
      m_llvm_stackrestore = llvm_intrinsic_stackrestore(module());
    }

    LLVMFunctionBuilder::~LLVMFunctionBuilder() {
    }

    LLVMValue LLVMFunctionBuilder::value_impl(Term* term) {
      llvm::BasicBlock *old_insert_block = m_irbuilder->GetInsertBlock();

      // Set the insert point to the dominator block of the value
      llvm::BasicBlock *new_insert_block;
      Term *src = term->source();
      PSI_ASSERT(src);
      if (src->term_type() == term_block) {
        ValueTermMap::iterator it = m_value_terms.find(term->source());
        PSI_ASSERT((it != m_value_terms.end()) && it->second.is_known());
        new_insert_block = llvm::cast<llvm::BasicBlock>(it->second.value());
      } else {
        PSI_ASSERT(src->term_type() == term_function);
        new_insert_block = &function()->front();
      }

      if (new_insert_block != old_insert_block) {
        if (new_insert_block->empty()) {
          m_irbuilder->SetInsertPoint(new_insert_block);
        } else {
          PSI_ASSERT(new_insert_block->back().isTerminator());
          // if the block has been completed, it should have a stack
          // save and jump instruction at the end, and we want to insert
          // before that.
          llvm::BasicBlock::iterator pos = new_insert_block->end();
          --pos; --pos;
          m_irbuilder->SetInsertPoint(new_insert_block, pos);
        }
      } else {
        old_insert_block = NULL;
      }

      LLVMValue result = build_term(term, m_value_terms, InstructionBuilderCallback(this)).first;

      // restore original insert block
      if (old_insert_block)
        m_irbuilder->SetInsertPoint(old_insert_block);

      return result;
    }

    llvm::Function* llvm_intrinsic_memcpy(llvm::Module& m) {
      const char *name = "llvm.memcpy.p0i8.p0i8.i64";
      llvm::Function *f = m.getFunction(name);
      if (f)
	return f;

      llvm::LLVMContext& c = m.getContext();
      std::vector<const llvm::Type*> args;
      args.push_back(llvm::Type::getInt8PtrTy(c));
      args.push_back(llvm::Type::getInt8PtrTy(c));
      args.push_back(llvm::Type::getInt64Ty(c));
      args.push_back(llvm::Type::getInt32Ty(c));
      args.push_back(llvm::Type::getInt1Ty(c));
      llvm::FunctionType *ft = llvm::FunctionType::get(llvm::Type::getVoidTy(c), args, false);
      f = llvm::Function::Create(ft, llvm::GlobalValue::ExternalLinkage, name, &m);

      return f;
    }

    llvm::Function* llvm_intrinsic_stacksave(llvm::Module& m) {
      const char *name = "llvm.stacksave";
      llvm::Function *f = m.getFunction(name);
      if (f)
	return f;

      llvm::LLVMContext& c = m.getContext();
      std::vector<const llvm::Type*> args;
      llvm::FunctionType *ft = llvm::FunctionType::get(llvm::Type::getInt8PtrTy(c), args, false);
      f = llvm::Function::Create(ft, llvm::GlobalValue::ExternalLinkage, name, &m);

      return f;
    }

    llvm::Function* llvm_intrinsic_stackrestore(llvm::Module& m) {
      const char *name = "llvm.stackrestore";
      llvm::Function *f = m.getFunction(name);
      if (f)
	return f;

      llvm::LLVMContext& c = m.getContext();
      std::vector<const llvm::Type*> args;
      args.push_back(llvm::Type::getInt8PtrTy(c));
      llvm::FunctionType *ft = llvm::FunctionType::get(llvm::Type::getVoidTy(c), args, false);
      f = llvm::Function::Create(ft, llvm::GlobalValue::ExternalLinkage, name, &m);

      return f;
    }
  }
}
