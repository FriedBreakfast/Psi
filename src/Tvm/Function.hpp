#ifndef HPP_PSI_TVM_FUNCTION
#define HPP_PSI_TVM_FUNCTION

#include "Core.hpp"
#include "Functional.hpp"
#include "ValueList.hpp"

#include <boost/unordered_map.hpp>

namespace Psi {
  namespace Tvm {
    class FunctionType;
    class Block;    
    
    /**
     * \brief Base class for terms which belong to a block.
     */
    class PSI_TVM_EXPORT BlockMember : public Value {
      friend class Block;

    public:
      /// \brief Get the block this term is part of.
      ValuePtr<Block> block() {return ValuePtr<Block>(m_block);}
      /// \brief Get a raw pointer to the block this term is part of.
      Block* block_ptr() {return m_block;}
      /// \brief Get the function the block this is in is part of
      ValuePtr<Function> function();

      template<typename V> static void visit(V& v) {visit_base<Value>(v);}

      virtual Value* disassembler_source();

    protected:
      BlockMember(TermType term_type, const ValuePtr<>& type, const SourceLocation& location);
      
    private:
      template<typename T, boost::intrusive::list_member_hook<> T::*> friend class ValueList;
      void list_release() {m_block = NULL;}
      Block *m_block;
    };
    
    class PSI_TVM_EXPORT InstructionVisitor {
    public:
      virtual void next(ValuePtr<>& ptr) = 0;
    };
    
    class Instruction;
    
    class PSI_TVM_EXPORT InstructionVisitorWrapper : public ValuePtrVisitorBase<InstructionVisitorWrapper> {
      InstructionVisitor *m_callback;
    public:
      InstructionVisitorWrapper(InstructionVisitor *callback) : m_callback(callback) {}

      void visit_ptr(ValuePtr<>& ptr) {
        m_callback->next(ptr);
      }

      template<typename T> void visit_ptr(ValuePtr<T>& ptr) {
        ValuePtr<> copy(ptr);
        m_callback->next(copy);
        ptr = value_cast<T>(copy);
      }

      template<typename T> bool do_visit_base(VisitorTag<T>) {return !boost::is_same<T,Instruction>::value;}
    };

    /**
     * \brief Instruction term. Per-instruction funtionality is
     * created by implementing InstructionTermBackend and wrapping
     * that in InstructionTerm.
     */
    class PSI_TVM_EXPORT Instruction : public BlockMember {
      friend class Block;
      
    public:
      const char *operation_name() const {return m_operation;}
      virtual void instruction_visit(InstructionVisitor& visitor) = 0;
      
      void remove();
      
      static bool isa_impl(const Value& ptr) {return ptr.term_type() == term_instruction;}
      template<typename V> static void visit(V& v) {visit_base<BlockMember>(v);}
      
      virtual void type_check() = 0;

    protected:
      Instruction(const ValuePtr<>& type, const char *operation,
                  const SourceLocation& location);
      
      void require_available(const ValuePtr<>& value);
      virtual void check_source_hook(CheckSourceParameter& parameter);

    private:
      const char *m_operation;
      boost::intrusive::list_member_hook<> m_instruction_list_hook;
    };
    
    /**
     * \brief Base class for instructions which terminate blocks.
     */
    class PSI_TVM_EXPORT TerminatorInstruction : public Instruction {
    public:
      TerminatorInstruction(Context& context, const char *operation, const SourceLocation& location);
      
      virtual std::vector<ValuePtr<Block> > successors() = 0;
      
      static bool isa_impl(const Value& ptr);
      template<typename V> static void visit(V& v) {visit_base<Instruction>(v);}
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
    PSI_TVM_VALUE_DECL(Type) \
  public: \
    static const char operation[]; \
    virtual void type_check(); \
    virtual void instruction_visit(InstructionVisitor& callback); \
    template<typename V> static void visit(V& v); \
    static bool isa_impl(const Value& val) {return (val.term_type() == term_instruction) && (operation == static_cast<const Type&>(val).operation_name());} \
    
#define PSI_TVM_INSTRUCTION_IMPL(Type,Base,Name) \
    PSI_TVM_VALUE_IMPL(Type,Base) \
    \
    const char Type::operation[] = #Name; \
    \
    void Type::instruction_visit(InstructionVisitor& visitor) { \
      InstructionVisitorWrapper vw(&visitor); \
      boost::array<Type*,1> c = {{this}}; \
      visit_members(vw, c); \
    }

