#ifndef HPP_PSI_TVM_FUNCTION
#define HPP_PSI_TVM_FUNCTION

#include "Core.hpp"
#include "Functional.hpp"

namespace Psi {
  namespace Tvm {
    class BlockTerm;

    /**
     * \brief Instruction term. Per-instruction funtionality is
     * created by implementing InstructionTermBackend and wrapping
     * that in InstructionTerm.
     */
    class InstructionTerm : public Term {
      friend class BlockTerm;
      template<typename> friend class InstructionTermSpecialized;

    public:
      const char *operation() {return m_operation;}
      BlockTerm* block() {return m_block;}
      std::size_t n_parameters() {return Term::n_base_parameters();}
      Term* parameter(std::size_t n) {return get_base_parameter(n);}

      /// \brief Append any blocks this instruction jumps to to the
      /// specified vector.
      virtual void jump_targets(std::vector<BlockTerm*>&) = 0;

    private:
      class Initializer;
      InstructionTerm(const UserInitializer& ui, Context *context,
		      Term* type, const char *operation, ArrayPtr<Term*const> parameters,
                      BlockTerm* block);

      const char *m_operation;
      typedef boost::intrusive::list_member_hook<> InstructionListHook;
      InstructionListHook m_instruction_list_hook;
      BlockTerm *m_block;
    };

#ifndef PSI_DOXYGEN
    template<> struct CastImplementation<InstructionTerm> : CoreCastImplementation<InstructionTerm, term_instruction> {};
#endif

    template<typename TermTagType>
    class InstructionTermSpecialized : public InstructionTerm, CompressedBase<typename TermTagType::Data> {
      friend class BlockTerm;
      template<typename> friend class InstructionTermSetupSpecialized;
      typedef typename TermTagType::Data Data;

    public:
      const Data& data() {return CompressedBase<Data>::get();}

      virtual void jump_targets(std::vector<BlockTerm*>& targets) {
        TermTagType::jump_targets(typename TermTagType::Ptr(typename TermTagType::PtrHook(this)), targets);
      }

    private:
      InstructionTermSpecialized(const UserInitializer& ui, Context *context,
                                 Term* type, const char *operation, ArrayPtr<Term*const> parameters,
                                 BlockTerm* block, const Data& data)
        : InstructionTerm(ui, context, type, operation, parameters, block),
          CompressedBase<Data>(data) {
      }
    };

    class InstructionTermSetup {
      template<typename> friend struct InstructionTermSetupSpecialized;
      friend class BlockTerm;
      friend class InstructionTerm;

      InstructionTermSetup(const char *operation_, std::size_t term_size_)
        : operation(operation_), term_size(term_size_) {}

      const char *operation;
      std::size_t term_size;
      virtual InstructionTerm* construct(void *ptr,
                                         const UserInitializer& ui, Context *context, Term* type,
                                         const char *operation, ArrayPtr<Term*const> parameters,
                                         BlockTerm *block) const = 0;

      virtual Term* type(FunctionTerm* function, ArrayPtr<Term*const> parameters) const = 0;
    };

    template<typename TermTagType>
    class InstructionTermSetupSpecialized : InstructionTermSetup {
      friend class BlockTerm;
      typedef typename TermTagType::Data Data;

      InstructionTermSetupSpecialized(const Data *data_)
        : InstructionTermSetup(TermTagType::operation, sizeof(InstructionTermSpecialized<TermTagType>)), data(data_) {}

      const Data *data;

      virtual InstructionTerm* construct(void *ptr,
                                         const UserInitializer& ui, Context *context, Term* type,
                                         const char *operation, ArrayPtr<Term*const> parameters,
                                         BlockTerm *block) const {
        return new (ptr) InstructionTermSpecialized<TermTagType>
          (ui, context, type, operation, parameters, block, *data);
      }

      virtual Term* type(FunctionTerm* function, ArrayPtr<Term*const> parameters) const {
        return TermTagType::type(function, *data, parameters);
      }
    };

    /**
     * Base class for pointers to instruction terms. This provides
     * generic functions for all instruction terms in a non-template
     * class.
     */
    class InstructionTermPtr : public TermPtrBase {
    public:
      /// \brief To comply with the \c PtrAdapter interface.
      typedef InstructionTerm GetType;
      InstructionTermPtr() {}
      explicit InstructionTermPtr(InstructionTerm *term) : TermPtrBase(term) {}
      /// \brief To comply with the \c PtrAdapter interface.
      InstructionTerm* get() const {return checked_cast<InstructionTerm*>(m_ptr);}
      /// \copydoc InstructionTerm::operation
      const char *operation() const {return get()->operation();}
    };

