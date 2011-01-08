#ifndef HPP_PSI_TVM_AGGREGATELOWERING
#define HPP_PSI_TVM_AGGREGATELOWERING

#include "Core.hpp"
#include "Function.hpp"
#include "Aggregate.hpp"
#include "Instructions.hpp"
#include "InstructionBuilder.hpp"

#include <tr1/unordered_map>
#include <boost/shared_ptr.hpp>

namespace Psi {
  namespace Tvm {
    /**
     * Rewrite a term in two stages.
     * 
     * The first stage sets the term up, and the second initializes it.
     * This is effective for global and function terms, which can be
     * created in this way.
     */
    class GlobalTermRewriter {
      GlobalTerm *m_new_term;
      
    public:
      GlobalTermRewriter() : m_new_term(0) {}
      virtual ~GlobalTermRewriter() {}
      
      /**
       * Pointer to rewritten term.
       */
      
      GlobalTerm *new_term() const {return m_new_term;}
      
      /**
       * Run the term rewrite.
       */
      virtual void run() = 0;
      
    protected:
      /**
       * Set the value of the result term.
       */
      void new_term(GlobalTerm *value) {m_new_term = value;}
    };
    
    /**
     * Base class for types which completely rewrite functions.
     */
    struct ModuleRewriterPass {
      /**
       * Prepare a function rewrite.
       */
      virtual boost::shared_ptr<GlobalTermRewriter> rewrite_global(FunctionTerm *f) = 0;
    };
    
    /**
     * A function pass which removes aggregate operations by rewriting
     * them in terms of pointer offsets, so that later stages need not
     * handle them.
     */
    class AggregateLoweringPass : public ModuleRewriterPass {
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
          PSI_ASSERT(on_stack ? true :
            (isa<PointerType>(value->type()) && isa<ByteType>(cast<PointerType>(value->type())->target_type())));
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

    private:
      typedef std::tr1::unordered_map<Term*, Type> TypeMapType;
      typedef std::tr1::unordered_map<Term*, Value> ValueMapType;
      
    public:
      // Forward declaration required to get the 'friend' declaration in
      // TypeValueMap to work correctly
      class FunctionRunner;
      
      class TypeValueMap {
        friend class FunctionRunner;
        
      private:
        TypeMapType m_type_map;
        ValueMapType m_value_map;
      };

      /**
       * Class which actually runs the pass, and holds per-run data.
       */
      class FunctionRunner : public GlobalTermRewriter, public TypeValueMap {
        AggregateLoweringPass *m_pass;
        FunctionTerm *m_old_function;
        InstructionBuilder m_builder;
        
        std::vector<BlockTerm*> topsort_blocks();

      public:        
        FunctionRunner(AggregateLoweringPass *pass, FunctionTerm *function);
        virtual void run();
        
        /// \brief Get the pass object this belongs to
        AggregateLoweringPass& pass() {return *m_pass;}
        
        /// \brief Get the new version of the function
        FunctionTerm* new_function() {return cast<FunctionTerm>(new_term());}
        
        /// \brief Get the function being rebuilt
        FunctionTerm* old_function() {return m_old_function;}
        
        /// \brief Get the context this pass belongs to
        Context& context() {return pass().context();}
        
        /// \brief Return an InstructionBuilder set to the current instruction insert point.
        InstructionBuilder& builder() {return m_builder;}
        
        Type rewrite_type(Term*);
        
        Value rewrite_value(Term*);
        Term* rewrite_value_stack(Term*);
        Term* rewrite_value_ptr(Term*);
        
        Value store_value(Term *value, Term *ptr);
        Value load_value(Term *load_term, Term *ptr, bool=true);
      };
      
      struct PaddingStatus {
        unsigned original_size;
        unsigned rewrite_size;

        PaddingStatus();
        PaddingStatus(unsigned, unsigned);
      };
      
      class GlobalVariableRunner : public GlobalTermRewriter {
        AggregateLoweringPass *m_pass;
        GlobalVariableTerm *m_old_global;
        
      public:
        GlobalVariableRunner(AggregateLoweringPass *pass, GlobalVariableTerm *global);
        virtual void run();
        
        /// \brief Get the pass this object belongs to
        AggregateLoweringPass& pass() {return *m_pass;}
        
        /// \brief Get the new version of the global
        GlobalVariableTerm* new_global() {return cast<GlobalVariableTerm>(new_term());}
        
        /// \brief Get the old version of the global
        GlobalVariableTerm *old_global() {return m_old_global;}
        
        /// \brief Get the context this pass belongs to
        Context& context() {return pass().context();}
      };

      struct TypeSizeAlignment {
        unsigned size;
        unsigned alignment;
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
        virtual Value lower_function_call(FunctionRunner& runner, FunctionCall::Ptr term) = 0;

        /**
         * Create a return instruction to return the given value
         * according to the target-specific lowering routine.
         * 
         * \param value Value to return.
         * 
         * \return The actual return instruction created.
         */
        virtual Term* lower_return(FunctionRunner& runner, Term *value) = 0;
        
        /**
         * Change a function's type and add entry code to decode
         * parameters into aggregates in the simplest possible way. The
         * remaining aggregates lowering code will handle the rest.
         * 
         * \param runner Holds per-pass data.
         * 
         * \param function Function to rewrite.
         * 
         * \param parameters Values of non-phantom parameters should be
         * stored here.
         */
        virtual FunctionTerm* lower_function(FunctionRunner& runner, FunctionTerm *function, ArrayPtr<Value> parameters) = 0;
        
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
         * Its size must be the same as its alignment.
         */
        virtual std::pair<Term*, unsigned> type_from_alignment(unsigned alignment);
      };

    private:
      struct TypeTermRewriter;
      struct FunctionalTermRewriter;
      struct InstructionTermRewriter;
      TypeValueMap m_term_map;
      Context *m_context;
      
      Type rewrite_type(Term*);
      Value rewrite_value(Term*);

      struct GlobalBuildStatus {
        GlobalBuildStatus();
        GlobalBuildStatus(Term*, unsigned, unsigned, unsigned);
        std::vector<Term*> elements;
        /// \brief Size of the entries in elements as a sequence (not including end padding to reach a multiple of alignment)
        unsigned elements_size;
        /// \brief Desired size of this set of elements.
        unsigned size;
        /// \brief Desired alignment of this set of elements.
        unsigned alignment;
      };
      
      GlobalBuildStatus rewrite_global_type(Term*);
      void global_append(GlobalBuildStatus& status, const GlobalBuildStatus& child);

    public:
      AggregateLoweringPass(Context*,TargetCallback*);

      virtual boost::shared_ptr<GlobalTermRewriter> rewrite_global(GlobalTerm *f);

      /// \brief Get the context this pass belongs to
      Context& context() const {return *m_context;}

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
       *
       * Note that operations to compute the size and alignment of types
       * are always completely rewritten, regardless of this setting.
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
