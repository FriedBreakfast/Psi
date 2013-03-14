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
#include <boost/unordered_set.hpp>
#include <boost/make_shared.hpp>

#include <set>

namespace Psi {
  namespace Tvm {
    /**
     * \brief Size and alignment of a type after lowering.
     */
    struct TypeSizeAlignment {
      std::size_t size;
      std::size_t alignment;
    };
    
    class PSI_TVM_EXPORT LoweredType {
    public:
      typedef std::vector<LoweredType> EntryVector;
      
      enum Mode {
        mode_empty,
        mode_register,
        mode_split,
        mode_blob
      };
      
    private:
#ifdef PSI_DEBUG
      static bool all_global(const EntryVector& v) {
        for (EntryVector::const_iterator ii = v.begin(), ie = v.end(); ii != ie; ++ii) {
          if (!ii->global())
            return false;
        }
        return true;
      }
#endif
      
      struct Base : CheckedCastBase {Mode mode; ValuePtr<> origin; ValuePtr<> size; ValuePtr<> alignment;
      Base(Mode mode_, const ValuePtr<>& origin_, const ValuePtr<>& size_, const ValuePtr<>& alignment_)
      : mode(mode_), origin(origin_), size(size_), alignment(alignment_) {}};
      struct RegisterType : Base {ValuePtr<> register_type; RegisterType(const ValuePtr<>& origin, const ValuePtr<>& size, const ValuePtr<>& alignment, const ValuePtr<>& register_type_)
      : Base(mode_register, origin, size, alignment), register_type(register_type_) {}};
      struct SplitType : Base {EntryVector entries; SplitType(const ValuePtr<>& origin, const ValuePtr<>& size, const ValuePtr<>& alignment, const EntryVector& entries_)
      : Base(mode_split, origin, size, alignment), entries(entries_) {PSI_ASSERT(all_global(entries));}};
      
      boost::shared_ptr<const Base> m_value;
      
      LoweredType(const boost::shared_ptr<Base>& value) : m_value(value) {}
      
    public:
      LoweredType() {}
      
      /// \brief Construct a lowered type which is stored in a register
      static LoweredType register_(const ValuePtr<>& origin, const ValuePtr<>& size, const ValuePtr<>& alignment, const ValuePtr<>& register_type)
      {return LoweredType(boost::make_shared<RegisterType>(origin, size, alignment, register_type));}
      /// \brief Construct a lowered type which is split into component types
      static LoweredType split(const ValuePtr<>& origin, const ValuePtr<>& size, const ValuePtr<>& alignment, const EntryVector& entries)
      {return LoweredType(boost::make_shared<SplitType>(origin, size, alignment, entries));}
      /// \brief Construct a lowered type which is treated as a black box
      static LoweredType blob(const ValuePtr<>& origin, const ValuePtr<>& size, const ValuePtr<>& alignment)
      {return LoweredType(boost::make_shared<Base>(mode_blob, origin, size, alignment));}

      LoweredType with_origin(const ValuePtr<>& new_origin);
      
      /// \brief Is this value NULL?
      bool empty() const {return !m_value;}
      /// \brief Get the storage mode of this type
      Mode mode() const {return empty() ? mode_empty : m_value->mode;}
      /// \brief Is this a global type?
      bool global() const {return !empty() && (m_value->mode != mode_blob);}
      
      /// \brief Type from which this type was lowered (in the original context)
      const ValuePtr<>& origin() const {return m_value->origin;}

      /// \brief Get the type used to represent this type in a register
      const ValuePtr<>& register_type() const {return checked_cast<const RegisterType*>(m_value.get())->register_type;}
      
      /// \brief Get the entries used to represent this type, which is split
      const EntryVector& split_entries() const {return checked_cast<const SplitType*>(m_value.get())->entries;}

      /// \brief Get the size of this type in a suitable form for later passes
      const ValuePtr<>& size() const {return m_value->size;}
      /// \brief Get the alignment of this type in a suitable form for later passes
      const ValuePtr<>& alignment() const {return m_value->alignment;}
      
      /// \brief Get the size and alignment of this type as a constant
      TypeSizeAlignment size_alignment_const() const;
    };
    
    /**
     * Specialized LoweredValue-like structure which only contains a value and a flag indicating
     * whether the value is global or not.
     */
    struct LoweredValueSimple {
      LoweredValueSimple() : global(true) {}
      LoweredValueSimple(bool global_, const ValuePtr<>& value_) : global(global_), value(value_) {}
      bool global;
      ValuePtr<> value;
    };
    
