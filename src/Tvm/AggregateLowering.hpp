#ifndef HPP_PSI_TVM_AGGREGATELOWERING
#define HPP_PSI_TVM_AGGREGATELOWERING

#include "Core.hpp"
#include "Function.hpp"
#include "Aggregate.hpp"
#include "Instructions.hpp"
#include "InstructionBuilder.hpp"
#include "ModuleRewriter.hpp"
#include "../SharedMap.hpp"

#include <boost/unordered_map.hpp>
#include <boost/variant.hpp>
#include <boost/make_shared.hpp>

namespace Psi {
  namespace Tvm {
    class LoweredType {
    public:
      typedef std::vector<LoweredType> EntryVector;
      
    private:
      bool m_global;
      ValuePtr<> m_origin;
      ValuePtr<> m_size, m_alignment;
      typedef boost::shared_ptr<EntryVector> EntryVectorPtr;
      typedef boost::variant<boost::blank, ValuePtr<>, EntryVectorPtr> VariantType;
      VariantType m_type;
      
    public:
      LoweredType() : m_global(false) {}
      /// \brief Constructor for unknown types (including non-primitive unions)
      LoweredType(const ValuePtr<>& origin, bool global, const ValuePtr<>& size, const ValuePtr<>& alignment)
      : m_global(global), m_origin(origin), m_size(size), m_alignment(alignment) {}
      /// \brief Constructor for primitive types
      LoweredType(const ValuePtr<>& origin, bool global, const ValuePtr<>& size, const ValuePtr<>& alignment, const ValuePtr<>& type)
      : m_global(global), m_origin(origin), m_size(size), m_alignment(alignment), m_type(type) {}
      /// \brief Constructor for split types
      LoweredType(const ValuePtr<>& origin, bool global, const ValuePtr<>& size, const ValuePtr<>& alignment, const EntryVector& entries)
      : m_global(global), m_origin(origin), m_size(size), m_alignment(alignment), m_type(boost::make_shared<EntryVector>(entries)) {}
      
      
      /// \brief Is this value NULL
      bool empty() const {return !m_size;}
      /// \brief Is this representable by a primitive type
      bool primitive() const {return boost::get<ValuePtr<> >(&m_type);}
      /// \brief Is this representable by a list of types
      bool split() const {return boost::get<EntryVectorPtr>(&m_type);}
      /// \brief Is this a global type?
      bool global() const {return m_global;}
      
      /// \brief Type from which this type was lowered (in the original context)
      const ValuePtr<>& origin() const {return m_origin;}

      /// \brief Get the type used to represent this type in a register
      const ValuePtr<>& register_type() const {const ValuePtr<> *p = boost::get<ValuePtr<> >(&m_type); PSI_ASSERT(p); return *p;}
      
      /// \brief Get the entries used to represent this type, which is split
      const EntryVector& entries() const {const EntryVectorPtr *v = boost::get<EntryVectorPtr>(&m_type); PSI_ASSERT(v); return **v;}
      
      /// \brief Get the size of this type in a suitable form for later passes
      const ValuePtr<>& size() const {return m_size;}
      
      /// \brief Get the alignment of this type in a suitable form for later passes
      const ValuePtr<>& alignment() const {return m_alignment;}
    };
    
    /**
      * \brief Per-term result of aggregate lowering.
      * 
      * The lowering pass will try to split aggregate operations
      * into simpler operations on scalars, so a value can be
      * represented in one of several ways depending on whether
      * it is an aggregate or not.
      */
    class LoweredValue {
    public:
      typedef std::vector<LoweredValue> EntryVector;

    private:
      bool m_global;
      ValuePtr<> m_type;
      struct RegisterValue {ValuePtr<> x; RegisterValue(const ValuePtr<>& x_) : x(x_) {}};
      struct StackValue {ValuePtr<> x; StackValue(const ValuePtr<>& x_) : x(x_) {}};
      struct ZeroValue {};
      struct UndefinedValue {};
      typedef boost::shared_ptr<EntryVector> EntryVectorPtr;
      typedef boost::variant<boost::blank, RegisterValue, StackValue, EntryVectorPtr, ZeroValue, UndefinedValue> ValueType;
      ValueType m_value;
      
