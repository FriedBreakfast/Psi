#include "Builder.hpp"

#include "../Aggregate.hpp"
#include "../Recursive.hpp"
#include "../FunctionalBuilder.hpp"

#include <boost/next_prior.hpp>
#include <boost/unordered_set.hpp>

#include "LLVMPushWarnings.hpp"
#include <llvm/Function.h>
#include <llvm/Module.h>
#include "LLVMPopWarnings.hpp"

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      FunctionBuilder::FunctionBuilder(ModuleBuilder *global_builder,
                                       const ValuePtr<Function>& function,
                                       llvm::Function *llvm_function)
        : m_module_builder(global_builder),
          m_irbuilder(global_builder->llvm_context(), llvm::TargetFolder(global_builder->llvm_target_machine()->getDataLayout())),
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
        if (isa<Global>(term))
          return module_builder()->build_constant(term);

        switch (term->term_type()) {
        case term_function_parameter:
        case term_instruction:
        case term_phi:
        case term_block: {
          llvm::Value *const* value = m_value_terms.lookup(term);
          PSI_ASSERT(value);
          return *value;
        }

        case term_apply:
        case term_functional: {
          if (llvm::Value *const* lookup = m_value_terms.lookup(term)) {
            if (!*lookup)
              throw BuildError("Circular term found");
            return *lookup;
          }
          m_value_terms.put(term, NULL);
          
          llvm::Value* result;
          switch(term->term_type()) {
          case term_functional: {
            result = build_value_functional(value_cast<FunctionalValue>(term));
            break;
          }

          case term_apply: {
            ValuePtr<> actual = value_cast<ApplyType>(term)->unpack();
            PSI_ASSERT(actual->term_type() != term_apply);
            result = build_value(actual);
            break;
          }

          default:
            PSI_FAIL("unexpected term type");
          }

          llvm::Instruction *value_insn = llvm::dyn_cast<llvm::Instruction>(result);
          if (value_insn && !value_insn->hasName() && !value_insn->getType()->isVoidTy())
            value_insn->setName(term_name(term));
          
          m_value_terms.put(term, result);

          return result;
        }

        default:
          PSI_FAIL("unexpected term type");
        }
      }

      void FunctionBuilder::switch_to_block(const ValuePtr<Block>& block) {
        m_block_value_terms[m_current_block] = m_value_terms;
        BlockMapType::const_iterator new_block = m_block_value_terms.find(block);
        PSI_ASSERT(new_block != m_block_value_terms.end());
        m_current_block = block;
        m_value_terms = new_block->second;
      }
      
      void FunctionBuilder::run() {
        PSI_ASSERT(!m_function->blocks().empty());
        
        // Set up parameters
        {
          llvm::Function::ArgumentListType::iterator ii = m_llvm_function->getArgumentList().begin(), ie = m_llvm_function->getArgumentList().end();
          if (m_function->function_type()->sret()) {
            ValuePtr<FunctionParameter> param = m_function->parameters().back();
            llvm::Argument *value = &*ii;
            value->setName(term_name(param));
            value->addAttr(llvm::Attributes::get(module_builder()->llvm_context(), llvm::Attributes::StructRet));
            m_value_terms.insert(std::make_pair(param, value));
            ++ii;
          }
          
          for (std::size_t in = 0; ii != ie; ++ii, ++in) {
            ValuePtr<FunctionParameter> param = m_function->parameters().at(in);
            llvm::Value *value = &*ii;
            value->setName(term_name(param));
            m_value_terms.insert(std::make_pair(param, value));
          }
        }
        
        // create llvm blocks
        std::vector<std::pair<ValuePtr<Block>, llvm::BasicBlock*> > blocks;
        for (Function::BlockList::const_iterator it = m_function->blocks().begin(), ie = m_function->blocks().end(); it != ie; ++it) {
          llvm::BasicBlock *llvm_bb = llvm::BasicBlock::Create(module_builder()->llvm_context(), term_name(*it), m_llvm_function);
          PSI_CHECK(!m_value_terms.put(*it, llvm_bb));
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
          switch_to_block(it->first->dominator());
          m_current_block = it->first;
        
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
            if (r)
              m_value_terms.insert(std::make_pair(insn, r));
          }

          if (!it->second->getTerminator())
            throw BuildError("LLVM block was not terminated during function building");
        }

        // Set up LLVM phi node incoming edges
        for (boost::unordered_map<ValuePtr<Phi>, llvm::PHINode*>::iterator it = phi_node_map.begin(), ie = phi_node_map.end(); it != ie; ++it) {
          for (std::vector<PhiEdge>::const_iterator ji = it->first->edges().begin(), je = it->first->edges().end(); ji != je; ++ji) {
            const PhiEdge& edge = *ji;
            PSI_ASSERT(m_value_terms.lookup(edge.block));
            llvm::BasicBlock *incoming_block = llvm::cast<llvm::BasicBlock>(*m_value_terms.lookup(edge.block));
            PSI_ASSERT(incoming_block);
            switch_to_block(edge.block);
            llvm::Value* incoming_value = build_value(edge.value);
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
