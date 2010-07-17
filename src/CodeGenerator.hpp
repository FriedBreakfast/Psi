#ifndef HPP_PSI_CODEGENERATOR
#define HPP_PSI_CODEGENERATOR

#include "Box.hpp"
#include "TypeSystem.hpp"
#include "Variant.hpp"
#include "PoolGC.hpp"
#include "IntrusivePtr.hpp"

#include <boost/intrusive/list.hpp>

#include <llvm/Instruction.h>

namespace Psi {
  namespace Compiler {
    namespace Detail {
#ifndef NDEBUG
      template<typename T> struct GCPoolType {typedef GC::TypedNewPool<T> type;};
      typedef GC::NewPoolBase GCBaseType;
#else
      template<typename T> struct GCPoolType {typedef GC::TypePool<T> type;};
      typedef GC::TypePoolBase GCBaseType;
#endif

      struct BlockP;
    }

    class Block;
    class Type;
    class Instruction;
    class InstructionI;
    class TemplateType;
    class UserType;

    class InstructionI {
    public:

      virtual std::unique_ptr<llvm::Instruction> to_llvm() = 0;
      virtual void check_variables() = 0;

      virtual bool terminator();
      virtual std::vector<Block> jump_targets();

      virtual Type result_type();

      /**
       * Clear any references to external values/instructions. This is
       * used when the parent block is destroyed so that invalid
       * references are not left dangling.
       */
      virtual void clear() = 0;

    private:
      friend class Detail::BlockP;
      Detail::BlockP *block;
      boost::intrusive::list_member_hook<> list_hook;
    };

    class TemplateTypeI {
    public:
#if 0
      /**
       * A value may require some adaptation when a type is
       * specialized, particularly in the case of template
       * functions. This function gives the opportunity to perform
       * that specialization.
       */
      virtual CodeValue specialize(const Context& context,
                                   const std::vector<Type>& parameters,
                                   const Value& value) = 0;
#endif

      //const std::shared_ptr<UserType>& user() const {return m_user;}
    };

    namespace Detail {
      struct BlockP;
      struct TypeP;
      struct Context;

      // This class will perform all manipulations spanning multiple
      // types, hence all classes in this header should have it as a \c
      // friend.
      class CodeGenerator {
      public:
        CodeGenerator() = delete;

        static void block_append(Block& block, Instruction& instruction);
        static Block block_create_child(Block& block);

	static GC::GCPtr<TypeP> type_specialize(TemplateType& template_, std::vector<Type> parameters);
      };

      struct TypeP : GCBaseType {
        GC::GCPtr<TemplateType> template_;
        std::vector<GC::GCPtr<TypeP> > parameters;

        virtual void gc_visit(const std::function<bool(GC::Node*)>& visitor);
      };

      struct BlockP : IntrusivePtr::IntrusiveBase<BlockP> {
        // Whether any more instructions can be added to this block
        bool terminated;
        boost::intrusive::list<InstructionI,
                               boost::intrusive::member_hook<InstructionI, boost::intrusive::list_member_hook<>, &InstructionI::list_hook>,
                               boost::intrusive::constant_time_size<false> > instructions;

	//@{
	/**
	 * These members track parent/child relationships amoung
	 * blocks. The \c parent pointer points to the parent block
	 * and allows checking of defined variables; the other two
	 * exist to allow clearing the \c parent pointer when the
	 * parent is destroyed.
	 */

        // Parent of this block when it is created. \c parent must
        // always be dominated by \c dominator if \c dominator is not
        // NULL.
	BlockP *parent;
	boost::intrusive::list_member_hook<> child_list_hook;
	boost::intrusive::list<BlockP,
			       boost::intrusive::member_hook<BlockP, boost::intrusive::list_member_hook<>, &BlockP::child_list_hook>,
			       boost::intrusive::constant_time_size<false> > children;
        //@}

        // Dominating block.
	Block *dominator;
      };
    }

    class CodeValue;

    class TemplateType;
    class Value;
    class Type;
    class Block;
    class Instruction;

    /**
     * This class allows types to carry user-defined data.
     */
    class UserType {
    public:
      /**
       * Gets a pointer to the specified type, if available. If the
       * specified type is not available, std::runtime_error is
       * thrown.
       */
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

    class ParameterType {
    private:
      GC::GCPtr<Detail::TypeP> m_data;
    };

    class TemplateType {
    public:
      TemplateType();
    };

    class TemplateType {
    public:
      TemplateType(std::vector<ParameterType> parameters, std::shared_ptr<UserType> user)
        : m_parameters(std::move(parameters)), m_user(std::move(user)) {}


    private:
      std::vector<ParameterType> m_parameters;
      std::shared_ptr<UserType> m_user;
    };

    class Type {
    private:
      GC::GCPtr<Detail::TypeP> m_data;

    public:
      Type(TemplateType template_, std::vector<Type> parameters)
	: m_data(Detail::CodeGenerator::type_specialize(std::move(template_), std::move(parameters))) {}

      bool parameterized() const {return !m_data->template_;}

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

    class FunctionType : public TemplateTypeI {
    public:
    private:
      std::vector<FunctionParameter> m_arguments;
      std::vector<FunctionResult> m_results;
    };

    class StructTemplateType : public TemplateTypeI {
    private:
      std::vector<Type> m_members;
    };

    class UnionTemplateType : public TemplateTypeI {
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

    class ValueI {
    public:
      virtual llvm::Value* to_llvm() = 0;
    };

    class Value {
      friend class CodeGenerator;

    public:
      const Type& type() const {return m_data->type;}

    private:
      struct Data {
        Type type;
        std::unique_ptr<ValueI> value;
      };

      std::shared_ptr<Data> m_data;
    };

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

    class Instruction {
      friend class CodeGenerator;

    public:

      Instruction(const Instruction&) = delete;
      Instruction(Instruction&&) = default;

      Value value();

    private:
      GC::GCPtr<InstructionI> m_ptr;
    };

    class Block {
      friend class CodeGenerator;
      Block(GC::GCPtr<Detail::BlockP> data);

    public:
      Block(Block&&) = default;
      Block& operator = (Block&& src) {m_data = std::move(src.m_data); return *this;}

      Value phi(const Type& type);
      void append(Instruction&& insn) {CodeGenerator::block_append(*this, insn);}
      Block create_child() {return CodeGenerator::block_create_child(*this);}

    private:
      GC::GCPtr<BlockData> m_data;
    };

    /**
     * A group of #Blocks with a single entry and exit point.
     */
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
