#ifndef HPP_PSI_TVM_FUNCTION
#define HPP_PSI_TVM_FUNCTION

#include "Core.hpp"
#include "Functional.hpp"

#include <boost/unordered_map.hpp>

namespace Psi {
  namespace Tvm {
    class FunctionType;
    class Block;
    
    /**
     * \brief Base class for terms which belong to a block.
     */
    class BlockMember : public Value {
      friend class Block;

    public:
      /// \brief Get the block this term is part of.
      const ValuePtr<Block>& block() {return m_block;}

    protected:
      BlockMember(Context* context, TermType term_type, const ValuePtr<>& type, const ValuePtr<>& source);
      
      ValuePtr<Block> m_block;
    };
    
    /**
     * \brief Instruction term. Per-instruction funtionality is
     * created by implementing InstructionTermBackend and wrapping
     * that in InstructionTerm.
     */
    class Instruction : public BlockMember {
      friend class Block;
      
    public:
      const char *operation_name() const {return m_operation;}
      virtual void rewrite(RewriteCallback& callback) = 0;

    protected:
      Instruction(const ValuePtr<>& type, const OperationSetup& setup, const SourceLocation& location);

    private:
      const char *m_operation;
      typedef boost::intrusive::list_member_hook<> InstructionListHook;
      InstructionListHook m_instruction_list_hook;
    };
    
    template<typename T>
    OperationSetup instruction_setup() {
      return OperationSetup(T::operation);
    }

    template<typename T, typename U>
    OperationSetup instruction_setup(const U& x) {
      return instruction_setup<T>()(x);
    }
    
#define PSI_TVM_INSTRUCTION_DECL(Type) \
  public: \
    static const char operation[]; \
    virtual void rewrite(RewriteCallback& callback); \
    static bool isa_impl(const Value *ptr) {return (ptr->term_type() == term_instruction) && (operation == value_cast<Type>(ptr)->operation_name());} \
    
#define PSI_TVM_INSTRUCTION_IMPL(Type,Base,Name) \
    const char Type::operation[] = #Name; \
    \
    ValuePtr<FunctionalValue> Type::rewrite(RewriteCallback& callback) { \
      return callback.context().get_functional(Type(callback, *this)); \
    }

    /**
     * Describes incoming edges for Phi nodes.
     */
    struct PhiEdge {
      ValuePtr<Block> block;
      ValuePtr<> value;
    };

    /**
     * \brief Phi node. These are used to unify values from different
     * predecessors on entry to a block.
     *
     * \sa http://en.wikipedia.org/wiki/Static_single_assignment_form
     */
    class Phi : public BlockMember {
      friend class Block;

    public:
      void add_edge(const ValuePtr<Block>& block, const ValuePtr<>& value);
      /// \brief Get incoming edge list
      const std::vector<PhiEdge>& edges() const {return m_edges;}
      
      /// \brief Get the value from a specific source block.
      ValuePtr<> incoming_value_from(const ValuePtr<Block>& block);

    private:
      Phi(Context *context, const ValuePtr<>& type, const ValuePtr<Block>& block);
      typedef boost::intrusive::list_member_hook<> PhiListHook;
      PhiListHook m_phi_list_hook;
      std::vector<PhiEdge> m_edges;
    };

    /**
     * \brief Block (list of instructions) inside a function. The
     * value of this term is the label used to jump to this block.
     */
    class Block : public Value {
      friend class FunctionTerm;

    public:
      typedef boost::intrusive::list<Instruction,
                                     boost::intrusive::member_hook<Instruction, Instruction::InstructionListHook, &Instruction::m_instruction_list_hook>,
                                     boost::intrusive::constant_time_size<false> > InstructionList;
      typedef boost::intrusive::list<Phi,
                                     boost::intrusive::member_hook<Phi, Phi::PhiListHook, &Phi::m_phi_list_hook>,
                                     boost::intrusive::constant_time_size<false> > PhiList;