    /**
      * \brief Per-term result of aggregate lowering.
      * 
      * The lowering pass will try to split aggregate operations
      * into simpler operations on scalars, so a value can be
      * represented in one of several ways depending on whether
      * it is an aggregate or not.
      */
    class PSI_TVM_EXPORT LoweredValue {
    public:
      typedef std::vector<LoweredValue> EntryVector;
      
      /**
       * Storage mode of a value.
       * 
       * Note that the numerical values of this enumeration must match the order of the
       * entries in ValueType.
       */
      enum Mode {
        mode_empty,
        mode_register,
        mode_split
      };

    private:
      static bool all_global(const EntryVector& e) {
        for (EntryVector::const_iterator ii = e.begin(), ie = e.end(); ii != ie; ++ii) {
          if (!ii->global())
            return false;
        }
        return true;
      }

      struct Base : CheckedCastBase {Mode mode; LoweredType type; bool global; Base(Mode mode_, const LoweredType& type_, bool global_) : mode(mode_), type(type_), global(global_) {PSI_ASSERT(type.global());}};
      struct RegisterValue : Base {ValuePtr<> value; RegisterValue(const LoweredType& type, bool global, const ValuePtr<>& value_) : Base(mode_register, type, global), value(value_) {PSI_ASSERT(type.mode() == LoweredType::mode_register);}};
      struct SplitValue : Base {EntryVector entries; SplitValue(const LoweredType& type, const EntryVector& entries_) : Base(mode_split, type, type.global() && all_global(entries_)), entries(entries_) {PSI_ASSERT(type.mode() == LoweredType::mode_split);}};
      boost::shared_ptr<Base> m_value;

      explicit LoweredValue(const boost::shared_ptr<Base>& value) : m_value(value) {}

    public:
      LoweredValue() {}
      
      /// \brief Construct a lowered value with a single entry in a register
      static LoweredValue register_(const LoweredType& type, bool global, const ValuePtr<>& value) {return LoweredValue(boost::make_shared<RegisterValue>(type, global, value));}
      /// \brief Construct a lowered value with a single entry in a register
      static LoweredValue register_(const LoweredType& type, const LoweredValueSimple& r) {return register_(type, type.global() && r.global, r.value);}
      /// \brief Construct a lowered value which is a list of other values
      static LoweredValue split(const LoweredType& type, const EntryVector& entries) {return LoweredValue(boost::make_shared<SplitValue>(type, entries));}
      
      /// \brief Whether this value is global
      bool global() const {return m_value->global;}
      /// \brief The original type of this value.
      const LoweredType& type() const {return m_value->type;}
      
      /// \brief Get the storage mode of this value
      Mode mode() const {return m_value ? m_value->mode : mode_empty;}
      /// \brief Whether this value is empty
      bool empty() const {return !m_value;}
      
      /// \brief Get value of data allocated in register
      const ValuePtr<>& register_value() const {return checked_cast<const RegisterValue*>(m_value.get())->value;}

      /// \brief Get list of entries
      const EntryVector& split_entries() const {return checked_cast<const SplitValue*>(m_value.get())->entries;}
      
      /// \brief Convert a LoweredValue on the stack to a LoweredValueSimple
      LoweredValueSimple register_simple() const {return LoweredValueSimple(global(), register_value());}
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
    class PSI_TVM_EXPORT AggregateLoweringPass : public ModuleRewriter {
    public:
      class FunctionRunner;
      class GlobalVariableRunner;
      class AggregateLoweringRewriter;
      
      /// \brief Computes offsets and alignment of struct-like (and thereby also array-like) data structures.
      class ElementOffsetGenerator {
        AggregateLoweringRewriter *m_rewriter;
        SourceLocation m_location;
        bool m_global;
        ValuePtr<> m_offset, m_size, m_alignment;
        
      public:
        ElementOffsetGenerator(AggregateLoweringRewriter *rewriter, const SourceLocation& location);
        
        /// \brief Are size(), offset() and alignment() all global values?
        bool global() const {return m_global;}
        /// \brief Offset of last element inserted
        const ValuePtr<>& offset() const {return m_offset;}
        /// \brief Current total size of all elements (may not be a multiple of alignment until finish() is called)
        const ValuePtr<>& size() const {return m_size;}
        /// \brief Current alignment of all elements
        const ValuePtr<>& alignment() const {return m_alignment;}
        