    /**
     * Describes incoming edges for Phi nodes.
     */
    struct PhiEdge {
      ValuePtr<Block> block;
      ValuePtr<> value;
    };

    template<typename V>
    void visit(V& v, VisitorTag<PhiEdge>) {
      v("block", &PhiEdge::block)
      ("value", &PhiEdge::value);
    }

    /**
     * \brief Phi node. These are used to unify values from different
     * predecessors on entry to a block.
     *
     * \sa http://en.wikipedia.org/wiki/Static_single_assignment_form
     */
    class PSI_TVM_EXPORT Phi : public BlockMember {
      PSI_TVM_VALUE_DECL(Phi)
      friend class Block;

    public:
      void add_edge(const ValuePtr<Block>& block, const ValuePtr<>& value);
      /// \brief Get incoming edge list
      const std::vector<PhiEdge>& edges() const {return m_edges;}
      
      /// \brief Get the value from a specific source block.
      ValuePtr<> incoming_value_from(const ValuePtr<Block>& block);

      void remove();

      static bool isa_impl(const Value& v) {return v.term_type() == term_phi;}
      template<typename V> static void visit(V& v);

    private:
      Phi(const ValuePtr<>& type, const SourceLocation& location);
      std::vector<PhiEdge> m_edges;
      boost::intrusive::list_member_hook<> m_phi_list_hook;
      virtual void check_source_hook(CheckSourceParameter& parameter);
    };

    /**
     * \brief Block (list of instructions) inside a function. The
     * value of this term is the label used to jump to this block.
     */
    class PSI_TVM_EXPORT Block : public Value {
      PSI_TVM_VALUE_DECL(Block);
      friend class Function;
      friend class Instruction;

    public:
      typedef ValueList<Instruction, &Instruction::m_instruction_list_hook> InstructionList;
      typedef ValueList<Phi, &Phi::m_phi_list_hook> PhiList;

      ValuePtr<Phi> insert_phi(const ValuePtr<>& type, const SourceLocation& location);
      void insert_instruction(const ValuePtr<Instruction>& insn, const ValuePtr<Instruction>& before=ValuePtr<Instruction>());
      
      void erase_phi(Phi& phi);
      void erase_instruction(Instruction& instruction);

      const InstructionList& instructions() const {return m_instructions;}
      const PhiList& phi_nodes() const {return m_phi_nodes;}

      /** \brief Whether this block has been terminated so no more instructions can be added. */
      bool terminated() {return !m_instructions.empty() && isa<TerminatorInstruction>(m_instructions.back());}
      /** \brief Get the function which contains this block. */
      ValuePtr<Function> function() {return ValuePtr<Function>(m_function);}
      /** \brief Get a raw pointer to the function which contains this block. */
      Function* function_ptr() {return m_function;}
      /** \brief Get a pointer to the dominating block. */
      const ValuePtr<Block>& dominator() {return m_dominator;}
      /** \brief Get this block's catch list (this will be NULL for a regular block). */
      const ValuePtr<Block>& landing_pad() {return m_landing_pad;}
      /// \brief Whether this block is a landing pad.
      bool is_landing_pad() const {return m_is_landing_pad;}
      
      std::vector<ValuePtr<Block> > successors();

      bool dominated_by(Block *block);
      /// \copydoc dominated_by(Block*)
      bool dominated_by(const ValuePtr<Block>& block) {return dominated_by(block.get());}
      bool same_or_dominated_by(Block *block);
      /// \copydoc same_or_dominated_by(Block*)
      bool same_or_dominated_by(const ValuePtr<Block>& block) {return same_or_dominated_by(block.get());}
      
