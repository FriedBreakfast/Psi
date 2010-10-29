#ifndef HPP_PSI_TVM_FUNCTION
#define HPP_PSI_TVM_FUNCTION

#include "Core.hpp"

namespace Psi {
  namespace Tvm {
    /**
     * \brief Base class for building custom InstructionTerm instances.
     */
    class InstructionTermBackend {
    public:
      virtual ~InstructionTermBackend();
      std::size_t hash_value() const;
      virtual bool equals(const InstructionTermBackend&) const = 0;
      virtual std::pair<std::size_t, std::size_t> size_align() const = 0;
      virtual InstructionTermBackend* clone(void *dest) const = 0;
      virtual TermPtr<> type(Context& context, std::size_t n_parameters, Term *const* parameters) const = 0;
      virtual LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, InstructionTerm&) const = 0;
    private:
      virtual std::size_t hash_internal() const = 0;
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
      const InstructionTermBackend* backend() const {return m_backend;}
      std::size_t n_parameters() const {return Term::n_base_parameters();}
      TermPtr<> parameter(std::size_t n) const {return get_base_parameter(n);}

    private:
      InstructionTerm(const UserInitializer& ui,
		      TermRef<> type, std::size_t n_parameters, Term *const* parameters,
		      InstructionTermBackend *backend);

      InstructionTermBackend *m_backend;

      typedef boost::intrusive::list_member_hook<> InstructionListHook;
      InstructionListHook m_instruction_list_hook;
      BlockTerm *m_block;
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
      TermPtr<> new_instruction(const T& proto, std::size_t n_parameters, Term *const* parameters);

      const InstructionList& instructions() const {return m_instructions;}
      const PhiList& phi_nodes() const {return m_phi_nodes;}

      /** \brief Get a pointer to the (currently) dominating block. */
      TermPtr<BlockTerm> dominator() const {return get_base_parameter<BlockTerm>(0);}
      /** \brief Get the earliest block dominating this one which is required by variables used by instructions in this block. */
      TermPtr<BlockTerm> min_dominator() const {return get_base_parameter<BlockTerm>(1);}

    private:
      typedef boost::intrusive::list_member_hook<> BlockListHook;
      class Initializer;
      BlockTerm(const UserInitializer& ui, Context *context, FunctionTerm *function);
      BlockListHook m_block_list_hook;
      InstructionList m_instructions;
      PhiList m_phi_nodes;
      TermPtr<> new_instruction_internal(const InstructionTermBackend& backend, std::size_t n_parameters, Term *const* parameters);
    };

    class FunctionTerm;

    class FunctionParameterTerm : public Term {
      friend class FunctionTerm;
    public:

    private:
      class Initializer;
      FunctionParameterTerm(const UserInitializer& ui, Context *context, TermRef<> type);

      typedef boost::intrusive::list_member_hook<> FunctionParameterListHook;
      FunctionParameterListHook m_parameter_list_hook;

      std::size_t m_index;
      FunctionTerm *m_function;
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
      TermPtr<BlockTerm> entry() {return TermPtr<BlockTerm>(&m_blocks.front());}
      TermPtr<BlockTerm> new_block();
      CallingConvention calling_convention() const {return m_calling_convention;}

      const BlockList& blocks() const {return m_blocks;}
      const FunctionParameterList& parameters() const {return m_parameters;}

    private:
      class Initializer;
      FunctionTerm(const UserInitializer& ui, Context *context, TermRef<FunctionTypeTerm> type);
      BlockList m_blocks;
      FunctionParameterList m_parameters;
      CallingConvention m_calling_convention;
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

    private:
      class Initializer;
      FunctionTypeTerm(const UserInitializer& ui, Context *context, Term *result_type,
		       std::size_t n_parameters, FunctionTypeParameterTerm *const* parameters,
		       CallingConvention m_calling_convention);

      CallingConvention m_calling_convention;
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
    class FunctionTypeInternalTerm : public HashTerm {
      friend class Context;
    public:
      TermPtr<> parameter_type(std::size_t n) const {return get_base_parameter(n+2);}
      std::size_t n_parameters() const {return n_base_parameters()-2;}
      TermPtr<> result_type() const {return get_base_parameter(1);}
      CallingConvention calling_convention() const {return m_calling_convention;}

    private:
      class Setup;
      FunctionTypeInternalTerm(const UserInitializer& ui, Context *context, std::size_t hash, TermRef<> result_type,
			       std::size_t n_parameters, Term *const* parameter_types, CallingConvention calling_convention);
      FunctionTypeTerm* get_function_type() const {return checked_cast<FunctionTypeTerm*>(get_base_parameter_ptr(0));}
      void set_function_type(FunctionTypeTerm *term) {set_base_parameter(0, term);}

      CallingConvention m_calling_convention;
    };

    /**
     * \brief Internal type used to build function types.
     */
    class FunctionTypeInternalParameterTerm : public HashTerm {
      friend class Context;
    public:

    private:
      class Setup;
      FunctionTypeInternalParameterTerm(const UserInitializer& ui, Context *context, std::size_t hash, TermRef<> type, std::size_t depth, std::size_t index);
      std::size_t m_depth;
      std::size_t m_index;
    };

    template<typename T>
    class InstructionTermBackendImpl : public InstructionTermBackend {
    public:
      typedef T ImplType;
      typedef InstructionTermBackendImpl<T> ThisType;

      virtual TermPtr<> type(Context& context, std::size_t n_parameters, Term *const* parameters) const {
	return m_impl.type(context, n_parameters, parameters);
      }

      virtual std::pair<std::size_t, std::size_t> size_align() const {
        return std::make_pair(sizeof(ThisType), boost::alignment_of<ThisType>::value);
      }

      virtual bool equals(const InstructionTermBackend& other) const {
	return m_impl == checked_cast<const ThisType&>(other).m_impl;
      }

      virtual InstructionTermBackend* clone(void *dest) const {
        return new (dest) ThisType(*this);
      }

      virtual LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) const {
	return m_impl.llvm_value_instruction(builder, term);
      }

    private:
      virtual std::size_t hash_internal() const {
        boost::hash<ImplType> hasher;
        return hasher(m_impl);
      }

      ImplType m_impl;
    };

    template<typename T>
    TermPtr<> BlockTerm::new_instruction(const T& proto, std::size_t n_parameters, Term *const* parameters) {
      return new_instruction_internal(InstructionTermBackendImpl<T>(proto), n_parameters, parameters);
    }
  }
}

#endif
