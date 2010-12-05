#ifndef HPP_PSI_TVM_FUNCTION
#define HPP_PSI_TVM_FUNCTION

#include <boost/preprocessor/facilities/intercept.hpp>
#include <boost/preprocessor/repetition/repeat.hpp>
#include <boost/preprocessor/repetition/enum_binary_params.hpp>
#include <boost/preprocessor/repetition/enum_trailing_params.hpp>

#include "Core.hpp"

namespace Psi {
  namespace Tvm {
    /**
     * \brief Base class for building custom InstructionTerm instances.
     */
    class InstructionTermBackend : public TermBackend {
    public:
      virtual InstructionTermBackend* clone(void *dest) const = 0;

      /**
       * Get the result type of this instruction for the given
       * arguments. If this returns NULL, it means that this
       * instruction terminates the block.
       */
      virtual Term* type(Context& context, const FunctionTerm& function, ArrayPtr<Term*const> parameters) const = 0;

      /**
       * Generate code to calculate the value for this term.
       *
       * \param builder Builder used to get functional values and to
       * create instructions.
       *
       * \param term Term (with parameters) to generate code for.
       */
      virtual LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) const = 0;

      /**
       * Get blocks which this function could jump to. This will only
       * be called if #type returns NULL indicating this instruction
       * terminates a block.
       */
      virtual void jump_targets(Context&, InstructionTerm&, std::vector<BlockTerm*>& targets) const = 0;
    };

    class BlockTerm;

    /**
     * \brief Instruction term. Per-instruction funtionality is
     * created by implementing InstructionTermBackend and wrapping
     * that in InstructionTerm.
     */
    class InstructionTerm : public Term {
      friend class BlockTerm;

    public:
      virtual ~InstructionTerm();

      const InstructionTermBackend* backend() const {return m_backend;}
      BlockTerm* block() const {return checked_cast<BlockTerm*>(source());}
      std::size_t n_parameters() const {return Term::n_base_parameters();}
      Term* parameter(std::size_t n) const {return get_base_parameter(n);}

    private:
      class Initializer;
      InstructionTerm(const UserInitializer& ui, Context *context,
		      Term* type, ArrayPtr<Term*const> parameters,
		      InstructionTermBackend *backend,
                      BlockTerm* block);

      InstructionTermBackend *m_backend;

      typedef boost::intrusive::list_member_hook<> InstructionListHook;
      InstructionListHook m_instruction_list_hook;
    };

    template<>
    struct TermIteratorCheck<InstructionTerm> {
      static bool check (TermType t) {
	return t == term_instruction;
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
      BlockTerm* block() const {return checked_cast<BlockTerm*>(source());}
      void add_incoming(BlockTerm* block, Term* value);

      /// \brief Number of incoming edges
      std::size_t n_incoming() const {return m_n_incoming;}
      /// \brief Get the block corresponding to a given incoming edge
      BlockTerm *incoming_block(std::size_t n) {return checked_cast<BlockTerm*>(get_base_parameter(n*2));}
      /// \brief Get the value of a given incoming edge
      Term *incoming_value(std::size_t n) {return get_base_parameter(n*2+1);}

    private:
      class Initializer;
      PhiTerm(const UserInitializer& ui, Context *context, Term* type, BlockTerm *block);
      typedef boost::intrusive::list_member_hook<> PhiListHook;
      PhiListHook m_phi_list_hook;
      std::size_t m_n_incoming;
    };

    template<>
    struct TermIteratorCheck<PhiTerm> {
      static bool check (TermType t) {
	return t == term_phi;
      }
    };

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

      template<typename T>
      InstructionTermPtr<T> new_instruction(const T& proto, ArrayPtr<Term*const> parameters);

      const InstructionList& instructions() const {return m_instructions;}
      const PhiList& phi_nodes() const {return m_phi_nodes;}

      /** \brief Whether this block has been terminated so no more instructions can be added. */
      bool terminated() const {return m_terminated;}
      /** \brief Get the function which contains this block. */
      FunctionTerm* function() const {return checked_cast<FunctionTerm*>(get_base_parameter(0));}
      /** \brief Get a pointer to the (currently) dominating block. */
      BlockTerm* dominator() const {return checked_cast<BlockTerm*>(get_base_parameter(1));}