      LoweredValue(bool global, const ValuePtr<>& type, const ValuePtr<>& value, boost::mpl::true_) : m_global(global), m_type(type), m_value(RegisterValue(value)) {}
      LoweredValue(bool global, const ValuePtr<>& type, const ValuePtr<>& value, boost::mpl::false_) : m_global(global), m_type(type), m_value(StackValue(value)) {}
      LoweredValue(bool global, const ValuePtr<>& type, ZeroValue) : m_global(global), m_type(type), m_value(ZeroValue()) {}
      LoweredValue(bool global, const ValuePtr<>& type, UndefinedValue) : m_global(global), m_type(type), m_value(UndefinedValue()) {}

    public:
      LoweredValue() {}
      LoweredValue(const ValuePtr<>& type, bool global, const EntryVector& entries) : m_global(global), m_type(type), m_value(boost::make_shared<EntryVector>(entries)) {}
      
      /// \brief Construct a lowered value with a single entry in a register
      static LoweredValue primitive(bool global, const ValuePtr<>& value) {return LoweredValue(global, ValuePtr<>(), value, boost::mpl::true_());}
      /// \brief Construct a lowered value with a single entry in a register
      static LoweredValue register_(const ValuePtr<>& type, bool global, const ValuePtr<>& value) {return LoweredValue(global, type, value, boost::mpl::true_());}
      /// \brief Construct a lowered value with a single entry on the stack
      static LoweredValue stack(const ValuePtr<>& type, bool global, const ValuePtr<>& value) {return LoweredValue(global, type, value, boost::mpl::false_());}
      /// \brief Construct a lowered value which is zero
      static LoweredValue zero(const ValuePtr<>& type, bool global) {return LoweredValue(global, type, ZeroValue());}
      /// \brief Construct a lowered value which is zero
      static LoweredValue zero(const LoweredType& type) {return LoweredValue(type.global(), type.origin(), ZeroValue());}
      /// \brief Construct a lowered value which is undefined
      static LoweredValue undefined(const ValuePtr<>& type, bool global) {return LoweredValue(global, type, UndefinedValue());}
      /// \brief Construct a lowered value which is undefined
      static LoweredValue undefined(const LoweredType& type) {return LoweredValue(type.global(), type.origin(), UndefinedValue());}
      
      /// \brief Whether this value is global
      bool global() const {return m_global;}
      /// \brief The original type of this value.
      const ValuePtr<>& type() const {return m_type;}
      
      /**
       * Storage mode of a value.
       * 
       * Note that the numerical values of this enumeration must match the order of the
       * entries in ValueType.
       */
      enum Mode {
        mode_empty=0,
        mode_register=1,
        mode_stack=2,
        mode_split=3,
        mode_zero=4,
        mode_undefined=5
      };
      
      /// \brief Get the storage mode of this value
      Mode mode() const {return static_cast<Mode>(m_value.which());}
      /// \brief Whether this value is empty
      bool empty() const {return mode() == mode_empty;}
      
      /// \brief Get value of data allocated in register
      const ValuePtr<>& register_value() const {const RegisterValue *ptr = boost::get<RegisterValue>(&m_value); PSI_ASSERT(ptr); return ptr->x;}
      /// \brief Get value of data allocated on stack
      const ValuePtr<>& stack_value() const {const StackValue *ptr = boost::get<StackValue>(&m_value); PSI_ASSERT(ptr); return ptr->x;}
      /// \brief Get list of entries
      const EntryVector& entries() const {const EntryVectorPtr *ptr = boost::get<EntryVectorPtr>(&m_value); PSI_ASSERT(ptr); return **ptr;}
    };
    
    /**
     * Specialized LoweredValue-like structure which only contains a value and a flag indicating
     * whether the value is global or not.
     */
    struct LoweredValueRegister {
      LoweredValueRegister() : global(true) {}
      LoweredValueRegister(bool global_, const ValuePtr<>& value_) : global(global_), value(value_) {}
      bool global;
      ValuePtr<> value;
    };
    
    /**
     * A function pass which removes aggregate operations by rewriting
     * them in terms of pointer offsets, so that later stages need not
     * handle them.
     * 
     * A note on pointer handling: I do not traverse pointer types during
     * type lowering. This breaks a potentially hazardous reference cycle
     * introduced by having dependent types: it is possible to have a
     * type such as:
     * 
     * \code r = recursive (%n:iptr) > pointer (apply r (add %n #ip1)); \endcode
     * 
     * This would lead to an infinite recursion. On the stack, pointers may
     * be byte pointers or other pointer types: I do not generate type casts
     * before they are required so the pointed-to type should not be depended
     * upon.
     */
    class AggregateLoweringPass : public ModuleRewriter {
    public:
      class ElementOffsetGenerator;
      class FunctionRunner;
      class GlobalVariableRunner;
      
