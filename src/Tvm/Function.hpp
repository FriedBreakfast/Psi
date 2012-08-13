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
      /// \brief Get the function the block this is in is part of
      ValuePtr<Function> function();

      template<typename V> static void visit(V& v) {visit_base<Value>(v);}

    protected:
      BlockMember(TermType term_type, const ValuePtr<>& type,
                  Value* source, const SourceLocation& location);
      
      ValuePtr<Block> m_block;
    };
    
    class InstructionVisitor {
    public:
      virtual void next(ValuePtr<>& ptr) = 0;
    };
    
    class Instruction;
    
    class InstructionVisitorWrapper : public ValuePtrVistorBase<InstructionVisitorWrapper> {
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
    class Instruction : public BlockMember {
      friend class Block;
      
    public:
      const char *operation_name() const {return m_operation;}
      virtual void instruction_visit(InstructionVisitor& visitor) = 0;
      
      static bool isa_impl(const Value& ptr) {return ptr.term_type() == term_instruction;}
      template<typename V> static void visit(V& v) {visit_base<BlockMember>(v);}
      
      virtual void type_check() = 0;

    protected:
      Instruction(const ValuePtr<>& type, const char *operation,
                  const SourceLocation& location);

    private:
      const char *m_operation;
      typedef boost::intrusive::list_member_hook<> InstructionListHook;
      InstructionListHook m_instruction_list_hook;
    };
    
    /**
     * \brief Base class for instructions which terminate blocks.
     */
    class TerminatorInstruction : public Instruction {
    protected:
      void check_dominated(const ValuePtr<Block>& target);

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
    static bool isa_impl(const Value& val) {return (val.term_type() == term_instruction) && (operation == static_cast<const Type&>(val).operation_name());}
    
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
    class Phi : public BlockMember {
      PSI_TVM_VALUE_DECL(Phi)
      friend class Block;

    public:
      void add_edge(const ValuePtr<Block>& block, const ValuePtr<>& value);
      /// \brief Get incoming edge list
      const std::vector<PhiEdge>& edges() const {return m_edges;}
      
      /// \brief Get the value from a specific source block.
      ValuePtr<> incoming_value_from(const ValuePtr<Block>& block);
      
      template<typename V> static void visit(V& v);

    private:
      Phi(const ValuePtr<>& type, const SourceLocation& location);
      typedef boost::intrusive::list_member_hook<> PhiListHook;
      PhiListHook m_phi_list_hook;
      std::vector<PhiEdge> m_edges;
    };

    /**

     * \brief Block (list of instructions) inside a function. The
     * value of this term is the label used to jump to this block.
     */
    class Block : public Value {
      PSI_TVM_VALUE_DECL(Block);
      friend class Function;

    public:
      typedef std::vector<ValuePtr<Instruction> > InstructionList;
      typedef std::vector<ValuePtr<Phi> > PhiList;

      ValuePtr<Phi> insert_phi(const ValuePtr<>& type, const SourceLocation& location);
      void insert_instruction(const ValuePtr<Instruction>& insn, const ValuePtr<Instruction>& before=ValuePtr<Instruction>());

      const InstructionList& instructions() const {return m_instructions;}
      const PhiList& phi_nodes() const {return m_phi_nodes;}

      /** \brief Whether this block has been terminated so no more instructions can be added. */
      bool terminated() {return m_terminated;}
      /** \brief Get the function which contains this block. */
      const ValuePtr<Function>& function() {return m_function;}
      /** \brief Get a pointer to the dominating block. */
      const ValuePtr<Block>& dominator() {return m_dominator;}
      /** \brief Get this block's catch list (this will be NULL for a regular block). */
      const ValuePtr<Block>& landing_pad() {return m_landing_pad;}
      /// \brief Whether this block is a landing pad.
      bool is_landing_pad() const {return m_is_landing_pad;}
      
      std::vector<ValuePtr<Block> > successors();

      bool check_available(const ValuePtr<>& term, const ValuePtr<Instruction>& before=ValuePtr<Instruction>());
      bool dominated_by(const ValuePtr<Block>& block);
      
      static ValuePtr<Block> common_dominator(const ValuePtr<Block>&, const ValuePtr<Block>&);
      static bool isa_impl(const Value& v) {return v.term_type() == term_block;}
      
      template<typename V> static void visit(V& v);

    private:
      Block(const ValuePtr<Function>& function, const ValuePtr<Block>& dominator,
            bool is_landing_pad, const ValuePtr<Block>& landing_pad,
            const SourceLocation& location);
      InstructionList m_instructions;
      PhiList m_phi_nodes;
      
      ValuePtr<Function> m_function;
      ValuePtr<Block> m_dominator;
      ValuePtr<Block> m_landing_pad;
      
      bool m_is_landing_pad;
      bool m_terminated;
    };
    
    inline ValuePtr<Function> BlockMember::function() {return m_block ? m_block->function() : ValuePtr<Function>();}

    class Function;

    class FunctionParameter : public Value {
      PSI_TVM_VALUE_DECL(FunctionParameter);
      friend class Function;
    public:
      const ValuePtr<Function>& function() {return m_function;}
      bool parameter_phantom() {return m_phantom;}
      
      static bool isa_impl(const Value& ptr) {
        return ptr.term_type() == term_function_parameter;
      }

      template<typename V> static void visit(V& v);

    private:
      FunctionParameter(Context& context, const ValuePtr<Function>& function, const ValuePtr<>& type, bool phantom, const SourceLocation& location);
      bool m_phantom;
      ValuePtr<Function> m_function;
    };

    /**
     * \brief Function.
     */
    class Function : public Global {
      PSI_TVM_VALUE_DECL(Function);
      friend class Module;
    public:
      typedef boost::unordered_multimap<ValuePtr<>, std::string> TermNameMap;

      /**
       * Get the type of this function. This returns the raw function
       * type, whereas the actual type of this term is a pointer to
       * that type.
       */
      ValuePtr<FunctionType> function_type() const;

      std::size_t n_parameters() const {return m_parameters.size();}
      /** \brief Get a function parameter. */
      const ValuePtr<FunctionParameter>& parameter(std::size_t n) const {return m_parameters[n];}

      /**
       * Get the return type of this function, as viewed from inside the
       * function (i.e., with parameterized types replaced by parameters
       * to this function).
       */
      const ValuePtr<>& result_type() const {return m_result_type;}

      const ValuePtr<Block>& entry() {return m_entry;}
      void set_entry(const ValuePtr<Block>& block);

      ValuePtr<Block> new_block(const SourceLocation& location,
                                const ValuePtr<Block>& dominator=ValuePtr<Block>(),
                                const ValuePtr<Block>& landing_pad=ValuePtr<Block>());
      ValuePtr<Block> new_landing_pad(const SourceLocation& location,
                                const ValuePtr<Block>& dominator=ValuePtr<Block>(),
                                const ValuePtr<Block>& landing_pad=ValuePtr<Block>());

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

      template<typename V> static void visit(V& v);

    private:
      Function(Context& context, const ValuePtr<FunctionType>& type, const std::string& name,
               Module *module, const SourceLocation& location);
      void create_parameters();

      TermNameMap m_name_map;
      std::string m_exception_personality;
      ValuePtr<Block> m_entry;
      std::vector<ValuePtr<FunctionParameter> > m_parameters;
      ValuePtr<> m_result_type;
    };

    /**
     * \brief Term type appearing in dependent types of completed function types.
     */
    class FunctionTypeResolvedParameter : public FunctionalValue {
      PSI_TVM_FUNCTIONAL_DECL(FunctionTypeResolvedParameter)
      
    private:
      ValuePtr<> m_parameter_type;
      unsigned m_depth;
      unsigned m_index;
      
    public:
      FunctionTypeResolvedParameter(const ValuePtr<>& type, unsigned depth, unsigned index, const SourceLocation& location);

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
      
      static ValuePtr<FunctionTypeResolvedParameter> get(const ValuePtr<>& type, unsigned depth, unsigned index, const SourceLocation& location);
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
      PSI_TVM_HASHABLE_DECL(FunctionType)
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
      FunctionType(CallingConvention calling_convention, const ValuePtr<>& result_type,
                   const std::vector<ValuePtr<> >& parameter_types, unsigned n_phantom,
                   const SourceLocation& location);

      CallingConvention m_calling_convention;
      std::vector<ValuePtr<> > m_parameter_types;
      unsigned m_n_phantom;
      ValuePtr<> m_result_type;
    };
    
    class FunctionTypeParameter : public Value {
      PSI_TVM_VALUE_DECL(FunctionTypeParameter);
    private:
      friend class Context;
      FunctionTypeParameter(Context& context, const ValuePtr<>& type, const SourceLocation& location);
      ValuePtr<> m_parameter_type;
    public:
      static bool isa_impl(const Value& ptr) {return ptr.term_type() == term_function_type_parameter;}
      template<typename V> static void visit(V& v);
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