    /**
     * Base class for pointers to instruction terms - this is
     * specialized to individual term types.
     */
    template<typename Data>
    class InstructionTermPtrBase : public InstructionTermPtr {
    public:
      InstructionTermPtrBase() {}
      explicit InstructionTermPtrBase(InstructionTerm *term) : InstructionTermPtr(term) {}

    protected:
      const Data& data() const {
        return checked_cast<InstructionTermSpecialized<Data>*>(get())->data();
      }
    };

    /**
     * \brief Phi node. These are used to unify values from different
     * predecessors on entry to a block.
     *
     * \sa http://en.wikipedia.org/wiki/Static_single_assignment_form
     */
    class PhiTerm : public Term {
      friend class BlockTerm;

    public:
      BlockTerm* block();
      void add_incoming(BlockTerm* block, Term* value);

      /// \brief Number of incoming edges
      std::size_t n_incoming() {return m_n_incoming;}
      /// \brief Get the block corresponding to a given incoming edge
      BlockTerm *incoming_block(std::size_t n);
      /// \brief Get the value of a given incoming edge
      Term *incoming_value(std::size_t n) {return get_base_parameter(n*2+2);}

    private:
      class Initializer;
      PhiTerm(const UserInitializer& ui, Context *context, Term* type, BlockTerm *block);
      typedef boost::intrusive::list_member_hook<> PhiListHook;
      PhiListHook m_phi_list_hook;
      std::size_t m_n_incoming;
    };

#ifndef PSI_DOXYGEN
    template<> struct CastImplementation<PhiTerm> : CoreCastImplementation<PhiTerm, term_phi> {};
#endif

    /**
     * \brief Block (list of instructions) inside a function. The
     * value of this term is the label used to jump to this block.
     */
    class BlockTerm : public Term {
      friend class FunctionTerm;

    public:
      typedef boost::intrusive::list<InstructionTerm,
				     boost::intrusive::member_hook<InstructionTerm, InstructionTerm::InstructionListHook, &InstructionTerm::m_instruction_list_hook>,
				     boost::intrusive::constant_time_size<false> > InstructionList;
      typedef boost::intrusive::list<PhiTerm,
				     boost::intrusive::member_hook<PhiTerm, PhiTerm::PhiListHook, &PhiTerm::m_phi_list_hook>,
				     boost::intrusive::constant_time_size<false> > PhiList;

      PhiTerm* new_phi(Term* type);

      /**
       * Create a new instruction in this block.
       * 
       * \tparam T Instruction type to create.
       * 
       * \param parameters Parameters to the created instruction.
       * 
       * \param insert_before Instruction to insert the new instruction
       * before. If null, insert the new instruction at the end of this
       * block.
       * 
       * \param data Custom (non-term) data for the new instruction.
       */
      template<typename T>
      typename T::Ptr new_instruction(ArrayPtr<Term*const> parameters, InstructionTerm *insert_before=0, const typename T::Data& data = typename T::Data()) {
        return cast<T>(new_instruction_bare(InstructionTermSetupSpecialized<T>(&data), parameters, insert_before));
      }

      InstructionList& instructions() {return m_instructions;}
      PhiList& phi_nodes() {return m_phi_nodes;}

      /** \brief Whether this block has been terminated so no more instructions can be added. */
      bool terminated() {return m_terminated;}
      /** \brief Get the function which contains this block. */
      FunctionTerm* function();
      /** \brief Get a pointer to the dominating block. */
      BlockTerm* dominator();

      bool check_available(Term* term, InstructionTerm *before=0);
      bool dominated_by(BlockTerm* block);
      std::vector<BlockTerm*> successors();
      std::vector<BlockTerm*> recursive_successors();

    private:
      class Initializer;
      BlockTerm(const UserInitializer& ui, Context *context, FunctionTerm* function, BlockTerm* dominator);
      InstructionList m_instructions;
      PhiList m_phi_nodes;
      bool m_terminated;

      InstructionTerm* new_instruction_bare(const InstructionTermSetup& setup, ArrayPtr<Term*const> parameters, InstructionTerm *insert_before);
    };

#ifndef PSI_DOXYGEN
    template<> struct CastImplementation<BlockTerm> : CoreCastImplementation<BlockTerm, term_block> {};
#endif

