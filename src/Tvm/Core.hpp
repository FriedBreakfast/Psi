#ifndef HPP_PSI_TVM_CORE
#define HPP_PSI_TVM_CORE

#include <vector>

#include <tr1/unordered_map>
#include <tr1/unordered_set>

#include <boost/cast.hpp>
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
    class Term;
    class TermPtrBase;

    /**
     * \brief Identifies the Term subclass this object actually is.
     */
    enum TermType {
      term_ptr, ///<TermPtr: \copybrief TermPtr

      ///@{
      /// Non hashable terms
      term_instruction, ///< InstructionTerm: \copybrief InstructionTerm
      term_recursive, ///< RecursiveTerm: \copybrief RecursiveTerm
      term_recursive_parameter, ///<RecursiveParameterTerm: \copybrief RecursiveParameterTerm
      term_block, ///< BlockTerm: \copybrief BlockTerm
      term_global_variable, ///< GlobalVariableTerm: \copybrief GlobalVariableTerm
      term_function, ///< FunctionTerm: \copybrief FunctionTerm
      term_function_parameter, ///< FunctionParameterTerm: \copybrief FunctionParameterTerm
      term_phi, ///< PhiTerm: \copybrief PhiTerm
      term_function_type, ///< FunctionTypeTerm: \copybrief FunctionTypeTerm
      term_function_type_parameter, ///< FunctionTypeParameterTerm: \copybrief FunctionTypeParameterTerm
      term_metatype, ///< MetatypeTerm: \copybrief MetatypeTerm
      ///@}

      ///@{
      /// Hashable terms
      term_functional, ///< FunctionalTerm: \copybrief FunctionalTerm
      term_function_type_internal, ///< FunctionTypeInternalTerm: \copybrief FunctionTypeInternalTerm
      term_function_type_internal_parameter ///< FunctionTypeInternalParameterTerm: \copybrief FunctionTypeInternalParameterTerm
      ///@}
    };

    class TermUser : User {
      friend class Context;
      friend class Term;
      friend class TermPtrBase;
    public:
      TermType term_type() const {return static_cast<TermType>(m_term_type);}

    private:
      TermUser(const UserInitializer& ui, TermType term_type);
      ~TermUser();

      inline std::size_t n_uses() const {return User::n_uses();}
      inline Term* use_get(std::size_t n) const;
      inline void use_set(std::size_t n, Term *term);

      unsigned char m_term_type : 4;
    };

    class TermPtrBase : TermUser {
    public:
      TermPtrBase();
      TermPtrBase(const TermPtrBase&);
      explicit TermPtrBase(Term *ptr);
      ~TermPtrBase();

      const TermPtrBase& operator = (const TermPtrBase& o) {reset(o.get()); return *this;}

      bool operator == (const TermPtrBase& o) const {return get() == o.get();}
      bool operator != (const TermPtrBase& o) const {return get() != o.get();}

      Term* get() const {return use_get(0);}
      void reset(Term *term=0);

    private:
      Use m_uses[2];
    };

    template<typename T=Term>
    class TermPtr {
    public:
      typedef T value_type;

      TermPtr() {}
      explicit TermPtr(value_type *ptr) : m_base(ptr) {}

      bool operator == (const TermPtr& o) const {return o.m_base == m_base;}
      bool operator != (const TermPtr& o) const {return o.m_base != m_base;}
      template<typename U> bool operator == (const TermPtr<U>& o) const {return o.m_base == m_base;}
      template<typename U> bool operator != (const TermPtr<U>& o) const {return o.m_base != m_base;}

      template<typename U>
      TermPtr<T>& operator = (const TermPtr<U>& src) {
	reset(src.get());
	return *this;
      }

      value_type* operator -> () const {return get();}
      value_type& operator * () const {return *get();}
      value_type* get() const {return boost::polymorphic_downcast<value_type*>(m_base.get());}

      void reset(value_type *ptr) {
	m_base.reset(ptr);
      }

    private:
      TermPtrBase m_base;
    };

    /**
     * \brief Base class for all compile- and run-time values.
     *
     * All types which inherit from Term must be known about by
     * Context since they perform some sort of unusual function; other
     * types and instructions are created by subclasses
     * FunctionalTermBackend and InstructionTermBackend and then
     * wrapping that in either FunctionalTerm or InstructionTerm.
     */
    class Term : TermUser, Used {
      friend class TermUser;
      friend class TermPtrBase;

      friend class Context;

      friend class GlobalTerm;
      friend class FunctionParameterTerm;
      friend class BlockTerm;
      friend class FunctionTypeTerm;
      friend class FunctionTypeParameterTerm;
      friend class InstructionTerm;
      friend class MetatypeTerm;
      friend class PhiTerm;
      friend class HashTerm;
      friend class RecursiveTerm;
      friend class RecursiveParameterTerm;

      friend class FunctionalTerm;
      friend class FunctionTypeInternalTerm;
      friend class FunctionTypeInternalParameterTerm;

    public:
      virtual ~Term();

      enum Category {
	category_metatype,
	category_type,
	category_value
      };

      TermType term_type() const {return TermUser::term_type();}

      bool abstract() const {return m_abstract;}
      bool parameterized() const {return m_parameterized;}
      Category category() const {return static_cast<Category>(m_category);}

      /**
       * \brief Get the context this Term comes from.
       */
      Context& context() const {return *m_context;}

      /**
       *
       */
      Term* type() const {return use_get(0);}

      static bool any_abstract(std::size_t n, Term *const* terms);
      static bool any_parameterized(std::size_t n, Term *const* terms);

    private:
      Term(const UserInitializer& ui, Context *context, TermType term_type, bool abstract, bool parameterized, Term *type);

      std::size_t hash_value() const;
      std::size_t *term_use_count();
      void term_add_ref();
      void term_release();
      static void term_destroy(Term *term);

      unsigned char m_category : 2;
      unsigned char m_abstract : 1;
      unsigned char m_parameterized : 1;
      unsigned char m_use_count_ptr : 1;
      Context *m_context;
      union TermUseCount {
	std::size_t *ptr;
	std::size_t value;
      } m_use_count;

    protected:
      std::size_t n_base_parameters() const {
	return n_uses() - 1;
      }

      void set_base_parameter(std::size_t n, Term *t) {
	PSI_ASSERT_MSG(m_context == t->m_context, "term context mismatch");
	use_set(n+1, t);
      }

      Term* get_base_parameter(std::size_t n) const {
	return use_get(n+1);
      }
    };

    inline Term* TermUser::use_get(std::size_t n) const {
      return static_cast<Term*>(User::use_get(n));
    }

    inline void TermUser::use_set(std::size_t n, Term *term) {
      User::use_set(n, term);
    }

    class HashTerm : public Term {
      friend class Context;
      friend class Term;
      friend class FunctionalTerm;
      friend class FunctionTypeInternalTerm;
      friend class FunctionTypeInternalParameterTerm;

    private:
      HashTerm(const UserInitializer& ui, Context *context, TermType term_type, bool abstract, bool parameterized, Term *type, std::size_t hash);
      typedef boost::intrusive::unordered_set_member_hook<> TermSetHook;
      TermSetHook m_term_set_hook;
      std::size_t m_hash;
    };

    /**
     * \brief Unique type which is the type of type terms.
     */
    class MetatypeTerm : public Term {
      friend class Context;

    private:
      class Initializer;
      MetatypeTerm(const UserInitializer&, Context*);
    };

    class FunctionalTerm;

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
      virtual LLVMFunctionBuilder::Result llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm*) const = 0;
      virtual LLVMConstantBuilder::Constant llvm_value_constant(LLVMConstantBuilder&, FunctionalTerm*) const = 0;
      virtual LLVMConstantBuilder::Type llvm_type(LLVMConstantBuilder&, FunctionalTerm*) const = 0;

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
    class FunctionalTerm : public HashTerm {
      friend class Context;

    public:
      const FunctionalTermBackend& backend() const {return *m_backend;}
      std::size_t n_parameters() const {return Term::n_base_parameters();}
      Term* parameter(std::size_t n) const {return get_base_parameter(n);}

    private:
      class Setup;
      FunctionalTerm(const UserInitializer& ui, Context *context, Term *type,
		     std::size_t hash, FunctionalTermBackend *backend,
		     std::size_t n_parameters, Term *const* parameters);
      ~FunctionalTerm();

      FunctionalTermBackend *m_backend;
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
      std::size_t n_parameters() const {return Term::n_base_parameters();}
      Term* parameter(std::size_t n) const {return get_base_parameter(n);}

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

    class FunctionTypeParameterTerm : public Term {
      friend class Context;
    public:
      FunctionTypeTerm* source() const;
      std::size_t index() const {return m_index;}

    private:
      class Initializer;
      FunctionTypeParameterTerm(const UserInitializer& ui, Context *context, Term *type);
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
      FunctionTypeParameterTerm* parameter(std::size_t i) const {return boost::polymorphic_downcast<FunctionTypeParameterTerm*>(get_base_parameter(i+1));}
      Term* result_type() const {return get_base_parameter(0);}

    private:
      class Initializer;
      FunctionTypeTerm(const UserInitializer& ui, Context *context, Term *result_type,
		       std::size_t n_parameters, FunctionTypeParameterTerm *const* parameters);
      static bool any_parameter_abstract(std::size_t n, FunctionTypeParameterTerm *const* terms);
    };

    inline FunctionTypeTerm* FunctionTypeParameterTerm::source() const {
      return boost::polymorphic_downcast<FunctionTypeTerm*>(get_base_parameter(0));
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
      Term* parameter_type(std::size_t n) const {return get_base_parameter(n+2);}
      std::size_t n_parameters() const {return n_base_parameters()-2;}
      Term* result_type() const {return get_base_parameter(1);}

    private:
      class Setup;
      FunctionTypeInternalTerm(const UserInitializer& ui, Context *context, std::size_t hash, Term *result_type, std::size_t n_parameters, Term *const* parameter_types);
      FunctionTypeTerm* get_function_type() const {return boost::polymorphic_downcast<FunctionTypeTerm*>(get_base_parameter(0));}
      void set_function_type(FunctionTypeTerm *term) {set_base_parameter(0, term);}
    };

    /**
     * \brief Internal type used to build function types.
     */
    class FunctionTypeInternalParameterTerm : public HashTerm {
      friend class Context;
    public:

    private:
      class Setup;
      FunctionTypeInternalParameterTerm(const UserInitializer& ui, Context *context, std::size_t hash, std::size_t depth, std::size_t index);
      std::size_t m_depth;
      std::size_t m_index;
    };

    class RecursiveParameterTerm : public Term {
      friend class Context;

    private:
      class Initializer;
      RecursiveParameterTerm(const UserInitializer& ui, Context *context, Term *type);
    };

    /**
     * \brief Recursive term: usually used to create recursive types.
     *
     * To create a recursive type (or term), first create a
     * RecursiveTerm using Context::new_recursive, create the type as
     * normal and then call #resolve to finalize the type.
     */
    class RecursiveTerm : public Term {
      friend class Context;

    public:
      /**
       * \brief Resolve this term to its actual value.
       */
      void resolve(Term *term);

      void apply(std::size_t n_parameters, Term *const* values);

      std::size_t n_parameters() const {return n_base_parameters() - 2;}
      RecursiveParameterTerm* parameter(std::size_t i) const {return static_cast<RecursiveParameterTerm*>(get_base_parameter(i+2));}
      Term* result_type() const {return get_base_parameter(0);}
      Term* result() const {return get_base_parameter(1);}

    private:
      class Initializer;
      RecursiveTerm(const UserInitializer& ui, Context *context, Term *result_type,
		    std::size_t n_parameters, RecursiveParameterTerm *const* parameters);
    };

    /**
     * \brief Base class for globals: these are GlobalVariableTerm and FunctionTerm.
     */
    class GlobalTerm : public Term {
      friend class GlobalVariableTerm;
      friend class FunctionTerm;

    private:
      GlobalTerm(const UserInitializer& ui, Context *context, TermType term_type, Term *type);
    };

    /**
     * \brief Global variable.
     */
    class GlobalVariableTerm : public GlobalTerm {
      friend class Context;

    public:
      void set_value(Term *value);

    private:
      class Initializer;
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
      class Initializer;
      FunctionParameterTerm(const UserInitializer& ui, Context *context, Term *type);

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
      TermPtr<MetatypeTerm> m_metatype;

      struct HashTermDisposer;
      struct HashTermHasher {std::size_t operator () (const HashTerm&) const;};

      typedef boost::intrusive::unordered_set<HashTerm,
					      boost::intrusive::member_hook<HashTerm, boost::intrusive::unordered_set_member_hook<>, &HashTerm::m_term_set_hook>,
					      boost::intrusive::hash<HashTermHasher>,
					      boost::intrusive::power_2_buckets<true> > HashTermSetType;

      static const std::size_t initial_hash_term_buckets = 64;
      UniqueArray<HashTermSetType::bucket_type> m_hash_term_buckets;
      HashTermSetType m_hash_terms;

      struct FunctionalTermKeyEquals;
      struct FunctionTypeInternalTermKeyEquals;
      struct FunctionTypeInternalParameterTermKeyEquals;
      struct FunctionalTermFactory;
      struct FunctionTypeInternalTermFactory;
      struct FunctionTypeInternalParameterTermFactory;

      UniquePtr<llvm::LLVMContext> m_llvm_context;
      UniquePtr<llvm::Module> m_llvm_module;
      UniquePtr<llvm::ExecutionEngine> m_llvm_engine;

    public:
      Context();
      ~Context();

      const TermPtr<MetatypeTerm>& get_metatype() {return m_metatype;}

#if 0
      template<typename T>
      typename T::Wrapper get_functional(const T& proto, std::size_t n_parameters, Term *const* parameters) {
	return T::Wrapper(get_functional_internal(proto, n_parameters, parameters));
      }

#define PSI_TVM_MAKE_NEW_TERM(z,n,data) Term* new_term(const ProtoTerm& expression BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,Term *p)) {Term *ap[n] = {BOOST_PP_ENUM_PARAMS_Z(z,n,p)}; return new_term(expression, n, ap);}
      BOOST_PP_REPEAT(5,PSI_TVM_MAKE_NEW_TERM,)
#undef PSI_TVM_MAKE_NEW_TERM
#endif

      TermPtr<FunctionTypeTerm> get_function_type(Term* result,
						  std::size_t n_parameters,
						  FunctionTypeParameterTerm *const* parameters);

      TermPtr<FunctionTypeParameterTerm> new_function_type_parameter(Term* type);

      /**
       * \brief Create a new recursive term.
       */
      TermPtr<RecursiveTerm> new_recursive(Term* result_type,
					   std::size_t n_parameters,
					   Term *const* parameter_types);

      /**
       * \brief Resolve an opaque term.
       */
      void resolve_recursive(RecursiveTerm* recursive, Term* to);

      /**
       * \brief Create a new global term.
       */
      TermPtr<GlobalVariableTerm> new_global_variable(Term* type, bool constant);

      TermPtr<FunctionTerm> new_function(FunctionTypeTerm* type);

      /**
       * \brief Just-in-time compile a term, and a get a pointer to
       * the result.
       */
      void* term_jit(Term *term);

    private:
      Context(const Context&);

      template<typename T>
      typename T::TermType* allocate_term(const T& initializer);

      template<typename T>
      typename T::TermType* hash_term_get(T& Setup);

      RecursiveParameterTerm* new_recursive_parameter(Term *term);

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
  }
}

#endif
