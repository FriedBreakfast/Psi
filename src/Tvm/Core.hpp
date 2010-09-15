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
      friend class FunctionalTerm;
      friend class FunctionTypeInternalTerm;
      friend class FunctionTypeInternalParameterTerm;
      friend class FunctionalBaseTerm;
      friend class FunctionParameterTerm;
      friend class InstructionTerm;
      friend class MetatypeTerm;
      friend class PhiTerm;

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
	term_recursive, ///< RecursiveTerm: \copybrief RecursiveTerm
	term_block, ///< BlockTerm: \copybrief BlockTerm
	term_global_variable, ///< GlobalVariableTerm: \copybrief GlobalVariableTerm
	term_function, ///< FunctionTerm: \copybrief FunctionTerm
	term_function_parameter, ///< FunctionParameterTerm: \copybrief FunctionParameterTerm
	term_phi, ///< PhiTerm: \copybrief PhiTerm
	term_function_type, ///< FunctionTypeTerm: \copybrief FunctionTypeTerm
	term_function_type_parameter, ///< FunctionTypeParameterTerm: \copybrief FunctionTypeParameterTerm
	term_function_type_internal, ///< FunctionTypeInternalTerm: \copybrief FunctionTypeInternalTerm
	term_function_type_internal_parameter, ///< FunctionTypeInternalParameterTerm: \copybrief FunctionTypeInternalParameterTerm
	term_metatype ///< MetatypeTerm: \copybrief MetatypeTerm
      };

      bool abstract() const {return m_abstract;}
      bool parameterized() const {return m_parameterized;}
      Category category() const {return static_cast<Category>(m_category);}
      TermType term_type() const {return static_cast<TermType>(m_term_type);}

      /**
       * \brief Get the context this Term comes from.
       */
      Context& context() const {return *m_context;}

      /**
       *
       */
      Term* type() const {return use_get<Term>(0);}

      static bool any_abstract(std::size_t n, Term *const* terms);
      static bool any_parameterized(std::size_t n, Term *const* terms);

    private:
      Term(const UserInitializer& ui, Context *context, TermType term_type, bool abstract, bool parameterized, Term *type);
      ~Term();

      Context *m_context;
      unsigned m_term_type : 4;
      unsigned m_abstract : 1;
      unsigned m_parameterized : 1;
      unsigned m_category : 2;

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
     * \brief Base class of functional (machine state independent) terms.
     *
     * Functional terms are special since two terms of the same
     * operation and with the same parameters are equivalent; they are
     * therefore unified into one term so equivalence can be checked
     * via pointer equality. This is particularly required for type
     * checking, but also applies to other terms.
     */
    class FunctionalTerm : public Term {
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

      typedef boost::intrusive::unordered_set_member_hook<> TermSetHook;
      TermSetHook m_term_set_hook;
      std::size_t m_hash;
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
      friend class FunctionTypeTerm;
      friend class FunctionTypeParameterTerm;
      friend class RecursiveTerm;

    private:
      DistinctTerm(const UserInitializer& ui, Context* context, TermType term_type, bool abstract, bool parameterized, Term *type);
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

    class FunctionTypeTerm;

    class FunctionTypeParameterTerm : public DistinctTerm {
      friend class Context;
    public:
      FunctionTypeTerm* source() const {return m_source;}
      std::size_t index() const {return m_index;}

    private:
      struct Initializer;
      FunctionTypeParameterTerm(const UserInitializer& ui, Context *context, Term *type);

      FunctionTypeTerm *m_source;
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
    class FunctionTypeTerm : public DistinctTerm {
      friend class Context;
    public:
      std::size_t n_function_parameters() const {return n_parameters() - 1;}
      FunctionTypeParameterTerm* function_parameter(std::size_t i) const {return static_cast<FunctionTypeParameterTerm*>(get_parameter(i+1));}
      Term* function_result_type() const {return get_parameter(0);}

    private:
      struct Initializer;
      FunctionTypeTerm(const UserInitializer& ui, Context *context, Term *result_type,
		       std::size_t n_parameters, FunctionTypeParameterTerm *const* parameters);
      static bool any_parameter_abstract(std::size_t n, FunctionTypeParameterTerm *const* terms);
    };
 
    /**
     * \brief Internal type used to build function types.
     */
    class FunctionTypeInternalTerm : public Term {
      friend class Context;
    public:
      Term* function_parameter(std::size_t n) const {return get_parameter(n+1);}
      std::size_t n_function_parameters() const {return n_parameters()-1;}
      Term* function_result() const {return get_parameter(0);}

    private:
      struct Initializer;
      FunctionTypeInternalTerm(const UserInitializer& ui, Context *context, Term *result, std::size_t n_parameters, Term *const* parameters);
      typedef boost::intrusive::unordered_set_member_hook<> TermSetHook;
      std::size_t m_hash;
      TermSetHook m_term_set_hook;
      FunctionTypeTerm *m_function_type;
    };

    /**
     * \brief Internal type used to build function types.
     */
    class FunctionTypeInternalParameterTerm : public Term {
      friend class Context;
    public:

    private:
      struct Initializer;
      FunctionTypeInternalParameterTerm(const UserInitializer& ui, Context *context, std::size_t hash, std::size_t depth, std::size_t index);
      typedef boost::intrusive::unordered_set_member_hook<> TermSetHook;
      std::size_t m_hash;
      TermSetHook m_term_set_hook;
      std::size_t m_depth;
      std::size_t m_index;
    };

    /**
     * \brief Recursive term: usually used to create recursive types.
     *
     * To create a recursive type (or term), first create a
     * RecursiveTerm using Context::new_recursive, create the type as
     * normal and then call #resolve to finalize the type.
     */
    class RecursiveTerm : public DistinctTerm {
      friend class Context;

    public:
      /**
       * \brief Resolve this term to its actual value.
       */
      void resolve(Term *term);

      Term* value() const {return get_parameter(0);}

    private:
      struct Initializer;
      RecursiveTerm(const UserInitializer& ui, Context *context, Term *type);
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
      FunctionParameterTerm(const UserInitializer& ui, Context *context);

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
      struct DistinctTermDisposer;

      UniquePtr<MetatypeTerm> m_metatype;

      template<typename T>
      struct TermHasher {
	std::size_t operator () (const T& t) const {
	  return t.m_hash;
	}
      };

      template<typename TermType> struct TermDisposer;

      template<typename TermType, std::size_t initial_buckets=64>
      class TermHashSet {
      public:
	TermHashSet();
	~TermHashSet();

	template<typename Key, typename KeyHash, typename KeyValueEquals, typename KeyConstructor>
	TermType* get(const Key&, const KeyHash&, const KeyValueEquals&, const KeyConstructor&);

      private:
	typedef boost::intrusive::unordered_set<TermType,
						boost::intrusive::member_hook<TermType, boost::intrusive::unordered_set_member_hook<>, &TermType::m_term_set_hook>,
						boost::intrusive::hash<TermHasher<TermType> >,
						boost::intrusive::power_2_buckets<true> > HashSetType;

	UniqueArray<typename HashSetType::bucket_type> m_buckets;
	HashSetType m_hash_set;
      };

      struct FunctionalTermKeyEquals;
      struct FunctionTypeInternalTermKeyEquals;
      struct FunctionTypeInternalParameterTermKeyEquals;
      struct FunctionalTermFactory;
      struct FunctionTypeInternalTermFactory;
      struct FunctionTypeInternalParameterTermFactory;

      TermHashSet<FunctionalTerm> m_functional_terms;
      TermHashSet<FunctionTypeInternalTerm> m_function_type_internal_terms;
      TermHashSet<FunctionTypeInternalParameterTerm> m_function_type_internal_parameter_terms;

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

      FunctionTypeTerm* get_function_type(Term *result,
					  std::size_t n_parameters,
					  FunctionTypeParameterTerm *const* parameters);

      FunctionTypeParameterTerm* new_function_type_parameter(Term *type);

      /**
       * \brief Create a new recursive term.
       */
      RecursiveTerm* new_recursive(Term *type);

      /**
       * \brief Resolve an opaque term.
       */
      void resolve_recursive(RecursiveTerm *recursive, Term *to);

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

      static std::size_t term_hash(const Term *term);
      FunctionalTerm* get_functional_internal(const FunctionalTermBackend& backend,
					      std::size_t n_parameters, Term *const* parameters);
      FunctionalTerm* get_functional_internal_with_type(const FunctionalTermBackend& backend, Term *type,
							std::size_t n_parameters, Term *const* parameters);

      FunctionTypeInternalTerm* get_function_type_internal(Term *result, std::size_t n_parameters, Term *const* parameter_types);
      FunctionTypeInternalParameterTerm* get_function_type_internal_parameter(std::size_t depth, std::size_t index);

      /**
       * Check whether part of function type term is complete,
       * i.e. whether there are still function parameters which have
       * to be resolved by further function types (this happens in the
       * case of nested function types).
       */
      bool check_function_type_complete(Term *term, std::tr1::unordered_set<FunctionTypeTerm*>& functions);

      struct FunctionResolveStatus {
	/// Depth of this function
	std::size_t depth;
	/// Index of parameter currently being resolved
	std::size_t index;
      };
      typedef std::tr1::unordered_map<FunctionTypeTerm*, FunctionResolveStatus> FunctionResolveMap;
      Term* build_function_type_resolver_term(std::size_t depth, Term *term, FunctionResolveMap& functions);

      /**
       * \brief Deep search a term to determine whether it is really
       * abstract.
       */
      bool search_for_abstract(Term *term);

      static void clear_and_queue_if_abstract(std::vector<Term*>& queue, Term *t);

      /**
       * \brief Clear the abstract flag in this term and all its
       * descendents.
       */
      void clear_abstract(Term *term, std::vector<Term*>& queue);

#if 0
      InstructionTerm* get_instruction_internal(const InstructionProtoTerm& proto, std::size_t n_parameters,
						Term *const* parameters);
#endif
    };

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
