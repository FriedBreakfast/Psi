#include "Utility.hpp"
#include "Function.hpp"

namespace Psi {
  namespace Tvm {
    /**
     * \brief Return true if the value of this term is not known
     * 
     * What this means is somewhat type specific, for instance a
     * pointer type to phantom type is not considered phantom.
     */
    bool Term::phantom() const {
      if (FunctionParameterTerm *parameter = dyn_cast<FunctionParameterTerm>(source()))
        return parameter->parameter_phantom();
      return false;
    }
    
    /// \brief Whether this is part of a function type (i.e. it contains function type parameters)
    bool Term::parameterized() const {
      return source() && isa<FunctionTypeParameterTerm>(source());
    }

    namespace {
      Term* common_source_fail() {
        throw TvmUserError("cannot find common term source");
      }
      
      Term* common_source_global_global(GlobalTerm *g1, GlobalTerm *g2) {
        if (g1->module() == g2->module())
          return g1;
        else
          return common_source_fail();
      }

      Term* common_source_global_block(GlobalTerm *g, BlockTerm *b) {
        if (g->module() == b->function()->module())
          return b;
        else
          return common_source_fail();
      }
      
      Term* common_source_global_phi(GlobalTerm *g, PhiTerm *p) {
        if (g->module() == p->block()->function()->module())
          return p;
        else
          return common_source_fail();
      }
      
      Term* common_source_global_instruction(GlobalTerm* g, InstructionTerm *i) {
        if (g->module() == i->block()->function()->module())
          return i;
        else
          return common_source_fail();
      }
      
      Term* common_source_global_parameter(GlobalTerm *g, FunctionParameterTerm *p) {
        if (g->module() == p->function()->module())
          return p;
        else
          return common_source_fail();
      }
      
      Term* common_source_global_type_parameter(GlobalTerm*, FunctionTypeParameterTerm *p) {
        return p;
      }
      
      Term* common_source_block_block(BlockTerm *b1, BlockTerm *b2) {
        if (b1->function() == b2->function())
          return b1;
        else
          return common_source_fail();
      }
      
      Term* common_source_block_phi(BlockTerm *b, PhiTerm *p) {
        if (p->block()->function() == b->function())
          return p;
        else
          return common_source_fail();
      }
      
      Term* common_source_block_instruction(BlockTerm *b, InstructionTerm *i) {
        if (b->function() == i->block()->function())
          return i;
        else
          return common_source_fail();
      }
      
      Term* common_source_block_parameter(BlockTerm *b, FunctionParameterTerm *p) {
        if (b->function() == p->function())
          return p;
        else
          return common_source_fail();
      }

      Term* common_source_block_type_parameter(BlockTerm*, FunctionTypeParameterTerm *p) {
        return p;
      }
      
      Term *common_source_phi_phi(PhiTerm *p1, PhiTerm *p2) {
        BlockTerm *b1 = p1->block(), *b2 = p2->block();
        if (b1->dominated_by(b2))
          return p1;
        else if (b2->dominated_by(b1))
          return p2;
        else
          return common_source_fail();
      }
      
      Term *common_source_phi_instruction(PhiTerm *p, InstructionTerm *i) {
        BlockTerm *b = p->block();
        if (i->block()->dominated_by(b))
          return i;
        else if (b->dominated_by(i->block()))
          return p;
        else
          return common_source_fail();
      }
      
      Term *common_source_phi_parameter(PhiTerm *p, FunctionParameterTerm *pa) {
        if (p->block()->function() == pa->function())
          return pa->phantom() ? static_cast<Term*>(pa) : p;
        else
          return common_source_fail();
      }
      
      Term *common_source_phi_type_parameter(PhiTerm*, FunctionTypeParameterTerm *pa) {
        return pa;
      }