      virtual Value* disassembler_source();
      
      static ValuePtr<Block> common_dominator(const ValuePtr<Block>&, const ValuePtr<Block>&);
      static bool isa_impl(const Value& v) {return v.term_type() == term_block;}
      
      template<typename V> static void visit(V& v);

    private:
      Block(Function *function, const ValuePtr<Block>& dominator,
            bool is_landing_pad, const ValuePtr<Block>& landing_pad,
            const SourceLocation& location);
      InstructionList m_instructions;
      PhiList m_phi_nodes;
      
      Function *m_function;
      ValuePtr<Block> m_dominator;
      ValuePtr<Block> m_landing_pad;
      
      bool m_is_landing_pad;
      template<typename T, boost::intrusive::list_member_hook<> T::*> friend class ValueList;
      boost::intrusive::list_member_hook<> m_block_list_hook;
      void list_release() {m_function = NULL;}
      virtual void check_source_hook(CheckSourceParameter& parameter);
    };
    
    inline ValuePtr<Function> BlockMember::function() {return m_block ? m_block->function() : ValuePtr<Function>();}

    class Function;

    class PSI_TVM_EXPORT FunctionParameter : public Value {
      PSI_TVM_VALUE_DECL(FunctionParameter);
      friend class Function;
    public:
      ValuePtr<Function> function() {return ValuePtr<Function>(m_function);}
      Function *function_ptr() {return m_function;}
      bool parameter_phantom() {return m_phantom;}
      
      static bool isa_impl(const Value& ptr) {
        return ptr.term_type() == term_function_parameter;
      }

      virtual Value* disassembler_source();

      template<typename V> static void visit(V& v);

    private:
      FunctionParameter(Context& context, Function *function, const ValuePtr<>& type, bool phantom, const SourceLocation& location);
      bool m_phantom;
      
      virtual void check_source_hook(CheckSourceParameter& parameter);

      void list_release() {m_function = NULL;}
      Function *m_function;
      boost::intrusive::list_member_hook<> m_parameter_list_hook;
      template<typename T, boost::intrusive::list_member_hook<> T::*> friend class ValueList;
    };

    /**
     * \brief Function.
     */
    class PSI_TVM_EXPORT Function : public Global {
      PSI_TVM_VALUE_DECL(Function);
      friend class Module;
    public:
      typedef boost::unordered_multimap<ValuePtr<>, std::string> TermNameMap;
      typedef ValueList<FunctionParameter, &FunctionParameter::m_parameter_list_hook> ParameterList;
      typedef ValueList<Block, &Block::m_block_list_hook> BlockList;

      /**
       * Get the type of this function. This returns the raw function
       * type, whereas the actual type of this term is a pointer to
       * that type.
       */
      ValuePtr<FunctionType> function_type() const;

      /** \brief Get the parameters for this funtion */
      const ParameterList& parameters() const {return m_parameters;}

      /**
       * Get the return type of this function, as viewed from inside the
       * function (i.e., with parameterized types replaced by parameters
       * to this function).
       */
      const ValuePtr<>& result_type() const {return m_result_type;}

      const BlockList& blocks() {return m_blocks;}

      ValuePtr<Block> new_block(const SourceLocation& location,
                                const ValuePtr<Block>& dominator=ValuePtr<Block>(),
                                const ValuePtr<Block>& landing_pad=ValuePtr<Block>());
      ValuePtr<Block> new_landing_pad(const SourceLocation& location,
                                const ValuePtr<Block>& dominator=ValuePtr<Block>(),
                                const ValuePtr<Block>& landing_pad=ValuePtr<Block>());

      void add_term_name(const ValuePtr<>& term, const std::string& name);
      const TermNameMap& term_name_map() {return m_name_map;}
      
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

      template<typename V> static void visit(V& v);
      static bool isa_impl(const Value& v) {return v.term_type() == term_function;}

