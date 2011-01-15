#include "Utility.hpp"
#include "Function.hpp"

namespace Psi {
  namespace Tvm {
    namespace {
      Term* common_source_fail() {
        throw TvmUserError("cannot find common term source");
      }
      
      Term* common_source_function_function(FunctionTerm *f1, FunctionTerm *f2) {
        if (f1 == f2)
          return f1;
        else
          return common_source_fail();
      }
      
      Term* common_source_function_block(FunctionTerm *f, BlockTerm *b) {
        if (f == b->function())
          return f;
        else
          return common_source_fail();
      }

      Term* common_source_function_instruction(FunctionTerm *f, InstructionTerm *i) {
        if (f == i->block()->function())
          return i;
        else
          return common_source_fail();
      }

      Term* common_source_block_block(BlockTerm *b1, BlockTerm *b2) {
        if (b1->dominated_by(b2))
          return b1;
        else if (b2->dominated_by(b1))
          return b2;
        else
          return common_source_fail();
      }
      
      Term* common_source_block_instruction(BlockTerm *b, InstructionTerm *i) {
        if (i->block()->dominated_by(b))
          return i;
        else if (b->dominated_by(i->block()))
          return b;
        else
          return common_source_fail();
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
    }
    
    /**
     * Find the common dominator of the two blocks. If no such block
     * exists, throw an exception.
     */
    Term* common_source(Term *t1, Term *t2) {
      if (t1 && t2) {
        switch (t1->term_type()) {
        case term_function:
          switch (t2->term_type()) {
          case term_function: return common_source_function_function(cast<FunctionTerm>(t1), cast<FunctionTerm>(t2));
          case term_block: return common_source_function_block(cast<FunctionTerm>(t1), cast<BlockTerm>(t2));
          case term_instruction: return common_source_function_instruction(cast<FunctionTerm>(t1), cast<InstructionTerm>(t2));
          case term_function_parameter: return t2;
          case term_function_type_parameter: return t2;
          default: PSI_FAIL("unexpected term type");
          }

        case term_block:
          switch (t2->term_type()) {
          case term_function: return common_source_function_block(cast<FunctionTerm>(t2), cast<BlockTerm>(t1));
          case term_block: return common_source_block_block(cast<BlockTerm>(t1), cast<BlockTerm>(t2));
          case term_instruction: return common_source_block_instruction(cast<BlockTerm>(t1), cast<InstructionTerm>(t2));
          case term_function_parameter: return t2;
          case term_function_type_parameter: return t2;
          default: PSI_FAIL("unexpected term type");
          }

        case term_instruction:
          switch (t2->term_type()) {
          case term_function: return common_source_function_instruction(cast<FunctionTerm>(t2), cast<InstructionTerm>(t1));
          case term_block: return common_source_block_instruction(cast<BlockTerm>(t2), cast<InstructionTerm>(t1));
          case term_instruction: return common_source_instruction_instruction(cast<InstructionTerm>(t1), cast<InstructionTerm>(t2));
          case term_function_parameter: return t2;
          default: PSI_FAIL("unexpected term type");
          }
          
        case term_function_type_parameter:
          return t1;
          
        case term_function_parameter:
          switch (t2->term_type()) {
          default: return t1;
          case term_function_type_parameter: return t2;          
          case term_function_parameter: {
            if (cast<FunctionParameterTerm>(t1)->function() != cast<FunctionParameterTerm>(t2)->function())
              return common_source_fail();
            return t1;
          }
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
     */
    bool source_dominated(Term *dominator, Term *dominated) {
      if (dominator && dominated) {
        switch (dominator->term_type()) {
        case term_function:
          switch (dominated->term_type()) {
          default: return false;
          case term_function: return dominator == dominated;
          case term_block: return dominator == cast<BlockTerm>(dominated)->function();
          case term_instruction: return dominator == cast<InstructionTerm>(dominated)->block()->function();
          }

        case term_block:
          switch (dominated->term_type()) {
          default: return false;
          case term_block: return cast<BlockTerm>(dominated)->dominated_by(cast<BlockTerm>(dominator));
          case term_instruction: return cast<InstructionTerm>(dominated)->block()->dominated_by(cast<BlockTerm>(dominator));
          }
          
        case term_instruction:
          switch (dominated->term_type()) {
          default: return false;
          case term_block: return cast<BlockTerm>(dominated)->dominated_by(cast<InstructionTerm>(dominator)->block());
          case term_instruction: {
            InstructionTerm *dominator_insn = cast<InstructionTerm>(dominator);
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
          }
          
        case term_function_type_parameter:
          return true;
          
        case term_function_parameter:
          switch (dominated->term_type()) {
          default: return true;
          case term_function_type_parameter: return false;
          case term_function_parameter: return cast<FunctionParameterTerm>(dominator)->function() == cast<FunctionParameterTerm>(dominated)->function();
          }

        default:
          PSI_FAIL("unexpected term type");
        }
      } else {
        return dominated || !dominator;
      }
    }
  }
}