      Term* common_source_instruction_instruction(InstructionTerm *i1, InstructionTerm *i2) {
        BlockTerm *b1 = i1->block(), *b2 = i2->block();
        if (b1 == b2) {
          BlockTerm::InstructionList& insn_list = b1->instructions();
          for (BlockTerm::InstructionList::iterator it = insn_list.iterator_to(*i1); it != insn_list.end(); ++it) {
            if (&*it == i2)
              return i2;
          }
          return i1;
        } else if (b1->dominated_by(b2))
          return i1;
        else if (b2->dominated_by(b1))
          return i2;
        else
          return common_source_fail();
      }

      Term* common_source_instruction_parameter(InstructionTerm *i, FunctionParameterTerm *p) {
        if (i->block()->function() == p->function())
          return p->phantom() ? static_cast<Term*>(p) : i;
        else
          return common_source_fail();
      }

      Term* common_source_instruction_type_parameter(InstructionTerm*, FunctionTypeParameterTerm *p) {
        return p;
      }
      
      Term* common_source_parameter_parameter(FunctionParameterTerm *p1, FunctionParameterTerm *p2) {
        if (p1->function() != p2->function())
          return common_source_fail();
        
        return p1->phantom() ? p1 : p2;
      }
      
      Term* common_source_parameter_type_parameter(FunctionParameterTerm*, FunctionTypeParameterTerm *p) {
        return p;
      }
      
      Term* common_source_type_parameter_type_parameter(FunctionTypeParameterTerm *p, FunctionTypeParameterTerm*) {
        return p;
      }
    }
    
