#ifndef HPP_PSI_TVM_AGGREGATELOWERING
#define HPP_PSI_TVM_AGGREGATELOWERING

#include "Core.hpp"
#include "Function.hpp"
#include "Aggregate.hpp"
#include "Instructions.hpp"
#include "InstructionBuilder.hpp"
#include "ModuleRewriter.hpp"

#include <boost/unordered_map.hpp>

namespace Psi {
  namespace Tvm {    
    class LoweredType {
      ValuePtr<> m_stack_type, m_heap_type;
      ValuePtr<> m_size, m_alignment;
      
    public:
      LoweredType() : m_stack_type(0), m_heap_type(0), m_size(0), m_alignment(0) {}
      /// \brief Constructor for unknown types
      LoweredType(const ValuePtr<>& size, const ValuePtr<>& alignment) : m_stack_type(0), m_heap_type(0), m_size(size), m_alignment(alignment) {}
      /// \brief Constructor for types which are the same on the stack and heap
      LoweredType(const ValuePtr<>& size, const ValuePtr<>& alignment, const ValuePtr<>& type) : m_stack_type(type), m_heap_type(type), m_size(size), m_alignment(alignment) {}
      /// \brief Constructor for types which are different on the stack and heap
      LoweredType(const ValuePtr<>& size, const ValuePtr<>& alignment, const ValuePtr<>& stack_type, const ValuePtr<>& heap_type) : m_stack_type(stack_type), m_heap_type(heap_type), m_size(size), m_alignment(alignment) {}
      
      /// \brief Get the type used to represent this type on the stack
      const ValuePtr<>& stack_type() const {return m_stack_type;}
      
      /// \brief Get the type used to represent this type on the heap
      const ValuePtr<>& heap_type() const {return m_heap_type;}
      
      /// \brief Get the size of this type in a suitable form for later passes
      const ValuePtr<>& size() const {return m_size;}
      
      /// \brief Get the alignment of this type in a suitable form for later passes
      const ValuePtr<>& alignment() const {return m_alignment;}
      
      /// \brief Check whether this is a valid lowered type.
      bool valid() const {return m_size && m_alignment;}
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
      ValuePtr<> m_value;
      bool m_on_stack;
      
    public:
      LoweredValue() : m_value(0), m_on_stack(false) {}

      LoweredValue(const ValuePtr<>& value, bool on_stack) : m_value(value), m_on_stack(on_stack) {
        PSI_ASSERT(on_stack ? true : isa<PointerType>(value->type()));
      }

      /**
        * \brief Whether this is a type which the later passes recognise
        * and can be handled on the stack.
        * 
        * If this is false, the value is handled via pointers using
        * \c alloca.
        */
      bool on_stack() const {return m_on_stack;}

      /**
        * \brief Value of this term.
        */
      const ValuePtr<>& value() const {return m_value;}
    };

    /**
     * A function pass which removes aggregate operations by rewriting
     * them in terms of pointer offsets, so that later stages need not
     * handle them.
     */
    class AggregateLoweringPass : public ModuleRewriter {
    public:
      class FunctionRunner;
      class GlobalVariableRunner;
      
      class AggregateLoweringRewriter {
        friend class FunctionRunner;
        friend class GlobalVariableRunner;
        friend class AggregateLoweringPass;
        
        AggregateLoweringPass *m_pass;
        
        typedef boost::unordered_map<ValuePtr<>, LoweredType> TypeMapType;
        typedef boost::unordered_map<ValuePtr<>, LoweredValue> ValueMapType;

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
         * 
         * \param load_term Term to assign the result of this load to.
         */
        virtual LoweredValue load_value(const ValuePtr<>& load_term, const ValuePtr<>& ptr, const SourceLocation& location) = 0;
        
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

        ValuePtr<> rewrite_value_stack(const ValuePtr<>&);
        ValuePtr<> rewrite_value_ptr(const ValuePtr<>&);
        LoweredValue lookup_value(const ValuePtr<>&);
        ValuePtr<> lookup_value_stack(const ValuePtr<>&);
        ValuePtr<> lookup_value_ptr(const ValuePtr<>&);
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
        
        void create_phi_node(const ValuePtr<Block>&, const ValuePtr<>&);
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
        
        void add_mapping(const ValuePtr<>&, const ValuePtr<>&, bool);
        ValuePtr<Block> rewrite_block(const ValuePtr<Block>&);
        
        virtual LoweredValue load_value(const ValuePtr<>& load_term, const ValuePtr<>& ptr, const SourceLocation& location);
        virtual ValuePtr<> store_value(const ValuePtr<>& value, const SourceLocation& location);
        virtual ValuePtr<> store_type(const ValuePtr<>& size, const ValuePtr<>& alignment, const SourceLocation& location);
        ValuePtr<> store_value(const ValuePtr<>& value, const ValuePtr<>& ptr, const SourceLocation& location);

        virtual LoweredType rewrite_type(const ValuePtr<>&);
        virtual LoweredValue rewrite_value(const ValuePtr<>&);
      };
      
      class ModuleLevelRewriter : public AggregateLoweringRewriter {
      public:
        ModuleLevelRewriter(AggregateLoweringPass*);
        virtual LoweredValue load_value(const ValuePtr<>& load_term, const ValuePtr<>& ptr, const SourceLocation& location);
        virtual ValuePtr<> store_value(const ValuePtr<>&, const SourceLocation& location);
        virtual ValuePtr<> store_type(const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
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
      
      AggregateLoweringRewriter& global_rewriter() {return m_global_rewriter;}

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
      bool remove_stack_arrays;
      
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
