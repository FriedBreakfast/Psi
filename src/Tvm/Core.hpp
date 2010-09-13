#ifndef HPP_PSI_TVM_CORE
#define HPP_PSI_TVM_CORE

#include <vector>

#include <tr1/cstdint>
#include <tr1/unordered_map>
#include <tr1/unordered_set>

#include <boost/intrusive_ptr.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/unordered_set.hpp>

#include <boost/preprocessor/repetition/repeat.hpp>
#include <boost/preprocessor/repetition/enum_params.hpp>
#include <boost/preprocessor/repetition/enum_trailing_params.hpp>

#include "User.hpp"
#include "../Utility.hpp"
#include "LLVMBuilder.hpp"

namespace Psi {
  namespace Tvm {
    class Context;

    /**
     * \brief Base class for all compile- and run-time values.
     *
     * All types which inherit from Term must be known about by
     * Context since they perform some sort of unusual function; other
     * types and instructions are created by subclasses
     * FunctionalTermBackend and InstructionTermBackend and then
     * wrapping that in either FunctionalTerm or InstructionTerm.
     */
    class Term : public Used, public User {
      friend class Context;

      friend class BlockTerm;
      friend class DistinctTerm;
      friend class FunctionalBaseTerm;
      friend class FunctionParameterTerm;
      friend class InstructionTerm;
      friend class MetatypeTerm;
      friend class PhiTerm;
      friend class TemporaryTerm;

    public:
      enum Category {
	category_metatype,
	category_type,
	category_value
      };

      /**
       * \brief Identifies the Term subclass this object actually is.
       */
      enum TermType {
	term_functional, ///< FunctionalTerm: \copybrief FunctionalTerm
	term_instruction, ///< InstructionTerm: \copybrief InstructionTerm
	term_opaque, ///< OpaqueTerm: \copybrief OpaqueTerm
	term_opaque_resolver, ///< OpaqueResolverTerm: \copybrief OpaqueResolverTerm
	term_block, ///< BlockTerm: \copybrief BlockTerm
	term_global_variable, ///< GlobalVariableTerm: \copybrief GlobalVariableTerm
	term_function, ///< FunctionTerm: \copybrief FunctionTerm
	term_function_parameter, ///< FunctionParameterTerm: \copybrief FunctionParameterTerm
	term_phi, ///< PhiTerm: \copybrief PhiTerm
	term_function_type, ///< FunctionTypeTerm: \copybrief FunctionTypeTerm
	term_function_type_parameter, ///< FunctionTypeParameterTerm: \copybrief FunctionTypeParameterTerm
	term_metatype, ///< MetatypeTerm: \copybrief MetatypeTerm
	term_temporary ///< TemporaryTerm: \copybrief TemporaryTerm
      };

      bool complete() const {return m_complete;}

      /**
       * \brief Get the context this Term comes from.
       */
      Context& context() const {return *m_context;}

      /**
       *
       */
      Term* type() const {return use_get<Term>(0);}

    private:
      Term(const UserInitializer& ui, Context *context, TermType term_type, bool complete, Term *type);
      ~Term();

      Context *m_context;
      unsigned char m_term_type;
      unsigned char m_complete;
      unsigned char m_category;

    protected:
      std::size_t n_parameters() const {
	return use_slots() - 1;
      }

      void set_parameter(std::size_t n, Term *t) {
	PSI_ASSERT_MSG(m_context == t->m_context, "term context mismatch");
	use_set(n+1, t);
      }

      Term* get_parameter(std::size_t n) const {
	return use_get<Term>(n+1);
      }
    };

    /**
     * \brief Unique type which is the type of type terms.
     */
    class MetatypeTerm : public Term {
      friend class Context;

    private:
      struct Initializer;
      MetatypeTerm(const UserInitializer&, Context*);
    };

    /**
     * \brief Base class of functional (machine state independent) terms.
     *
     * Functional terms are special since two terms of the same
     * operation and with the same parameters are equivalent; they are
     * therefore unified into one term so equivalence can be checked
     * via pointer equality. This is particularly required for type
     * checking, but also applies to other terms.
     */
    class FunctionalBaseTerm : public Term {
      friend class Context;
      friend class FunctionalTerm;
      friend class FunctionTypeTerm;
      friend class FunctionTypeParameterTerm;
      friend class OpaqueResolverTerm;