    /**
     * Find the common source term of two terms. If no such source exists,
     * throw an exception.
     */
    Term* common_source(Term *t1, Term *t2) {
      if (t1 && t2) {
        switch (t1->term_type()) {
        case term_global_variable:
        case term_function:
          switch (t2->term_type()) {
            case term_global_variable:
            case term_function: return common_source_global_global(cast<GlobalTerm>(t1), cast<GlobalTerm>(t2));
            case term_block: return common_source_global_block(cast<GlobalTerm>(t1), cast<BlockTerm>(t2));
            case term_phi: return common_source_global_phi(cast<GlobalTerm>(t1), cast<PhiTerm>(t2));
            case term_instruction: return common_source_global_instruction(cast<GlobalTerm>(t1), cast<InstructionTerm>(t2));
            case term_function_parameter: return common_source_global_parameter(cast<GlobalTerm>(t1), cast<FunctionParameterTerm>(t2));
            case term_function_type_parameter: return common_source_global_type_parameter(cast<GlobalTerm>(t1), cast<FunctionTypeParameterTerm>(t2));
            default: PSI_FAIL("unexpected term type");
          }

        case term_block:
          switch (t2->term_type()) {
          case term_global_variable:
          case term_function: return common_source_global_block(cast<GlobalTerm>(t2), cast<BlockTerm>(t1));
          case term_block: return common_source_block_block(cast<BlockTerm>(t1), cast<BlockTerm>(t2));
          case term_phi: return common_source_block_phi(cast<BlockTerm>(t1), cast<PhiTerm>(t2));
          case term_instruction: return common_source_block_instruction(cast<BlockTerm>(t1), cast<InstructionTerm>(t2));
          case term_function_parameter: return common_source_block_parameter(cast<BlockTerm>(t1), cast<FunctionParameterTerm>(t2));
          case term_function_type_parameter: return common_source_block_type_parameter(cast<BlockTerm>(t1), cast<FunctionTypeParameterTerm>(t2));
          default: PSI_FAIL("unexpected term type");
          }
          
        case term_phi:
          switch (t2->term_type()) {
          case term_global_variable:
          case term_function: return common_source_global_phi(cast<GlobalTerm>(t2), cast<PhiTerm>(t1));
          case term_block: return common_source_block_phi(cast<BlockTerm>(t2), cast<PhiTerm>(t1));
          case term_phi: return common_source_phi_phi(cast<PhiTerm>(t1), cast<PhiTerm>(t2));
          case term_instruction: return common_source_phi_instruction(cast<PhiTerm>(t1), cast<InstructionTerm>(t2));
          case term_function_parameter: return common_source_phi_parameter(cast<PhiTerm>(t1), cast<FunctionParameterTerm>(t2));
          case term_function_type_parameter: return common_source_phi_type_parameter(cast<PhiTerm>(t1), cast<FunctionTypeParameterTerm>(t2));
          default: PSI_FAIL("unexpected term type");
          }

        case term_instruction:
          switch (t2->term_type()) {
          case term_global_variable:
          case term_function: return common_source_global_instruction(cast<GlobalTerm>(t2), cast<InstructionTerm>(t1));
          case term_block: return common_source_block_instruction(cast<BlockTerm>(t2), cast<InstructionTerm>(t1));
          case term_phi: return common_source_phi_instruction(cast<PhiTerm>(t2), cast<InstructionTerm>(t1));
          case term_instruction: return common_source_instruction_instruction(cast<InstructionTerm>(t1), cast<InstructionTerm>(t2));
          case term_function_parameter: return common_source_instruction_parameter(cast<InstructionTerm>(t1), cast<FunctionParameterTerm>(t2));
          case term_function_type_parameter: return common_source_instruction_type_parameter(cast<InstructionTerm>(t1), cast<FunctionTypeParameterTerm>(t2));
          default: PSI_FAIL("unexpected term type");
          }
          
        case term_function_parameter:
          switch (t2->term_type()) {
          case term_global_variable:
          case term_function: return common_source_global_parameter(cast<GlobalTerm>(t2), cast<FunctionParameterTerm>(t1));
          case term_block: return common_source_block_parameter(cast<BlockTerm>(t2), cast<FunctionParameterTerm>(t1));
          case term_phi: return common_source_phi_parameter(cast<PhiTerm>(t2), cast<FunctionParameterTerm>(t1));
          case term_instruction: return common_source_instruction_parameter(cast<InstructionTerm>(t1), cast<FunctionParameterTerm>(t2));
          case term_function_parameter: return common_source_parameter_parameter(cast<FunctionParameterTerm>(t1), cast<FunctionParameterTerm>(t2));
          case term_function_type_parameter: return common_source_parameter_type_parameter(cast<FunctionParameterTerm>(t1), cast<FunctionTypeParameterTerm>(t2));
          default: PSI_FAIL("unexpected term type");
          }
          
        case term_function_type_parameter:
          switch (t2->term_type()) {
          case term_global_variable:
          case term_function: return common_source_global_type_parameter(cast<GlobalTerm>(t2), cast<FunctionTypeParameterTerm>(t1));
          case term_block: return common_source_block_type_parameter(cast<BlockTerm>(t2), cast<FunctionTypeParameterTerm>(t1));
          case term_phi: return common_source_phi_type_parameter(cast<PhiTerm>(t2), cast<FunctionTypeParameterTerm>(t1));
          case term_instruction: return common_source_instruction_type_parameter(cast<InstructionTerm>(t1), cast<FunctionTypeParameterTerm>(t2));
          case term_function_parameter: return common_source_parameter_type_parameter(cast<FunctionParameterTerm>(t1), cast<FunctionTypeParameterTerm>(t2));
          case term_function_type_parameter: return common_source_type_parameter_type_parameter(cast<FunctionTypeParameterTerm>(t1), cast<FunctionTypeParameterTerm>(t2));
          default: PSI_FAIL("unexpected term type");
          }

        default:
          PSI_FAIL("unexpected term type");
        }
      } else {
        return t1 ? t1 : t2;
      }
    }