        void next(bool global, const ValuePtr<>& el_size, const ValuePtr<>& el_alignment);
        void finish();
        void next(const LoweredType& type);
        void next(const ValuePtr<>& type);
      };    
      
      class PSI_TVM_EXPORT AggregateLoweringRewriter {
        friend class FunctionRunner;
        friend class GlobalVariableRunner;
        friend class AggregateLoweringPass;
        
        AggregateLoweringPass *m_pass;
        
        typedef SharedMap<ValuePtr<>, LoweredType> TypeMapType;
        typedef SharedMap<ValuePtr<>, LoweredValue> ValueMapType;

        TypeMapType m_type_map;
        ValueMapType m_value_map;
        
        ValuePtr<> m_byte_type, m_byte_ptr_type;
        
      public:
        AggregateLoweringRewriter(AggregateLoweringPass*);
        
        /// \brief Get the pass object this belongs to
        AggregateLoweringPass& pass() {return *m_pass;}
        
        /// \brief Get the (target) context this pass belongs to
        Context& context() {return pass().context();}
        
        /// \brief Get the (lowered) byte type
        const ValuePtr<>& byte_type() const {return m_byte_type;}
        /// \brief Get the (lowered) byte pointer type
        const ValuePtr<>& byte_ptr_type() const {return m_byte_ptr_type;}

        /**
         * Work out the expected form of a type after this pass.
         */
        virtual LoweredType rewrite_type(const ValuePtr<>& type) = 0;
        
        /**
         * Rewrite a value for later passes.
         */
        virtual LoweredValue rewrite_value(const ValuePtr<>& value) = 0;
        
        /**
         * \brief Construct a value with a given type by bit-wise conversion from an existing value.
         * 
         * Any data not required by \c type but not specified by \c value is undefined, including padding bytes
         * and bytes at the end.
         * 
         * \param type Type to conert to, in the source context.
         * \param value Lowered value to convert.
         */
        virtual LoweredValue bitcast(const LoweredType& type, const LoweredValue& value, const SourceLocation& location) = 0;

        LoweredType lookup_type(const ValuePtr<>& type);

        LoweredValueSimple rewrite_value_register(const ValuePtr<>&);
        LoweredValue lookup_value(const ValuePtr<>&);
        LoweredValueSimple lookup_value_register(const ValuePtr<>&);
        
        ValuePtr<> simplify_argument_type(const ValuePtr<>& type);
        ValuePtr<> unwrap_exists(const ValuePtr<Exists>& exists);
      };

      /**
       * Class which actually runs the pass, and holds per-run data.
       */
      class PSI_TVM_EXPORT FunctionRunner : public AggregateLoweringRewriter {
        ValuePtr<Function> m_old_function, m_new_function;
        InstructionBuilder m_builder;
        
        struct BlockBuildState {
          TypeMapType types;
          ValueMapType values;
        };
        
        typedef boost::unordered_map<ValuePtr<Block>, BlockBuildState> BlockSlotMapType;

        /// Build state of each block
        BlockSlotMapType m_block_state;
        
        void switch_to_block(const ValuePtr<Block>& block);
        LoweredValue create_phi_node(const ValuePtr<Block>& block, const LoweredType& type, const SourceLocation& location);
        void create_phi_edge(const LoweredValue& phi_term, const LoweredValue& value);
        
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
        
        ValuePtr<> alloca_(const LoweredType& type, const SourceLocation& location);
        LoweredValue load_value(const LoweredType& type, const ValuePtr<>& ptr, const SourceLocation& location);
        void store_value(const LoweredValue& value, const ValuePtr<>& ptr, const SourceLocation& location);

        virtual LoweredType rewrite_type(const ValuePtr<>&);
        virtual LoweredValue rewrite_value(const ValuePtr<>&);
        virtual LoweredValue bitcast(const LoweredType& type, const LoweredValue& value, const SourceLocation& location);
      };
      
      class PSI_TVM_EXPORT ModuleLevelRewriter : public AggregateLoweringRewriter {
      public:
        ModuleLevelRewriter(AggregateLoweringPass*);
        virtual LoweredValue bitcast(const LoweredType& type, const LoweredValue& input, const SourceLocation& location);
        virtual LoweredType rewrite_type(const ValuePtr<>&);
        virtual LoweredValue rewrite_value(const ValuePtr<>&);
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
         * \brief Get the largest type with size less than or equal to that specified.
         * 
         * \param size Requested size, which must be a power of two.
         */
        virtual std::pair<ValuePtr<>, std::size_t> type_from_size(Context& context, std::size_t size, const SourceLocation& location) = 0;