      bool check_available(Term* term) const;
      bool dominated_by(BlockTerm* block) const;
      std::vector<BlockTerm*> successors() const;
      std::vector<BlockTerm*> recursive_successors() const;
      std::vector<BlockTerm*> dominated_blocks() const;

#define PSI_TVM_VA(z,n,data) template<typename T> InstructionTermPtr<T> new_instruction_v(const T& proto BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,Term* p)) {Term *ap[n] = {BOOST_PP_ENUM_PARAMS_Z(z,n,p)}; return new_instruction(proto, ArrayPtr<Term*const>(ap,n));}
      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

    private:
      class Initializer;
      BlockTerm(const UserInitializer& ui, Context *context, FunctionTerm* function, BlockTerm* dominator);
      InstructionList m_instructions;
      PhiList m_phi_nodes;
      bool m_terminated;

      InstructionTerm* new_instruction_internal(const InstructionTermBackend& backend, ArrayPtr<Term*const> parameters);
    };

    bool block_dominates(BlockTerm *a, BlockTerm *b);

    template<>
    struct TermIteratorCheck<BlockTerm> {
      static bool check (TermType t) {
	return t == term_block;
      }
    };

    class FunctionTerm;

    class FunctionParameterTerm : public Term {
      friend class FunctionTerm;
    public:
      FunctionTerm* function() const {return checked_cast<FunctionTerm*>(get_base_parameter(0));}

    private:
      class Initializer;
      FunctionParameterTerm(const UserInitializer& ui, Context *context, FunctionTerm* function, Term* type, bool phantom);
    };

    template<>
    struct TermIteratorCheck<FunctionParameterTerm> {
      static bool check (TermType t) {
	return t == term_function_parameter;
      }
    };

    /**
     * \brief %Function.
     */
    class FunctionTerm : public GlobalTerm {
      friend class Context;
    public:
      typedef std::tr1::unordered_multimap<Term*, std::string> TermNameMap;

      FunctionTypeTerm* function_type() const;

      std::size_t n_parameters() const {return n_base_parameters() - 2;}
      /** \brief Get a function parameter. */
      FunctionParameterTerm* parameter(std::size_t n) const {return checked_cast<FunctionParameterTerm*>(get_base_parameter(n+2));}

      /**
       * Get the return type of this function, as viewed from inside the
       * function (i.e., with parameterized types replaced by parameters
       * to this function).
       */
      Term* result_type() const {return get_base_parameter(1);}

      BlockTerm* entry() {return checked_cast<BlockTerm*>(get_base_parameter(0));}
      void set_entry(BlockTerm* block);

      BlockTerm* new_block();
      BlockTerm* new_block(BlockTerm* dominator);

      void add_term_name(Term *term, const std::string& name);
      const TermNameMap& term_name_map() const {return m_name_map;}

    private:
      class Initializer;
      FunctionTerm(const UserInitializer& ui, Context *context, FunctionTypeTerm* type, const std::string& name);
      TermNameMap m_name_map;
    };

    template<>
    struct TermIteratorCheck<FunctionTerm> {
      static bool check (TermType t) {
	return t == term_function;
      }
    };

    class FunctionTypeTerm;

    class FunctionTypeParameterTerm : public Term {
      friend class Context;
    public:
      FunctionTypeTerm* source() const;
      std::size_t index() const {return m_index;}

    private:
      class Initializer;
      FunctionTypeParameterTerm(const UserInitializer& ui, Context *context, Term* type);
      void set_source(FunctionTypeTerm *term);
      std::size_t m_index;
    };

    template<>
    struct TermIteratorCheck<FunctionTypeParameterTerm> {
      static bool check (TermType t) {
	return t == term_function_type_parameter;
      }
    };

    /**
     * Term for types of functions. This cannot be implemented as a
     * regular type because the types of later parameters can depend
     * on the values of earlier parameters.
     *
     * This is also used for implementing template types since the
     * template may be specialized by computing the result type of a
     * function call (note that there is no <tt>result_of</tt> term,
     * since the result must be the appropriate term itself).
     */
    class FunctionTypeTerm : public Term {
      friend class Context;
    public:
      /// \brief Return the number of phantom parameters.
      std::size_t n_phantom_parameters() const {return m_n_phantoms;}
      /// \brief Return the number of parameters, including both
      /// phantom and ordinary parameters.
      std::size_t n_parameters() const {return n_base_parameters() - 1;}
      FunctionTypeParameterTerm* parameter(std::size_t i) const {return checked_cast<FunctionTypeParameterTerm*>(get_base_parameter(i+1));}
      Term* result_type() const {return get_base_parameter(0);}
      CallingConvention calling_convention() const {return m_calling_convention;}
      Term* parameter_type_after(ArrayPtr<Term*const> previous) const;
      Term* result_type_after(ArrayPtr<Term*const> parameters) const;