    class FunctionTerm;

    class FunctionParameterTerm : public Term {
      friend class FunctionTerm;
    public:
      FunctionTerm* function();
      bool parameter_phantom() {return m_phantom;}

    private:
      class Initializer;
      FunctionParameterTerm(const UserInitializer&, Context*, FunctionTerm*, Term*, bool);
      bool m_phantom;
    };

#ifndef PSI_DOXYGEN
    template<> struct CastImplementation<FunctionParameterTerm> : CoreCastImplementation<FunctionParameterTerm, term_function_parameter> {};
#endif

    /**
     * \brief %Function.
     */
    class FunctionTerm : public GlobalTerm {
      friend class Module;
    public:
      typedef std::tr1::unordered_multimap<Term*, std::string> TermNameMap;

      /**
       * Get the type of this function. This returns the raw function
       * type, whereas the actual type of this term is a pointer to
       * that type.
       */
      FunctionTypeTerm* function_type();

      std::size_t n_parameters() {return n_base_parameters() - 3;}
      /** \brief Get a function parameter. */
      FunctionParameterTerm* parameter(std::size_t n) {return cast<FunctionParameterTerm>(get_base_parameter(n+3));}

      /**
       * Get the return type of this function, as viewed from inside the
       * function (i.e., with parameterized types replaced by parameters
       * to this function).
       */
      Term* result_type() {return get_base_parameter(2);}

      BlockTerm* entry() {return cast<BlockTerm>(get_base_parameter(1));}
      void set_entry(BlockTerm* block);

      BlockTerm* new_block();
      BlockTerm* new_block(BlockTerm* dominator);

      void add_term_name(Term *term, const std::string& name);
      const TermNameMap& term_name_map() {return m_name_map;}
      std::vector<BlockTerm*> topsort_blocks();

    private:
      class Initializer;
      FunctionTerm(const UserInitializer&, Context*, FunctionTypeTerm*, const std::string&, Module*);
      TermNameMap m_name_map;
    };

#ifndef PSI_DOXYGEN
    template<> struct CastImplementation<FunctionTerm> : CoreCastImplementation<FunctionTerm, term_function> {};
#endif

    /**
     * \brief Term type appearing in dependent types of completed function types.
     */
    PSI_TVM_FUNCTIONAL_TYPE(FunctionTypeResolvedParameter, FunctionalOperation)
    struct Data {
      Data(unsigned depth_, unsigned index_)
        : depth(depth_), index(index_) {}

      unsigned depth;
      unsigned index;

      bool operator == (const Data&) const;
      friend std::size_t hash_value(const Data&);
    };
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the depth of this parameter relative to the
    /// function type it is part of.
    ///
    /// If a parameter is used as the type of a parameter in its own
    /// functions argument list, then this is zero. For each function
    /// type it is then nested inside, this should increase by one.
    unsigned depth() const {return data().depth;}
    /// \brief Get the parameter number of this parameter in its
    /// function.
    unsigned index() const {return data().index;}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term* type, unsigned depth, unsigned index);
    PSI_TVM_FUNCTIONAL_TYPE_END(FunctionTypeResolvedParameter)

    /**
     * \brief Type of functions.
     * 
     * This is also used for implementing template types since the
     * template may be specialized by computing the result type of a
     * function call (note that there is no <tt>result_of</tt> term,
     * since the result must be the appropriate term itself).
     */
    class FunctionTypeTerm : public HashTerm {
      friend class Context;
    public:
      Term* parameter_type(std::size_t n) {return get_base_parameter(n+1);}
      /// \brief Return the number of phantom parameters.
      std::size_t n_phantom_parameters() {return m_n_phantom;}
      /// \brief Return the number of parameters, including both
      /// phantom and ordinary parameters.
      std::size_t n_parameters() {return n_base_parameters()-1;}
      Term* result_type() {return get_base_parameter(0);}
      CallingConvention calling_convention() {return m_calling_convention;}
      Term* parameter_type_after(ArrayPtr<Term*const> previous);
      Term* result_type_after(ArrayPtr<Term*const> parameters);

    private:
      class Setup;
      FunctionTypeTerm(const UserInitializer& ui, Context *context, std::size_t hash, Term* result_type,
                       ArrayPtr<Term*const> parameter_types, std::size_t n_phantom, CallingConvention calling_convention);

      std::size_t m_n_phantom;
      CallingConvention m_calling_convention;
    };

#ifndef PSI_DOXYGEN
    template<> struct CastImplementation<FunctionTypeTerm> : CoreCastImplementation<FunctionTypeTerm, term_function_type> {};
#endif

