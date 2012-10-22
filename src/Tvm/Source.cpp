#include "Utility.hpp"
#include "Function.hpp"
#include "Recursive.hpp"

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
      else if (RecursiveParameter *rp = dyn_cast<RecursiveParameter>(source()))
        return rp->parameter_phantom();
      return false;
    }
    
    /// \brief Whether this is part of a function type (i.e. it contains function type parameters)
    bool Value::parameterized() const {
      return source() && isa<ParameterPlaceholder>(source());
    }

    namespace {
      Value* common_source_fail() PSI_ATTRIBUTE((PSI_NORETURN));
      
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
          return pa->phantom() ? static_cast<Value*>(pa) : p;
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
          return p->phantom() ? static_cast<Value*>(p) : i;
        else
          return common_source_fail();
      }

      Value* common_source_instruction_type_parameter(Instruction*, ParameterPlaceholder *p) {
        return p;
      }
      
      Value* common_source_parameter_parameter(FunctionParameter *p1, FunctionParameter *p2) {
        if (p1->function_ptr() != p2->function_ptr())
          return common_source_fail();
        
        return p1->phantom() ? p1 : p2;
      }
      
      Value* common_source_parameter_type_parameter(FunctionParameter*, ParameterPlaceholder *p) {
        return p;
      }
      
      Value* common_source_type_parameter_type_parameter(ParameterPlaceholder *p, ParameterPlaceholder*) {
        return p;
      }
      
      Value* recursive_base_source(RecursiveParameter *p) {
        Value *v = p;
        while (v && (v->term_type() == term_recursive_parameter))
          v = value_cast<RecursiveParameter>(v)->recursive_ptr()->source();
        return v;
      }
      
      Value* common_source_recursive_parameter_recursive_parameter(RecursiveParameter *p1, RecursiveParameter *p2) {
        Value *v = p2;
        while (v->term_type() == term_recursive_parameter) {
          RecursiveParameter *vp = value_cast<RecursiveParameter>(v);
          if (vp->recursive_ptr() == p1->recursive_ptr())
            return p2;
          v = vp->recursive_ptr()->source();
        }
        
        v = p1;
        while (v->term_type() == term_recursive_parameter) {
          RecursiveParameter *vp = value_cast<RecursiveParameter>(v);
          if (vp->recursive_ptr() == p2->recursive_ptr())
            return p1;
          v = vp->recursive_ptr()->source();
        }
        
        common_source_fail();
      }
    }
    
    /**
     * Find the common source term of two terms. If no such source exists,
     * throw an exception.
     */
    Value* common_source(Value *t1, Value *t2) {
      if (t1 && t2) {
        // Phantom terms ALWAYS win
        if (t1->phantom())
          return t1;
        else if (t2->phantom())
          return t2;
        
        // Recursive type parameters are incompatible with any other term type
        if (t1->term_type() == term_recursive_parameter) {
          if (t2->term_type() == term_recursive_parameter)
            if (value_cast<RecursiveParameter>(t1)->recursive() == value_cast<RecursiveParameter>(t2)->recursive())
              return t1;
            
          common_source_fail();
        } else if (t2->term_type() == term_recursive_parameter) {
          common_source_fail();
        }
        
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
        if (dominated->term_type() == term_parameter_placeholder)
          return true;
        
        if (dominated->phantom())
          return true;
        
        // Easiest to handle RecursiveParameter case separately
        if (dominator->term_type() == term_recursive_parameter) {
          if (dominated->term_type() == term_recursive_parameter) {
            Value *v = dominated;
            RecursiveType *r = value_cast<RecursiveParameter>(dominator)->recursive_ptr();
            while (v->term_type() == term_recursive_parameter) {
              RecursiveParameter *rp = value_cast<RecursiveParameter>(v);
              if (rp->recursive_ptr() == r)
                return true;
            }
          }
          
          return false;
        } else if (dominated->term_type() == term_recursive_parameter) {
          return source_dominated(dominator, recursive_base_source(value_cast<RecursiveParameter>(dominated)));
        }

        switch (dominator->term_type()) {
        case term_global_variable:
        case term_function: {
          Module *module = value_cast<Global>(dominator)->module();
          switch (dominated->term_type()) {
          default: return false;
          case term_global_variable:
          case term_function: return module == value_cast<Global>(dominated)->module();
          case term_block: return module == value_cast<Block>(dominated)->function()->module();
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
          case term_phi: return function == value_cast<BlockMember>(dominated)->block()->function();
          case term_instruction: return function == value_cast<Instruction>(dominated)->block()->function();
          case term_function_parameter: return function == value_cast<FunctionParameter>(dominated)->function();
          }
        }

        case term_block: {
          Function *function = value_cast<Block>(dominator)->function_ptr();
          switch (dominated->term_type()) {
          default: return false;
          case term_block: return function == value_cast<Block>(dominated)->function_ptr();
          case term_phi: return function == value_cast<BlockMember>(dominated)->block_ptr()->function_ptr();
          case term_instruction: return function == value_cast<Instruction>(dominated)->block_ptr()->function_ptr();
          case term_function_parameter: return value_cast<FunctionParameter>(dominated)->phantom() &&
            (value_cast<FunctionParameter>(dominated)->function_ptr() == function);
          }
        }
        
        case term_phi: {
          Block *block = value_cast<Phi>(dominator)->block_ptr();
          switch (dominated->term_type()) {
          default: return false;
          case term_block: return block->function_ptr() == value_cast<Block>(dominated)->function_ptr();
          case term_phi: return value_cast<BlockMember>(dominated)->block_ptr()->dominated_by(block);
          case term_instruction: return value_cast<Instruction>(dominated)->block_ptr()->dominated_by(block);
          case term_function_parameter: return value_cast<FunctionParameter>(dominated)->phantom() &&
            (value_cast<FunctionParameter>(dominated)->function_ptr() == block->function_ptr());
          }
        }
          
        case term_instruction: {
          Instruction *dominator_insn = value_cast<Instruction>(dominator);
          switch (dominated->term_type()) {
          default: return false;
          case term_phi: {
            BlockMember *cast_dominated = value_cast<BlockMember>(dominated);
            if (cast_dominated->block_ptr() == dominator_insn->block_ptr())
              return false;
            else
              return cast_dominated->block_ptr()->dominated_by(dominator_insn->block_ptr());
          }
          case term_instruction: {
            Instruction *dominated_insn = value_cast<Instruction>(dominated);
            if (dominator_insn->block_ptr() == dominated_insn->block_ptr()) {
              return dominated_insn->block_ptr()->instructions().before(*dominated_insn, *dominated_insn);
            } else {
              return dominated_insn->block_ptr()->dominated_by(dominator_insn->block_ptr());
            }
          }
          case term_function_parameter: return value_cast<FunctionParameter>(dominated)->phantom() &&
            (value_cast<FunctionParameter>(dominated)->function_ptr() == dominator_insn->block_ptr()->function_ptr());
          }
        }
          
        case term_parameter_placeholder:
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
