#ifndef HPP_PSI_CODEGENERATOR
#define HPP_PSI_CODEGENERATOR

#include "Core.hpp"
#include "Function.hpp"

namespace Psi {
  namespace Tvm {
    class Return {
    public:
      TermPtr<> type(Context&, const FunctionTerm&, TermRefArray<>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, InstructionTerm&) const;
      void jump_targets(Context& context, InstructionTerm&, std::vector<TermPtr<BlockTerm> >&) const;

      class Access {
      public:
	Access(const InstructionTerm *term, const Return*) : m_term(term) {}
	TermPtr<> value() const {return m_term->parameter(0);}
      private:
	const InstructionTerm *m_term;
      };
    };

    class ConditionalBranch {
    public:
      TermPtr<> type(Context&, const FunctionTerm&, TermRefArray<>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, InstructionTerm&) const;
      void jump_targets(Context& context, InstructionTerm&, std::vector<TermPtr<BlockTerm> >&) const;

      class Access {
      public:
	Access(const InstructionTerm *term, const ConditionalBranch*) : m_term(term) {}
	TermPtr<> condition() const {return m_term->parameter(0);}
	TermPtr<BlockTerm> true_target() const {return checked_term_cast<BlockTerm>(m_term->parameter(1));}
	TermPtr<BlockTerm> false_target() const {return checked_term_cast<BlockTerm>(m_term->parameter(2));}
      private:
	const InstructionTerm *m_term;
      };
    };

    class UnconditionalBranch {
    public:
      TermPtr<> type(Context&, const FunctionTerm&, TermRefArray<>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, InstructionTerm&) const;
      void jump_targets(Context& context, InstructionTerm&, std::vector<TermPtr<BlockTerm> >&) const;

      class Access {
      public:
	Access(const InstructionTerm *term, const UnconditionalBranch*) : m_term(term) {}
	TermPtr<BlockTerm> target() const {return checked_term_cast<BlockTerm>(m_term->parameter(0));}
      private:
	const InstructionTerm *m_term;
      };
    };

    class FunctionCall {
    public:
      TermPtr<> type(Context&, const FunctionTerm&, TermRefArray<>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, InstructionTerm&) const;
      void jump_targets(Context& context, InstructionTerm&, std::vector<TermPtr<BlockTerm> >&) const;

      class Access {
      public:
	Access(const InstructionTerm *term, const FunctionCall*) : m_term(term) {}
	TermPtr<> target() const {return m_term->parameter(0);}
	TermPtr<> parameter(std::size_t n) const {return m_term->parameter(n+1);}
      private:
	const InstructionTerm *m_term;
      };
    };
  }
}

#endif
