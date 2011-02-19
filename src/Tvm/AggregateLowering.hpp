#ifndef HPP_PSI_TVM_AGGREGATELOWERING
#define HPP_PSI_TVM_AGGREGATELOWERING

#include "Core.hpp"
#include "Function.hpp"
#include "Aggregate.hpp"
#include "Instructions.hpp"
#include "InstructionBuilder.hpp"
#include "ModuleRewriter.hpp"

#include <tr1/unordered_map>

namespace Psi {
  namespace Tvm {    
    /**
     * A function pass which removes aggregate operations by rewriting
     * them in terms of pointer offsets, so that later stages need not
     * handle them.
     */
    class AggregateLoweringPass : public ModuleRewriter {
    public:
      class Type {
        Term *m_stack_type, *m_heap_type;
        
      public:
        /// \brief Constructor for unknown types
        Type() : m_stack_type(0), m_heap_type(0) {}
        /// \brief Constructor for types which are the same on the stack and heap
        explicit Type(Term *type) : m_stack_type(type), m_heap_type(type) {}
        /// \brief Constructor for types which are different on the stack and heap
        Type(Term *stack_type, Term *heap_type) : m_stack_type(stack_type), m_heap_type(heap_type) {}
        
        /// \brief Get the type used to represent this type on the stack
        Term *stack_type() const {return m_stack_type;}
        
        /// \brief Get the type used to represent this type on the heap
        Term *heap_type() const {return m_heap_type;}
      };
      
      /**
       * \brief Per-term result of aggregate lowering.
       * 
       * The lowering pass will try to split aggregate operations
       * into simpler operations on scalars, so a value can be
       * represented in one of several ways depending on whether
       * it is an aggregate or not.
       */
      class Value {
        Term *m_value;
        bool m_on_stack;
        
      public:
        Value() : m_value(0), m_on_stack(false) {}

        Value(Term *value, bool on_stack) : m_value(value), m_on_stack(on_stack) {
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
        Term *value() const {return m_value;}
      };

    public:
      class FunctionRunner;
      class GlobalVariableRunner;
      
      class AggregateLoweringRewriter {
        friend class FunctionRunner;
        friend class GlobalVariableRunner;
        friend class AggregateLoweringPass;
        
        AggregateLoweringPass *m_pass;
        
        typedef std::tr1::unordered_map<Term*, Type> TypeMapType;
        typedef std::tr1::unordered_map<Term*, Value> ValueMapType;

        TypeMapType m_type_map;
        ValueMapType m_value_map;
        
      public:
        AggregateLoweringRewriter(AggregateLoweringPass*);
        
        /// \brief Get the pass object this belongs to
        AggregateLoweringPass& pass() {return *m_pass;}
        
        /// \brief Get the (target) context this pass belongs to
        Context& context() {return pass().context();}

        virtual Type rewrite_type(Term*);
        virtual Value rewrite_value(Term*);
        
        /**
         * \brief \em Load a value.
         * 
         * Given a pointer to a value, this returns a Value corresponding to the
         * data at that pointer.
         * 
         * \param load_term Term to assign the result of this load to.
         */
        virtual Value load_value(Term* load_term, Term *ptr) = 0;
        
        /**
         * \brief \em Store a value.
         * 
         * This stores the value to memory, also creating the memory to store the
         * value in. If the value is a constant it will be created as a global;
         * if it is a variable it will be created using the alloca instruction.
         */
        virtual Term* store_value(Term *value, Term *alloc_type, Term *alloc_count, Term *alloc_alignment) = 0;

        Term* rewrite_value_stack(Term*);
        Term* rewrite_value_ptr(Term*);
      };

      /**
       * Class which actually runs the pass, and holds per-run data.
       */
      class FunctionRunner : public AggregateLoweringRewriter {
        FunctionTerm *m_old_function, *m_new_function;
        InstructionBuilder m_builder;
        
        std::vector<BlockTerm*> topsort_blocks();

      public:        
        FunctionRunner(AggregateLoweringPass *pass, FunctionTerm *function);
        virtual void run();
        
        /// \brief Get the new version of the function
        FunctionTerm* new_function() {return m_new_function;}
        
        /// \brief Get the function being rebuilt
        FunctionTerm* old_function() {return m_old_function;}
        
        /// \brief Return an InstructionBuilder set to the current instruction insert point.
        InstructionBuilder& builder() {return m_builder;}
        
        void add_mapping(Term*, Term*, bool);
        
        virtual Value load_value(Term*, Term*);
        Value load_value(Term*, Term*, bool);
        virtual Term* store_value(Term*, Term*, Term*, Term*);
        Term* store_value(Term*, Term*);

        virtual Type rewrite_type(Term*);
        virtual Value rewrite_value(Term*);
      };
      
      class ModuleLevelRewriter : public AggregateLoweringRewriter {
      public:
        ModuleLevelRewriter(AggregateLoweringPass*);
        virtual Value load_value(Term*, Term*);
        virtual Term* store_value(Term*, Term*, Term*, Term*);
      };
      
      struct TypeSizeAlignment {
        Term *size;
        Term *alignment;
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
        virtual void lower_function_call(FunctionRunner& runner, FunctionCall::Ptr term) = 0;

        /**
         * Create a return instruction to return the given value
         * according to the target-specific lowering routine.
         * 
         * \param value Value to return.
         * 
         * \return The actual return instruction created.
         */
        virtual InstructionTerm* lower_return(FunctionRunner& runner, Term *value) = 0;

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
        virtual FunctionTerm* lower_function(AggregateLoweringPass& runner, FunctionTerm *function) = 0;
        
        /**
         * Create necessary entry code into a function to convert low level
         * parameter format.
         * 
         * \param source_function Original function.
         * 
         * \param target_function Newly created function.
         */
        virtual void lower_function_entry(FunctionRunner& runner, FunctionTerm *source_function, FunctionTerm *target_function) = 0;
        
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
        virtual Term* convert_value(Term *value, Term *type) = 0;

        /**
         * \brief Get the size and alignment of a type.
         */
        virtual TypeSizeAlignment type_size_alignment(Term *type) = 0;

        /**
         * \brief Get the type with alignment closest to the specified alignment.
         * 
         * \return A type with an alignment is less than or equal to \c aligment.
         * Its size must be the same as its alignment. The first member of the
         * pair is the type, the second member of the pair is the size of the first.
         */
        virtual std::pair<Term*,Term*> type_from_alignment(Term *alignment) = 0;
      };

    private:
      struct TypeTermRewriter;
      struct FunctionalTermRewriter;
      struct InstructionTermRewriter;
      ModuleLevelRewriter m_global_rewriter;

      struct GlobalBuildStatus {
        GlobalBuildStatus(Context&);
        GlobalBuildStatus(Term*, Term*, Term*, Term*);
        std::vector<Term*> elements;
        /// \brief Size of the entries in elements as a sequence (not including end padding to reach a multiple of alignment)
        Term* elements_size;
        /// \brief Desired size of this set of elements.
        Term* size;
        /// \brief Desired alignment of this set of elements.
        Term* alignment;
      };
      
      GlobalBuildStatus rewrite_global_type(Term*);
      GlobalBuildStatus rewrite_global_value(Term*);
      void global_append(GlobalBuildStatus&, const GlobalBuildStatus&, bool);
      void global_pad_to_size(GlobalBuildStatus&, Term*, Term*, bool);
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
