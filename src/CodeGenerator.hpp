#ifndef HPP_PSI_CODEGENERATOR
#define HPP_PSI_CODEGENERATOR

#include "TypeSystem.hpp"

namespace Psi {
  namespace CodeGenerator {
    class Function {
    public:
      enum class ArgumentMode {
        InValue,
        InReference,
        InOut
      };

      unsigned result_count() const {return m_result_count;}

    private:
      std::vector<ArgumentMode> m_modes;
      unsigned m_result_count;
    };

    class Value {
    private:
      friend class PhiNode;
      friend class BlockGenerator;

      struct Data;
      std::shared_ptr<Data> m_data;

      Value(std::shared_ptr<Data> data) : m_data(std::move(data)) {}
    };

    class PhiNode;

    class Block {
    private:
      friend class PhiNode;
      friend class BlockGenerator;

      struct Data;
      std::shared_ptr<Data> m_data;
    };

    class PhiNode {
    public:
      void merge(const Block& incoming, const Value& value);

      /**
       * Get the value associated with this Phi node. This also fixes
       * the result type of this node, although further values may be
       * merged provided they are compatible with the output type.
       */
      const Value& value();

    private:
      friend class BlockGenerator;
      PhiNode(Block block);

      struct Data;
      std::shared_ptr<Data> m_data;
    };

    class BlockGenerator {
    public:
      BlockGenerator(Block block);
      ~BlockGenerator();

      /**
       * Create a Phi node at the start of this block.
       */
      PhiNode phi();

      /**
       * Create a function call instruction.
       */
      std::vector<Value> invoke(const Function& function, const std::vector<Value>& arguments);

      /**
       * Destroy an object. The specified value may no longer be used
       * after calling this function.
       */
      void destroy(Value& value);

      /**
       * Create a branch instruction. No more instructions may be
       * inserted into this block after this has been called.
       *
       * \param cond Condition on which to branch. This must be of
       * type \c bool.
       * \param if_true Block to jump to if \c cond is true.
       * \param if_false Block to jump to if \c cond is false.
       */
      void branch(const Value& cond, Block& if_true, Block& if_false);

      /**
       * Create a jump instruction. No more instructions may be
       * inserted into the block after this has been called.
       *
       * \param target Block to jump to.
       */
      void goto_(Block& target);

    private:
      Block m_block;
    };

    Value constant_integer(const std::string& num);
    Value constant_float(float value);
    Value constant_double(double value);
  }
}

#endif
