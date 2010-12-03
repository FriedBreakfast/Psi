#ifndef HPP_PSI_CODEGENERATOR
#define HPP_PSI_CODEGENERATOR

#include "Core.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Primitive.hpp"

namespace Psi {
  namespace Tvm {
    class Return {
    public:
      Term* type(Context&, const FunctionTerm&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, InstructionTerm&) const;
      void jump_targets(Context& context, InstructionTerm&, std::vector<BlockTerm*>&) const;

      class Access {
      public:
	Access(const InstructionTerm *term, const Return*) : m_term(term) {}
	Term* value() const {return m_term->parameter(0);}
      private:
	const InstructionTerm *m_term;
      };
    };

    class ConditionalBranch {
    public:
      Term* type(Context&, const FunctionTerm&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, InstructionTerm&) const;
      void jump_targets(Context& context, InstructionTerm&, std::vector<BlockTerm*>&) const;

      class Access {
      public:
	Access(const InstructionTerm *term, const ConditionalBranch*) : m_term(term) {}
	Term* condition() const {return m_term->parameter(0);}
	BlockTerm* true_target() const {return checked_cast<BlockTerm*>(m_term->parameter(1));}
	BlockTerm* false_target() const {return checked_cast<BlockTerm*>(m_term->parameter(2));}
      private:
	const InstructionTerm *m_term;
      };
    };

    class UnconditionalBranch {
    public:
      Term* type(Context&, const FunctionTerm&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, InstructionTerm&) const;
      void jump_targets(Context& context, InstructionTerm&, std::vector<BlockTerm*>&) const;

      class Access {
      public:
	Access(const InstructionTerm *term, const UnconditionalBranch*) : m_term(term) {}
	BlockTerm* target() const {return checked_cast<BlockTerm*>(m_term->parameter(0));}
      private:
	const InstructionTerm *m_term;
      };
    };

    class FunctionCall {
    public:
      Term* type(Context&, const FunctionTerm&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, InstructionTerm&) const;
      void jump_targets(Context& context, InstructionTerm&, std::vector<BlockTerm*>&) const;

      class Access {
      public:
	Access(const InstructionTerm *term, const FunctionCall*) : m_term(term) {}
	Term* target() const {return m_term->parameter(0);}
	Term* parameter(std::size_t n) const {return m_term->parameter(n+1);}
      private:
	const InstructionTerm *m_term;
      };
    };

    class FunctionApplyPhantom : public StatelessOperand {
    public:
      Term* type(Context& context, ArrayPtr<Term*const> parameters) const;
      LLVMType llvm_type(LLVMValueBuilder&, Term&) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const;

      class Access {
      public:
	Access(const FunctionalTerm *term, const FunctionApplyPhantom*) : m_term(term) {}
        Term* function() const {return m_term->parameter(0);}
        std::size_t n_parameters() const {return m_term->n_parameters() - 1;}
        Term* parameter(std::size_t n) const {return m_term->parameter(n+1);}
      private:
	const FunctionalTerm *m_term;
      };
    };
  }
}

#endif
