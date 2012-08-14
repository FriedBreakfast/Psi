#include "Builder.hpp"
#include "Templates.hpp"

#include "../Aggregate.hpp"
#include "../Recursive.hpp"
#include "../FunctionalBuilder.hpp"

#include <boost/next_prior.hpp>
#include <boost/unordered_set.hpp>

#include <llvm/Function.h>
#include <llvm/Module.h>
#include <llvm/Target/TargetData.h>

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      struct FunctionBuilder::ValueBuilderCallback : PtrValidBase<llvm::Value> {
        FunctionBuilder *self;
        const FunctionBuilder::ValueTermMap *value_terms;

        ValueBuilderCallback(FunctionBuilder *self_,
                             const FunctionBuilder::ValueTermMap *value_terms_)
          : self(self_), value_terms(value_terms_) {}

        llvm::Value* build(const ValuePtr<>& term) const {
          llvm::BasicBlock *old_insert_block = self->irbuilder().GetInsertBlock();

          // Set the insert point to the dominator block of the value
          llvm::BasicBlock *new_insert_block;
          Value *src = term->source();
          PSI_ASSERT(src);
          switch (src->term_type()) {
            case term_instruction: {
              FunctionBuilder::ValueTermMap::const_iterator it = value_terms->find(value_cast<Instruction>(src)->block());
              new_insert_block = llvm::cast<llvm::BasicBlock>(it->second);
              break;
            }
            
            case term_phi: {
              FunctionBuilder::ValueTermMap::const_iterator it = value_terms->find(value_cast<Phi>(src)->block());
              new_insert_block = llvm::cast<llvm::BasicBlock>(it->second);
              break;
            }
            
            case term_function_parameter: {
#ifdef PSI_DEBUG
              FunctionParameter *cast_src = value_cast<FunctionParameter>(src);
              PSI_ASSERT(!cast_src->phantom() && (cast_src->function() == self->function()));
#endif
              new_insert_block = &self->llvm_function()->front();
              break;
            }
            
            case term_block: {
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

            // if the block has been completed, it should have a jump
            // instruction at the end, and we want to insert before that.
            self->irbuilder().SetInsertPoint(new_insert_block, new_insert_block->getTerminator());
          } else {
            old_insert_block = NULL;
          }

          llvm::Value* result;
          switch(term->term_type()) {
          case term_functional: {
            result = self->build_value_functional(value_cast<FunctionalValue>(term));
            break;
          }

          case term_apply: {
            ValuePtr<> actual = value_cast<ApplyValue>(term)->unpack();
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
                                       const ValuePtr<Function>& function,
                                       llvm::Function *llvm_function)
        : m_module_builder(global_builder),
          m_irbuilder(global_builder->llvm_context(), llvm::TargetFolder(global_builder->llvm_target_machine()->getTargetData())),
          m_function(function),
          m_llvm_function(llvm_function) {
      }

      FunctionBuilder::~FunctionBuilder() {
      }

      /**
       * \brief Create the code required to generate a value for the
       * given term.
       *
       * \pre <tt>!term->phantom()</tt>
       */
      llvm::Value* FunctionBuilder::build_value(const ValuePtr<>& term) {
        PSI_ASSERT(!term->phantom());

        if (!term->source() || isa<Global>(term->source()))
          return module_builder()->build_constant(term);

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
       * Remove unnecessary stack save and restore instructions.
       */
      void FunctionBuilder::setup_stack_save_restore(const std::vector<std::pair<ValuePtr<Block>, llvm::BasicBlock*> >& blocks) {
        boost::unordered_map<ValuePtr<Block>, ValuePtr<Block> > stack_dominator;
        boost::unordered_map<ValuePtr<Block>, llvm::Value*> stack_save_values;
        boost::unordered_set<ValuePtr<Block> > stack_restore_required;

        stack_dominator[blocks.front().first] = blocks.front().first;
        for (std::vector<std::pair<ValuePtr<Block>, llvm::BasicBlock*> >::const_iterator ii = boost::next(blocks.begin()), ie = blocks.end(); ii != ie; ++ii)
          stack_dominator[ii->first] = stack_dominator[ii->first->dominator()];

        // Work out which saves and restores are required
        for (std::vector<std::pair<ValuePtr<Block>, llvm::BasicBlock*> >::const_iterator ii = boost::next(blocks.begin()), ie = blocks.end(); ii != ie; ++ii) {
          ValuePtr<> stack_restore = stack_dominator[ii->first];
          bool has_alloca = false;
          for (Block::InstructionList::const_iterator ji = ii->first->instructions().begin(), je = ii->first->instructions().end(); ji != je; ++ji) {
            if (isa<Alloca>(*ji)) {
              has_alloca = true;
              break;
            }
          }
          
          std::vector<ValuePtr<Block> > successors = ii->first->successors();
          for (std::vector<ValuePtr<Block> >::const_iterator ji = successors.begin(), je = successors.end(); ji != je; ++ji) {
            ValuePtr<Block> target_stack_restore = stack_dominator[*ji];
            if (has_alloca ? (target_stack_restore != ii->first) : (target_stack_restore != stack_restore)) {
              stack_save_values[target_stack_restore] = NULL;
              stack_restore_required.insert(*ji);
            }
          }
        }
        
        // Insert required saves
        for (std::vector<std::pair<ValuePtr<Block>, llvm::BasicBlock*> >::const_iterator ii = boost::next(blocks.begin()), ie = blocks.end(); ii != ie; ++ii) {
          boost::unordered_map<ValuePtr<Block>, llvm::Value*>::iterator ji = stack_save_values.find(ii->first);
          if (ji != stack_save_values.end()) {
            irbuilder().SetInsertPoint(ii->second, boost::prior(ii->second->end()));
            ji->second = irbuilder().CreateCall(module_builder()->llvm_stacksave());
          }
        }
        
        // Insert required restores
        for (std::vector<std::pair<ValuePtr<Block>, llvm::BasicBlock*> >::const_iterator ii = boost::next(blocks.begin()), ie = blocks.end(); ii != ie; ++ii) {
          if (stack_restore_required.find(ii->first) != stack_restore_required.end()) {
            irbuilder().SetInsertPoint(ii->second, ii->second->getFirstNonPHI());
            ValuePtr<Block> restore = stack_dominator[ii->first];
            irbuilder().CreateCall(module_builder()->llvm_stackrestore(), stack_save_values[restore]);
          }
        }
      }

      void FunctionBuilder::run() {
        // Set up parameters
        llvm::Function::ArgumentListType::iterator ii = m_llvm_function->getArgumentList().begin(), ie = m_llvm_function->getArgumentList().end();
        for (std::size_t in = function()->function_type()->n_phantom(); ii != ie; ++ii, ++in) {
          ValuePtr<FunctionParameter> param = m_function->parameters().at(in);
          llvm::Value *value = &*ii;
          value->setName(term_name(param));
          m_value_terms.insert(std::make_pair(param, value));
        }

        // create llvm blocks
        std::vector<std::pair<ValuePtr<Block>, llvm::BasicBlock*> > blocks;
        for (Function::BlockList::const_iterator it = m_function->blocks().begin(), ie = m_function->blocks().end(); it != ie; ++it) {
          llvm::BasicBlock *llvm_bb = llvm::BasicBlock::Create(module_builder()->llvm_context(), term_name(*it), m_llvm_function);
          std::pair<ValueTermMap::iterator, bool> insert_result = m_value_terms.insert(std::make_pair(*it, llvm_bb));
          PSI_ASSERT(insert_result.second);
          blocks.push_back(std::make_pair(*it, llvm_bb));
        }

        boost::unordered_map<ValuePtr<Phi>, llvm::PHINode*> phi_node_map;
        
        // Set up exception handling personality routine
        llvm::Constant *eh_personality;
        if (!m_function->exception_personality().empty()) {
          eh_personality = module_builder()->target_callback()->exception_personality_routine(module_builder()->llvm_module(), m_function->exception_personality());
          eh_personality = llvm::ConstantExpr::getBitCast(eh_personality, llvm::Type::getInt8PtrTy(module_builder()->llvm_context()));
        } else {
          eh_personality = NULL;
        }
        
        // Build basic blocks
        for (std::vector<std::pair<ValuePtr<Block>, llvm::BasicBlock*> >::iterator it = blocks.begin();
             it != blocks.end(); ++it) {
          irbuilder().SetInsertPoint(it->second);
          PSI_ASSERT(it->second->empty());

          // Set up phi terms
          for (Block::PhiList::const_iterator jt = it->first->phi_nodes().begin(), je = it->first->phi_nodes().end(); jt != je; ++jt) {
            const ValuePtr<Phi>& phi = *jt;
            llvm::Type *llvm_ty = module_builder()->build_type(phi->type());
            llvm::PHINode *llvm_phi = irbuilder().CreatePHI(llvm_ty, phi->edges().size(), term_name(phi));
            phi_node_map.insert(std::make_pair(phi, llvm_phi));
            m_value_terms.insert(std::make_pair(phi, llvm_phi));
          }
          
          // Check if this is a landing pad - if so, add entry instructions
          if (it->first->is_landing_pad()) {
#if 0
            if (!eh_personality)
              throw BuildError("Landing pad block occurs in function with no exception personality set.");

            llvm::Value *ex = irbuilder().CreateCall(m_module_builder->llvm_eh_exception());

            llvm::SmallVector<llvm::Value*, 6> select_args;
            select_args.push_back(ex);
            select_args.push_back(eh_personality);
            for (unsigned ii = 0, ie = catch_clause->n_clauses(); ii != ie; ++ii)
              select_args.push_back(build_value(catch_clause->clause(ii)));

            llvm::Value *ex_sel = irbuilder().CreateCall(m_module_builder->llvm_eh_selector(), select_args);
            m_value_terms[catch_clause] = ex_sel;
            
            // Get the value associated to each entry in the catch clause
            for (unsigned ii = 0, ie = catch_clause->n_clauses(); ii != ie; ++ii) {
              llvm::Value *catch_id = irbuilder().CreateCall(m_module_builder->llvm_eh_typeid_for(), select_args[ii+2]);
              m_value_terms[FunctionalBuilder::catch_(catch_clause, ii)] = catch_id;
            }
#endif
          }

          // Build instructions!
          for (Block::InstructionList::const_iterator jt = it->first->instructions().begin(), je = it->first->instructions().end(); jt != je; ++jt) {
            const ValuePtr<Instruction>& insn = *jt;
            llvm::Value *r = build_value_instruction(insn);
            m_value_terms.insert(std::make_pair(insn, r));
          }

          if (!it->second->getTerminator())
            throw BuildError("LLVM block was not terminated during function building");
        }

        // Set up LLVM phi node incoming edges
        for (boost::unordered_map<ValuePtr<Phi>, llvm::PHINode*>::iterator it = phi_node_map.begin(), ie = phi_node_map.end(); it != ie; ++it) {
          for (std::vector<PhiEdge>::const_iterator ji = it->first->edges().begin(), je = it->first->edges().end(); ji != je; ++ji) {
            const PhiEdge& edge = *ji;
            PSI_ASSERT(m_value_terms.find(edge.block) != m_value_terms.end());
            llvm::BasicBlock *incoming_block =
              llvm::cast<llvm::BasicBlock>(m_value_terms.find(edge.block)->second);
            PSI_ASSERT(incoming_block);
            llvm::Value* incoming_value = build_value(edge.value);
            it->second->addIncoming(incoming_value, incoming_block);
          }
        }
        
        setup_stack_save_restore(blocks);
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
      llvm::StringRef FunctionBuilder::term_name(const ValuePtr<>& term) {
        const Function::TermNameMap& map = m_function->term_name_map();
        Function::TermNameMap::const_iterator it = map.find(term);

        if (it != map.end()) {
          return llvm::StringRef(it->second);
        } else {
          return llvm::StringRef();
        }
      }
    }
  }
}