      ValuePtr<Phi> insert_phi(const ValuePtr<>& type, const SourceLocation& location);
      void insert_instruction(const ValuePtr<Instruction>& insn, const ValuePtr<Instruction>& before=ValuePtr<Instruction>());

      InstructionList& instructions() {return m_instructions;}
      PhiList& phi_nodes() {return m_phi_nodes;}

      /** \brief Whether this block has been terminated so no more instructions can be added. */
      bool terminated() {return m_terminated;}
      /** \brief Get the function which contains this block. */
      ValuePtr<Function> function();
      /** \brief Get a pointer to the dominating block. */
      const ValuePtr<Block>& dominator() {return m_dominator;}
      /** \brief Get this block's catch list (this will be NULL for a regular block). */
      const ValuePtr<Block>& landing_pad() {return m_landing_pad;}
      
      /** \brief Get the list of blocks which this one can exit to (including exceptions) */
      std::vector<ValuePtr<Block> > successors();

      bool check_available(const ValuePtr<>& term, const ValuePtr<Instruction>& before=ValuePtr<Instruction>());
      bool dominated_by(const ValuePtr<Block>& block);
      
      static ValuePtr<Block> common_dominator(const ValuePtr<Block>&, const ValuePtr<Block>&);
      static bool isa_impl(const Value& v) {return v.term_type() == term_block;}

    private:
      Block(Context *context, const ValuePtr<Function>& function, const ValuePtr<Block>& dominator, bool);
      InstructionList m_instructions;
      PhiList m_phi_nodes;
      bool m_terminated;
      
      ValuePtr<Block> m_dominator;
      ValuePtr<Block> m_landing_pad;
    };

    class Function;

    class FunctionParameter : public Value {
      friend class Function;
    public:
      ValuePtr<Function> function();
      bool parameter_phantom() {return m_phantom;}
      
      static bool isa_impl(const Value& ptr) {
        return ptr.term_type() == term_function_parameter;
      }

    private:
      class Initializer;
      FunctionParameter(Context*, const ValuePtr<Function>& function, const ValuePtr<>& type, bool phantom);
      bool m_phantom;
    };

    /**
     * \brief Function.
     */
    class Function : public Global {
      friend class Module;
    public:
      typedef boost::unordered_multimap<Value*, std::string> TermNameMap;

      /**
       * Get the type of this function. This returns the raw function
       * type, whereas the actual type of this term is a pointer to
       * that type.
       */
      ValuePtr<FunctionType> function_type() const;

      std::size_t n_parameters() const;
      /** \brief Get a function parameter. */
      ValuePtr<FunctionParameter> parameter(std::size_t n) const;

      /**
       * Get the return type of this function, as viewed from inside the
       * function (i.e., with parameterized types replaced by parameters
       * to this function).
       */
      ValuePtr<> result_type() const;

      ValuePtr<Block> entry();
      void set_entry(const ValuePtr<Block>& block);

      ValuePtr<Block> new_block(const SourceLocation& location);
      ValuePtr<Block> new_block(const ValuePtr<Block>& dominator, const SourceLocation& location);
      ValuePtr<Block> new_landing_pad(const SourceLocation& location);
      ValuePtr<Block> new_landing_pad(const ValuePtr<Block>& dominator, const SourceLocation& location);

      void add_term_name(const ValuePtr<>& term, const std::string& name);
      const TermNameMap& term_name_map() {return m_name_map;}
      std::vector<ValuePtr<Block> > topsort_blocks();
      
      /**
       * \brief Get the exception handling personality of this function.
       * 
       * The exception handling personality is a string which is interpreted in an
       * unspecified way by the backend to distinguish different exception handling
       * styles for different languages.
       */
      const std::string& exception_personality() const {return m_exception_personality;}
      
      /**
       * \brief Set the exception handling personality of this function.
       * \see exception_personality()
       */
      void exception_personality(const std::string& v) {m_exception_personality = v;}

