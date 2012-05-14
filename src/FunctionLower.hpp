#ifndef HPP_PSI_FUNCTION_LOWER
#define HPP_PSI_FUNCTION_LOWER

#include <boost/unordered/unordered_map.hpp>

#include "Tree.hpp"

/**
 * \file
 * 
 * Define functions and trees used after lowering functions into a simpler
 * form.
 * 
 * A pass is also defined to rewrite data structures from abstract to concrete
 * types by using aligned allocation instructions.
 */

namespace Psi {
namespace Compiler {
class InstructionBlock;
class Instruction;

/**
 * \brief Base class for term rewriting compiler passes.
 * 
 * This handles dependency management.
 */
class RewritePass {
  class Callback;
  typedef boost::unordered_map<TreePtr<Term>, TreePtr<Term> > MapType;
  MapType m_map;
  CompileContext *m_compile_context;
  
  virtual TreePtr<Term> derived_apply(const TreePtr<Term>& term) = 0;

public:
  RewritePass();
  virtual ~RewritePass();

  CompileContext& compile_context() {return *m_compile_context;}
  TreePtr<Term> apply(const TreePtr<Term>& term);
  TreePtr<Term> operator () (const TreePtr<Term>& term);
};

/**
 * \brief Rewriting pass which expands functions into SSA and block form.
 */
class FunctionLoweringPass : public RewritePass {
  struct Context;

  virtual TreePtr<Term> derived_apply(const TreePtr<Term>& term) = 0;
  TreePtr<Term> rewrite_body(Context& context, const TreePtr<Term>& term);
  
public:
  virtual ~FunctionLoweringPass();
};

/**
 * \brief Class encapsulating the body of a lowered function.
 * 
 * A wrapper is required since this does not have a tree-like structure: its value is
 * determined by return instructions appearing in blocks.
 */
class LoweredFunctionBody : public Term {
public:
  static const TermVtable vtable;
  
  LoweredFunctionBody(CompileContext& context, const SourceLocation& location);
  LoweredFunctionBody(const TreePtr<Term>& type, const SourceLocation& location);

  /**
   * \brief List of all blocks in this function.
   * 
   * The first one is the entry block.
   */
  PSI_STD::vector<TreePtr<InstructionBlock> > blocks;

  template<typename Visitor> static void visit(Visitor& v);
};

/**
 * \brief Block class for lowered functions.
 */
class InstructionBlock : public Term {
public:
  static const TermVtable vtable;

  InstructionBlock(CompileContext& context, const SourceLocation& location);

  /// \brief Cleanup block to jump to if any exceptions are raised in this block.
  TreePtr<InstructionBlock> cleanup;
  /// \brief Instructions
  PSI_STD::vector<TreePtr<Instruction> > instructions;

  template<typename Visitor> static void visit(Visitor& v);
};

/**
 * \brief Base class for lowered instructions.
 */
class Instruction : public Term {
public:
  static const SIVtable vtable;

  Instruction(const TermVtable *vptr, CompileContext& context, const SourceLocation& location);
  Instruction(const TermVtable *vptr, const TreePtr<Term>& type, const SourceLocation& location);

  template<typename Visitor> static void visit(Visitor& v);
};

/**
 * \brief Stack allocation instruction.
 */
class InstructionAlloca : public Instruction {
public:
  static const TermVtable vtable;
  
  InstructionAlloca(CompileContext& context, const SourceLocation& location);
  InstructionAlloca(const TreePtr<Term>& type, const TreePtr<Term>& size, const TreePtr<Term>& alignment, const SourceLocation& location);

  /// \brief Type to allocate.
  TreePtr<Term> type;
  /**
   * \brief Number of elements of \c type to allocate.
   * 
   * May be NULL, in which case one element is allocated.
   */
  TreePtr<Term> size;
  /// \brief Minimum alignment of returned memory. May be NULL.
  TreePtr<Term> alignment;

  template<typename Visitor> static void visit(Visitor& v);
};

/**
 * \brief Return instruction.
 */
class InstructionReturn : public Instruction {
public:
  static const TermVtable vtable;
  
  InstructionReturn(CompileContext& context, const SourceLocation& location);
  InstructionReturn(const TreePtr<Term>& value, const SourceLocation& location);
  
  TreePtr<Term> value;

  template<typename Visitor> static void visit(Visitor& v);
};

/**
 * \brief Conditional jump instruction.
 */
class InstructionJump : public Instruction {
public:
  static const TermVtable vtable;
  
  InstructionJump(CompileContext& context, const SourceLocation& location);
  InstructionJump(const TreePtr<Term>& condition, const TreePtr<InstructionBlock>& true_target, const TreePtr<InstructionBlock>& false_target, const SourceLocation& location);
  
  /// \brief Condition on which to select jump.
  TreePtr<Term> condition;
  /// \brief Target to jump to if \c condition is true.
  TreePtr<InstructionBlock> true_target;
  /// \brief Target to jump to if \c condition is false.
  TreePtr<InstructionBlock> false_target;

  template<typename Visitor> static void visit(Visitor& v);
};

/**
 * \brief Unconditional jump instruction.
 */
class InstructionGoto : public Instruction {
public:
  static const TermVtable vtable;
  
  InstructionGoto(CompileContext& context, const SourceLocation& location);
  InstructionGoto(const TreePtr<InstructionBlock>& target, const SourceLocation& location);

  /// \brief Jump target.
  TreePtr<InstructionBlock> target;

  template<typename Visitor> static void visit(Visitor& v);
};

/**
 * \brief Function call instruction.
 */
class InstructionCall : public Instruction {
public:
  static const TermVtable vtable;
  
  InstructionCall(CompileContext& context, const SourceLocation& location);
  InstructionCall(const TreePtr<Term>& target, const PSI_STD::vector<TreePtr<Term> >& arguments, const SourceLocation& location);

  /// \brief Call target.
  TreePtr<Term> target;
  /// \brief Call arguments.
  PSI_STD::vector<TreePtr<Term> > arguments;

  template<typename Visitor> static void visit(Visitor& v);
};

/**
 * \brief Store instruction.
 */
class InstructionStore : public Instruction {
public:
  static const TermVtable vtable;

  InstructionStore(CompileContext& context, const SourceLocation& location);
  InstructionStore(const TreePtr<Term>& target, const TreePtr<Term>& value, const SourceLocation& location);

  /// \brief Memory location to store to.
  TreePtr<Term> target;
  /// \brief Value to store.
  TreePtr<Term> value;

  template<typename Visitor> static void visit(Visitor& v);
};

/**
 * \brief Load instruction
 */
class InstructionLoad : public Instruction {
public:
  static const TermVtable vtable;

  InstructionLoad(CompileContext& context, const SourceLocation& location);
  InstructionLoad(const TreePtr<Term>& source, const SourceLocation& location);

  /// \brief Memory location to load from.
  TreePtr<Term> source;

  template<typename Visitor> static void visit(Visitor& v);
};

/**
 * \brief Any other instruction.
 */
class InstructionCompute : public Instruction {
public:
  static const TermVtable vtable;

  InstructionCompute(CompileContext& context, const SourceLocation& location);
  InstructionCompute(const String& name, const TreePtr<Term>& type, const PSI_STD::vector<TreePtr<Term> >& arguments, const SourceLocation& location);

  /// \brief Instruction to compute.
  String name;
  /// \brief Parameters to the instruction.
  PSI_STD::vector<TreePtr<Term> > arguments;

  template<typename Visitor> static void visit(Visitor& v);
};
}
}

#endif