    private:
      class Initializer;
      FunctionTypeTerm(const UserInitializer& ui, Context *context, Term *result_type,
                       ArrayPtr<FunctionTypeParameterTerm*const> phantom_parameters,
		       ArrayPtr<FunctionTypeParameterTerm*const> parameters,
		       CallingConvention calling_convention);

      CallingConvention m_calling_convention;
      std::size_t m_n_phantoms;
    };

    template<>
    struct TermIteratorCheck<FunctionTypeTerm> {
      static bool check (TermType t) {
	return t == term_function_type;
      }
    };

    inline FunctionTypeTerm* FunctionTypeParameterTerm::source() const {
      return checked_cast<FunctionTypeTerm*>(get_base_parameter(0));
    }
 
    inline void FunctionTypeParameterTerm::set_source(FunctionTypeTerm *term) {
      set_base_parameter(0, term);
    }

    /**
     * \brief Internal type used to build function types.
     */
    class FunctionTypeResolverTerm : public HashTerm {
      friend class Context;
    public:
      Term* parameter_type(std::size_t n) const {return get_base_parameter(n+2);}
      std::size_t n_phantom_parameters() const {return m_n_phantom;}
      std::size_t n_parameters() const {return n_base_parameters()-2;}
      Term* result_type() const {return get_base_parameter(1);}
      CallingConvention calling_convention() const {return m_calling_convention;}

    private:
      class Setup;
      FunctionTypeResolverTerm(const UserInitializer& ui, Context *context, std::size_t hash, Term* result_type,
                               ArrayPtr<Term*const> parameter_types, std::size_t n_phantom, CallingConvention calling_convention);
      FunctionTypeTerm* get_function_type() const {return checked_cast<FunctionTypeTerm*>(get_base_parameter(0));}
      void set_function_type(FunctionTypeTerm *term) {set_base_parameter(0, term);}

      std::size_t m_n_phantom;
      CallingConvention m_calling_convention;
    };

    template<>
    struct TermIteratorCheck<FunctionTypeResolverTerm> {
      static bool check (TermType t) {
	return t == term_function_type_resolver;
      }
    };

    /**
     * \brief Internal type used to build function types.
     */
    class FunctionTypeResolverParameter {
    public:
      FunctionTypeResolverParameter(std::size_t depth, std::size_t index);

      FunctionalTypeResult type(Context& context, ArrayPtr<Term*const> parameters) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const;

      LLVMType llvm_type(LLVMValueBuilder&, Term&) const;
      bool operator == (const FunctionTypeResolverParameter&) const;
      friend std::size_t hash_value(const FunctionTypeResolverParameter&);

      class Access {
      public:
	Access(const FunctionalTerm*, const FunctionTypeResolverParameter *self) : m_self(self) {}
	std::size_t depth() const {return m_self->m_depth;}
	std::size_t index() const {return m_self->m_index;}
      private:
	const FunctionTypeResolverParameter *m_self;
      };

    private:
      std::size_t m_depth;
      std::size_t m_index;
    };

    template<typename T>
    class InstructionTermBackendImpl : public InstructionTermBackend {
    public:
      typedef T ImplType;
      typedef InstructionTermBackendImpl<T> ThisType;

      InstructionTermBackendImpl(const T& impl) : m_impl(impl) {}

      virtual Term* type(Context& context, const FunctionTerm& function, ArrayPtr<Term*const> parameters) const {
	return m_impl.type(context, function, parameters);
      }

      virtual std::pair<std::size_t, std::size_t> size_align() const {
        return std::make_pair(sizeof(ThisType), boost::alignment_of<ThisType>::value);
      }

      virtual InstructionTermBackend* clone(void *dest) const {
        return new (dest) ThisType(*this);
      }

      virtual LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) const {
	return m_impl.llvm_value_instruction(builder, term);
      }

      virtual void jump_targets(Context& context, InstructionTerm& term, std::vector<BlockTerm*>& targets) const {
        return m_impl.jump_targets(context, term, targets);
      }

    private:
      ImplType m_impl;
    };

    template<typename T>
    InstructionTermPtr<T> BlockTerm::new_instruction(const T& proto, ArrayPtr<Term*const> parameters) {
      return InstructionTermPtr<T>(new_instruction_internal(InstructionTermBackendImpl<T>(proto), parameters));
    }
  }
}

#endif