    private:
      Function(Context& context, const ValuePtr<FunctionType>& type, const std::string& name,
               Module *module, const SourceLocation& location);

      TermNameMap m_name_map;
      std::string m_exception_personality;
      ValuePtr<> m_result_type;
      ParameterList m_parameters;
      BlockList m_blocks;
    };

    /**
     * \brief Term type appearing in dependent types of completed function types.
     */
    class PSI_TVM_EXPORT ResolvedParameter : public HashableValue {
      PSI_TVM_HASHABLE_DECL(ResolvedParameter)
      
    private:
      ValuePtr<> m_parameter_type;
      unsigned m_depth;
      unsigned m_index;
      
    public:
      ResolvedParameter(const ValuePtr<>& type, unsigned depth, unsigned index, const SourceLocation& location);

      static bool isa_impl(const Value& ptr) {return ptr.term_type() == term_resolved_parameter;}

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
    };
    
    template<typename T>
    struct ParameterTemplateType {
      ValuePtr<T> value;
      ParameterAttributes attributes;
      
      ParameterTemplateType() {}
      ParameterTemplateType(const ValuePtr<T>& value_) : value(value_) {}
      ParameterTemplateType(const ValuePtr<T>& value_, ParameterAttributes attributes_) : value(value_), attributes(attributes_) {}
      
      template<typename V>
      static void visit(V& v) {
        v("value", &ParameterTemplateType::value)
        ("attributes", &ParameterTemplateType::attributes);
      }
      
      friend std::size_t hash_value(const ParameterTemplateType<T>& self) {
        std::size_t h = 0;
        boost::hash_combine(h, self.value);
        boost::hash_combine(h, self.attributes);
        return h;
      }
    };
    
    template<typename T> bool operator == (const ParameterTemplateType<T>& lhs, const ParameterTemplateType<T>& rhs) {return (lhs.value==rhs.value) && (lhs.attributes==rhs.attributes);}
    template<typename T> bool operator != (const ParameterTemplateType<T>& lhs, const ParameterTemplateType<T>& rhs) {return !(lhs == rhs);}

    /**
     * \brief Type of functions.
     * 
     * This is also used for implementing template types since the
     * template may be specialized by computing the result type of a
     * function call (note that there is no <tt>result_of</tt> term,
     * since the result must be the appropriate term itself).
     */
    class PSI_TVM_EXPORT FunctionType : public HashableValue {
      PSI_TVM_HASHABLE_DECL(FunctionType)
      friend class Context;

    public:
      FunctionType(CallingConvention calling_convention, const ParameterType& result_type,
                   const std::vector<ParameterType>& parameter_types, unsigned n_phantom,
                   bool sret, const SourceLocation& location);

      CallingConvention calling_convention() const {return m_calling_convention;}
      const ParameterType& result_type() const {return m_result_type;}
      /// \brief Get the number of phantom parameters.
      unsigned n_phantom() const {return m_n_phantom;}
      /**
       * \brief Whether this has an sret parameter.
       * 
       * This means the last parameter is assumed to be a valid pointer to store the result
       * of the function to. It will also usually be passed as the first parameter according to
       * the platform calling convention.
       */
      bool sret() const {return m_sret;}
      /// \brief Get the vector parameter types.
      const std::vector<ParameterType>& parameter_types() const {return m_parameter_types;}

      ValuePtr<> parameter_type_after(const SourceLocation& location, const std::vector<ValuePtr<> >& previous);
      ValuePtr<> result_type_after(const SourceLocation& location, const std::vector<ValuePtr<> >& parameters);
      
      /// \brief Get the attributes of the nth parameter
      const ParameterAttributes& parameter_attributes(std::size_t n) const {return m_parameter_types[n].attributes;}
      /// \brief Get the return attributes
      const ParameterAttributes& result_attributes() const {return m_result_type.attributes;}
      
      static bool isa_impl(const Value& ptr) {return ptr.term_type() == term_function_type;}