        /**
         * \brief Get the type with alignment closest to the specified alignment.
         * 
         * \return A type with an alignment is less than or equal to \c aligment.
         * Its size must be the same as its alignment. The first member of the
         * pair is the type, the second member of the pair is the size of the first.
         */
        virtual std::pair<ValuePtr<>, std::size_t> type_from_alignment(Context& context, std::size_t alignment, const SourceLocation& location) = 0;
        
        /**
         * \brief Get the size and alignment of a type.
         * 
         * This should only be used for primitive types (note that this includes
         * pointers). Aggregate types are handled internally by AggregateLoweringPass.
         * 
         * \param type A primitive type.
         */
        virtual TypeSizeAlignment type_size_alignment(const ValuePtr<>& type) = 0;
        
        /**
         * \brief Shift a primitive value by a number of bytes.
         * 
         * This operation is equivalent to writing \c value to memory at a location
         * \c ptr and then reading back a value of type \c result_type from \c ptr+shift,
         * with zero padding surrounding \c value.
         */
        virtual ValuePtr<> byte_shift(const ValuePtr<>& value, const ValuePtr<>& result_type, int shift, const SourceLocation& location) = 0;
      };

    private:
      struct TypeTermRewriter;
      struct FunctionalTermRewriter;
      struct InstructionTermRewriter;
      
      static LoweredType type_term_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<HashableValue>& term);
      static LoweredType type_term_rewrite_parameter(AggregateLoweringRewriter& rewriter, const ValuePtr<>& term);
      static LoweredValue hashable_term_rewrite(AggregateLoweringRewriter& rewriter, const ValuePtr<HashableValue>& term);
      static LoweredValue instruction_term_rewrite(FunctionRunner& runner, const ValuePtr<Instruction>& insn);
      
      ModuleLevelRewriter m_global_rewriter;
      
      struct ExplodeEntry {
        std::size_t offset;
        TypeSizeAlignment tsa;
        ValuePtr<> value;
      };
      
      struct ExplodeCompareStart;
      struct ExplodeCompareEnd;
      
      void explode_lowered_type(const LoweredType& type, std::vector<ExplodeEntry>& entries, std::size_t& offset, const SourceLocation& location);
      void explode_constant_value(const ValuePtr<>& value, std::vector<ExplodeEntry>& entries, std::size_t& offset, const SourceLocation& location, bool expand_aggregates);
      void explode_lowered_value(const LoweredValue& value, std::vector<ExplodeEntry>& entries, std::size_t& offset, const SourceLocation& location, bool expand_aggregates);
      ValuePtr<> implode_constant_value(const ValuePtr<>& type, const std::vector<ExplodeEntry>& entries, std::size_t& offset, const SourceLocation& location);
      LoweredValue implode_lowered_value(const LoweredType& type, const std::vector<ExplodeEntry>& entries, std::size_t& offset, const SourceLocation& location);

      std::pair<ValuePtr<>, ValuePtr<> > build_global_type(const ValuePtr<>& type, const SourceLocation& location);
      ValuePtr<> build_global_value(const ValuePtr<>& value, const SourceLocation& location);

      virtual void update_implementation(bool);
      
      LoweredType m_size_type, m_pointer_type, m_block_type;

    public:
      AggregateLoweringPass(Module*, TargetCallback*, Context* =0);

      /// \brief Get the (target) context of this pass
      Context& context() {return target_module()->context();}
      
      ModuleLevelRewriter& global_rewriter() {return m_global_rewriter;}
      
      const LoweredType& size_type();
      const LoweredType& pointer_type();
      const LoweredType& block_type();
      
      std::size_t lowered_type_alignment(const ValuePtr<>& alignment);

      /**
       * Callback used to rewrite function types and function calls
       * in a system-specific way.
       */
      TargetCallback *target_callback;
      
      /**
       * \brief Always split fixed length arrays on loading
       * 
       * Note that an array containing a split type is not a candidate for
       * keeping in a regsiter.
       */
      bool split_arrays;
      /**
       * \brief Always split fixed struct types on loading.
       * 
       * Note that a struct which contains a split type is not a candidate
       * for keeping in a register.
       */
      bool split_structs;

      /// Whether to replace all unions in the IR with pointer operations
      bool remove_unions;
      
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
