#include "Builder.hpp"
#include "Templates.hpp"

#include "../Aggregate.hpp"
#include "../Recursive.hpp"

#include <boost/next_prior.hpp>

#include <llvm/Function.h>
#include <../lib/llvm-2.8/include/llvm/Target/TargetData.h>

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      struct FunctionBuilder::ValueBuilderCallback : PtrValidBase<llvm::Value> {
        FunctionBuilder *self;
        const FunctionBuilder::ValueTermMap *value_terms;

        ValueBuilderCallback(FunctionBuilder *self_,
                             const FunctionBuilder::ValueTermMap *value_terms_)
          : self(self_), value_terms(value_terms_) {}

        llvm::Value* build(Term *term) const {
          llvm::BasicBlock *old_insert_block = self->irbuilder().GetInsertBlock();

          // Set the insert point to the dominator block of the value
          llvm::BasicBlock *new_insert_block;
          Term *src = term->source();
          PSI_ASSERT(src);
          switch (src->term_type()) {
            case term_instruction: {
              FunctionBuilder::ValueTermMap::const_iterator it = value_terms->find(cast<InstructionTerm>(src)->block());
              new_insert_block = llvm::cast<llvm::BasicBlock>(it->second);
              break;
            }
            
            case term_block: {
              FunctionBuilder::ValueTermMap::const_iterator it = value_terms->find(cast<BlockTerm>(src));
              new_insert_block = llvm::cast<llvm::BasicBlock>(it->second);
              break;
            }
            
            case term_function_parameter: {
              FunctionParameterTerm *cast_src = cast<FunctionParameterTerm>(src);
              PSI_ASSERT(!cast_src->phantom() && (cast_src->function() == self->function()));
              new_insert_block = &self->llvm_function()->front();
              break;
            }
            
            default:
              PSI_FAIL("unexpected source term type");
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

          llvm::Value* result;
          switch(term->term_type()) {
          case term_functional: {
            result = self->build_value_functional(cast<FunctionalTerm>(term));
            break;
          }

          case term_apply: {
            Term* actual = cast<ApplyTerm>(term)->unpack();
            PSI_ASSERT(actual->term_type() != term_apply);
            result = self->build_value(actual);
            break;
          }

          default:
            PSI_FAIL("unexpected term type");
          }

          llvm::Instruction *value_insn = llvm::dyn_cast<llvm::Instruction>(result);
          if (value_insn && !value_insn->hasName() && !value_insn->getType()->isVoidTy())
            value_insn->setName(self->term_name(term));

          // restore original insert block
          if (old_insert_block)
            self->irbuilder().SetInsertPoint(old_insert_block);

          return result;
        }
      };

      FunctionBuilder::FunctionBuilder(ModuleBuilder *global_builder,
                                       FunctionTerm *function,
                                       llvm::Function *llvm_function)
        : ConstantBuilder(*global_builder),
          m_global_builder(global_builder),
          m_irbuilder(global_builder->llvm_context(), llvm::TargetFolder(global_builder->llvm_target_machine()->getTargetData())),
          m_function(function),
          m_llvm_function(llvm_function) {
      }

      FunctionBuilder::~FunctionBuilder() {
      }

      llvm::Constant* FunctionBuilder::build_constant(Term *term) {
        return m_global_builder->build_constant(term);
      }

      const llvm::Type* FunctionBuilder::build_type(Term* term) {
        if (!term->source() || isa<GlobalTerm>(term->source())) {
          return m_global_builder->build_type(term);
        } else {
          return ConstantBuilder::build_type(term);
        }
      }

      /**
       * \brief Create the code required to generate a value for the
       * given term.
       *
       * \pre <tt>!term->phantom()</tt>
       */
      llvm::Value* FunctionBuilder::build_value(Term* term) {
        PSI_ASSERT(!term->phantom());

        if (!term->source() || isa<GlobalTerm>(term->source()))
          return build_constant(term);

        switch (term->term_type()) {
        case term_function_parameter:
        case term_instruction:
        case term_phi:
        case term_block: {
          ValueTermMap::iterator it = m_value_terms.find(term);
          PSI_ASSERT(it != m_value_terms.end());
          return it->second;
        }

        case term_apply:
        case term_functional:
          return build_term(m_value_terms, term, ValueBuilderCallback(this, &m_value_terms)).first;

        default:
          PSI_FAIL("unexpected term type");
        }
      }

      /**
       * Set up function entry. This converts function parameters from
       * whatever format the calling convention passes them in.
       */
      llvm::BasicBlock* FunctionBuilder::build_function_entry() {
        llvm::BasicBlock *prolog_block = llvm::BasicBlock::Create(llvm_context(), "", llvm_function());
        
        llvm::Function::ArgumentListType::iterator ii = m_llvm_function->getArgumentList().begin(), ie = m_llvm_function->getArgumentList().end();
        for (std::size_t in = function()->function_type()->n_phantom_parameters(); ii != ie; ++ii, ++in) {
          FunctionParameterTerm* param = m_function->parameter(in);
          llvm::Value *value = &*ii;
          value->setName(term_name(param));
          m_value_terms.insert(std::make_pair(param, value));
        }

        return prolog_block;
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
      bool FunctionBuilder::has_outstanding_alloca(llvm::BasicBlock *block) {
        llvm::Function *llvm_stackrestore = intrinsic_stackrestore(*m_llvm_function->getParent());

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
      llvm::CallInst* FunctionBuilder::first_stack_restore(llvm::BasicBlock *block) {
        llvm::Function *llvm_stackrestore = intrinsic_stackrestore(*m_llvm_function->getParent());

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
      void FunctionBuilder::simplify_stack_save_restore() {
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
        PSI_ASSERT(save_insn->getCalledFunction() == intrinsic_stacksave(*m_llvm_function->getParent()));
        if (save_insn->hasNUses(0))
          save_insn->eraseFromParent();
      }

      void FunctionBuilder::run() {
        std::tr1::unordered_map<BlockTerm*, llvm::Value*> stack_pointers;

        // Set up parameters
        llvm::BasicBlock *llvm_prolog_block = build_function_entry();

        // Set up basic blocks
        BlockTerm* entry_block = m_function->entry();
        std::vector<BlockTerm*> sorted_blocks = m_function->topsort_blocks();

        // create llvm blocks
        std::vector<std::pair<BlockTerm*, llvm::BasicBlock*> > blocks;
        for (std::vector<BlockTerm*>::iterator it = sorted_blocks.begin(); it != sorted_blocks.end(); ++it) {
          llvm::BasicBlock *llvm_bb = llvm::BasicBlock::Create(llvm_context(), term_name(*it), m_llvm_function);
          std::pair<ValueTermMap::iterator, bool> insert_result = m_value_terms.insert(std::make_pair(*it, llvm_bb));
          PSI_ASSERT(insert_result.second);
          blocks.push_back(std::make_pair(*it, llvm_bb));
        }

        // Finish prolog block
        irbuilder().SetInsertPoint(llvm_prolog_block);
        // Save prolog stack and jump into entry
        stack_pointers[NULL] = irbuilder().CreateCall(intrinsic_stacksave(*m_llvm_function->getParent()));
        PSI_ASSERT(blocks[0].first == entry_block);
        irbuilder().CreateBr(blocks[0].second);

        std::tr1::unordered_map<PhiTerm*, llvm::PHINode*> phi_node_map;

        // Build basic blocks
        for (std::vector<std::pair<BlockTerm*, llvm::BasicBlock*> >::iterator it = blocks.begin();
             it != blocks.end(); ++it) {
          irbuilder().SetInsertPoint(it->second);
          PSI_ASSERT(it->second->empty());

          // Set up phi terms
          BlockTerm::PhiList& phi_list = it->first->phi_nodes();
          for (BlockTerm::PhiList::iterator jt = phi_list.begin(); jt != phi_list.end(); ++jt) {
            PhiTerm *phi = &*jt;
            const llvm::Type *llvm_ty = build_type(phi->type());
            llvm::PHINode *llvm_phi = irbuilder().CreatePHI(llvm_ty, term_name(phi));
	    phi_node_map.insert(std::make_pair(phi, llvm_phi));
            m_value_terms.insert(std::make_pair(phi, llvm_phi));
          }

          // Restore stack as it was when dominating block exited, so
          // any values alloca'd since then are removed. This is
          // necessary to allow loops which handle unknown types without
          // unbounded stack growth.
          PSI_ASSERT(stack_pointers.find(it->first->dominator()) != stack_pointers.end());
          llvm::Value *dominator_stack_ptr = stack_pointers[it->first->dominator()];
          irbuilder().CreateCall(intrinsic_stackrestore(*m_llvm_function->getParent()), dominator_stack_ptr);

          // Build instructions!
          BlockTerm::InstructionList& insn_list = it->first->instructions();
          for (BlockTerm::InstructionList::iterator jt = insn_list.begin(); jt != insn_list.end(); ++jt) {
            InstructionTerm *insn = &*jt;
            llvm::Value *r = build_value_instruction(insn);
            m_value_terms.insert(std::make_pair(insn, r));
          }

          if (!it->second->getTerminator())
            throw BuildError("LLVM block was not terminated during function building");

          // Build block epilog: must move the IRBuilder insert point to
          // before the terminating instruction first.
          irbuilder().SetInsertPoint(it->second, boost::prior(it->second->end()));

          // Save stack pointer so it can be restored in dominated
          // blocks. This only needs to be done if the alloca is used
          // during this block outside of a save/restore, and the block
          // does not terminate the function
          PSI_ASSERT(stack_pointers.find(it->first) == stack_pointers.end());
          if ((it->second->getTerminator()->getNumSuccessors() > 0) && has_outstanding_alloca(it->second)) {
            stack_pointers[it->first] = irbuilder().CreateCall(intrinsic_stacksave(*m_llvm_function->getParent()));
          } else {
            stack_pointers[it->first] = dominator_stack_ptr;
          }
        }

        simplify_stack_save_restore();

        // Set up LLVM phi node incoming edges
        for (std::tr1::unordered_map<PhiTerm*, llvm::PHINode*>::iterator it = phi_node_map.begin();
             it != phi_node_map.end(); ++it) {

          for (std::size_t n = 0; n < it->first->n_incoming(); ++n) {
            PSI_ASSERT(m_value_terms.find(it->first->incoming_block(n)) != m_value_terms.end());
            llvm::BasicBlock *incoming_block =
              llvm::cast<llvm::BasicBlock>(m_value_terms.find(it->first->incoming_block(n))->second);
	    PSI_ASSERT(incoming_block);
            llvm::Value* incoming_value = build_value(it->first->incoming_value(n));
	    it->second->addIncoming(incoming_value, incoming_block);
          }
        }
      }

      /// Returns the maximum alignment for any type supported. This
      /// seems to have to be hardwired which is bad, but 16 should be
      /// enough for all current platforms.
      unsigned FunctionBuilder::unknown_alloca_align() {
        return 16;
      }

      /**
       * Get one of the names for a term, or an empty StringRef if the
       * term has no name.
       */
      llvm::StringRef FunctionBuilder::term_name(Term *term) {
        const FunctionTerm::TermNameMap& map = m_function->term_name_map();
        FunctionTerm::TermNameMap::const_iterator it = map.find(term);

        if (it != map.end()) {
          return llvm::StringRef(it->second);
        } else {
          return llvm::StringRef();
        }
      }
    }
  }
}
