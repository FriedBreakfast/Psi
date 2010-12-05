#ifndef HPP_PSI_TVM_MEMORY
#define HPP_PSI_TVM_MEMORY

namespace Psi {
  namespace Tvm {
    class Store {
    public:
      Term* type(Context&, const FunctionTerm&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, InstructionTerm&) const;
      void jump_targets(Context&, InstructionTerm&, std::vector<BlockTerm*>&) const;

      class Access {
      public:
	Access(const InstructionTerm *term, const Store*) : m_term(term) {}
        /// \brief Get the value to be stored
        Term* value() const {return m_term->parameter(0);}
        /// \brief Get the memory address which is to be written to
        Term* target() const {return m_term->parameter(1);}
      private:
	const InstructionTerm *m_term;
      };
    };

    class Load {
    public:
      Term* type(Context&, const FunctionTerm&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, InstructionTerm&) const;
      void jump_targets(Context&, InstructionTerm&, std::vector<BlockTerm*>&) const;

      class Access {
      public:
	Access(const InstructionTerm *term, const Load*) : m_term(term) {}
        /// \brief Get the memory address being read from
        Term* target() const {return m_term->parameter(0);}
      private:
	const InstructionTerm *m_term;
      };
    };

    class Alloca {
    public:
      Term* type(Context&, const FunctionTerm&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, InstructionTerm&) const;
      void jump_targets(Context&, InstructionTerm&, std::vector<BlockTerm*>&) const;

      class Access {
      public:
	Access(const InstructionTerm *term, const Alloca*) : m_term(term) {}
        /// \brief Get the type which storage is allocated for
        Term* stored_type() const {return m_term->parameter(0);}
      private:
	const InstructionTerm *m_term;
      };
    };
  }
}

#endif
