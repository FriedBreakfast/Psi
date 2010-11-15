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
      virtual TermPtr<> type(Context& context, const FunctionTerm& function, TermRefArray<> parameters) const = 0;
      virtual LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, InstructionTerm&) const = 0;
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
      std::size_t n_parameters() const {return Term::n_base_parameters();}
      TermPtr<> parameter(std::size_t n) const {return get_base_parameter(n);}

    private:
      class Initializer;
      InstructionTerm(const UserInitializer& ui, Context *context,
		      TermRef<> type, TermRefArray<> parameters,
		      InstructionTermBackend *backend);

      InstructionTermBackend *m_backend;

      typedef boost::intrusive::list_member_hook<> InstructionListHook;
      InstructionListHook m_instruction_list_hook;
      BlockTerm *m_block;
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
      void add_incoming(TermRef<BlockTerm> block, TermRef<> value);

    private:
      PhiTerm(const UserInitializer& ui, TermRef<> type);
      typedef boost::intrusive::list_member_hook<> PhiListHook;
      PhiListHook m_phi_list_hook;
      BlockTerm *m_block;
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

      void new_phi(TermRef<> type);

      template<typename T>
      TermPtr<InstructionTerm> new_instruction(const T& proto, TermRefArray<> parameters);

      const InstructionList& instructions() const {return m_instructions;}
      const PhiList& phi_nodes() const {return m_phi_nodes;}

      /** \brief Get the function which contains this block. */
      TermPtr<FunctionTerm> function() const {return get_base_parameter<FunctionTerm>(0);}
      /** \brief Get a pointer to the (currently) dominating block. */
      TermPtr<BlockTerm> dominator() const {return get_base_parameter<BlockTerm>(1);}
      /** \brief Get the earliest block dominating this one which is required by variables used by instructions in this block. */
      TermPtr<BlockTerm> min_dominator() const {return get_base_parameter<BlockTerm>(2);}

#define PSI_TVM_VA(z,n,data) template<typename T> TermPtr<InstructionTerm> new_instruction_v(const T& proto BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,TermRef<> p)) {Term *ap[n] = {BOOST_PP_ENUM_BINARY_PARAMS_Z(z,n,p,.get() BOOST_PP_INTERCEPT)}; return new_instruction(proto, TermRefArray<>(n,ap));}
      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

    private:
      typedef boost::intrusive::list_member_hook<> BlockListHook;
      class Initializer;
      BlockTerm(const UserInitializer& ui, Context *context, FunctionTerm *function);
      BlockListHook m_block_list_hook;
      InstructionList m_instructions;
      PhiList m_phi_nodes;

      TermPtr<InstructionTerm> new_instruction_internal(const InstructionTermBackend& backend, TermRefArray<> parameters);
      void set_dominator(TermRef<BlockTerm> value) {set_base_parameter(1, value);}
      void set_min_dominator(TermRef<BlockTerm> value) {set_base_parameter(2, value);}
    };

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

    private:
      class Initializer;
      FunctionParameterTerm(const UserInitializer& ui, Context *context, TermRef<FunctionTerm> function, TermRef<> type);

      typedef boost::intrusive::list_member_hook<> FunctionParameterListHook;
      FunctionParameterListHook m_parameter_list_hook;

      std::size_t m_index;
      FunctionTerm *m_function;
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
      typedef boost::intrusive::list<BlockTerm,
				     boost::intrusive::member_hook<BlockTerm, BlockTerm::BlockListHook, &BlockTerm::m_block_list_hook>,
				     boost::intrusive::constant_time_size<false> > BlockList;
      typedef boost::intrusive::list<FunctionParameterTerm,
				     boost::intrusive::member_hook<FunctionParameterTerm, FunctionParameterTerm::FunctionParameterListHook, &FunctionParameterTerm::m_parameter_list_hook>,
				     boost::intrusive::constant_time_size<false> > FunctionParameterList;

      std::size_t n_parameters() const {return m_parameters.size();}
      TermPtr<FunctionParameterTerm> parameter(std::size_t n) const;
      TermPtr<BlockTerm> entry() {return TermPtr<BlockTerm>(&m_blocks.front());}
      TermPtr<BlockTerm> new_block();
      TermPtr<> result_type() const;
      TermPtr<FunctionTypeTerm> function_type() const;

      const BlockList& blocks() const {return m_blocks;}

    private:
      class Initializer;
      FunctionTerm(const UserInitializer& ui, Context *context, TermRef<FunctionTypeTerm> type);
      BlockList m_blocks;
      FunctionParameterList m_parameters;
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
      TermPtr<FunctionTypeTerm> source() const;
      std::size_t index() const {return m_index;}

    private:
      class Initializer;
      FunctionTypeParameterTerm(const UserInitializer& ui, Context *context, TermRef<> type);
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
      std::size_t n_parameters() const {return n_base_parameters() - 1;}
      TermPtr<FunctionTypeParameterTerm> parameter(std::size_t i) const {return get_base_parameter<FunctionTypeParameterTerm>(i+1);}
      TermPtr<> result_type() const {return get_base_parameter(0);}
      CallingConvention calling_convention() const {return m_calling_convention;}
      TermPtr<> parameter_type_after(TermRefArray<> previous) const;
      TermPtr<> result_type_after(TermRefArray<> parameters) const;

    private:
      class Initializer;
      FunctionTypeTerm(const UserInitializer& ui, Context *context, Term *result_type,
		       TermRefArray<FunctionTypeParameterTerm> parameters,
		       CallingConvention calling_convention);

      CallingConvention m_calling_convention;
    };

    template<>
    struct TermIteratorCheck<FunctionTypeTerm> {
      static bool check (TermType t) {
	return t == term_function_type;
      }
    };

    inline TermPtr<FunctionTypeTerm> FunctionTypeParameterTerm::source() const {
      return get_base_parameter<FunctionTypeTerm>(0);
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
      TermPtr<> parameter_type(std::size_t n) const {return get_base_parameter(n+2);}
      std::size_t n_parameters() const {return n_base_parameters()-2;}
      TermPtr<> result_type() const {return get_base_parameter(1);}
      CallingConvention calling_convention() const {return m_calling_convention;}

    private:
      class Setup;
      FunctionTypeResolverTerm(const UserInitializer& ui, Context *context, std::size_t hash, TermRef<> result_type,
                               TermRefArray<> parameter_types, CallingConvention calling_convention);
      FunctionTypeTerm* get_function_type() const {return checked_cast<FunctionTypeTerm*>(get_base_parameter_ptr(0));}
      void set_function_type(FunctionTypeTerm *term) {set_base_parameter(0, term);}

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

      TermPtr<> type(Context& context, TermRefArray<> parameters) const;
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

      virtual TermPtr<> type(Context& context, const FunctionTerm& function, TermRefArray<> parameters) const {
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

    private:
      ImplType m_impl;
    };

    template<typename T>
    TermPtr<InstructionTerm> BlockTerm::new_instruction(const T& proto, TermRefArray<> parameters) {
      return new_instruction_internal(InstructionTermBackendImpl<T>(proto), parameters);
    }
  }
}

#endif