    private:
      Function(Context*, const ValuePtr<FunctionType>&, const std::string&, Module*);
      TermNameMap m_name_map;
      std::string m_exception_personality;
    };

    /**
     * \brief Term type appearing in dependent types of completed function types.
     */
    class FunctionTypeResolvedParameter : public SimpleOp {
      unsigned m_depth;
      unsigned m_index;
      
    public:
      /// \brief Get the depth of this parameter relative to the
      /// function type it is part of.
      ///
      /// If a parameter is used as the type of a parameter in its own
      /// functions argument list, then this is zero. For each function
      /// type it is then nested inside, this should increase by one.
      unsigned depth() const {return m_depth;}
      /// \brief Get the parameter number of this parameter in its
      /// function.
      unsigned index() const {return m_index;}
      
      static ValuePtr<FunctionTypeResolvedParameter> get(const ValuePtr<>& type, unsigned depth, unsigned index);
      
    private:
      FunctionTypeResolvedParameter(Context *context, const ValuePtr<>& type, unsigned depth, unsigned index);
    };

    /**
     * \brief Type of functions.
     * 
     * This is also used for implementing template types since the
     * template may be specialized by computing the result type of a
     * function call (note that there is no <tt>result_of</tt> term,
     * since the result must be the appropriate term itself).
     */
    class FunctionType : public HashableValue {
      friend class Context;
    public:
      CallingConvention calling_convention() {return m_calling_convention;}
      const ValuePtr<>& result_type() const {return m_result_type;}
      /// \brief Get the number of phantom parameters.
      unsigned n_phantom() const {return m_n_phantom;}
      /// \brief Get the vector parameter types.
      const std::vector<ValuePtr<> >& parameter_types() const {return m_parameter_types;}

      ValuePtr<> parameter_type_after(const std::vector<ValuePtr<> >& previous);
      ValuePtr<> result_type_after(const std::vector<ValuePtr<> >& parameters);
      
      static bool isa_impl(const Value& ptr) {return ptr.term_type() == term_function_type;}

    private:
      FunctionType(Context *context, CallingConvention calling_convention, const ValuePtr<>& result_type,
                   const std::vector<ValuePtr<> >& parameter_types, unsigned n_phantom);

      CallingConvention m_calling_convention;

      ValuePtr<> m_result_type;
      std::vector<ValuePtr<> > m_parameter_types;
      unsigned m_n_phantom;
    };

    class FunctionTypeParameter : public Value {
      friend class Context;
      FunctionTypeParameter(Context *context, const ValuePtr<>& type);
    };

    /**
     * Helper class for inserting instructions into blocks.
     */
    class InstructionInsertPoint {
      ValuePtr<Block> m_block;
      ValuePtr<Instruction> m_instruction;

    public:
      /**
       * Constructs an invalid inserter.
       */
      InstructionInsertPoint() : m_block(0), m_instruction(0) {}
      
      /**
       * Construct an inserter which inserts instructions at the end of
       * the specified block.
       * 
       * \param insert_at_end Block to append instructions to.
       */
      explicit InstructionInsertPoint(const ValuePtr<Block>& insert_at_end)
      : m_block(insert_at_end), m_instruction(0) {}
      
      /**
       * Construct an inserter which inserts instructions in the same
       * block as the specified instruction, just before it.
       * 
       * \param insert_before Instruction to insert created instructions
       * before.
       */
      explicit InstructionInsertPoint(const ValuePtr<Instruction>& insert_before)
      : m_block(insert_before->block()), m_instruction(insert_before) {}
      
      static InstructionInsertPoint after_source(const ValuePtr<>& source);
      
      void insert(const ValuePtr<Instruction>& instruction);
      
      /// \brief Block to insert instructions into
      const ValuePtr<Block>& block() const {return m_block;}

      /**
       * \brief Instruction to insert new instructions before
       * 
       * If this is NULL, instructions will be inserted at the end of the block.
       */
      const ValuePtr<Instruction>& instruction() const {return m_instruction;}
    };
  }
}

#endif
