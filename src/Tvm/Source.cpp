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
    bool Value::phantom() const {
      if (FunctionParameter *parameter = dyn_cast<FunctionParameter>(source()))
        return parameter->parameter_phantom();
      return false;
    }
    
    /// \brief Whether this is part of a function type (i.e. it contains function type parameters)
    bool Value::parameterized() const {
      return source() && isa<FunctionTypeParameter>(source());
    }

    namespace {
      Value* common_source_fail() {
        throw TvmUserError("cannot find common term source");
      }
      
      Value* common_source_global_global(Global *g1, Global *g2) {
        if (g1->module() == g2->module())
          return g1;
        else
          return common_source_fail();
      }

      Value* common_source_global_block(Global *g, Block *b) {
        if (g->module() == b->function()->module())
          return b;
        else
          return common_source_fail();
      }

      Value* common_source_global_phi(Global *g, BlockMember *p) {
        if (g->module() == p->block()->function()->module())
          return p;
        else
          return common_source_fail();
      }
      
      Value* common_source_global_instruction(Global* g, Instruction *i) {
        if (g->module() == i->block()->function()->module())
          return i;
        else
          return common_source_fail();
      }
      
      Value* common_source_global_parameter(Global *g, FunctionParameter *p) {
        if (g->module() == p->function()->module())
          return p;
        else
          return common_source_fail();
      }
      
      Value* common_source_global_type_parameter(Global*, FunctionTypeParameter *p) {
        return p;
      }
      
      Value* common_source_block_block(Block *b1, Block *b2) {
        if (b1->function() == b2->function())
          return b1;
        else
          return common_source_fail();
      }

      Value* common_source_block_phi(Block *b, BlockMember *p) {
        if (p->block()->function() == b->function())
          return p;
        else
          return common_source_fail();
      }
      
      Value* common_source_block_instruction(Block *b, Instruction *i) {
        if (b->function() == i->block()->function())
          return i;
        else
          return common_source_fail();
      }
      
      Value* common_source_block_parameter(Block *b, FunctionParameter *p) {
        if (b->function() == p->function())
          return p;
        else
          return common_source_fail();
      }

      Value* common_source_block_type_parameter(Block*, FunctionTypeParameter *p) {
        return p;
      }

      Value *common_source_phi_phi(BlockMember *p1, BlockMember *p2) {
        const ValuePtr<Block>& b1 = p1->block(), b2 = p2->block();
        if (b1->dominated_by(b2))
          return p1;
        else if (b2->dominated_by(b1))
          return p2;
        else
          return common_source_fail();
      }
      
      Value *common_source_phi_instruction(BlockMember *p, Instruction *i) {
        const ValuePtr<Block>& b = p->block();
        if (i->block()->dominated_by(b))
          return i;
        else if (b->dominated_by(i->block()))
          return p;
        else
          return common_source_fail();
      }
      
      Value *common_source_phi_parameter(BlockMember *p, FunctionParameter *pa) {
        if (p->block()->function() == pa->function())
          return pa->phantom() ? static_cast<Value*>(pa) : p;
        else
          return common_source_fail();
      }
      
      Value *common_source_phi_type_parameter(BlockMember*, FunctionTypeParameter *pa) {
        return pa;
      }

      Value* common_source_instruction_instruction(Instruction *i1, Instruction *i2) {
        const ValuePtr<Block>& b1 = i1->block(), b2 = i2->block();
        if (b1 == b2) {
          const Block::InstructionList& insn_list = b1->instructions();
          for (Block::InstructionList::const_iterator ii = insn_list.begin(), ie = insn_list.end(); ii != ie; ++ii) {
            if (i1 == ii->get())
              return i2;
            else if (i2 == ii->get())
              return i1;
          }
          PSI_FAIL("Unreachable");
        } else if (b1->dominated_by(b2))
          return i1;
        else if (b2->dominated_by(b1))
          return i2;
        else
          return common_source_fail();
      }

      Value* common_source_instruction_parameter(Instruction *i, FunctionParameter *p) {
        if (i->block()->function() == p->function())
          return p->phantom() ? static_cast<Value*>(p) : i;
        else
          return common_source_fail();
      }

      Value* common_source_instruction_type_parameter(Instruction*, FunctionTypeParameter *p) {
        return p;
      }
      
      Value* common_source_parameter_parameter(FunctionParameter *p1, FunctionParameter *p2) {
        if (p1->function() != p2->function())
          return common_source_fail();
        
        return p1->phantom() ? p1 : p2;
      }
      
      Value* common_source_parameter_type_parameter(FunctionParameter*, FunctionTypeParameter *p) {
        return p;
      }
      
      Value* common_source_type_parameter_type_parameter(FunctionTypeParameter *p, FunctionTypeParameter*) {
        return p;
      }
    }
    
    /**
     * Find the common source term of two terms. If no such source exists,
     * throw an exception.
     */
    Value* common_source(Value *t1, Value *t2) {
      if (t1 && t2) {
        switch (t1->term_type()) {
        case term_global_variable:
        case term_function:
          switch (t2->term_type()) {
            case term_global_variable:
            case term_function: return common_source_global_global(value_cast<Global>(t1), value_cast<Global>(t2));
            case term_block: return common_source_global_block(value_cast<Global>(t1), value_cast<Block>(t2));
            case term_catch_clause:
            case term_phi: return common_source_global_phi(value_cast<Global>(t1), value_cast<BlockMember>(t2));
            case term_instruction: return common_source_global_instruction(value_cast<Global>(t1), value_cast<Instruction>(t2));
            case term_function_parameter: return common_source_global_parameter(value_cast<Global>(t1), value_cast<FunctionParameter>(t2));
            case term_function_type_parameter: return common_source_global_type_parameter(value_cast<Global>(t1), value_cast<FunctionTypeParameter>(t2));
            default: PSI_FAIL("unexpected term type");
          }

        case term_block:
          switch (t2->term_type()) {
          case term_global_variable:
          case term_function: return common_source_global_block(value_cast<Global>(t2), value_cast<Block>(t1));
          case term_block: return common_source_block_block(value_cast<Block>(t1), value_cast<Block>(t2));
          case term_catch_clause:
          case term_phi: return common_source_block_phi(value_cast<Block>(t1), value_cast<BlockMember>(t2));
          case term_instruction: return common_source_block_instruction(value_cast<Block>(t1), value_cast<Instruction>(t2));
          case term_function_parameter: return common_source_block_parameter(value_cast<Block>(t1), value_cast<FunctionParameter>(t2));
          case term_function_type_parameter: return common_source_block_type_parameter(value_cast<Block>(t1), value_cast<FunctionTypeParameter>(t2));
          default: PSI_FAIL("unexpected term type");
          }

        case term_catch_clause:
        case term_phi:
          switch (t2->term_type()) {
          case term_global_variable:
          case term_function: return common_source_global_phi(value_cast<Global>(t2), value_cast<BlockMember>(t1));
          case term_block: return common_source_block_phi(value_cast<Block>(t2), value_cast<BlockMember>(t1));
          case term_catch_clause:
          case term_phi: return common_source_phi_phi(value_cast<BlockMember>(t1), value_cast<BlockMember>(t2));
          case term_instruction: return common_source_phi_instruction(value_cast<BlockMember>(t1), value_cast<Instruction>(t2));
          case term_function_parameter: return common_source_phi_parameter(value_cast<BlockMember>(t1), value_cast<FunctionParameter>(t2));
          case term_function_type_parameter: return common_source_phi_type_parameter(value_cast<BlockMember>(t1), value_cast<FunctionTypeParameter>(t2));
          default: PSI_FAIL("unexpected term type");
          }

        case term_instruction:
          switch (t2->term_type()) {
          case term_global_variable:
          case term_function: return common_source_global_instruction(value_cast<Global>(t2), value_cast<Instruction>(t1));
          case term_block: return common_source_block_instruction(value_cast<Block>(t2), value_cast<Instruction>(t1));
          case term_catch_clause:
          case term_phi: return common_source_phi_instruction(value_cast<Phi>(t2), value_cast<Instruction>(t1));
          case term_instruction: return common_source_instruction_instruction(value_cast<Instruction>(t1), value_cast<Instruction>(t2));
          case term_function_parameter: return common_source_instruction_parameter(value_cast<Instruction>(t1), value_cast<FunctionParameter>(t2));
          case term_function_type_parameter: return common_source_instruction_type_parameter(value_cast<Instruction>(t1), value_cast<FunctionTypeParameter>(t2));
          default: PSI_FAIL("unexpected term type");
          }
          
        case term_function_parameter:
          switch (t2->term_type()) {
          case term_global_variable:
          case term_function: return common_source_global_parameter(value_cast<Global>(t2), value_cast<FunctionParameter>(t1));
          case term_block: return common_source_block_parameter(value_cast<Block>(t2), value_cast<FunctionParameter>(t1));
          case term_catch_clause:
          case term_phi: return common_source_phi_parameter(value_cast<BlockMember>(t2), value_cast<FunctionParameter>(t1));
          case term_instruction: return common_source_instruction_parameter(value_cast<Instruction>(t1), value_cast<FunctionParameter>(t2));
          case term_function_parameter: return common_source_parameter_parameter(value_cast<FunctionParameter>(t1), value_cast<FunctionParameter>(t2));
          case term_function_type_parameter: return common_source_parameter_type_parameter(value_cast<FunctionParameter>(t1), value_cast<FunctionTypeParameter>(t2));
          default: PSI_FAIL("unexpected term type");
          }
          
        case term_function_type_parameter:
          switch (t2->term_type()) {
          case term_global_variable:
          case term_function: return common_source_global_type_parameter(value_cast<Global>(t2), value_cast<FunctionTypeParameter>(t1));
          case term_block: return common_source_block_type_parameter(value_cast<Block>(t2), value_cast<FunctionTypeParameter>(t1));
          case term_catch_clause:
          case term_phi: return common_source_phi_type_parameter(value_cast<BlockMember>(t2), value_cast<FunctionTypeParameter>(t1));
          case term_instruction: return common_source_instruction_type_parameter(value_cast<Instruction>(t1), value_cast<FunctionTypeParameter>(t2));
          case term_function_parameter: return common_source_parameter_type_parameter(value_cast<FunctionParameter>(t1), value_cast<FunctionTypeParameter>(t2));
          case term_function_type_parameter: return common_source_type_parameter_type_parameter(value_cast<FunctionTypeParameter>(t1), value_cast<FunctionTypeParameter>(t2));
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
    bool source_dominated(Value *dominator, Value *dominated) {
      if (dominator && dominated) {
        if (dominated->term_type() == term_function_type_parameter)
          return true;
        
        switch (dominator->term_type()) {
        case term_global_variable:
        case term_function: {
          Module *module = value_cast<Global>(dominator)->module();
          switch (dominated->term_type()) {
          default: return false;
          case term_global_variable:
          case term_function: return module == value_cast<Global>(dominated)->module();
          case term_block: return module == value_cast<Block>(dominated)->function()->module();
          case term_catch_clause:
          case term_phi: return module == value_cast<BlockMember>(dominated)->block()->function()->module();
          case term_instruction: return module == value_cast<Instruction>(dominated)->block()->function()->module();
          case term_function_parameter: return module == value_cast<FunctionParameter>(dominated)->function()->module();
          }
        }

        case term_function_parameter: {
          FunctionParameter *parameter = value_cast<FunctionParameter>(dominator);
          if (parameter->phantom()) {
            FunctionParameter *parameter2 = dyn_cast<FunctionParameter>(dominated);
            return parameter2 && parameter2->phantom() && (parameter->function() == parameter2->function());
          }
          Function *function = parameter->function().get();
          switch (dominated->term_type()) {
          default: return false;
          case term_block: return function == value_cast<Block>(dominated)->function();
          case term_catch_clause:
          case term_phi: return function == value_cast<BlockMember>(dominated)->block()->function();
          case term_instruction: return function == value_cast<Instruction>(dominated)->block()->function();
          case term_function_parameter: return function == value_cast<FunctionParameter>(dominated)->function();
          }
        }

        case term_block: {
          const ValuePtr<Function>& function = value_cast<Block>(dominator)->function();
          switch (dominated->term_type()) {
          default: return false;
          case term_block: return function == value_cast<Block>(dominated)->function();
          case term_catch_clause:
          case term_phi: return function == value_cast<BlockMember>(dominated)->block()->function();
          case term_instruction: return function == value_cast<Instruction>(dominated)->block()->function();
          case term_function_parameter: return value_cast<FunctionParameter>(dominated)->phantom() &&
            (value_cast<FunctionParameter>(dominated)->function() == function);
          }
        }
        
        case term_phi: {
          const ValuePtr<Block>& block = value_cast<Phi>(dominator)->block();
          switch (dominated->term_type()) {
          default: return false;
          case term_block: return block->function() == value_cast<Block>(dominated)->function();
          case term_catch_clause:
          case term_phi: return value_cast<BlockMember>(dominated)->block()->dominated_by(block);
          case term_instruction: return value_cast<Instruction>(dominated)->block()->dominated_by(block);
          case term_function_parameter: return value_cast<FunctionParameter>(dominated)->phantom() &&
            (value_cast<FunctionParameter>(dominated)->function() == block->function());
          }
        }
          
        case term_instruction: {
          Instruction *dominator_insn = value_cast<Instruction>(dominator);
          switch (dominated->term_type()) {
          default: return false;
          case term_phi:
          case term_catch_clause: {
            BlockMember *cast_dominated = value_cast<BlockMember>(dominated);
            if (cast_dominated->block() == dominator_insn->block())
              return false;
            else
              return cast_dominated->block()->dominated_by(dominator_insn->block());
          }
          case term_instruction: {
            Instruction *dominated_insn = value_cast<Instruction>(dominated);
            if (dominator_insn->block() == dominated_insn->block()) {
              const ValuePtr<Block>& block = dominator_insn->block();
              const Block::InstructionList& insn_list = block->instructions();
              for (Block::InstructionList::const_iterator ii = insn_list.begin(), ie = insn_list.end(); ii != ie; ++ii) {
                if (dominator_insn == ii->get())
                  return true;
                else if (dominated_insn == ii->get())
                  return false;
              }
              PSI_FAIL("Unreachable");
            } else {
              return dominated_insn->block()->dominated_by(dominator_insn->block());
            }
          }
          case term_function_parameter: return value_cast<FunctionParameter>(dominated)->phantom() &&
            (value_cast<FunctionParameter>(dominated)->function() == dominator_insn->block()->function());
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


