#ifndef HPP_PSI_CODEGENERATOR
#define HPP_PSI_CODEGENERATOR

#include "Box.hpp"
#include "TypeSystem.hpp"
#include "Variant.hpp"

#include <llvm/BasicBlock.h>
#include <llvm/Instruction.h>

namespace Psi {
  namespace Compiler {
    class CodeValue;

    class TemplateType;
    class Value;
    class Context;
    class ConcreteType;
    class Type;

    class ParameterTypeTag;
    typedef std::shared_ptr<ParameterTypeTag> ParameterType;

    class UserType {
    public:
      template<typename T>
      std::shared_ptr<T> cast() const {
        std::shared_ptr<T> result;
        Box b(&result);
        if (!cast_impl(b) || !result)
          throw std::runtime_error("cast failed");
        return result;
      }

    private:
      virtual bool cast_impl(Box& box) const = 0;
    };

    class Type;

    class ConcreteType {
    public:
      const std::vector<Type>& parameters();
      const TemplateType& template_();
      const UserType& user();

    private:
      struct Data {
	std::vector<Type> parameters;
	std::shared_ptr<TemplateType> type;
        std::shared_ptr<UserType> user;
      };
    };

    class TemplateType {
    public:
      TemplateType(std::vector<ParameterType> parameters, std::shared_ptr<UserType> user)
        : m_parameters(std::move(parameters)), m_user(std::move(user)) {}

      virtual CodeValue specialize(const Context& context,
                                   const std::vector<Type>& parameters,
                                   const Value& value) = 0;

      const std::shared_ptr<UserType>& user() const {return m_user;}

    private:
      std::vector<ParameterType> m_parameters;
      std::shared_ptr<UserType> m_user;
    };

    class Type {
    private:
      struct Data {
        Data(const Data&) = delete;
        Data(Data&&) = delete;

        Data(std::shared_ptr<UserType>&& user_) : user(std::move(user_)) {}

        Data(std::shared_ptr<TemplateType>&& template2, std::vector<Type>&& parameters_)
          : template_(std::move(template2)), parameters(std::move(parameters_)) {
          user = template_->user();
        }

        std::shared_ptr<UserType> user;

        std::shared_ptr<TemplateType> template_;
        std::vector<Type> parameters;
      };

      std::shared_ptr<Data> m_data;

    public:
      Type(std::shared_ptr<TemplateType> template_, std::vector<Type> parameters)
        : m_data(std::make_shared<Data>(std::move(template_), std::move(parameters))) {}
      Type(std::shared_ptr<UserType> user)
        : m_data(std::make_shared<Data>(std::move(user))) {}

      bool parameterized() const {return !m_data->template_;}
      const UserType& user() const {return *m_data->user;}

      const TemplateType& template_() const {return *m_data->template_;}
      const std::vector<Type>& parameters() const {return m_data->parameters;}
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

    /**
     * Replace the #UserType of an existing type.
     */
    class WrapperType : public TemplateType {
    public:
      WrapperType(std::shared_ptr<UserType> user, Type underlying);

    private:
      Type m_underlying;
    };

    class Value {
    public:
      const Type& type() const {return m_type;}

    private:
      Type m_type;
    };

    class Instruction;

    class CodeBlock {
    public:
      CodeBlock();
      CodeBlock(std::initializer_list<CodeBlock> elements);
      CodeBlock(const CodeBlock&) = delete;
      CodeBlock(CodeBlock&&) = default;

      void append(CodeBlock&& other);
      void append(Instruction&& insn);
    };

    /**
     * A class which will contain a set of instructions and also a
     * value corresponding to whatever is considered the result of
     * those instructions.
     */
    struct CodeValue {
      CodeValue(Value value_, CodeBlock&& code_) : value(std::move(value_)), code(std::move(code_)) {}
      CodeValue(const CodeValue&) = delete;
      CodeValue(CodeValue&&) = default;

      Value value;
      CodeBlock code;
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

    class InstructionList;

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

      Value value();

    private:
      std::unique_ptr<InstructionI> m_ptr;
    };

    class InstructionList {
    public:
      InstructionList();
      InstructionList(const InstructionList&) = delete;
      InstructionList(InstructionList&&) = default;
      ~InstructionList();
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
    
    /**
     * Raise a compilation error. Currently this is simply a
     * placeholder in the source.
     */
    std::exception& compile_error(const std::string& message) __attribute__((noreturn));
  }
}

#endif
