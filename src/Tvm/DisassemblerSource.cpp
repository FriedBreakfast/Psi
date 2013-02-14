#include "Utility.hpp"
#include "Function.hpp"
#include "Recursive.hpp"

namespace Psi {
  namespace Tvm {
    namespace {
      PSI_ATTRIBUTE((PSI_NORETURN)) Value* common_source_fail();
      
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
        if (g->module() == b->function_ptr()->module())
          return b;
        else
          return common_source_fail();
      }

      Value* common_source_global_phi(Global *g, BlockMember *p) {
        if (g->module() == p->block_ptr()->function_ptr()->module())
          return p;
        else
          return common_source_fail();
      }
      
      Value* common_source_global_instruction(Global* g, Instruction *i) {
        if (g->module() == i->block_ptr()->function_ptr()->module())
          return i;
        else
          return common_source_fail();
      }
      
      Value* common_source_global_parameter(Global *g, FunctionParameter *p) {
        if (g->module() == p->function_ptr()->module())
          return p;
        else
          return common_source_fail();
      }
      
      Value* common_source_global_type_parameter(Global*, ParameterPlaceholder *p) {
        return p;
      }
      
      Value* common_source_block_block(Block *b1, Block *b2) {
        if (b1->function_ptr() == b2->function_ptr())
          return b1;
        else
          return common_source_fail();
      }

      Value* common_source_block_phi(Block *b, BlockMember *p) {
        if (p->block_ptr()->function_ptr() == b->function_ptr())
          return p;
        else
          return common_source_fail();
      }
      
      Value* common_source_block_instruction(Block *b, Instruction *i) {
        if (b->function_ptr() == i->block_ptr()->function_ptr())
          return i;
        else
          return common_source_fail();
      }
      
      Value* common_source_block_parameter(Block *b, FunctionParameter *p) {
        if (b->function_ptr() == p->function_ptr())
          return p;
        else
          return common_source_fail();
      }

      Value* common_source_block_type_parameter(Block*, ParameterPlaceholder *p) {
        return p;
      }

      Value *common_source_phi_phi(BlockMember *p1, BlockMember *p2) {
        Block *b1 = p1->block_ptr(), *b2 = p2->block_ptr();
        if (b1->dominated_by(b2))
          return p1;
        else if (b2->dominated_by(b1))
          return p2;
        else
          return common_source_fail();
      }
      
      Value *common_source_phi_instruction(BlockMember *p, Instruction *i) {
        Block *b = p->block_ptr();
        if (i->block_ptr()->dominated_by(b))
          return i;
        else if (b->dominated_by(i->block()))
          return p;
        else
          return common_source_fail();
      }
      
      Value *common_source_phi_parameter(BlockMember *p, FunctionParameter *pa) {
        if (p->block_ptr()->function_ptr() == pa->function_ptr())
          return p;
        else
          return common_source_fail();
      }
      
      Value *common_source_phi_type_parameter(BlockMember*, ParameterPlaceholder *pa) {
        return pa;
      }

      Value* common_source_instruction_instruction(Instruction *i1, Instruction *i2) {
        Block *b1 = i1->block_ptr(), *b2 = i2->block_ptr();
        if (b1 == b2) {
          return b1->instructions().before(*i1, *i2) ? i2 : i1;
        } else if (b1->dominated_by(b2))
          return i1;
        else if (b2->dominated_by(b1))
          return i2;
        else
          return common_source_fail();
      }

      Value* common_source_instruction_parameter(Instruction *i, FunctionParameter *p) {
        if (i->block_ptr()->function_ptr() == p->function_ptr())
          return i;
        else
          return common_source_fail();
      }

      Value* common_source_instruction_type_parameter(Instruction*, ParameterPlaceholder *p) {
        return p;
      }
      
      Value* common_source_parameter_parameter(FunctionParameter *p1, FunctionParameter *p2) {
        if (p1->function_ptr() != p2->function_ptr())
          return common_source_fail();
        
        return p1;
      }
      
      Value* common_source_parameter_type_parameter(FunctionParameter*, ParameterPlaceholder *p) {
        return p;
      }
      
      Value* common_source_type_parameter_type_parameter(ParameterPlaceholder *p, ParameterPlaceholder*) {
        return p;
      }
    }
    