    private:
      FunctionalBaseTerm(const UserInitializer& ui, Context *context,
			 TermType term_type, bool complete, Term *type,
			 std::size_t hash);
      ~FunctionalBaseTerm();
      static bool check_complete(Term *type, std::size_t n_parameters, Term *const* parameters);

      typedef boost::intrusive::unordered_set_member_hook<> TermSetHook;
      TermSetHook m_term_set_hook;
      std::size_t m_hash;

      bool m_resolve_source;
      /**
       * If #m_resolve_source is true, this points to the term that
       * this term resolves to. If it is false, this pointer to the
       * term that resolves to this one.
       */
      FunctionalBaseTerm *m_resolve;
    };

    /**
     * \brief Base class for building custom FunctionalTerm instances.
     */
    class FunctionalTermBackend {
    public:
      virtual ~FunctionalTermBackend();
      std::size_t hash_value() const;
      virtual bool equals(const FunctionalTermBackend&) const = 0;
      virtual std::pair<std::size_t, std::size_t> size_align() const = 0;
      virtual FunctionalTermBackend* clone(void *dest) const = 0;
      virtual Term* type(Context& context, std::size_t n_parameters, Term *const* parameters) const = 0;
#if 0
      virtual LLVMFunctionBuilder::Result llvm_value_instruction(LLVMFunctionBuilder&, Term*) const = 0;
      virtual LLVMConstantBuilder::Constant llvm_value_constant(LLVMConstantBuilder&, Term*) const = 0;
      virtual LLVMConstantBuilder::Type llvm_type(LLVMConstantBuilder&, Term*) const = 0;
#endif

    private:
      virtual std::size_t hash_internal() const = 0;
    };

    /**
     * \brief Class of most functional terms. Functionality is
     * provided by a user-specified FunctionalTermBackend instance.
     */
    class FunctionalTerm : public FunctionalBaseTerm {
      friend class Context;

    public:
      std::size_t n_parameters() const {return Term::n_parameters();}
      Term* parameter(std::size_t n) const {return get_parameter(n);}

    private:
      struct Initializer;
      FunctionalTerm(const UserInitializer& ui, Context *context, Term *type,
		     std::size_t hash, FunctionalTermBackend *backend,
		     std::size_t n_parameters, Term *const* parameters);
      ~FunctionalTerm();
      FunctionalTermBackend *m_backend;
    };

    /**
     * \brief Base class for terms which are stored in the context's
     * distinct term list.
     *
     * Subclasses of this class are opaque terms and globals, both of
     * which are always considered unique. Instructions, blocks and
     * phi terms are also always unique, however, they do not subclass
     * DistinctTerm since Context does not directly hold a reference
     * to them.
     */
    class DistinctTerm : public Term {
      friend class Context;
      friend class GlobalTerm;
      friend class OpaqueTerm;

    private:
      DistinctTerm(const UserInitializer& ui, Context* context, TermType term_type, bool complete, Term *type);
      ~DistinctTerm();

      typedef boost::intrusive::list_member_hook<> TermListHook;
      TermListHook m_term_list_hook;
    };

    /**
     * \brief Base class for building custom InstructionTerm instances.
     */
    class InstructionTermBackend {
    public:
#if 0
      virtual LLVMFunctionBuilder::Result llvm_value_instruction(LLVMFunctionBuilder&, Term*) const = 0;
      virtual LLVMConstantBuilder::Type llvm_type(LLVMConstantBuilder&, Term*) const = 0;
#endif
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
      std::size_t n_parameters() const {return Term::n_parameters();}
      Term* parameter(std::size_t n) const {return get_parameter(n);}

