#include "LLVMBuilder.hpp"
#include "Core.hpp"
#include "Derived.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Primitive.hpp"
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
	    throw LLVMBuildError("Cyclical term found");
	  }
	}

	typename ValueMap::value_type::second_type r = cb.build(*term);
	if (cb.valid(r)) {
	  itp.first->second = r;
	} else {
	  values.erase(itp.first);
	  throw LLVMBuildError("LLVM term building failed");
	}

	return std::make_pair(r, true);
      }

      struct TypeBuilderCallback {
	LLVMValueBuilder *self;
	TypeBuilderCallback(LLVMValueBuilder *self_) : self(self_) {}

	LLVMType build(Term& term) const {
	  switch(term.term_type()) {
	  case term_recursive_parameter: {
	    throw LLVMBuildError("not implemented");
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
	      return LLVMType::known(llvm::FunctionType::get(llvm::Type::getVoidTy(self->context()), params, false));
	    } else {
              std::size_t n_phantom = actual.n_phantom_parameters();
	      std::size_t n_parameters = actual.n_parameters() - n_phantom;
	      std::vector<const llvm::Type*> params;
	      for (std::size_t i = 0; i < n_parameters; ++i) {
		LLVMType param_type = self->type(actual.parameter(i+n_phantom)->type());
		if (!param_type.is_known())
		  throw LLVMBuildError("Only tvm calling convention supports dependent parameters");
		params.push_back(param_type.type());
	      }
	      LLVMType result_type = self->type(actual.result_type());
	      if (!result_type.is_known())
		throw LLVMBuildError("Only tvm calling convention support dependent result type");
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
	    PSI_FAIL("unexpected type term type");
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
	    throw LLVMBuildError("not implemented");
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
	    PSI_FAIL("unexpected term type");
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
	  LLVMValue result = term.backend()->llvm_value_instruction(self, term);

          llvm::Value *val =
            result.is_known() ? result.known_value()
            : result.is_unknown() ? result.unknown_value()
            : NULL;

          if (val && llvm::isa<llvm::Instruction>(val) && (val->getType() != llvm::Type::getVoidTy(self.context())))
            val->setName(self.term_name(&term));

          return result;
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
	      return new llvm::GlobalVariable(self->module(), llvm_type.type(), global.constant(), llvm::GlobalValue::InternalLinkage, NULL, global.name());
	    } else {
	      PSI_ASSERT(llvm_type.is_unknown());
	      PSI_FAIL("global variable has unknown type");
	    }
	  }

	  case term_function: {
	    FunctionTerm& func = checked_cast<FunctionTerm&>(term);
	    FunctionalTermPtr<PointerType> type = checked_cast_functional<PointerType>(func.type());
	    FunctionTypeTerm* func_type = checked_cast<FunctionTypeTerm*>(type.backend().target_type());
	    LLVMType llvm_ty = self->type(func_type);
	    PSI_ASSERT(llvm_ty.is_known() && llvm_ty.type()->isFunctionTy());
	    return llvm::Function::Create(llvm::cast<llvm::FunctionType>(llvm_ty.type()),
					  llvm::GlobalValue::InternalLinkage, func.name(), &self->module());
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

    LLVMBuildError::LLVMBuildError(const std::string& msg)
      : std::logic_error(msg) {
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
          return LLVMValue::known(gv);
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
      PSI_ASSERT((term->term_type() == term_function) || (term->term_type() == term_global_variable));

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
      llvm::BasicBlock *prolog_block = llvm::BasicBlock::Create(context(), "", llvm_func);
      LLVMIRBuilder irbuilder(prolog_block);

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
            const llvm::PointerType *ptr_ty = param_type_llvm.type()->getPointerTo();
            llvm::Value *cast_param = irbuilder.CreatePointerCast(llvm_param, ptr_ty);
            llvm::Value *load = irbuilder.CreateLoad(cast_param, func_builder.term_name(param));
            func_builder.m_value_terms.insert(std::make_pair(param, LLVMValue::known(load)));
	  } else {
            llvm_param->setName(func_builder.term_name(param));
	    func_builder.m_value_terms.insert(std::make_pair(param, LLVMValue::unknown(llvm_param)));
	  }
	} else {
	  PSI_ASSERT(param_type_llvm.is_known());
          llvm_param->setName(func_builder.term_name(param));
	  func_builder.m_value_terms.insert(std::make_pair(param, LLVMValue::known(llvm_param)));
	}
      }

      return prolog_block;
    }

    /**
     * Allocate space on the stack for unknown typed-phi node values
     * in all dominated blocks. This wastes some space since it has to
     * be done in the dominating rather than dominated block, but
     * without closer control over the stack pointer (which isn't
     * available in LLVM) I can't do anything else.
     *
     * This is also somewhat inefficient since it uses the
     * user-specified dominator blocks to decide where to put the
     * alloca instructions, when accurate dominator blocks could
     * instead be used.
     */
    void LLVMValueBuilder::build_phi_alloca(std::tr1::unordered_map<PhiTerm*, llvm::Value*>& phi_storage_map,
                                            LLVMFunctionBuilder& func_builder, const std::vector<BlockTerm*>& dominated) {
      for (std::vector<BlockTerm*>::const_iterator jt = dominated.begin(); jt != dominated.end(); ++jt) {
        const BlockTerm::PhiList& phi_list = (*jt)->phi_nodes();
        for (BlockTerm::PhiList::const_iterator kt = phi_list.begin(); kt != phi_list.end(); ++kt) {
          PhiTerm& phi = const_cast<PhiTerm&>(*kt);
          LLVMType ty = type(phi.type());
          if (ty.is_unknown()) {
            LLVMValue ty_val = func_builder.value(phi.type());
            PSI_ASSERT(ty_val.is_known());
            llvm::Value *size = func_builder.irbuilder().CreateExtractValue(ty_val.known_value(), 0);
            llvm::AllocaInst *storage = func_builder.irbuilder().CreateAlloca(llvm::Type::getInt8Ty(context()), size);
            storage->setAlignment(func_builder.llvm_align_max());
            PSI_ASSERT(phi_storage_map.find(&phi) == phi_storage_map.end());
            phi_storage_map[&phi] = storage;
          }
        }
      }
    }

    /**
     * Checks whether the given block has any outstanding alloca
     * instructions, i.e. whether the stack pointer on exit is
     * different to the stack pointer on entry, apart from the
     * adjustment to equal the stack pointer of the dominating block.
     *
     * Note that this function only works on correctly structured Tvm
     * blocks where stack save and restore points are paired (except
     * for the one at block entry); in particular it should not be
     * used on the prolog block.
     */
    bool LLVMFunctionBuilder::has_outstanding_alloca(llvm::BasicBlock *block) {
      llvm::CallInst *target_save = NULL;
      // Find last restore instruction in this block
      llvm::BasicBlock::iterator it = block->end();
      do {
        --it;

        if (!target_save) {
          if (it->getOpcode() == llvm::Instruction::Call) {
            llvm::CallInst *call = llvm::cast<llvm::CallInst>(&*it);
            if (call->getCalledFunction() == llvm_stackrestore()) {
              // we have a save instruction to look for. ignore all
              // calls to alloca between now and then.
              target_save = llvm::cast<llvm::CallInst>(call->getArgOperand(0));
            }
          } else if (it->getOpcode() == llvm::Instruction::Alloca) {
            return true;
          }
        } else if (&*it == target_save) {
          target_save = NULL;
        }
        
      } while (it != block->begin());

      return false;
    }

    /**
     * Find the first stackrestore instruction in a block.
     */
    llvm::CallInst* LLVMFunctionBuilder::first_stack_restore(llvm::BasicBlock *block) {
      for (llvm::BasicBlock::iterator it = block->begin(); it != block->end(); ++it) {
        if (it->getOpcode() == llvm::Instruction::Call) {
          llvm::CallInst *call = llvm::cast<llvm::CallInst>(&*it);
          if (call->getCalledFunction() == llvm_stackrestore())
            return call;
        }
      }
      return NULL;
    }

    namespace {
      struct BlockStackInfo {
        BlockStackInfo(llvm::BasicBlock *block_,
                       bool outstanding_alloca_,
                       llvm::BasicBlock *stack_restore_,
                       llvm::CallInst *stack_restore_insn_)
          : block(block_),
            outstanding_alloca(outstanding_alloca_),
            stack_restore(stack_restore_),
            stack_restore_insn(stack_restore_insn_) {}

        // block which this structure refers to
        llvm::BasicBlock *block;
        // whether this block has an outstanding alloca, i.e., it
        // adjusts the stack pointer.
        bool outstanding_alloca;
        // where this block restores the stack to on entry
        llvm::BasicBlock *stack_restore;
        // the instruction which restores the stack on entry
        llvm::CallInst *stack_restore_insn;
        // list of predecessor blocks
        std::vector<BlockStackInfo*> predecessors;
      };
    }

    /**
     * Remove unnecessary stack save and restore instructions.
     */
    void LLVMFunctionBuilder::simplify_stack_save_restore() {
      std::tr1::unordered_map<llvm::BasicBlock*, BlockStackInfo> block_info;

      for (llvm::Function::iterator it = boost::next(m_llvm_function->begin()); it != m_llvm_function->end(); ++it) {
        llvm::BasicBlock *block = &*it;
        llvm::CallInst *stack_restore = first_stack_restore(block);
        PSI_ASSERT(stack_restore);
        llvm::BasicBlock *restore_block = llvm::cast<llvm::Instruction>(stack_restore->getArgOperand(0))->getParent();
        block_info.insert(std::make_pair(block, BlockStackInfo(block, has_outstanding_alloca(block), restore_block, stack_restore)));
      }

      for (std::tr1::unordered_map<llvm::BasicBlock*, BlockStackInfo>::iterator it = block_info.begin();
           it != block_info.end(); ++it) {
        llvm::TerminatorInst *terminator = it->first->getTerminator();
        unsigned n_successors = terminator->getNumSuccessors();
        for (unsigned n = 0; n < n_successors; ++n) {
          llvm::BasicBlock *successor = terminator->getSuccessor(n);
          PSI_ASSERT(block_info.find(successor) != block_info.end());
          BlockStackInfo& successor_info = block_info.find(successor)->second;
          successor_info.predecessors.push_back(&it->second);
        }
      }

      for (std::tr1::unordered_map<llvm::BasicBlock*, BlockStackInfo>::iterator it = block_info.begin();
           it != block_info.end(); ++it) {
        for (std::vector<BlockStackInfo*>::iterator jt = it->second.predecessors.begin();
             jt != it->second.predecessors.end(); ++jt) {
          if ((*jt)->outstanding_alloca || (it->second.stack_restore != (*jt)->stack_restore))
            goto cannot_restore_stack;
        }

        // sp is the same on all incoming edges, so remove the restore instruction
        it->second.stack_restore_insn->eraseFromParent();
        continue;

      cannot_restore_stack:
        continue;
      }

      // finally, see whether the save instruction in the prolog block
      // is still necessary
      llvm::BasicBlock *prolog_block = &m_llvm_function->getEntryBlock();
      llvm::CallInst *save_insn = llvm::cast<llvm::CallInst>(&*boost::prior(boost::prior(prolog_block->end())));
      PSI_ASSERT(save_insn->getCalledFunction() == llvm_stacksave());
      if (save_insn->hasNUses(0))
        save_insn->eraseFromParent();
    }

    void LLVMValueBuilder::build_function(FunctionTerm *psi_func, llvm::Function *llvm_func) {
      LLVMIRBuilder irbuilder(context());
      LLVMFunctionBuilder func_builder(this, psi_func, llvm_func, &irbuilder, psi_func->function_type()->calling_convention());
      std::tr1::unordered_map<BlockTerm*, llvm::Value*> stack_pointers;
      std::tr1::unordered_map<PhiTerm*, llvm::Value*> phi_storage_map;

      // Set up parameters
      llvm::BasicBlock *llvm_prolog_block = build_function_entry(psi_func, llvm_func, func_builder);

      // Set up basic blocks
      BlockTerm* entry_block = psi_func->entry();
      std::tr1::unordered_set<BlockTerm*> visited_blocks;
      std::vector<BlockTerm*> block_queue;
      std::vector<BlockTerm*> entry_blocks;
      visited_blocks.insert(entry_block);
      block_queue.push_back(entry_block);
      entry_blocks.push_back(entry_block);

      // find root block set
      while (!block_queue.empty()) {
        BlockTerm *bl = block_queue.back();
        block_queue.pop_back();

        if (!bl->terminated())
          throw LLVMBuildError("cannot compile function with unterminated blocks");

        std::vector<BlockTerm*> successors = bl->successors();
        for (std::vector<BlockTerm*>::iterator it = successors.begin();
             it != successors.end(); ++it) {
          std::pair<std::tr1::unordered_set<BlockTerm*>::iterator, bool> p = visited_blocks.insert(*it);
          if (p.second) {
            block_queue.push_back(*it);
            if (!(*it)->dominator())
              entry_blocks.push_back(*it);
          }
        }
      }

      // Set up entry blocks
      std::vector<std::pair<BlockTerm*, llvm::BasicBlock*> > blocks;
      for (std::vector<BlockTerm*>::iterator it = entry_blocks.begin(); it != entry_blocks.end(); ++it)
        blocks.push_back(std::make_pair(*it, static_cast<llvm::BasicBlock*>(0)));

      // get remaining blocks in topological order
      for (std::size_t i = 0; i < blocks.size(); ++i) {
        std::vector<BlockTerm*> dominated = blocks[i].first->dominated_blocks();
        for (std::vector<BlockTerm*>::iterator it = dominated.begin(); it != dominated.end(); ++it)
          blocks.push_back(std::make_pair(*it, static_cast<llvm::BasicBlock*>(0)));
      }

      // create llvm blocks
      for (std::vector<std::pair<BlockTerm*, llvm::BasicBlock*> >::iterator it = blocks.begin();
           it != blocks.end(); ++it) {
        llvm::BasicBlock *llvm_bb = llvm::BasicBlock::Create(context(), func_builder.term_name(it->first), llvm_func);
        it->second = llvm_bb;
        std::pair<ValueTermMap::iterator, bool> insert_result =
          func_builder.m_value_terms.insert(std::make_pair(it->first, LLVMValue::known(llvm_bb)));
        PSI_ASSERT(insert_result.second);
      }

      // Finish prolog block
      irbuilder.SetInsertPoint(llvm_prolog_block);
      // set up phi nodes for entry blocks
      build_phi_alloca(phi_storage_map, func_builder, entry_blocks);
      // Save prolog stack and jump into entry
      stack_pointers[NULL] = irbuilder.CreateCall(func_builder.llvm_stacksave());
      PSI_ASSERT(blocks[0].first == entry_block);
      irbuilder.CreateBr(blocks[0].second);

      std::tr1::unordered_map<PhiTerm*, llvm::PHINode*> phi_node_map;

      // Build basic blocks
      for (std::vector<std::pair<BlockTerm*, llvm::BasicBlock*> >::iterator it = blocks.begin();
	   it != blocks.end(); ++it) {
	irbuilder.SetInsertPoint(it->second);
        PSI_ASSERT(it->second->empty());

        // Set up phi terms
        const BlockTerm::PhiList& phi_list = it->first->phi_nodes();
        for (BlockTerm::PhiList::const_iterator jt = phi_list.begin(); jt != phi_list.end(); ++jt) {
          PhiTerm& phi = const_cast<PhiTerm&>(*jt);
          LLVMType ty = type(phi.type());
          if (ty.is_known()) {
            llvm::PHINode *llvm_phi = irbuilder.CreatePHI(ty.type());
            phi_node_map[&phi] = llvm_phi;
            func_builder.m_value_terms.insert(std::make_pair(&phi, LLVMValue::known(llvm_phi)));
          } else {
            PSI_ASSERT(ty.is_unknown());
            llvm::PHINode *llvm_phi = irbuilder.CreatePHI(llvm::Type::getInt8PtrTy(context()));
            phi_node_map[&phi] = llvm_phi;
            PSI_ASSERT(phi_storage_map.find(&phi) != phi_storage_map.end());
            llvm::Value *phi_storage = phi_storage_map[&phi];
            func_builder.m_value_terms.insert(std::make_pair(&phi, LLVMValue::unknown(phi_storage)));
          }
        }

        // For phi terms of unknown types, copy from existing storage
        // which is possibly about to be deallocated to new storage.
        for (BlockTerm::PhiList::const_iterator jt = phi_list.begin(); jt != phi_list.end(); ++jt) {
          PhiTerm& phi = const_cast<PhiTerm&>(*jt);
          LLVMType ty = type(phi.type());
          if (ty.is_unknown()) {
            PSI_ASSERT(phi_node_map.find(&phi) != phi_node_map.end());
            PSI_ASSERT(phi_storage_map.find(&phi) != phi_storage_map.end());
            func_builder.create_store_unknown(phi_storage_map[&phi], phi_node_map[&phi], phi.type());
          }
        }        

        // Restore stack as it was when dominating block exited, so
        // any values alloca'd since then are removed. This is
        // necessary to allow loops which handle unknown types without
        // unbounded stack growth.
        PSI_ASSERT(stack_pointers.find(it->first->dominator()) != stack_pointers.end());
        llvm::Value *dominator_stack_ptr = stack_pointers[it->first->dominator()];
        irbuilder.CreateCall(func_builder.llvm_stackrestore(), dominator_stack_ptr);

        // Build instructions!
	const BlockTerm::InstructionList& insn_list = it->first->instructions();
	for (BlockTerm::InstructionList::const_iterator jt = insn_list.begin(); jt != insn_list.end(); ++jt) {
	  InstructionTerm& insn = const_cast<InstructionTerm&>(*jt);
	  LLVMValue r = insn.backend()->llvm_value_instruction(func_builder, insn);
	  func_builder.m_value_terms.insert(std::make_pair(&insn, r));
	}

        if (!it->second->getTerminator())
          throw LLVMBuildError("LLVM block was not terminated during function building");

        // Build block epilog: must move the IRBuilder insert point to
        // before the terminating instruction first.
        irbuilder.SetInsertPoint(it->second, boost::prior(it->second->end()));

        // Allocate phi node storage for dominated blocks
        build_phi_alloca(phi_storage_map, func_builder, it->first->dominated_blocks());

        // Save stack pointer so it can be restored in dominated
        // blocks. This only needs to be done if the alloca is used
        // during this block outside of a save/restore, and the block
        // does not terminate the function
        PSI_ASSERT(stack_pointers.find(it->first) == stack_pointers.end());
        if ((it->second->getTerminator()->getNumSuccessors() > 0) && func_builder.has_outstanding_alloca(it->second)) {
          stack_pointers[it->first] = irbuilder.CreateCall(func_builder.llvm_stacksave());
        } else {
          stack_pointers[it->first] = dominator_stack_ptr;
        }
      }

      func_builder.simplify_stack_save_restore();

      // Set up LLVM phi node incoming edges
      for (std::tr1::unordered_map<PhiTerm*, llvm::PHINode*>::iterator it = phi_node_map.begin();
           it != phi_node_map.end(); ++it) {
#ifdef PSI_DEBUG
        LLVMType ty = type(it->first->type());
#endif
        for (std::size_t n = 0; n < it->first->n_incoming(); ++n) {
          LLVMValue incoming_block = func_builder.value(it->first->incoming_block(n));
          LLVMValue incoming_value = func_builder.value(it->first->incoming_value(n));

          llvm::Value *value =
#ifdef PSI_DEBUG
            ty.is_known()
#else
            incoming_value.is_known()
#endif
            ? incoming_value.known_value() : incoming_value.unknown_value();

          it->second->addIncoming(value, llvm::cast<llvm::BasicBlock>(incoming_block.known_value()));
        }
      }

      if (m_debug_stream)
        llvm_func->print(*m_debug_stream);
    }

    void LLVMValueBuilder::build_global_variable(GlobalVariableTerm *psi_var, llvm::GlobalVariable *llvm_var) {
      Term* init_value = psi_var->value();
      if (init_value) {
	LLVMValue llvm_init_value = value(init_value);
	if (llvm_init_value.is_known()) {
	  llvm_var->setInitializer(llvm::cast<llvm::Constant>(llvm_init_value.known_value()));
	} else {
          PSI_FAIL("global value initializer is not a known value");
	}
      }
    }

    /**
     * Cast a pointer to a generic pointer (i8*).
     *
     * \param type The type to cast to. This is the type of the
     * returned value, so it is the pointer type not the pointed-to
     * type.
     */
    llvm::Value* LLVMValueBuilder::cast_pointer_to_generic(llvm::Value *value) {
      PSI_ASSERT(value->getType()->isPointerTy());

      const llvm::Type *i8ptr = llvm::Type::getInt8PtrTy(context());
      if (value->getType() == i8ptr)
        return value;

      if (llvm::Constant *const_value = llvm::dyn_cast<llvm::Constant>(value)) {
        return llvm::ConstantExpr::getPointerCast(const_value, i8ptr);
      } else {
        return cast_pointer_impl(value, i8ptr);
      }
    }

    /**
     * Cast a pointer from a possibly-generic pointer. The type of
     * value must either be the same as \c target_type, or it must be
     * <tt>i8*</tt>.
     *
     * \param type The type to cast to. This is the type of the
     * returned value, so it is the pointer type not the pointed-to
     * type.
     */
    llvm::Value* LLVMValueBuilder::cast_pointer_from_generic(llvm::Value *value, const llvm::Type *type) {
      PSI_ASSERT(value->getType()->isPointerTy());
      PSI_ASSERT(type->isPointerTy());
      if (value->getType() == type)
        return value;

      PSI_ASSERT(value->getType() == llvm::Type::getInt8PtrTy(context()));
      if (llvm::Constant *const_value = llvm::dyn_cast<llvm::Constant>(value)) {
        return llvm::ConstantExpr::getPointerCast(const_value, type);
      } else {
        return cast_pointer_impl(value, type);
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

    llvm::Value* LLVMConstantBuilder::cast_pointer_impl(llvm::Value*,const llvm::Type*) {
      PSI_FAIL("global llvm builder given non-constant value to cast_pointer");
    }

    LLVMFunctionBuilder::LLVMFunctionBuilder(LLVMValueBuilder *parent, FunctionTerm *function, llvm::Function *llvm_function,
                                             LLVMIRBuilder *irbuilder, CallingConvention calling_convention) 
      : LLVMValueBuilder(parent),
	m_function(function),
        m_llvm_function(llvm_function),
	m_irbuilder(irbuilder),
	m_calling_convention(calling_convention) {
      m_llvm_memcpy = llvm_intrinsic_memcpy(module());
      m_llvm_stacksave = llvm_intrinsic_stacksave(module());
      m_llvm_stackrestore = llvm_intrinsic_stackrestore(module());
      m_llvm_align_zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context()), 0);
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
        new_insert_block = llvm::cast<llvm::BasicBlock>(it->second.known_value());
      } else {
        PSI_ASSERT(src->term_type() == term_function);
        new_insert_block = &function()->front();
      }

      if (new_insert_block != old_insert_block) {
        // If inserting into another block, it should dominate this
        // one, and therefore already have been built, and terminated.
        PSI_ASSERT(new_insert_block->getTerminator());

        // if the block has been completed, it should have a stack
        // save and jump instruction at the end, and we want to insert
        // before that.
        llvm::BasicBlock::iterator pos = new_insert_block->end();
        --pos; --pos;
        m_irbuilder->SetInsertPoint(new_insert_block, pos);
      } else {
        old_insert_block = NULL;
      }

      LLVMValue result = build_term(term, m_value_terms, InstructionBuilderCallback(this)).first;

      // restore original insert block
      if (old_insert_block)
        m_irbuilder->SetInsertPoint(old_insert_block);

      return result;
    }

    llvm::Value* LLVMFunctionBuilder::cast_pointer_impl(llvm::Value* value, const llvm::Type* type) {
      return irbuilder().CreatePointerCast(value, type);
    }

    /**
     * Create an alloca instruction for the specified number of bytes
     * using the maximum system alignment.
     */
    llvm::Instruction* LLVMFunctionBuilder::create_alloca(llvm::Value *size) {
      llvm::AllocaInst *inst = irbuilder().CreateAlloca(llvm::Type::getInt8Ty(context()), size);
      inst->setAlignment(llvm_align_max());
      return inst;
    }

    /**
     * Create an alloca instruction for the specified type. This
     * requires that the type have a known value.
     */
    llvm::Value* LLVMFunctionBuilder::create_alloca_for(Term *stored_type) {
      LLVMType llvm_stored_type = type(stored_type);
      if (llvm_stored_type.is_known()) {
        return irbuilder().CreateAlloca(llvm_stored_type.type());
      }

      PSI_ASSERT(llvm_stored_type.is_unknown());

      // Okay, the type is unknown. However if it is an unknown-length
      // array of values with a known type, I can still get that
      // through to LLVM.
      if (FunctionalTermPtr<ArrayType> as_array = dynamic_cast_functional<ArrayType>(stored_type)) {
        LLVMType element_type = this->type(as_array.backend().element_type());
        if (element_type.is_known()) {
          LLVMValue length = value(as_array.backend().length());
          PSI_ASSERT(length.is_known());
          return irbuilder().CreateAlloca(element_type.type(), length.known_value());
        }
      }

      // Okay, it's really unknown, so just allocate as i8[n]
      LLVMValue size_align = value(stored_type);
      PSI_ASSERT(size_align.is_known() && (size_align.known_value()->getType() == Metatype::llvm_type(*this).type()));
      llvm::Value *size = irbuilder().CreateExtractValue(size_align.known_value(), 0);
      llvm::AllocaInst *inst = irbuilder().CreateAlloca(llvm::Type::getInt8Ty(context()), size);
      inst->setAlignment(llvm_align_max());
      return inst;
    }

    /**
     * Create a store instruction for the given term into
     * memory. Handles cases where the term is known or not correctly.
     *
     * \return The store instruction created. This may be NULL if the
     * type of \c src is empty.
     */
    llvm::Instruction* LLVMFunctionBuilder::create_store(llvm::Value *dest, Term *src) {
      LLVMValue llvm_src = value(src);

      if (llvm_src.is_known()) {
        llvm::Value *cast_dest = cast_pointer_from_generic(dest, llvm_src.known_value()->getType()->getPointerTo());
        return irbuilder().CreateStore(llvm_src.known_value(), cast_dest);
      } else {
        PSI_ASSERT(llvm_src.is_unknown());
        return create_store_unknown(dest, llvm_src.unknown_value(), src->type());
      }
    }

    /**
     * Create a memcpy call which stores an unknown term into a
     * pointer.
     */
    llvm::Instruction* LLVMFunctionBuilder::create_store_unknown(llvm::Value *dest, llvm::Value *src, Term *src_type) {
      LLVMValue src_type_value = value(src_type);
      PSI_ASSERT(src_type_value.is_known() && (src_type_value.known_value()->getType() == Metatype::llvm_type(*this).type()));

      llvm::Value *size = irbuilder().CreateExtractValue(src_type_value.known_value(), 0);

      return irbuilder().CreateCall5(llvm_memcpy(), dest, src, size, llvm_align_zero(),
                                     llvm::ConstantInt::getFalse(context()));
    }

    /**
     * Get one of the names for a term, or an empty StringRef if the
     * term has no name.
     */
    llvm::StringRef LLVMFunctionBuilder::term_name(Term *term) {
      const FunctionTerm::TermNameMap& map = m_function->term_name_map();
      FunctionTerm::TermNameMap::const_iterator it = map.find(term);

      if (it != map.end()) {
        return llvm::StringRef(it->second);
      } else {
        return llvm::StringRef();
      }
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
