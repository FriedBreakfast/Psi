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
      build_term(ValueMap& values, typename ValueMap::value_type::first_type term, const Callback& cb) {
	std::pair<typename ValueMap::iterator, bool> itp =
	  values.insert(std::make_pair(term, cb.invalid()));
	if (!itp.second) {
	  if (cb.valid(itp.first->second)) {
	    return std::make_pair(itp.first->second, false);
	  } else {
	    throw LLVMBuildError("Cyclical term found");
	  }
	}

	typename ValueMap::value_type::second_type r = cb.build(term);
	if (cb.valid(r)) {
	  itp.first->second = r;
	} else {
	  values.erase(itp.first);
	  throw LLVMBuildError("LLVM term building failed");
	}

	return std::make_pair(r, true);
      }

      template<typename T>
      struct PtrValidBase {
        T* invalid() const {return NULL;}
	bool valid(const T* t) const {return t;}
      };

      struct TypeBuilderCallback {
	LLVMConstantBuilder *self;
	TypeBuilderCallback(LLVMConstantBuilder *self_) : self(self_) {}

        const llvm::Type* build(Term* term) const {
	  switch(term->term_type()) {
	  case term_functional: {
	    FunctionalTerm *cast_term = checked_cast<FunctionalTerm*>(term);
	    return cast_term->backend()->llvm_type(*self, *cast_term);
	  }

	  case term_apply: {
	    Term* actual = checked_cast<ApplyTerm*>(term)->unpack();
	    PSI_ASSERT(actual->term_type() != term_apply);
	    return self->build_type(actual);
	  }

	  case term_function_type: {
	    FunctionTypeTerm* actual = checked_cast<FunctionTypeTerm*>(term);
	    if (actual->calling_convention() == cconv_tvm) {
	      const llvm::Type* i8ptr = llvm::Type::getInt8PtrTy(self->llvm_context());
	      std::vector<const llvm::Type*> params(actual->n_parameters() - actual->n_phantom_parameters() + 1, i8ptr);
	      return llvm::FunctionType::get(llvm::Type::getVoidTy(self->llvm_context()), params, false);
	    } else {
              std::size_t n_phantom = actual->n_phantom_parameters();
	      std::size_t n_parameters = actual->n_parameters() - n_phantom;
	      std::vector<const llvm::Type*> params;
	      for (std::size_t i = 0; i < n_parameters; ++i) {
                const llvm::Type *param_type = self->build_type(actual->parameter(i+n_phantom)->type());
                if (!param_type)
                  return NULL;
                params.push_back(param_type);
              }
              const llvm::Type *result_type = self->build_type(actual->result_type());
              if (!result_type)
                return NULL;
	      return llvm::FunctionType::get(result_type, params, false);
	    }
	  }

	  case term_function_parameter:
          case term_function_type_parameter:
            return NULL;

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

        boost::optional<const llvm::Type*> invalid() const {return boost::none;}
	bool valid(boost::optional<const llvm::Type*> t) const {return t;}
      };

      struct ConstantBuilderCallback : PtrValidBase<llvm::Constant> {
	LLVMConstantBuilder *self;
	ConstantBuilderCallback(LLVMConstantBuilder *self_) : self(self_) {}

        llvm::Constant* build(Term *term) const {
          switch (term->term_type()) {
          case term_functional: {
            FunctionalTerm *cast_term = checked_cast<FunctionalTerm*>(term);
            return cast_term->backend()->llvm_value_constant(*self, *cast_term);
          }
           
          case term_apply: {
            Term* actual = checked_cast<ApplyTerm*>(term)->unpack();
	    PSI_ASSERT(actual->term_type() != term_apply);
	    return self->build_constant(actual);
	  }

          default:
	    PSI_FAIL("unexpected type term type");
          }
        }
      };

      struct ValueBuilderCallback {
	LLVMFunctionBuilder *self;
        const LLVMFunctionBuilder::ValueTermMap *value_terms;

	ValueBuilderCallback(LLVMFunctionBuilder *self_,
                             const LLVMFunctionBuilder::ValueTermMap *value_terms_)
          : self(self_), value_terms(value_terms_) {}

	LLVMValue build(Term *term) const {
          llvm::BasicBlock *old_insert_block = self->irbuilder().GetInsertBlock();

          // Set the insert point to the dominator block of the value
          llvm::BasicBlock *new_insert_block;
          Term *src = term->source();
          PSI_ASSERT(src);
          if (src->term_type() == term_block) {
            LLVMFunctionBuilder::ValueTermMap::const_iterator it = value_terms->find(term->source());
            PSI_ASSERT((it != value_terms->end()) && it->second.is_known());
            new_insert_block = llvm::cast<llvm::BasicBlock>(it->second.known_value());
          } else {
            PSI_ASSERT(src->term_type() == term_function);
            new_insert_block = &self->llvm_function()->front();
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
            self->irbuilder().SetInsertPoint(new_insert_block, pos);
          } else {
            old_insert_block = NULL;
          }

          LLVMValue result;
	  switch(term->term_type()) {
	  case term_functional: {
	    FunctionalTerm *cast_term = checked_cast<FunctionalTerm*>(term);

            result = cast_term->backend()->llvm_value_instruction(*self, *cast_term);

            llvm::Value *val = result.is_known() ? result.known_value() : result.unknown_value();
            if (llvm::isa<llvm::Instruction>(val) && (val->getType() != llvm::Type::getVoidTy(self->llvm_context())))
              val->setName(self->term_name(term));

            break;
	  }

	  case term_apply: {
	    Term* actual = checked_cast<ApplyTerm*>(term)->unpack();
	    PSI_ASSERT(actual->term_type() != term_apply);
	    result = self->build_value(actual);
            break;
	  }

	  default:
	    PSI_FAIL("unexpected term type");
	  }

          // restore original insert block
          if (old_insert_block)
            self->irbuilder().SetInsertPoint(old_insert_block);

          return result;
	}

	LLVMValue invalid() const {return LLVMValue();}
	bool valid(const LLVMValue& t) const {return t.is_valid();}
      };

      struct GlobalBuilderCallback : PtrValidBase<llvm::GlobalValue> {
	LLVMGlobalBuilder *self;
	GlobalBuilderCallback(LLVMGlobalBuilder *self_) : self(self_) {}

	llvm::GlobalValue* build(GlobalTerm *term) const {
	  switch (term->term_type()) {
	  case term_global_variable: {
	    GlobalVariableTerm *global = checked_cast<GlobalVariableTerm*>(term);
	    FunctionalTermPtr<PointerType> type = checked_cast_functional<PointerType>(global->type());
	    const llvm::Type *llvm_type = self->build_type(type.backend().target_type());
            if (!llvm_type)
              throw LLVMBuildError("could not create global variable because its LLVM type is not known");
            return new llvm::GlobalVariable(self->llvm_module(), llvm_type,
                                            global->constant(), llvm::GlobalValue::InternalLinkage,
                                            NULL, global->name());
	  }

	  case term_function: {
	    FunctionTerm *func = checked_cast<FunctionTerm*>(term);
	    FunctionalTermPtr<PointerType> type = checked_cast_functional<PointerType>(func->type());
	    FunctionTypeTerm* func_type = checked_cast<FunctionTypeTerm*>(type.backend().target_type());
	    const llvm::Type *llvm_type = self->build_type(func_type);
            if (!llvm_type)
              throw LLVMBuildError("could not create function because its LLVM type is not known");
	    return llvm::Function::Create(llvm::cast<llvm::FunctionType>(llvm_type),
					  llvm::GlobalValue::InternalLinkage,
                                          func->name(), &self->llvm_module());
	  }

	  default:
	    PSI_FAIL("unexpected global term type");
	  }
	}
      };
    }

    LLVMBuildError::LLVMBuildError(const std::string& msg)
      : std::logic_error(msg) {
    }

    LLVMConstantBuilder::LLVMConstantBuilder(LLVMConstantBuilder *parent)
      : m_parent(parent), m_context(parent->m_context) {
    }

    LLVMConstantBuilder::LLVMConstantBuilder(llvm::LLVMContext *context)
      : m_parent(0), m_context(context) {
      PSI_ASSERT(m_context);
    }

    LLVMConstantBuilder::~LLVMConstantBuilder() {
    }

    /**
     * \brief Return the type specified by the specified term.
     *
     * This should never return NULL, since for global terms, all
     * types should be known.
     *
     * Note that this is not the LLVM type of the LLVM value of this
     * term: it is the LLVM type of the LLVM value of terms whose type
     * is this term.
     */
    const llvm::Type* LLVMConstantBuilder::build_type(Term* term) {
      if (m_parent && term->global()) {
        return m_parent->build_type(term);
      } else if (!m_parent && !term->global()) {
        throw LLVMBuildError("global type builder called on non-global term");
      } else {
        boost::optional<const llvm::Type*> result = build_term(m_type_terms, term, TypeBuilderCallback(this)).first;
        return result ? *result : NULL;
      }
    }

    /**
     * \brief Return the constant value specified by the given term.
     */
    llvm::Constant* LLVMConstantBuilder::build_constant(Term *term) {
      if (term->phantom())
        throw LLVMBuildError("cannot build value of phantom term");

      if (m_parent && term->global()) {
        return m_parent->build_constant(term);
      } else if (!m_parent && !term->global()) {
        throw LLVMBuildError("global constant builder called on non-global term");
      } else {
        switch (term->term_type()) {
        case term_function:
        case term_global_variable:
          PSI_ASSERT(!m_parent && term->global());
          return static_cast<LLVMGlobalBuilder*>(this)->build_global(checked_cast<GlobalTerm*>(term));

        case term_apply:
        case term_functional:
          return build_term(m_constant_terms, term, ConstantBuilderCallback(this)).first;

        default:
          throw LLVMBuildError("constant builder encountered unexpected term type");
        }
      }
    }

    LLVMGlobalBuilder::LLVMGlobalBuilder(llvm::LLVMContext *context, llvm::Module *module)
      : LLVMConstantBuilder(context), m_module(module) {
      PSI_ASSERT(m_module);
    }

    LLVMGlobalBuilder::~LLVMGlobalBuilder() {
    }

    /**
     * Set the module created globals will be put into.
     */
    void LLVMGlobalBuilder::set_module(llvm::Module *module) {
      m_module = module;
    }

    /**
     * \brief Get the global variable specified by the given term.
     */
    llvm::GlobalValue* LLVMGlobalBuilder::build_global(GlobalTerm* term) {
      PSI_ASSERT((term->term_type() == term_function) || (term->term_type() == term_global_variable));

      std::pair<llvm::GlobalValue*, bool> gv = build_term(m_global_terms, term, GlobalBuilderCallback(this));

      if (gv.second) {
	if (m_global_build_list.empty()) {
	  m_global_build_list.push_back(std::make_pair(term, gv.first));
	  while (!m_global_build_list.empty()) {
	    const std::pair<Term*, llvm::GlobalValue*>& t = m_global_build_list.front();
	    if (t.first->term_type() == term_function) {
              LLVMIRBuilder irbuilder(llvm_context());
              LLVMFunctionBuilder fb(this,
                                     checked_cast<FunctionTerm*>(t.first),
                                     llvm::cast<llvm::Function>(t.second),
                                     &irbuilder);
              fb.run();
	    } else {
	      PSI_ASSERT(t.first->term_type() == term_global_variable);
              if (Term* init_value = checked_cast<GlobalVariableTerm*>(t.first)->value())
                llvm::cast<llvm::GlobalVariable>(t.second)->setInitializer(build_constant(init_value));
	    }
	    m_global_build_list.pop_front();
	  }
	} else {
	  m_global_build_list.push_back(std::make_pair(term, gv.first));
	}
      }

      return gv.first;
    }

    LLVMFunctionBuilder::LLVMFunctionBuilder(LLVMGlobalBuilder *constant_builder,
                                             FunctionTerm *function,
                                             llvm::Function *llvm_function,
                                             LLVMIRBuilder *irbuilder)
      : LLVMConstantBuilder(constant_builder),
        m_constant_builder(constant_builder),
        m_irbuilder(irbuilder),
        m_function(function),
        m_llvm_function(llvm_function) {
    }

    LLVMFunctionBuilder::~LLVMFunctionBuilder() {
    }

    /**
     * \brief Create the code required to generate a value for the
     * given term.
     */
    LLVMValue LLVMFunctionBuilder::build_value(Term* term) {
      if (term->global())
        return LLVMValue::known(m_constant_builder->build_constant(term));

      if (term->phantom())
        throw LLVMBuildError("cannot get value for phantom term");

      LLVMValue result;
      switch (term->term_type()) {
      case term_function_parameter:
      case term_instruction:
      case term_phi:
      case term_block: {
        ValueTermMap::iterator it = m_value_terms.find(term);
        PSI_ASSERT(it != m_value_terms.end());
        result = it->second;
        break;
      }

      case term_apply:
      case term_functional:
        result = build_term(m_value_terms, term, ValueBuilderCallback(this, &m_value_terms)).first;
        break;

      default:
        PSI_FAIL("unexpected term type");
      }

      PSI_ASSERT((term->category() != Term::category_type) ||
                 (result.is_known() && (result.known_value()->getType() == LLVMMetatype::type(llvm_context()))));
      return result;
    }

    /**
     * \brief Identical to build_value, but requires that the result
     * be of a known type so that an llvm::Value can be returned.
     */
    llvm::Value* LLVMFunctionBuilder::build_known_value(Term *term) {
      LLVMValue v = build_value(term);
      PSI_ASSERT(v.is_known());
      return v.known_value();
    }

    /**
     * Set up function entry. This converts function parameters from
     * whatever format the calling convention passes them in.
     */
    llvm::BasicBlock* LLVMFunctionBuilder::build_function_entry() {
      llvm::BasicBlock *prolog_block = llvm::BasicBlock::Create(llvm_context(), "", m_llvm_function);
      m_irbuilder->SetInsertPoint(prolog_block);

      llvm::Function::ArgumentListType& argument_list = m_llvm_function->getArgumentList();
      llvm::Function::ArgumentListType::const_iterator llvm_it = argument_list.begin();

      CallingConvention calling_convention = m_function->function_type()->calling_convention();
      std::size_t n_phantom = m_function->function_type()->n_phantom_parameters();

      if (calling_convention == cconv_tvm) {
	// Skip the first LLVM parameter snce it is the return
	// address.
	++llvm_it;
      }

      for (std::size_t n = n_phantom; llvm_it != argument_list.end(); ++n, ++llvm_it) {
	FunctionParameterTerm* param = m_function->parameter(n);
	llvm::Argument *llvm_param = const_cast<llvm::Argument*>(&*llvm_it);

	if (calling_convention == cconv_tvm) {
          const llvm::Type* param_type_llvm = build_type(param->type());

	  if (param_type_llvm) {
            llvm::Value *cast_param = irbuilder().CreatePointerCast(llvm_param, param_type_llvm->getPointerTo());
            llvm::Value *load = irbuilder().CreateLoad(cast_param, term_name(param));
            m_value_terms.insert(std::make_pair(param, LLVMValue::known(load)));
	  } else {
            llvm_param->setName(term_name(param));
	    m_value_terms.insert(std::make_pair(param, LLVMValue::unknown(llvm_param)));
	  }
	} else {
          llvm_param->setName(term_name(param));
	  m_value_terms.insert(std::make_pair(param, LLVMValue::known(llvm_param)));
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
    void LLVMFunctionBuilder::build_phi_alloca(std::tr1::unordered_map<PhiTerm*, llvm::Value*>& phi_storage_map,
                                               const std::vector<BlockTerm*>& dominated) {
      for (std::vector<BlockTerm*>::const_iterator jt = dominated.begin(); jt != dominated.end(); ++jt) {
        const BlockTerm::PhiList& phi_list = (*jt)->phi_nodes();
        for (BlockTerm::PhiList::const_iterator kt = phi_list.begin(); kt != phi_list.end(); ++kt) {
          PhiTerm& phi = const_cast<PhiTerm&>(*kt);
          const llvm::Type *ty = build_type(phi.type());
          if (!ty) {
            llvm::Value *size = irbuilder().CreateExtractValue(build_known_value(phi.type()), 0);
            llvm::AllocaInst *storage = irbuilder().CreateAlloca(llvm::Type::getInt8Ty(llvm_context()), size);
            storage->setAlignment(unknown_alloca_align());
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
      llvm::Function *llvm_stackrestore = LLVMIntrinsics::stackrestore(m_constant_builder->llvm_module());

      llvm::CallInst *target_save = NULL;
      // Find last restore instruction in this block
      llvm::BasicBlock::iterator it = block->end();
      do {
        --it;

        if (!target_save) {
          if (it->getOpcode() == llvm::Instruction::Call) {
            llvm::CallInst *call = llvm::cast<llvm::CallInst>(&*it);
            if (call->getCalledFunction() == llvm_stackrestore) {
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
      llvm::Function *llvm_stackrestore = LLVMIntrinsics::stackrestore(m_constant_builder->llvm_module());

      for (llvm::BasicBlock::iterator it = block->begin(); it != block->end(); ++it) {
        if (it->getOpcode() == llvm::Instruction::Call) {
          llvm::CallInst *call = llvm::cast<llvm::CallInst>(&*it);
          if (call->getCalledFunction() == llvm_stackrestore)
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
      PSI_ASSERT(save_insn->getCalledFunction() == LLVMIntrinsics::stacksave(llvm_module()));
      if (save_insn->hasNUses(0))
        save_insn->eraseFromParent();
    }

    void LLVMFunctionBuilder::run() {
      std::tr1::unordered_map<BlockTerm*, llvm::Value*> stack_pointers;
      std::tr1::unordered_map<PhiTerm*, llvm::Value*> phi_storage_map;

      // Set up parameters
      llvm::BasicBlock *llvm_prolog_block = build_function_entry();

      // Set up basic blocks
      BlockTerm* entry_block = m_function->entry();
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
        llvm::BasicBlock *llvm_bb = llvm::BasicBlock::Create(llvm_context(), term_name(it->first), m_llvm_function);
        it->second = llvm_bb;
        std::pair<ValueTermMap::iterator, bool> insert_result =
          m_value_terms.insert(std::make_pair(it->first, LLVMValue::known(llvm_bb)));
        PSI_ASSERT(insert_result.second);
      }

      // Finish prolog block
      irbuilder().SetInsertPoint(llvm_prolog_block);
      // set up phi nodes for entry blocks
      build_phi_alloca(phi_storage_map, entry_blocks);
      // Save prolog stack and jump into entry
      stack_pointers[NULL] = irbuilder().CreateCall(LLVMIntrinsics::stacksave(llvm_module()));
      PSI_ASSERT(blocks[0].first == entry_block);
      irbuilder().CreateBr(blocks[0].second);

      std::tr1::unordered_map<PhiTerm*, llvm::PHINode*> phi_node_map;

      // Build basic blocks
      for (std::vector<std::pair<BlockTerm*, llvm::BasicBlock*> >::iterator it = blocks.begin();
	   it != blocks.end(); ++it) {
	irbuilder().SetInsertPoint(it->second);
        PSI_ASSERT(it->second->empty());

        // Set up phi terms
        const BlockTerm::PhiList& phi_list = it->first->phi_nodes();
        for (BlockTerm::PhiList::const_iterator jt = phi_list.begin(); jt != phi_list.end(); ++jt) {
          PhiTerm& phi = const_cast<PhiTerm&>(*jt);
          const llvm::Type* ty = build_type(phi.type());
          llvm::PHINode *llvm_phi;
          if (ty) {
            llvm_phi = irbuilder().CreatePHI(ty);
            m_value_terms.insert(std::make_pair(&phi, LLVMValue::known(llvm_phi)));
          } else {
            llvm_phi = irbuilder().CreatePHI(llvm::Type::getInt8PtrTy(llvm_context()));
            PSI_ASSERT(phi_storage_map.find(&phi) != phi_storage_map.end());
            llvm::Value *phi_storage = phi_storage_map.find(&phi)->second;
            m_value_terms.insert(std::make_pair(&phi, LLVMValue::unknown(phi_storage)));
          }
          phi_node_map.insert(std::make_pair(&phi, llvm_phi));
        }

        // For phi terms of unknown types, copy from existing storage
        // which is possibly about to be deallocated to new storage.
        for (BlockTerm::PhiList::const_iterator jt = phi_list.begin(); jt != phi_list.end(); ++jt) {
          PhiTerm& phi = const_cast<PhiTerm&>(*jt);
          const llvm::Type *ty = build_type(phi.type());
          if (!ty) {
            PSI_ASSERT(phi_node_map.find(&phi) != phi_node_map.end());
            PSI_ASSERT(phi_storage_map.find(&phi) != phi_storage_map.end());
            create_store_unknown(phi_storage_map.find(&phi)->second, phi_node_map.find(&phi)->second, phi.type());
          }
        }        

        // Restore stack as it was when dominating block exited, so
        // any values alloca'd since then are removed. This is
        // necessary to allow loops which handle unknown types without
        // unbounded stack growth.
        PSI_ASSERT(stack_pointers.find(it->first->dominator()) != stack_pointers.end());
        llvm::Value *dominator_stack_ptr = stack_pointers[it->first->dominator()];
        irbuilder().CreateCall(LLVMIntrinsics::stackrestore(llvm_module()), dominator_stack_ptr);

        // Build instructions!
	const BlockTerm::InstructionList& insn_list = it->first->instructions();
	for (BlockTerm::InstructionList::const_iterator jt = insn_list.begin(); jt != insn_list.end(); ++jt) {
	  InstructionTerm& insn = const_cast<InstructionTerm&>(*jt);
	  LLVMValue r = insn.backend()->llvm_value_instruction(*this, insn);
	  m_value_terms.insert(std::make_pair(&insn, r));
	}

        if (!it->second->getTerminator())
          throw LLVMBuildError("LLVM block was not terminated during function building");

        // Build block epilog: must move the IRBuilder insert point to
        // before the terminating instruction first.
        irbuilder().SetInsertPoint(it->second, boost::prior(it->second->end()));

        // Allocate phi node storage for dominated blocks
        build_phi_alloca(phi_storage_map, it->first->dominated_blocks());

        // Save stack pointer so it can be restored in dominated
        // blocks. This only needs to be done if the alloca is used
        // during this block outside of a save/restore, and the block
        // does not terminate the function
        PSI_ASSERT(stack_pointers.find(it->first) == stack_pointers.end());
        if ((it->second->getTerminator()->getNumSuccessors() > 0) && has_outstanding_alloca(it->second)) {
          stack_pointers[it->first] = irbuilder().CreateCall(LLVMIntrinsics::stacksave(llvm_module()));
        } else {
          stack_pointers[it->first] = dominator_stack_ptr;
        }
      }

      simplify_stack_save_restore();

      // Set up LLVM phi node incoming edges
      for (std::tr1::unordered_map<PhiTerm*, llvm::PHINode*>::iterator it = phi_node_map.begin();
           it != phi_node_map.end(); ++it) {

        bool unknown_type = phi_storage_map.find(it->first) != phi_storage_map.end();
        for (std::size_t n = 0; n < it->first->n_incoming(); ++n) {
          PSI_ASSERT(m_value_terms.find(it->first->incoming_block(n)) != m_value_terms.end());
          llvm::BasicBlock *incoming_block =
            llvm::cast<llvm::BasicBlock>(m_value_terms.find(it->first->incoming_block(n))->second.known_value());
          LLVMValue incoming_value = build_value(it->first->incoming_value(n));

          llvm::Value *value;
          if (unknown_type) {
            if (!incoming_value.is_unknown())
              throw LLVMBuildError("inconsistent incoming types to phi node");
            value = incoming_value.unknown_value();
          } else {
            if (!incoming_value.is_known())
              throw LLVMBuildError("inconsistent incoming types to phi node");
            value = incoming_value.known_value();
          }

          it->second->addIncoming(value, incoming_block);
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
    llvm::Value* LLVMFunctionBuilder::cast_pointer_to_generic(llvm::Value *value) {
      PSI_ASSERT(value->getType()->isPointerTy());

      const llvm::Type *i8ptr = llvm::Type::getInt8PtrTy(llvm_context());
      if (value->getType() == i8ptr)
        return value;

      if (llvm::Constant *const_value = llvm::dyn_cast<llvm::Constant>(value)) {
        return llvm::ConstantExpr::getPointerCast(const_value, i8ptr);
      } else {
        return irbuilder().CreatePointerCast(value, i8ptr);
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
    llvm::Value* LLVMFunctionBuilder::cast_pointer_from_generic(llvm::Value *value, const llvm::Type *type) {
      PSI_ASSERT(value->getType()->isPointerTy());
      PSI_ASSERT(type->isPointerTy());
      if (value->getType() == type)
        return value;

      PSI_ASSERT(value->getType() == llvm::Type::getInt8PtrTy(llvm_context()));
      if (llvm::Constant *const_value = llvm::dyn_cast<llvm::Constant>(value)) {
        return llvm::ConstantExpr::getPointerCast(const_value, type);
      } else {
        return irbuilder().CreatePointerCast(value, type);
      }
    }

    /**
     * Create an alloca instruction for the specified number of bytes
     * using the maximum system alignment.
     */
    llvm::Instruction* LLVMFunctionBuilder::create_alloca(llvm::Value *size) {
      llvm::AllocaInst *inst = irbuilder().CreateAlloca(llvm::Type::getInt8Ty(llvm_context()), size);
      inst->setAlignment(unknown_alloca_align());
      return inst;
    }

    /**
     * Create an alloca instruction for the specified type. This
     * requires that the type have a known value.
     */
    llvm::Value* LLVMFunctionBuilder::create_alloca_for(Term *stored_type) {
      PSI_ASSERT(stored_type->category() == Term::category_type);

      if (const llvm::Type* ty = build_type(stored_type))
        return irbuilder().CreateAlloca(ty);

      // Okay, the type is unknown. However if it is an unknown-length
      // array of values with a known type, I can still get that
      // through to LLVM.
      if (FunctionalTermPtr<ArrayType> as_array = dynamic_cast_functional<ArrayType>(stored_type)) {
        const llvm::Type *element_type = build_type(as_array.backend().element_type());
        if (element_type) {
          llvm::Value *length = build_known_value(as_array.backend().length());
          return irbuilder().CreateAlloca(element_type, length);
        }
      }

      // Okay, it's really unknown, so just allocate as i8[n]
      llvm::Value *size_align = build_known_value(stored_type);
      llvm::Value *size = irbuilder().CreateExtractValue(size_align, 0);
      return create_alloca(size);
    }

    /**
     * Call <tt>llvm.memcpy.p0i8.p0i8.i64</tt> with default alignment
     * and volatile parameters.
     */
    void LLVMFunctionBuilder::create_memcpy(llvm::Value *dest, llvm::Value *src, llvm::Value *count) {
      llvm::ConstantInt *align = llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_context()), 0);
      llvm::ConstantInt *false_val = llvm::ConstantInt::getFalse(llvm_context());
      irbuilder().CreateCall5(LLVMIntrinsics::memcpy(llvm_module()), dest, src, count, align, false_val);
    }

    /**
     * Create a store instruction for the given term into
     * memory. Handles cases where the term is known or not correctly.
     *
     * \return The store instruction created. This may be NULL if the
     * type of \c src is empty.
     */
    void LLVMFunctionBuilder::create_store(llvm::Value *dest, Term *src) {
      LLVMValue llvm_src = build_value(src);

      if (llvm_src.is_known()) {
        llvm::Value *cast_dest = cast_pointer_from_generic(dest, llvm_src.known_value()->getType()->getPointerTo());
        irbuilder().CreateStore(llvm_src.known_value(), cast_dest);
      } else {
        PSI_ASSERT(llvm_src.is_unknown());
        create_store_unknown(dest, llvm_src.unknown_value(), src->type());
      }
    }

    /**
     * Create a memcpy call which stores an unknown term into a
     * pointer.
     */
    void LLVMFunctionBuilder::create_store_unknown(llvm::Value *dest, llvm::Value *src, Term *src_type) {
      PSI_ASSERT(src_type->category() == Term::category_type);
      llvm::Value *src_type_value = build_known_value(src_type);
      llvm::Value *size = irbuilder().CreateExtractValue(src_type_value, 0);
      create_memcpy(dest, src, size);
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

    namespace LLVMIntrinsics {
      /// \brief Gets the LLVM intrinsic <tt>llvm.memcpy.p0i8.p0i8.i64</tt>
      llvm::Function* memcpy(llvm::Module& m) {
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

      /// \brief Gets the LLVM intrnisic <tt>llvm.stacksave</tt>
      llvm::Function* stacksave(llvm::Module& m) {
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

      /// \brief Gets the LLVM intrinsic <tt>llvm.stackrestore</tt>
      llvm::Function* stackrestore(llvm::Module& m) {
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

    namespace LLVMMetatype {
      /**
       * \brief Get the LLVM type for Metatype values.
       */
      llvm::Type *type(llvm::LLVMContext& c) {
        const llvm::Type* i64 = llvm::Type::getInt64Ty(c);
        return llvm::StructType::get(c, i64, i64, NULL);
      }

      /**
       * \brief Get a metatype value for size and alignment
       * specified in \c size_t.
       */
      llvm::Constant* from_size_t(llvm::LLVMContext& c, std::size_t size, std::size_t align) {
        if (!align || (size % align != 0) || (align & (align - 1)))
          throw LLVMBuildError("invalid values for size or align of Metatype");

        const llvm::Type *i64 = llvm::Type::getInt64Ty(c);
        return from_constant(llvm::ConstantInt::get(i64, size),
                             llvm::ConstantInt::get(i64, align));
      }

      /**
       * \brief Get an LLVM value for Metatype for the given LLVM type.
       */
      llvm::Constant* from_type(const llvm::Type* ty) {
        return from_constant(llvm::ConstantExpr::getSizeOf(ty),
                             llvm::ConstantExpr::getAlignOf(ty));
      }

      /**
       * \brief Get an LLVM value for a specified size and alignment.
       *
       * The result of this call will be a global constant.
       */
      llvm::Constant* from_constant(llvm::Constant *size, llvm::Constant *align) {
        if (!size->getType()->isIntegerTy(64) || !align->getType()->isIntegerTy(64))
          throw TvmUserError("size or align in metatype is not a 64-bit integer");

        if (llvm::isa<llvm::ConstantInt>(align) && (llvm::cast<llvm::ConstantInt>(align)->getValue().exactLogBase2() < 0))
          throw TvmUserError("alignment is not a power of two");

        llvm::Constant* values[2] = {size, align};
        return llvm::ConstantStruct::get(size->getContext(), values, 2, false);
      }

      /**
       * \brief Get an LLVM value for a specified size and alignment.
       *
       * The result of this call will be a global constant.
       */
      LLVMValue from_value(LLVMIRBuilder& irbuilder, llvm::Value *size, llvm::Value *align) {
        llvm::Value *undef = llvm::UndefValue::get(type(irbuilder.getContext()));
        llvm::Value *stage1 = irbuilder.CreateInsertValue(undef, size, 0);
        llvm::Value *stage2 = irbuilder.CreateInsertValue(stage1, align, 1);
        return LLVMValue::known(stage2);
      }

      /**
       * \brief Get the size value from a Metatype constant.
       */
      llvm::Constant* to_size_constant(llvm::Constant *value) {
        unsigned zero = 0;
        return llvm::ConstantExpr::getExtractValue(value, &zero, 1);
      }

      /**
       * \brief Get the alignment value from a Metatype constant.
       */
      llvm::Constant* to_align_constant(llvm::Constant *value) {
        unsigned one = 1;
        return llvm::ConstantExpr::getExtractValue(value, &one, 1);
      }

      /**
       * \brief Get the size value from a Metatype value.
       */
      llvm::Value* to_size_value(LLVMIRBuilder& irbuilder, llvm::Value* value) {
        return irbuilder.CreateExtractValue(value, 0);
      }

      /**
       * \brief Get the align value from a Metatype value.
       */
      llvm::Value* to_align_value(LLVMIRBuilder& irbuilder, llvm::Value* value) {
        return irbuilder.CreateExtractValue(value, 1);
      }
    }
  }
}