    /**
     * Check whether a source term is dominated by another.
     * 
     * This effectively tests whether:
     *
     * \code common_source(dominator,dominated) == dominated \endcode
     * 
     * (Including whether that expression would throw).
     * However since common_source is not entirely symmetric this handles
     * the cases where common_source could return either correctly.
     */
    bool source_dominated(Term *dominator, Term *dominated) {
      if (dominator && dominated) {
        if (dominated->term_type() == term_function_type_parameter)
          return true;
        
        switch (dominator->term_type()) {
        case term_global_variable:
        case term_function: {
          Module *module = cast<GlobalTerm>(dominator)->module();
          switch (dominated->term_type()) {
          default: return false;
          case term_global_variable:
          case term_function: return module == cast<GlobalTerm>(dominated)->module();
          case term_block: return module == cast<BlockTerm>(dominated)->function()->module();
          case term_phi: return module == cast<PhiTerm>(dominated)->block()->function()->module();
          case term_instruction: return module == cast<InstructionTerm>(dominated)->block()->function()->module();
          case term_function_parameter: return module == cast<FunctionParameterTerm>(dominated)->function()->module();
          }
        }

        case term_function_parameter: {
          FunctionParameterTerm *parameter = cast<FunctionParameterTerm>(dominator);
          if (parameter->phantom()) {
            FunctionParameterTerm *parameter2 = dyn_cast<FunctionParameterTerm>(dominated);
            return parameter2 && parameter2->phantom() && (parameter->function() == parameter2->function());
          }
          FunctionTerm *function = parameter->function();
          switch (dominated->term_type()) {
          default: return false;
          case term_block: return function == cast<BlockTerm>(dominated)->function();
          case term_phi: return function == cast<PhiTerm>(dominated)->block()->function();
          case term_instruction: return function == cast<InstructionTerm>(dominated)->block()->function();
          case term_function_parameter: return function == cast<FunctionParameterTerm>(dominated)->function();
          }
        }

        case term_block: {
          FunctionTerm *function = cast<BlockTerm>(dominator)->function();
          switch (dominated->term_type()) {
          default: return false;
          case term_block: return function == cast<BlockTerm>(dominated)->function();//dominated_by(block);
          case term_phi: return function == cast<PhiTerm>(dominated)->block()->function();
          case term_instruction: return function == cast<InstructionTerm>(dominated)->block()->function();
          case term_function_parameter: return cast<FunctionParameterTerm>(dominated)->phantom() &&
            (cast<FunctionParameterTerm>(dominated)->function() == function);
          }
        }
        
        case term_phi: {
          BlockTerm *block = cast<PhiTerm>(dominator)->block();
          switch (dominated->term_type()) {
          default: return false;
          case term_block: return block->function() == cast<BlockTerm>(dominated)->function();
          case term_phi: return cast<PhiTerm>(dominated)->block()->dominated_by(block);
          case term_instruction: return cast<InstructionTerm>(dominated)->block()->dominated_by(block);
          case term_function_parameter: return cast<FunctionParameterTerm>(dominated)->phantom() &&
            (cast<FunctionParameterTerm>(dominated)->function() == block->function());
          }
        }
          
        case term_instruction: {
          InstructionTerm *dominator_insn = cast<InstructionTerm>(dominator);
          switch (dominated->term_type()) {
          default: return false;
          case term_phi: {
            PhiTerm *dominated_phi = cast<PhiTerm>(dominated);
            if (dominated_phi->block() == dominator_insn->block())
              return false;
            else
              return dominated_phi->block()->dominated_by(dominator_insn->block());
          }
          case term_instruction: {
            InstructionTerm *dominated_insn = cast<InstructionTerm>(dominated);
            if (dominator_insn->block() == dominated_insn->block()) {
              BlockTerm *block = dominator_insn->block();
              BlockTerm::InstructionList::iterator dominator_it = block->instructions().iterator_to(*dominator_insn);
              BlockTerm::InstructionList::iterator dominated_it = block->instructions().iterator_to(*dominated_insn);
              for (; dominator_it != block->instructions().end(); ++dominator_it) {
                if (dominator_it == dominated_it)
                  return true;
              }
              return false;
            } else {
              return dominated_insn->block()->dominated_by(dominator_insn->block());
            }
          }
          case term_function_parameter: return cast<FunctionParameterTerm>(dominated)->phantom() &&
            (cast<FunctionParameterTerm>(dominated)->function() == dominator_insn->block()->function());
          }
        }
          
        case term_function_type_parameter:
          return true;

        default:
          PSI_FAIL("unexpected term type");
        }
      } else {
        return dominated || !dominator;
      }
    }
  }
}