      class AggregateLoweringRewriter {
        friend class FunctionRunner;
        friend class GlobalVariableRunner;
        friend class AggregateLoweringPass;
        
        AggregateLoweringPass *m_pass;
        
        typedef SharedMap<ValuePtr<>, LoweredType> TypeMapType;
        typedef SharedMap<ValuePtr<>, LoweredValue> ValueMapType;

        TypeMapType m_type_map;
        ValueMapType m_value_map;
        
      public:
        AggregateLoweringRewriter(AggregateLoweringPass*);
        
        /// \brief Get the pass object this belongs to
        AggregateLoweringPass& pass() {return *m_pass;}
        
        /// \brief Get the (target) context this pass belongs to
        Context& context() {return pass().context();}

        /**
         * Work out the expected form of a type after this pass.
         */
        virtual LoweredType rewrite_type(const ValuePtr<>& type) = 0;
        
        /**
         * Rewrite a value for later passes.
         */
        virtual LoweredValue rewrite_value(const ValuePtr<>& value) = 0;
        
        /**
         * \brief \em Load a value.
         * 
         * Given a pointer to a value, this returns a Value corresponding to the
         * data at that pointer.
         */
        virtual LoweredValue load_value(const LoweredType& type, const LoweredValueRegister& ptr, const SourceLocation& location) = 0;
        
        /**
         * \brief \em Store a value.
         * 
         * This stores the value to memory, also creating the memory to store the
         * value in. If the value is a constant it will be created as a global;
         * if it is a variable it will be created using the alloca instruction.
         */
        virtual ValuePtr<> store_value(const ValuePtr<>& value, const SourceLocation& location) = 0;
        
        /**
         * \brief \em Store a type value.
         * 
         * This stores the value (size and alignment) of a type to memory, also
         * creating the memory to store the value in. This is distinct from
         * store_value since \c size and \c alignment are values in the
         * rewritten, not original, module.
         * 
         * \param size Size of the type (this value should belong to the target
         * context).
         * 
         * \param alignment Alignment of the type (this value should belong to
         * the target context).
         */
        virtual ValuePtr<> store_type(const ValuePtr<>& size, const ValuePtr<>& alignment, const SourceLocation& location) = 0;
        
        /**
         * \brief Construct a value with a given type by bit-wise conversion from an existing value.
         * 
         * Any data not required by \c type but not specified by \c value is undefined, including padding bytes
         * and bytes at the end.
         * 
         * \param type Type to conert to, in the source context.
         * \param value Lowered value to convert.
         */
        virtual LoweredValue bitcast(const ValuePtr<>& type, const LoweredValue& value, const SourceLocation& location) = 0;

        LoweredType lookup_type(const ValuePtr<>& type);

        LoweredValueRegister rewrite_value_register(const ValuePtr<>&);
        LoweredValueRegister rewrite_value_ptr(const ValuePtr<>&);
        LoweredValue lookup_value(const ValuePtr<>&);
        LoweredValueRegister lookup_value_register(const ValuePtr<>&);
        LoweredValueRegister lookup_value_ptr(const ValuePtr<>&);
      };

      /**
       * Class which actually runs the pass, and holds per-run data.
       */
      class FunctionRunner : public AggregateLoweringRewriter {
        ValuePtr<Function> m_old_function, m_new_function;
        InstructionBuilder m_builder;
        
        struct BlockPhiData {
          // Phi nodes derived from Phi nodes in the original function
          std::vector<ValuePtr<Phi> > user;
          // Phi nodes generated as a replacement for alloca
          std::vector<ValuePtr<Phi> > alloca_;
          // List of used slots
          std::vector<ValuePtr<> > used;
          // List of free slots
          std::vector<ValuePtr<> > free_;
        };
        
        typedef boost::unordered_map<ValuePtr<Block>, BlockPhiData> BlockPhiMapType;
        typedef boost::unordered_map<ValuePtr<>, BlockPhiMapType> TypePhiMapType;
        
        TypePhiMapType m_generated_phi_terms;
        