    class FunctionTypeParameterTerm : public Term {
      friend class Context;
      class Initializer;
      FunctionTypeParameterTerm(const UserInitializer&, Context*, Term*);
    };

#ifndef PSI_DOXYGEN
    template<> struct CastImplementation<FunctionTypeParameterTerm> : CoreCastImplementation<FunctionTypeParameterTerm, term_function_type_parameter> {};
#endif

    /**
     * Helper class for inserting instructions into blocks.
     */
    class InstructionInsertPoint {
      /// \brief Block to insert instructions into
      BlockTerm *m_block;
      /// \brief Instruction to insert new instructions before
      InstructionTerm *m_instruction;
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
      explicit InstructionInsertPoint(BlockTerm *insert_at_end)
      : m_block(insert_at_end), m_instruction(0) {}
      
      /**
       * Construct an inserter which inserts instructions in the same
       * block as the specified instruction, just before it.
       * 
       * \param insert_before Instruction to insert created instructions
       * before.
       */
      explicit InstructionInsertPoint(InstructionTerm *insert_before)
      : m_block(insert_before->block()), m_instruction(insert_before) {}
      
      static InstructionInsertPoint after_source(Term*);
      
      template<typename T>
      typename T::Ptr create(ArrayPtr<Term*const> parameters, const typename T::Data& data = typename T::Data()) {
        PSI_ASSERT(m_block);
        return m_block->new_instruction<T>(parameters, m_instruction, data);
      }
    };

    template<typename T>
    struct InstructionCastImplementation {
      typedef typename T::Ptr Ptr;

      static Ptr cast(Term *t) {
        return cast(checked_cast<InstructionTerm*>(t));
      }

      static Ptr cast(InstructionTerm *t) {
        PSI_ASSERT(T::operation == t->operation());
        return Ptr(typename T::PtrHook(checked_cast<InstructionTermSpecialized<T>*>(t)));
      }

      static bool isa(Term *t) {
        InstructionTerm *ft = dyn_cast<InstructionTerm>(t);
        if (!ft)
          return false;

        return T::operation == ft->operation();
      }
    };

#define PSI_TVM_INSTRUCTION_TYPE(name) \
    struct name : NonConstructible {   \
    typedef name ThisType;             \
    static const char operation[];

#define PSI_TVM_INSTRUCTION_PTR_HOOK()                                  \
    struct PtrHook : InstructionTermPtrBase<Data> {                     \
    friend struct InstructionCastImplementation<ThisType>;              \
    friend class InstructionTermSpecialized<ThisType>;                  \
  private:                                                              \
  explicit PtrHook(InstructionTerm *t) : InstructionTermPtrBase<Data>(t) {} \
  public:                                                               \
  PtrHook() {}

#define PSI_TVM_INSTRUCTION_PTR_HOOK_END() }; typedef PtrDecayAdapter<PtrHook> Ptr;

#ifndef PSI_DOXYGEN
#define PSI_TVM_INSTRUCTION_TYPE_CAST(name) template<> struct CastImplementation<name> : InstructionCastImplementation<name> {};
#else
#define PSI_TVM_INSTRUCTION_TYPE_CAST(name)
#endif

#define PSI_TVM_INSTRUCTION_TYPE_END(name)                              \
    static Term* type(FunctionTerm*, const Data&, ArrayPtr<Term*const>); \
    static void jump_targets(Ptr, std::vector<BlockTerm*>&);            \
  }; PSI_TVM_INSTRUCTION_TYPE_CAST(name)

    inline BlockTerm* PhiTerm::block() {
      return cast<BlockTerm>(get_base_parameter(0));
    }

    inline BlockTerm* PhiTerm::incoming_block(std::size_t n) {
      return cast<BlockTerm>(get_base_parameter(n*2+1));
    }

    inline BlockTerm* BlockTerm::dominator() {
      return cast<BlockTerm>(get_base_parameter(1));
    }

    inline FunctionTerm* BlockTerm::function() {
      return cast<FunctionTerm>(get_base_parameter(0));
    }

    inline FunctionTerm* FunctionParameterTerm::function() {
      return cast<FunctionTerm>(get_base_parameter(0));
    }

    inline FunctionTypeTerm* FunctionTerm::function_type() {
      return cast<FunctionTypeTerm>(value_type());
    }
  }
}

#endif