    /**
     * Find the common source term of two terms. If no such source exists,
     * throw an exception.
     */
    Value* disassembler_merge_source(Value *t1, Value *t2) {
      if (t1 && t2) {
        switch (t1->term_type()) {
        case term_global_variable:
        case term_function:
          switch (t2->term_type()) {
          case term_global_variable:
          case term_function: return common_source_global_global(value_cast<Global>(t1), value_cast<Global>(t2));
          case term_block: return common_source_global_block(value_cast<Global>(t1), value_cast<Block>(t2));
          case term_phi: return common_source_global_phi(value_cast<Global>(t1), value_cast<BlockMember>(t2));
          case term_instruction: return common_source_global_instruction(value_cast<Global>(t1), value_cast<Instruction>(t2));
          case term_function_parameter: return common_source_global_parameter(value_cast<Global>(t1), value_cast<FunctionParameter>(t2));
          case term_parameter_placeholder: return common_source_global_type_parameter(value_cast<Global>(t1), value_cast<ParameterPlaceholder>(t2));
          default: PSI_FAIL("unexpected term type");
          }

        case term_block:
          switch (t2->term_type()) {
          case term_global_variable:
          case term_function: return common_source_global_block(value_cast<Global>(t2), value_cast<Block>(t1));
          case term_block: return common_source_block_block(value_cast<Block>(t1), value_cast<Block>(t2));
          case term_phi: return common_source_block_phi(value_cast<Block>(t1), value_cast<BlockMember>(t2));
          case term_instruction: return common_source_block_instruction(value_cast<Block>(t1), value_cast<Instruction>(t2));
          case term_function_parameter: return common_source_block_parameter(value_cast<Block>(t1), value_cast<FunctionParameter>(t2));
          case term_parameter_placeholder: return common_source_block_type_parameter(value_cast<Block>(t1), value_cast<ParameterPlaceholder>(t2));
          default: PSI_FAIL("unexpected term type");
          }

        case term_phi:
          switch (t2->term_type()) {
          case term_global_variable:
          case term_function: return common_source_global_phi(value_cast<Global>(t2), value_cast<BlockMember>(t1));
          case term_block: return common_source_block_phi(value_cast<Block>(t2), value_cast<BlockMember>(t1));
          case term_phi: return common_source_phi_phi(value_cast<BlockMember>(t1), value_cast<BlockMember>(t2));
          case term_instruction: return common_source_phi_instruction(value_cast<BlockMember>(t1), value_cast<Instruction>(t2));
          case term_function_parameter: return common_source_phi_parameter(value_cast<BlockMember>(t1), value_cast<FunctionParameter>(t2));
          case term_parameter_placeholder: return common_source_phi_type_parameter(value_cast<BlockMember>(t1), value_cast<ParameterPlaceholder>(t2));
          default: PSI_FAIL("unexpected term type");
          }

        case term_instruction:
          switch (t2->term_type()) {
          case term_global_variable:
          case term_function: return common_source_global_instruction(value_cast<Global>(t2), value_cast<Instruction>(t1));
          case term_block: return common_source_block_instruction(value_cast<Block>(t2), value_cast<Instruction>(t1));
          case term_phi: return common_source_phi_instruction(value_cast<Phi>(t2), value_cast<Instruction>(t1));
          case term_instruction: return common_source_instruction_instruction(value_cast<Instruction>(t1), value_cast<Instruction>(t2));
          case term_function_parameter: return common_source_instruction_parameter(value_cast<Instruction>(t1), value_cast<FunctionParameter>(t2));
          case term_parameter_placeholder: return common_source_instruction_type_parameter(value_cast<Instruction>(t1), value_cast<ParameterPlaceholder>(t2));
          default: PSI_FAIL("unexpected term type");
          }
          
        case term_function_parameter:
          switch (t2->term_type()) {
          case term_global_variable:
          case term_function: return common_source_global_parameter(value_cast<Global>(t2), value_cast<FunctionParameter>(t1));
          case term_block: return common_source_block_parameter(value_cast<Block>(t2), value_cast<FunctionParameter>(t1));
          case term_phi: return common_source_phi_parameter(value_cast<BlockMember>(t2), value_cast<FunctionParameter>(t1));
          case term_instruction: return common_source_instruction_parameter(value_cast<Instruction>(t1), value_cast<FunctionParameter>(t2));
          case term_function_parameter: return common_source_parameter_parameter(value_cast<FunctionParameter>(t1), value_cast<FunctionParameter>(t2));
          case term_parameter_placeholder: return common_source_parameter_type_parameter(value_cast<FunctionParameter>(t1), value_cast<ParameterPlaceholder>(t2));
          default: PSI_FAIL("unexpected term type");
          }
          
        case term_parameter_placeholder:
          switch (t2->term_type()) {
          case term_global_variable:
          case term_function: return common_source_global_type_parameter(value_cast<Global>(t2), value_cast<ParameterPlaceholder>(t1));
          case term_block: return common_source_block_type_parameter(value_cast<Block>(t2), value_cast<ParameterPlaceholder>(t1));
          case term_phi: return common_source_phi_type_parameter(value_cast<BlockMember>(t2), value_cast<ParameterPlaceholder>(t1));
          case term_instruction: return common_source_instruction_type_parameter(value_cast<Instruction>(t1), value_cast<ParameterPlaceholder>(t2));
          case term_function_parameter: return common_source_parameter_type_parameter(value_cast<FunctionParameter>(t1), value_cast<ParameterPlaceholder>(t2));
          case term_parameter_placeholder: return common_source_type_parameter_type_parameter(value_cast<ParameterPlaceholder>(t1), value_cast<ParameterPlaceholder>(t2));
          default: PSI_FAIL("unexpected term type");
          }

        default:
          PSI_FAIL("unexpected term type");
        }
      } else {
        return t1 ? t1 : t2;
      }
    }
  }
}