        LoweredValue create_phi_node(const ValuePtr<Block>& block, const LoweredType& type, const SourceLocation& location);
        void populate_phi_node(const ValuePtr<>&, const std::vector<PhiEdge>& edges);
        ValuePtr<> create_storage(const ValuePtr<>& type, const SourceLocation& location);
        ValuePtr<> create_alloca(const ValuePtr<>& type, const SourceLocation& location);
        void create_phi_alloca_terms(const std::vector<std::pair<ValuePtr<Block>, ValuePtr<Block> > >& sorted_blocks);
        
      public:        
        FunctionRunner(AggregateLoweringPass *pass, const ValuePtr<Function>& function);
        virtual void run();
        
        /// \brief Get the new version of the function
        const ValuePtr<Function>& new_function() {return m_new_function;}
        
        /// \brief Get the function being rebuilt
        const ValuePtr<Function>& old_function() {return m_old_function;}
        
        /// \brief Return an InstructionBuilder set to the current instruction insert point.
        InstructionBuilder& builder() {return m_builder;}
        
        void add_mapping(const ValuePtr<>& source, const LoweredValue& target);
        ValuePtr<Block> rewrite_block(const ValuePtr<Block>&);
        
        virtual LoweredValue load_value(const LoweredType& type, const LoweredValueRegister& ptr, const SourceLocation& location);
        LoweredValue load_value(const LoweredType& type, const ValuePtr<>& ptr, const SourceLocation& location);
        virtual ValuePtr<> store_value(const ValuePtr<>& value, const SourceLocation& location);
        virtual ValuePtr<> store_type(const ValuePtr<>& size, const ValuePtr<>& alignment, const SourceLocation& location);
        void store_value(const ValuePtr<>& value, const ValuePtr<>& ptr, const SourceLocation& location);

        virtual LoweredType rewrite_type(const ValuePtr<>&);
        virtual LoweredValue rewrite_value(const ValuePtr<>&);
        virtual LoweredValue bitcast(const ValuePtr<>& type, const LoweredValue& value, const SourceLocation& location);
      };
      
      class ModuleLevelRewriter : public AggregateLoweringRewriter {
      public:
        ModuleLevelRewriter(AggregateLoweringPass*);
        virtual LoweredValue load_value(const LoweredType& type, const LoweredValueRegister& ptr, const SourceLocation& location);
        virtual ValuePtr<> store_value(const ValuePtr<>&, const SourceLocation& location);
        virtual ValuePtr<> store_type(const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
        virtual LoweredValue bitcast(const ValuePtr<>& type, const LoweredValue& input, const SourceLocation& location);
        virtual LoweredType rewrite_type(const ValuePtr<>&);
        virtual LoweredValue rewrite_value(const ValuePtr<>&);
      };
      
      struct TypeSizeAlignment {
        ValuePtr<> size;
        ValuePtr<> alignment;
      };

      /**
       * Interface for lowering function calls and function types by
       * removing aggregates from the IR. This is necessarily target
       * specific, so must be done by callbacks.
       */
      struct TargetCallback {
        /**
         * Cast the type of function being called and adjust the
         * parameters to remove aggregates.
         * 
         * \param runner Holds per-pass data.
         */
        virtual void lower_function_call(FunctionRunner& runner, const ValuePtr<Call>& term) = 0;

        /**
         * Create a return instruction to return the given value
         * according to the target-specific lowering routine.
         * 
         * \param value Value to return.
         * 
         * \return The actual return instruction created.
         */
        virtual ValuePtr<Instruction> lower_return(FunctionRunner& runner, const ValuePtr<>& value, const SourceLocation& location) = 0;

        /**
         * Change a function's type and add entry code to decode
         * parameters into aggregates in the simplest possible way. The
         * remaining aggregates lowering code will handle the rest.
         * 
         * The returned function need not have linkage set correctly,
         * since this will always be the same as the source function it
         * can be handled by the generic lowering code.
         * 
         * \param runner Holds per-pass data.
         * 
         * \param name Name of function to create.
         * 
         * \param function Function being lowered.
         */
        virtual ValuePtr<Function> lower_function(AggregateLoweringPass& runner, const ValuePtr<Function>& function) = 0;
        
        /**
         * Create necessary entry code into a function to convert low level
         * parameter format.
         * 
         * \param source_function Original function.
         * 
         * \param target_function Newly created function.
         */
        virtual void lower_function_entry(FunctionRunner& runner, const ValuePtr<Function>& source_function, const ValuePtr<Function>& target_function) = 0;
        
