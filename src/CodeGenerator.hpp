#ifndef HPP_PSI_CODEGENERATOR
#define HPP_PSI_CODEGENERATOR

#include "TypeSystem.hpp"
#include "Variant.hpp"

#include <llvm/BasicBlock.h>
#include <llvm/Instruction.h>

namespace Psi {
  namespace Compiler {
    class InstructionList {
    public:
      InstructionList();
      InstructionList(InstructionList&& src);
      ~InstructionList();

      void append(InstructionList& src);
      void append(InstructionList&& src);

    private:
      typedef llvm::iplist<llvm::Instruction> ListType;
      ListType m_list;
    };

    class TemplateType;
    class Value;
    class Context;
    class ConcreteType;

    class ParameterTypeTag;
    typedef std::shared_ptr<ParameterTypeTag> ParameterType;

    typedef Variant<ParameterType, ConcreteType> Type;

    class ConcreteType {
    public:
      const std::vector<Type>& parameters();
      const std::shared_ptr<TemplateType> type();

    private:
      struct Data {
	std::vector<Type> m_parameters;
	std::shared_ptr<TemplateType> m_type;
      };
    };

    class TemplateType {
    public:
      /// To get dynamic casts
      virtual ~TemplateType();

      virtual InstructionList specialize(const Context& context,
					 const std::vector<Type>& parameters,
					 const std::shared_ptr<Value>& value) = 0;

    private:
      std::vector<std::shared_ptr<ParameterType> > m_parameters;
    };

    class FunctionType : public TemplateType {
    public:
      enum class InMode {
        InValue,
        InReference,
	InOut,
      };

      enum class OutMode {
	Out,
	OutReference
      };

    private:
      std::vector<std::pair<InMode, Type> > m_in_arguments;
      std::vector<std::pair<OutMode, Type> > m_out_arguments;
    };

    class StructTemplateType : public TemplateType {
    private:
      std::vector<Type> m_members;
    };

    class UnionTemplateType : public TemplateType {
    private:
      std::vector<Type> m_members;
    };

#if 0
    class Context {
    private:
      /// Value holds size, alignment and destructors for this type.
      std::unordered_map<ParameterType, std::shared_ptr<Value> > m_parameter_types;
    };

    class Function {
    public:
      std::shared_ptr<Value> value();
      std::shared_ptr<Block> start_block();
      const std::vector<std::shared_ptr<Value> >& parameters();

    private:
    };

    class Block {
    public:
      std::shared_ptr<Value> phi(const std::shared_ptr<Type>& type);
      void add_incoming(const std::shared_ptr<Block>& incoming,
			const std::unordered_map<std::shared_ptr<Value>, std::shared_ptr<Value> >& phi_values);
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
    std::shared_ptr<Instruction> branch_instruction(const Value& cond, const Block& if_true, const Block& if_false);

    /**
     * Create a jump instruction. No more instructions may be
     * inserted into the block after this has been called.
     *
     * \param target Block to jump to.
     */
    std::shared_ptr<Instruction> goto_instruction(const Block& target);

    /**
     * Create a call instruction.
     */
    std::shared_ptr<Instruction> call_instruction(const Value& function, const std::vector<Value>& parameters);

    std::shared_ptr<Instruction> call_instruction_maybe(const Value& function, const std::vector<Value>& parameters);

    std::shared_ptr<Instruction> destroy_instruction(const Value& value);

    std::shared_ptr<Value> constant_integer(const std::string& num);
    std::shared_ptr<Value> constant_float(float value);
    std::shared_ptr<Value> constant_double(double value);
#endif
  }
}

#endif
