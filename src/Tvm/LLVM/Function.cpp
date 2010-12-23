#include "Builder.hpp"
#include "Templates.hpp"

#include "../Aggregate.hpp"
#include "../Recursive.hpp"

#include <boost/next_prior.hpp>

#include <llvm/Function.h>

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      struct FunctionBuilder::ValueBuilderCallback {
        FunctionBuilder *self;
        const FunctionBuilder::ValueTermMap *value_terms;

        ValueBuilderCallback(FunctionBuilder *self_,
                             const FunctionBuilder::ValueTermMap *value_terms_)
          : self(self_), value_terms(value_terms_) {}

        BuiltValue build(Term *term) const {
          llvm::BasicBlock *old_insert_block = self->irbuilder().GetInsertBlock();

          // Set the insert point to the dominator block of the value
          llvm::BasicBlock *new_insert_block;
          Term *src = term->source();
          PSI_ASSERT(src);
          if (src->term_type() == term_block) {
            FunctionBuilder::ValueTermMap::const_iterator it = value_terms->find(term->source());
            PSI_ASSERT((it != value_terms->end()) && it->second.known());
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

          BuiltValue result;
          switch(term->term_type()) {
          case term_functional: {
            result = self->build_value_functional(cast<FunctionalTerm>(term));

            llvm::Value *val = result.known() ? result.known_value() : result.unknown_value();
            if (llvm::isa<llvm::Instruction>(val) && (val->getType() != llvm::Type::getVoidTy(self->llvm_context())))
              val->setName(self->term_name(term));

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

          // restore original insert block
          if (old_insert_block)
            self->irbuilder().SetInsertPoint(old_insert_block);

          return result;
        }

        BuiltValue invalid() const {return BuiltValue();}
        bool valid(const BuiltValue& t) const {return t.valid();}
      };

      FunctionBuilder::FunctionBuilder(GlobalBuilder *global_builder,
                                       FunctionTerm *function,
                                       llvm::Function *llvm_function,
                                       IRBuilder *irbuilder)
        : ConstantBuilder(&global_builder->llvm_context(),
                          global_builder->llvm_target_machine()),
          m_global_builder(global_builder),
          m_irbuilder(irbuilder),
          m_function(function),
          m_llvm_function(llvm_function) {
      }

      FunctionBuilder::~FunctionBuilder() {
      }

      llvm::Constant* FunctionBuilder::build_constant(Term *term) {
        return m_global_builder->build_constant(term);
      }

      const llvm::Type* FunctionBuilder::build_type(Term* term) {
        if (term->global()) {
          return m_global_builder->build_type(term);
        } else {
          return ConstantBuilder::build_type(term);
        }
      }

      /**
       * \brief Create the code required to generate a value for the
       * given term.
       */
      BuiltValue FunctionBuilder::build_value(Term* term) {
        if (term->global())
          return value_known(build_constant(term));

        if (term->phantom())
          throw BuildError("cannot get value for phantom term");

        BuiltValue result;
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
                   (result.known() && (result.known_value()->getType() == metatype_type(*this))));
        return result;
      }

      /**
       * \brief Identical to build_value, but requires that the result
       * be of a known type so that an llvm::Value can be returned.
       */
      llvm::Value* FunctionBuilder::build_known_value(Term *term) {
        BuiltValue v = build_value(term);
        PSI_ASSERT(v.known());
        return v.known_value();
      }

      /**
       * Set up function entry. This converts function parameters from
       * whatever format the calling convention passes them in.
       */
      llvm::BasicBlock* FunctionBuilder::build_function_entry() {
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
              m_value_terms.insert(std::make_pair(param, value_known(load)));
            } else {
              llvm_param->setName(term_name(param));
              m_value_terms.insert(std::make_pair(param, value_unknown(llvm_param)));
            }
          } else {
            llvm_param->setName(term_name(param));
            m_value_terms.insert(std::make_pair(param, value_known(llvm_param)));
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
      void FunctionBuilder::build_phi_alloca(std::tr1::unordered_map<PhiTerm*, llvm::Value*>& phi_storage_map,
                                             const std::vector<BlockTerm*>& dominated) {
        for (std::vector<BlockTerm*>::const_iterator jt = dominated.begin(); jt != dominated.end(); ++jt) {
          BlockTerm::PhiList& phi_list = (*jt)->phi_nodes();
          for (BlockTerm::PhiList::iterator kt = phi_list.begin(); kt != phi_list.end(); ++kt) {
            PhiTerm& phi = *kt;
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
      bool FunctionBuilder::has_outstanding_alloca(llvm::BasicBlock *block) {
        llvm::Function *llvm_stackrestore = intrinsic_stackrestore(llvm_module());

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
        llvm::Function *llvm_stackrestore = intrinsic_stackrestore(llvm_module());

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
        PSI_ASSERT(save_insn->getCalledFunction() == intrinsic_stacksave(llvm_module()));
        if (save_insn->hasNUses(0))
          save_insn->eraseFromParent();
      }

      void FunctionBuilder::run() {
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
            throw BuildError("cannot compile function with unterminated blocks");

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
            m_value_terms.insert(std::make_pair(it->first, value_known(llvm_bb)));
          PSI_ASSERT(insert_result.second);
        }

        // Finish prolog block
        irbuilder().SetInsertPoint(llvm_prolog_block);
        // set up phi nodes for entry blocks
        build_phi_alloca(phi_storage_map, entry_blocks);
        // Save prolog stack and jump into entry
        stack_pointers[NULL] = irbuilder().CreateCall(intrinsic_stacksave(llvm_module()));
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
              m_value_terms.insert(std::make_pair(&phi, value_known(llvm_phi)));
            } else {
              llvm_phi = irbuilder().CreatePHI(llvm::Type::getInt8PtrTy(llvm_context()));
              PSI_ASSERT(phi_storage_map.find(&phi) != phi_storage_map.end());
              llvm::Value *phi_storage = phi_storage_map.find(&phi)->second;
              m_value_terms.insert(std::make_pair(&phi, value_unknown(phi_storage)));
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
          irbuilder().CreateCall(intrinsic_stackrestore(llvm_module()), dominator_stack_ptr);

          // Build instructions!
          BlockTerm::InstructionList& insn_list = it->first->instructions();
          for (BlockTerm::InstructionList::iterator jt = insn_list.begin(); jt != insn_list.end(); ++jt) {
            InstructionTerm& insn = *jt;
            BuiltValue r = build_value_instruction(&insn);
            m_value_terms.insert(std::make_pair(&insn, r));
          }

          if (!it->second->getTerminator())
            throw BuildError("LLVM block was not terminated during function building");

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
            stack_pointers[it->first] = irbuilder().CreateCall(intrinsic_stacksave(llvm_module()));
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
            BuiltValue incoming_value = build_value(it->first->incoming_value(n));

            llvm::Value *value;
            if (unknown_type) {
              if (!incoming_value.unknown())
                throw BuildError("inconsistent incoming types to phi node");
              value = incoming_value.unknown_value();
            } else {
              if (!incoming_value.known())
                throw BuildError("inconsistent incoming types to phi node");
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
      llvm::Value* FunctionBuilder::cast_pointer_to_generic(llvm::Value *value) {
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
      llvm::Value* FunctionBuilder::cast_pointer_from_generic(llvm::Value *value, const llvm::Type *type) {
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
      llvm::Instruction* FunctionBuilder::create_alloca(llvm::Value *size) {
        llvm::AllocaInst *inst = irbuilder().CreateAlloca(llvm::Type::getInt8Ty(llvm_context()), size);
        inst->setAlignment(unknown_alloca_align());
        return inst;
      }

      /**
       * Create an alloca instruction for the specified type. This
       * requires that the type have a known value.
       */
      llvm::Value* FunctionBuilder::create_alloca_for(Term *stored_type) {
        PSI_ASSERT(stored_type->category() == Term::category_type);

        if (const llvm::Type* ty = build_type(stored_type))
          return irbuilder().CreateAlloca(ty);

        // Okay, the type is unknown. However if it is an unknown-length
        // array of values with a known type, I can still get that
        // through to LLVM.
        if (ArrayType::Ptr as_array = dyn_cast<ArrayType>(stored_type)) {
          const llvm::Type *element_type = build_type(as_array->element_type());
          if (element_type) {
            llvm::Value *length = build_known_value(as_array->length());
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
      void FunctionBuilder::create_memcpy(llvm::Value *dest, llvm::Value *src, llvm::Value *count) {
        llvm::ConstantInt *align = llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_context()), 0);
        llvm::ConstantInt *false_val = llvm::ConstantInt::getFalse(llvm_context());
        PSI_ASSERT(llvm::cast<llvm::IntegerType>(count->getType())->getBitWidth() == intptr_type_bits());
        if (intptr_type_bits() <= 32) {
          irbuilder().CreateCall5(intrinsic_memcpy_32(llvm_module()), dest, src, count, align, false_val);
        } else {
          irbuilder().CreateCall5(intrinsic_memcpy_64(llvm_module()), dest, src, count, align, false_val);
        }
      }

      /**
       * Create a store instruction for the given term into
       * memory. Handles cases where the term is known or not correctly.
       *
       * \return The store instruction created. This may be NULL if the
       * type of \c src is empty.
       */
      void FunctionBuilder::create_store(llvm::Value *dest, Term *src) {
        BuiltValue llvm_src = build_value(src);

        if (llvm_src.known()) {
          llvm::Value *cast_dest = cast_pointer_from_generic(dest, llvm_src.known_value()->getType()->getPointerTo());
          irbuilder().CreateStore(llvm_src.known_value(), cast_dest);
        } else {
          PSI_ASSERT(llvm_src.unknown());
          create_store_unknown(dest, llvm_src.unknown_value(), src->type());
        }
      }

      /**
       * Create a memcpy call which stores an unknown term into a
       * pointer.
       */
      void FunctionBuilder::create_store_unknown(llvm::Value *dest, llvm::Value *src, Term *src_type) {
        PSI_ASSERT(src_type->category() == Term::category_type);
        llvm::Value *src_type_value = build_known_value(src_type);
        llvm::Value *size = irbuilder().CreateExtractValue(src_type_value, 0);
        create_memcpy(dest, src, size);
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