        /**
         * \brief Convert a value to another type.
         * 
         * This function must simulate a store/load pair on the
         * target system, storing \c value as its actual type and
         * then loading it back as an instance of \c type, and
         * return the result.
         * 
         * Note that the size of the type of \c value need not equal
         * the size of \c type and is in fact unlikely to since this
         * is used to implement accessing different members of unions.
         * 
         * \param value Value to be converted.
         * 
         * \param type Type of value to return.
         */
        virtual ValuePtr<> convert_value(const ValuePtr<>& value, const ValuePtr<>& type) = 0;

        /**
         * \brief Get the type with alignment closest to the specified alignment.
         * 
         * \return A type with an alignment is less than or equal to \c aligment.
         * Its size must be the same as its alignment. The first member of the
         * pair is the type, the second member of the pair is the size of the first.
         */
        virtual std::pair<ValuePtr<>,ValuePtr<> > type_from_alignment(const ValuePtr<>& alignment) = 0;
        
        /**
         * \brief Get the size and alignment of a type.
         * 
         * This should only be used for primitive types (note that this includes
         * pointers). Aggregate types are handled internally by AggregateLoweringPass.
         * 
         * \param type A primitive type.
         */
        virtual TypeSizeAlignment type_size_alignment(const ValuePtr<>& type) = 0;
      };

    private:
      struct TypeTermRewriter;
      struct FunctionalTermRewriter;
      struct InstructionTermRewriter;
      ModuleLevelRewriter m_global_rewriter;

      struct GlobalBuildStatus {
        GlobalBuildStatus(Context& context, const SourceLocation& location);
        GlobalBuildStatus(const ValuePtr<>& element, const ValuePtr<>& element_size_, const ValuePtr<>& element_alignment_, const ValuePtr<>& size_, const ValuePtr<>& alignment_);
        std::vector<ValuePtr<> > elements;
        /// \brief Size of the entries in elements as a sequence (not including end padding to reach a multiple of alignment)
        ValuePtr<> elements_size;
        /// \brief Actual alignment of the first element in this set
        ValuePtr<> first_element_alignment;
        /// \brief Largest alignment of any elements in this set
        ValuePtr<> max_element_alignment;
        /// \brief Desired size of this set of elements.
        ValuePtr<> size;
        /// \brief Desired alignment of this set of elements.
        ValuePtr<> alignment;
      };
      
      GlobalBuildStatus rewrite_global_type(const ValuePtr<>&);
      GlobalBuildStatus rewrite_global_value(const ValuePtr<>&);
      void global_append(GlobalBuildStatus&, const GlobalBuildStatus&, bool, const SourceLocation& location);
      void global_pad_to_size(GlobalBuildStatus&, const ValuePtr<>&, const ValuePtr<>&, bool, const SourceLocation& location);
      void global_group(GlobalBuildStatus&, bool, const SourceLocation& location);
      virtual void update_implementation(bool);

    public:
      AggregateLoweringPass(Module*, TargetCallback*, Context* =0);

      /// \brief Get the (target) context of this pass
      Context& context() {return target_module()->context();}
      
      ModuleLevelRewriter& global_rewriter() {return m_global_rewriter;}

      /**
       * Callback used to rewrite function types and function calls
       * in a system-specific way.
       */
      TargetCallback *target_callback;

      /**
       * Whether to only rewrite aggregate operations which act on types
       * whose binary representation is not fully known. \c
       * remove_all_unions affects the behaviour of this option, since
       * if \c remove_all_unions is true \em any type containing a union
       * is considered not fully known.
       */
      bool remove_only_unknown;

      /// Whether to replace all unions in the IR with pointer operations
      bool remove_all_unions;
      
      /**
       * Whether all arrays on the stack should be rewritten as an
       * alloca'd pointer.
       */
      bool remove_register_arrays;
      
      /**
       * Whether instances of \c sizeof and \c alignof on aggregate types
       * should be replaced using explicit calculations.
       * 
       * For primitive types, the target can alter behaviour by changing
       * TargetCallback::type_size_alignment.
       * 
       * Note that if \c remove_only_unknown is not set all aggregate types are
       * removed anyway, so the value of this flag is irrelevent.
       */
      bool remove_sizeof;
      
      /**
       * Force all pointer arithmetic to take place on byte pointers.
       */
      bool pointer_arithmetic_to_bytes;
      
      /**
       * Whether globals should still be represented hierarchically
       * or just using a single level of values.
       * 
       * If this is set to true, all global variable types will be
       * either primitive or a single struct containing only
       * primitive types.
       */
      bool flatten_globals;
    };
  }
}

#endif
