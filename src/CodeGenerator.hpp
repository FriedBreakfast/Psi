#ifndef HPP_PSI_CODEGENERATOR
#define HPP_PSI_CODEGENERATOR

#include "Box.hpp"
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
					 const Value& value) = 0;

      virtual bool cast(const Box& box) = 0;

    private:
      std::vector<ParameterType> m_parameters;
    };

    enum class ParameterMode {
      /// Pass by value
      value,
      /// Pass by reference
      reference,
      /// Pass by reference and allow modification
      in_out,
    };

    enum class ResultMode {
      /// Return by value
      value,
      /// Return a reference (the parent function does not own the result)
      reference
    };

    struct FunctionParameter {
      ParameterMode mode;
      Type type;
    };

    struct FunctionResult {
      ResultMode mode;
      Type type;
    };

    class FunctionType : public TemplateType {
    public:
    private:
      std::vector<FunctionParameter> m_arguments;
      std::vector<FunctionResult> m_results;
    };

    class StructTemplateType : public TemplateType {
    private:
      std::vector<Type> m_members;
    };

    class UnionTemplateType : public TemplateType {
    private:
      std::vector<Type> m_members;
    };

    class Value {
    public:

    private:
      ConcreteType m_type;
    };

    class Block;

    class Function {
    public:
      Function(Function&&) = default;

      static Function global(std::vector<ParameterType> template_parameters,
			     std::vector<FunctionParameter> parameters,
			     std::vector<FunctionResult> results);

      Function lambda(std::vector<ParameterType> template_parameters,
		      std::vector<FunctionParameter> parameters,
		      std::vector<FunctionResult> results);

      /// Whether this currently refers to a function.
      explicit operator bool () const {return m_data.get();}
      /// Block run on entering this function
      Block entry();
      /// Values of parameters to this function as seen inside the function
      const std::vector<Value>& parameters() const {return m_data->parameters;}

    private:
      struct Data {
	std::vector<Value> parameters;
      };

      std::shared_ptr<Data> m_data;
    };

    class Block {
    public:
      Value phi(const Type& type);
      void append(InstructionList&& insn);
    };

    class InstructionI {
    public:
      virtual std::unique_ptr<llvm::Instruction> to_llvm() = 0;

    private:
      //boost::intrusive::list_member_hook<> m_list_hook;
    };

    class Instruction {
    public:
      Instruction(const Instruction&) = delete;
      Instruction(Instruction&&) = default;

    private:
      std::unique_ptr<InstructionI> m_ptr;
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

    Instruction call_instruction_maybe(const Value& function, const std::vector<Value>& parameters);

    Instruction destroy_instruction(const Value& value);

    Value constant_integer(const std::string& num);
    Value constant_float(float value);
    Value constant_double(double value);
  }
}

#endif