    private:
      CallingConvention m_calling_convention;
      std::vector<ParameterType> m_parameter_types;
      unsigned m_n_phantom;
      bool m_sret;
      ParameterType m_result_type;
    };
    
    /**
     * \brief Type which implements the notion of unknown or partially known types.
     * 
     * Used to implement virtual function calls with a type safe derived parameter.
     * 
     * \note This is implemented next to FunctionType because they work in a
     * similar way, not because they are particularly conceptually related.
     */
    class PSI_TVM_EXPORT Exists : public HashableValue {
      PSI_TVM_HASHABLE_DECL(Exists)

    public:
      Exists(const ValuePtr<>& result, const std::vector<ValuePtr<> >& parameter_types, const SourceLocation& location);

      const ValuePtr<>& result() const {return m_result;}
      /// \brief Get the vector parameter types.
      const std::vector<ValuePtr<> >& parameter_types() const {return m_parameter_types;}

      ValuePtr<> parameter_type_after(const std::vector<ValuePtr<> >& previous);
      ValuePtr<> result_after(const std::vector<ValuePtr<> >& parameters);
      
      static bool isa_impl(const Value& ptr) {return ptr.term_type() == term_exists;}

    private:
      std::vector<ValuePtr<> > m_parameter_types;
      ValuePtr<> m_result;
    };
    
    /**
     * \brief Used to unwrap a value whose type is an exists term.
     */
    class PSI_TVM_EXPORT Unwrap : public FunctionalValue {
      PSI_TVM_FUNCTIONAL_DECL(Unwrap)
      
    public:
      Unwrap(const ValuePtr<>& value, const SourceLocation& location);

      /// \brief value must have an "exists" type
      const ValuePtr<>& value() const {return m_value;}
      
    private:
      ValuePtr<> m_value;
    };
    
    /**
     * \brief Used to replace exists parameters in unwrapped values.
     */
    class PSI_TVM_EXPORT UnwrapParameter : public FunctionalValue {
      PSI_TVM_FUNCTIONAL_DECL(UnwrapParameter)
      
    public:
      UnwrapParameter(const ValuePtr<>& value, unsigned index, const SourceLocation& location);
      
      /// \brief value must have an "exists" type
      const ValuePtr<>& value() const {return m_value;}
      /// \brief Index of exists parameter this corresponds to.
      unsigned index() const {return m_index;}
      
      friend void hashable_check_source_hook(UnwrapParameter& self, CheckSourceParameter& parameter);

    private:
      ValuePtr<> m_value;
      unsigned m_index;
    };
    
    /**
     * \brief Introduce exists quantification to a value.
     */
    class PSI_TVM_EXPORT IntroduceExists : public FunctionalValue {
      PSI_TVM_FUNCTIONAL_DECL(IntroduceExists)
      
    public:
      IntroduceExists(const ValuePtr<>& exists_type, const ValuePtr<>& value, const SourceLocation& location);
      
      /// \brief Get the expected type of this term
      const ValuePtr<>& exists_type() const {return m_exists_type;}
      /// \brief Get the value of this term
      const ValuePtr<>& value() const {return m_value;}
      
    private:
      ValuePtr<> m_exists_type;
      ValuePtr<> m_value;
    };
    
    /**
     * \brief Placeholder used for parameters during type setup.
     * 
     * This is used by function type, recursive and exists.
     */
    class PSI_TVM_EXPORT ParameterPlaceholder : public Value {
      PSI_TVM_VALUE_DECL(ParameterPlaceholder);
    private:
      friend class Context;
      ParameterPlaceholder(Context& context, const ValuePtr<>& type, const SourceLocation& location);
      ValuePtr<> m_parameter_type;
      virtual void check_source_hook(CheckSourceParameter& parameter);
    public:
      static bool isa_impl(const Value& ptr) {return ptr.term_type() == term_parameter_placeholder;}
      template<typename V> static void visit(V& v);
      Value* disassembler_source();
    };

    /**
     * Helper class for inserting instructions into blocks.
     */
    class PSI_TVM_EXPORT InstructionInsertPoint {
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