    private:
      InstructionTerm(const UserInitializer& ui,
		      Term *type, std::size_t n_parameters, Term *const* parameters,
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
      void add_incoming(BlockTerm *block, Term *value);

    private:
      PhiTerm(const UserInitializer& ui, Term *type);
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
      void new_phi(Term *type);
      void add_instruction();

    private:
      typedef boost::intrusive::list_member_hook<> BlockListHook;
      BlockListHook m_block_list_hook;

      typedef boost::intrusive::list<InstructionTerm,
				     boost::intrusive::member_hook<InstructionTerm, InstructionTerm::InstructionListHook, &InstructionTerm::m_instruction_list_hook>,
				     boost::intrusive::constant_time_size<false> > InstructionList;
      InstructionList m_instructions;

      typedef boost::intrusive::list<PhiTerm,
				     boost::intrusive::member_hook<PhiTerm, PhiTerm::PhiListHook, &PhiTerm::m_phi_list_hook>,
				     boost::intrusive::constant_time_size<false> > PhiList;
      PhiList m_phi_nodes;
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
    class FunctionTypeTerm : public FunctionalBaseTerm {
      friend class Context;
    public:
      std::size_t n_function_parameters() const {return n_parameters() - 1;}
      Term* function_parameter(std::size_t i) const {return get_parameter(i+1);}
      Term* function_result_type() const {return get_parameter(0);}

    private:
      struct Initializer;
      FunctionTypeTerm(const UserInitializer& ui, Context *context, std::size_t hash, Term *result_type, std::size_t n_parameters, Term *const* parameter_types);
    };

    class FunctionTypeParameterTerm : public FunctionalBaseTerm {
      friend class Context;
    public:
      Term* source() const {return get_parameter(0);}
      std::size_t index() const {return m_index;}

    private:
      struct Initializer;
      FunctionTypeParameterTerm(const UserInitializer& ui, Context *context, Term *type, std::size_t hash, Term *source, std::size_t index);

      std::size_t m_index;
    };

    /**
     * \brief Opaque term (as in LLVM). %Used to construct recursive
     * types.
     *
     * To create a recursive type (or term), first create an
     * OpaqueTerm using Context::new_opaque, create the type as normal
     * and then call #resolve to finalize the type. Any term
     * containing an opaque term is incomplete; calling #resolve will
     * cause new terms to be generated (or equivalent existing terms
     * to be found). Incomplete terms cannot be used as arguments to
     * non-functional terms.
     */
    class OpaqueTerm : public DistinctTerm {
      friend class Context;

    public:
      /**
       * \brief Resolve this term to its actual value.
       */
      template<typename T> T* resolve(T *term);

    private:
      struct Initializer;
      OpaqueTerm(const UserInitializer& ui, Context *context, Term *type);
    };

    /**
     * \brief Internal type used to solve structural equivalence when
     * resolving opaque terms.
     */
    class OpaqueResolverTerm : public FunctionalBaseTerm {
      friend class Context;

    private:
      struct Initializer;
      OpaqueResolverTerm(const UserInitializer& ui, Context *context, std::size_t hash, Term *type, std::size_t depth);

      /**
       * Depth of this instance of the opaque term from the root
       * instance.
       */
      std::size_t m_depth;
    };

    /**
     * \brief Internal type used during term resolution as a
     * placeholder when reconstructing circular types - it is always
     * replaced.
     */
    class TemporaryTerm : public Term {
      friend class Context;

    private:
      TemporaryTerm(Context *context, bool complete, Term *type);
      StaticUses<1> m_uses;
    };

    /**
     * \brief Base class for globals: these are GlobalVariableTerm and FunctionTerm.
     */
    class GlobalTerm : public DistinctTerm {
    };

    /**
     * \brief Global variable.
     */
    class GlobalVariableTerm : public GlobalTerm {
      friend class Context;

    public:
      void set_value(Term *value);

    private:
      struct Initializer;
      /**
       * Need to add parameters for linkage and possibly thread
       * locality.
       */
      GlobalVariableTerm(const UserInitializer& ui, Context *context, Term *type, bool constant);

      bool m_constant;
    };

    class FunctionTerm;

    class FunctionParameterTerm : public Term {
      friend class FunctionTerm;
    public:

    private:
      FunctionParameterTerm(const UserInitializer& ui, Context *context, FunctionTerm *function, std::size_t index);

      typedef boost::intrusive::list_member_hook<> FunctionParameterListHook;
      FunctionParameterListHook m_parameter_list_hook;

      std::size_t m_index;
      FunctionTerm *m_function;
    };

    /**
     * \brief %Function.
     */
    class FunctionTerm : public GlobalTerm {
    public:
      BlockTerm *entry();
      BlockTerm *new_block();

    private:
      typedef boost::intrusive::list<BlockTerm,
				     boost::intrusive::member_hook<BlockTerm, BlockTerm::BlockListHook, &BlockTerm::m_block_list_hook>,
				     boost::intrusive::constant_time_size<false> > BlockList;
      BlockList m_blocks;

      typedef boost::intrusive::list<FunctionParameterTerm,
				     boost::intrusive::member_hook<FunctionParameterTerm, FunctionParameterTerm::FunctionParameterListHook, &FunctionParameterTerm::m_parameter_list_hook>,
				     boost::intrusive::constant_time_size<false> > FunctionParameterList;
      FunctionParameterList m_parameters;
    };

    class Context {
      struct FunctionalBaseTermDisposer;
      struct DistinctTermDisposer;

      UniquePtr<MetatypeTerm> m_metatype;

      //struct FunctionalBaseTermEquals {bool operator () (const FunctionalBaseTerm&, const FunctionalBaseTerm&) const;};
      struct FunctionalBaseTermHash {std::size_t operator () (const FunctionalBaseTerm&) const;};
      struct FunctionalTermKeyEquals;
      struct FunctionalTermKeyWithTypeEquals;
      struct FunctionTypeTermKeyEquals;
      struct FunctionTypeParameterTermKeyEquals;
      struct OpaqueResolverTermKeyEquals;

      typedef boost::intrusive::unordered_set<FunctionalBaseTerm,
					      boost::intrusive::member_hook<FunctionalBaseTerm, FunctionalBaseTerm::TermSetHook, &FunctionalBaseTerm::m_term_set_hook>,
					      //boost::intrusive::equal<FunctionalBaseTermEquals>,
					      boost::intrusive::hash<FunctionalBaseTermHash>,
					      boost::intrusive::power_2_buckets<true> > FunctionalTermSet;
      static const std::size_t functional_terms_n_initial_buckets = 256;
      std::size_t m_functional_terms_n_buckets;
      UniqueArray<FunctionalTermSet::bucket_type> m_functional_terms_buckets;
      FunctionalTermSet m_functional_terms;

      typedef boost::intrusive::list<DistinctTerm,
				     boost::intrusive::member_hook<DistinctTerm, DistinctTerm::TermListHook, &DistinctTerm::m_term_list_hook>,
				     boost::intrusive::constant_time_size<false> > DistinctTermList;
      DistinctTermList m_distinct_terms;

      UniquePtr<llvm::LLVMContext> m_llvm_context;
      UniquePtr<llvm::Module> m_llvm_module;
      UniquePtr<llvm::ExecutionEngine> m_llvm_engine;

    public:
      Context();
      ~Context();

      MetatypeTerm* get_metatype() {return m_metatype.get();}

#if 0
      template<typename T>
      typename T::Wrapper get_functional(const T& proto, std::size_t n_parameters, Term *const* parameters) {
	return T::Wrapper(get_functional_internal(proto, n_parameters, parameters));
      }

#define PSI_TVM_MAKE_NEW_TERM(z,n,data) Term* new_term(const ProtoTerm& expression BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,Term *p)) {Term *ap[n] = {BOOST_PP_ENUM_PARAMS_Z(z,n,p)}; return new_term(expression, n, ap);}
      BOOST_PP_REPEAT(5,PSI_TVM_MAKE_NEW_TERM,)
#undef PSI_TVM_MAKE_NEW_TERM
#endif

      FunctionTypeTerm* get_function_type(Term *result, std::size_t n_parameters, Term *const* parameter_types);

      FunctionTypeParameterTerm* get_function_type_parameter(Term *type, OpaqueTerm *func, std::size_t index) {
	return get_function_type_parameter_internal(type, func, index);
      }

      /**
       * \brief Create a new placeholder term.
       */
      OpaqueTerm* new_opaque(Term *type);

      /**
       * \brief Resolve an opaque term.
       */
      template<typename T>
      T* resolve_opaque(OpaqueTerm *opaque, T *term) {
	return static_cast<T*>(resolve_opaque_internal(opaque, term));
      }

      /**
       * \brief Create a new global term.
       */
      GlobalVariableTerm* new_global_variable(Term *type, bool constant);

      FunctionTerm* new_function(FunctionTypeTerm *type);

      /**
       * \brief Just-in-time compile a term, and a get a pointer to
       * the result.
       */
      void* term_jit(Term *term);

    private:
      Context(const Context&);

      template<typename T>
      typename T::ResultType allocate_term(const T& initializer);

      template<typename T>
      typename T::ResultType allocate_distinct_term(const T& initializer);

      /**
       * Check whether a rehash is needed. Since this is a chaining
       * hash table, a load factor of 1.0 is used.
       */
      void check_functional_terms_rehash();
      static std::size_t term_hash(const Term *term);
      FunctionalTerm* get_functional_internal(const FunctionalTermBackend& backend,
					      std::size_t n_parameters, Term *const* parameters);
      FunctionalTerm* get_functional_internal_with_type(const FunctionalTermBackend& backend, Term *type,
							std::size_t n_parameters, Term *const* parameters);
      FunctionTypeParameterTerm* get_function_type_parameter_internal(Term *type, Term *func, std::size_t index);
      OpaqueResolverTerm* get_opaque_resolver(std::size_t depth, Term *type);

      /**
       * Build a term which uniquely identifies the term we are
       * resolving - this uses OpaqueResolverTerm instances to mark
       * references back to the root of the resolving type, and once
       * the complete type is built we will have identified the
       * correct type.
       *
       * \param non_rewritten_terms Terms that have already been
       * visited and do not need rewriting.
       */
      Term* build_resolver_term(std::size_t depth, std::tr1::unordered_map<Term*, std::size_t>& parent_terms,
				std::tr1::unordered_set<Term*>& non_rewritten_terms, Term *term, std::size_t& up_reference_depth);

      /**
       * Rewrite the resolving term from the root down to build the
       * final term to be returned. An upward rewrite (and further
       * unification) is still necessary for any terms which contain
       * the resolved opaque type outside of a cycle.
       */
      Term* rewrite_resolver_term(Term *term, Term *resolved,
				  std::vector<Term*>& parent_terms,
				  const std::tr1::unordered_set<Term*>& non_rewritten_terms,
				  std::tr1::unordered_map<Term*,Term*>& rewritten_terms);

      FunctionalBaseTerm* resolve_opaque_internal(OpaqueTerm *opaque, FunctionalBaseTerm *target);
#if 0
      InstructionTerm* get_instruction_internal(const InstructionProtoTerm& proto, std::size_t n_parameters,
						Term *const* parameters);
      template<typename T>
      T* allocate_proto_term(const ValueCloner& proto_cloner,
			     Term *type, std::size_t n_parameters, Term *const* parameters);
#endif
    };

    template<typename T> T* OpaqueTerm::resolve(T* term) {
      return context().resolve_opaque(this, term);
    }

    /**
     * \brief Value type of #Metatype.
     *
     * This is here for easy interfacing with C++ and must be kept in
     * sync with Metatype::build_llvm_type.
     */
    struct MetatypeValue {
      std::tr1::uint64_t size;
      std::tr1::uint64_t align;
    };

#if 0
    /**
     * \brief The type of #Type terms.
     *
     * There is one global #Metatype object (per context), and all types
     * are of type #Metatype. #Metatype does not have a type (it is
     * impossible to quantify over #Metatype so this does not matter).
     */
    class Metatype : public ProtoTerm {
      friend class Context;

    public:
      Metatype();

      static Term* create(Context& con);

      virtual Term* type(Context& context, std::size_t n_parameters, Term *const* parameters) const;
      /**
       * \brief Get an LLVM value for Metatype for the given LLVM type.
       */
      static LLVMConstantBuilder::Constant llvm_value(const llvm::Type* ty);

      /**
       * \brief Get an LLVM value for Metatype for an empty type.
       */
      static LLVMConstantBuilder::Constant llvm_value_empty(llvm::LLVMContext& context);

      /**
       * \brief Get an LLVM value for a specified size and alignment.
       *
       * The result of this call will be a global constant.
       */
      static LLVMConstantBuilder::Constant llvm_value(llvm::Constant *size, llvm::Constant *align);

      /**
       * \brief Get an LLVM value for a specified size and alignment.
       *
       * The result of this call will be a global constant.
       */
      static LLVMFunctionBuilder::Result llvm_value(LLVMFunctionBuilder& builder, llvm::Value *size, llvm::Value *align);

    private:
      virtual bool equals_internal(const ProtoTerm& other) const;
      virtual LLVMFunctionBuilder::Result llvm_value_instruction(LLVMFunctionBuilder&, Term*) const;
      virtual LLVMConstantBuilder::Constant llvm_value_constant(LLVMConstantBuilder&, Term*) const;
      virtual LLVMConstantBuilder::Type llvm_type(LLVMConstantBuilder&, Term*) const;
    };
#endif
  }
}

#endif
