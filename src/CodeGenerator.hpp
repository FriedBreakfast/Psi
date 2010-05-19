#ifndef HPP_PSI_CODEGENERATOR
#define HPP_PSI_CODEGENERATOR

#include "TypeSystem.hpp"

namespace Psi {
  namespace CodeGenerator {
    class Type {
    };

    class ValueInterface {
    public:
    };

    typedef std::shared_ptr<ValueInterface> Value;

    class FunctionType {
    public:
      enum class ArgumentMode {
        InValue,
        InReference,
	InOut,
	Out,
	OutReference
      };

      unsigned result_count() const {return m_result_count;}

    private:
      std::vector<ArgumentMode> m_modes;
      unsigned m_result_count;
    };

    class InstructionInterface;
    typedef std::shared_ptr<InstructionInterface> Instruction;

    class FunctionCall {
    public:
      std::shared_ptr<Instruction> apply();
    };

    class Function {
    public:
      Value value();
      Block start_block();
      const std::vector<Value>& parameters();

    private:
    };

    Maybe<FunctionCall> call(const Value& function, const std::vector<Value>& parameters);

    class BlockInterface;
    typedef std::shared_ptr<BlockInterface> Block;

    class BlockInterface {
    public:
      Value phi(const Type& type);
      void add_incoming(const Block& incoming, const std::unordered_map<Value, Value>& phi_values);
      void append(const Instruction& insn);
      void destroy(const Value& value);
    };

    /**
     * Create a branch instruction. No more instructions may be
     * inserted into this block after this has been called.
     *
     * \param cond Condition on which to branch. This must be of
     * type \c bool.
     * \param if_true Block to jump to if \c cond is true.
     * \param if_false Block to jump to if \c cond is false.
     */
    Instruction branch_instruction(const Value& cond, const Block& if_true, const Block& if_false);

    /**
     * Create a jump instruction. No more instructions may be
     * inserted into the block after this has been called.
     *
     * \param target Block to jump to.
     */
    Instruction goto_instruction(const Block& target);

    /**
     * Create a call instruction.
     */
    Instruction call_instruction(const Value& function, const std::vector<Value>& parameters);

    Maybe<Instruction> call_instruction_maybe(const Value& function, const std::vector<Value>& parameters);

    Instruction destroy_instruction(const Value& value);

    Value constant_integer(const std::string& num);
    Value constant_float(float value);
    Value constant_double(double value);
  }
}

#endif
