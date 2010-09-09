#ifndef HPP_PSI_TVM_CORE
#define HPP_PSI_TVM_CORE

#include <tr1/cstdint>
#include <tr1/unordered_set>

#include <boost/preprocessor/repetition/repeat.hpp>
#include <boost/preprocessor/repetition/enum_params.hpp>
#include <boost/preprocessor/repetition/enum_trailing_params.hpp>

#include "User.hpp"
#include "../Utility.hpp"
#include "LLVMBuilder.hpp"

namespace Psi {
  namespace Tvm {
    class Context;
    class ProtoTerm;

    class TermParameter {
    public:
      typedef IntrusivePtr<TermParameter> Ref;

      static Ref create();
      static Ref create(const Ref& parent);

      static std::size_t depth(const Ref& r) {
	return r ? r->m_depth : 0;
      }

      static bool global(const Ref& r) {
	return !r;
      }

      const Ref& parent() const {
	return m_parent;
      }

      friend void intrusive_ptr_add_ref(TermParameter *p) {
	++p->m_n_references;
      }

      friend void intrusive_ptr_release(TermParameter *p) {
	if (--p->m_n_references == 0)
	  delete p;
      }

    private:
      TermParameter(const Ref&);
      TermParameter(const TermParameter&);

      Ref m_parent;
      std::size_t m_depth;
      std::size_t m_n_references;
    };

    typedef TermParameter::Ref TermParameterRef;

    class Term : public Used, public User {
      friend class Context;

    public:
      ~Term();

      bool complete() const {return m_complete;}

      /**
       * \brief Get the context this Term comes from.
       */
      Context& context() const {return *m_context;}

      const TermParameterRef& term_context() const {return m_term_context;}

      /**
       *
       */
      Term* type() const {return use_get<Term>(0);}

      std::size_t n_parameters() const {return use_slots() - 1;}

      /**
       *
       */
      Term* parameter(std::size_t n) const {return use_get<Term>(n+1);}

      /**
       * Set the value of a global or recursive term.
       */
      void set_value(Term *value);

      ProtoTerm& proto() const {return *m_proto_term;}

      std::size_t hash() const {return m_hash;}

    private:
      Term(const UserInitializer& ui, Context *context,
	   const TermParameterRef& term_context, ProtoTerm *proto_term,
	   Term *type, std::size_t n_parameters, Term *const* parameters);

      std::size_t m_hash;
      Context *m_context;
      TermParameterRef m_term_context;
      bool m_complete;
      ProtoTerm *m_proto_term;
    };

    class ProtoTerm {
      friend class Context;
      friend class LLVMBuilderInvoker;

    public:
      /**
       * \brief Category of a Terms using this prototype.
       *
       * This gives which set of types this term is from, given that a
       * the category of the type of a value is type, the category of
       * the type of a type is metatype, and metatype has no type, so
       * the ordering is:
       *
       * value : type : metatype
       */
      enum Category {
	term_metatype,
	term_type,
	term_value
      };

      /**
       * \brief How the value of terms of this prototype is generated.
       *
       * Values can either come from a functional calculation (which
       * may depend on procedural values), an instruction (which
       * modifies machine state), and a phi node (which unifies values
       * from different computations on entering a block).
       *
       * Placeholder terms exist to be replaced by another term at a
       * later time - they are by definition incomplete.
       */
      enum Source {
	/// Functional term: depends entirely on its parameters, not machine state.
	term_functional,
	/// Instruction term: depends on its parameters plus machine state.
	term_instruction,
	/// Phi node: merges values from different computation paths.
	term_phi_node,
	/// Global term
	term_global,
	/// Recursive term
	term_recursive,
      };

      virtual ~ProtoTerm();

      virtual Term* type(Context& context, std::size_t n_parameters, Term *const* parameters) const = 0;

      std::size_t hash() const;

      bool operator == (const ProtoTerm& other) const;
      bool operator != (const ProtoTerm& other) const;

      /// \brief Category of this expression.
      Category category() const {return m_category;}
      /// \brief Source of this expression.
      Source source() const {return m_source;}

    protected:
      ProtoTerm(Category category, Source source);

    private:
      /**
       * Overridable equals() implementation. \c other will be the
       * same type as \c this.
       */
      virtual bool equals_internal(const ProtoTerm& other) const = 0;
      virtual std::size_t hash_internal() const = 0;
      virtual ProtoTerm* clone() const = 0;

      virtual LLVMFunctionBuilder::Result llvm_value_instruction(LLVMFunctionBuilder&, Term*) const = 0;
      virtual LLVMConstantBuilder::Constant llvm_value_constant(LLVMConstantBuilder&, Term*) const = 0;
      virtual LLVMConstantBuilder::Type llvm_type(LLVMConstantBuilder&, Term*) const = 0;

      std::size_t m_n_uses;
      Category m_category;
      Source m_source;
    };

    class Context {
      friend Term::~Term();
      friend struct ProtoPtr;

      struct TermHasher {std::size_t operator () (const Term *t) const;};
      struct TermEquals {bool operator () (const Term *lhs, const Term *rhs) const;};
      struct ProtoTermHasher {std::size_t operator () (const ProtoTerm *t) const;};
      struct ProtoTermEquals {bool operator () (const ProtoTerm *lhs, const ProtoTerm *rhs) const;};

      typedef std::tr1::unordered_set<Term*, TermHasher, TermEquals> TermSet;
      TermSet m_term_set;

      typedef std::tr1::unordered_set<ProtoTerm*, ProtoTermHasher, ProtoTermEquals> ProtoTermSet;
      ProtoTermSet m_proto_term_set;

      ProtoTerm* new_proto(const ProtoTerm&);
      void proto_release(ProtoTerm *proto);

      UniquePtr<llvm::LLVMContext> m_llvm_context;
      UniquePtr<llvm::Module> m_llvm_module;
      UniquePtr<llvm::ExecutionEngine> m_llvm_engine;

    public:
      Context();
      ~Context();

      /**
       * \brief Create a new term.
       */
      Term* new_term(const ProtoTerm& expression, std::size_t n_parameters, Term *const* parameters);

#define PSI_TVM_MAKE_NEW_TERM(z,n,data) Term* new_term(const ProtoTerm& expression BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,Term *p)) {Term *ap[n] = {BOOST_PP_ENUM_PARAMS_Z(z,n,p)}; return new_term(expression, n, ap);}
      BOOST_PP_REPEAT(5,PSI_TVM_MAKE_NEW_TERM,)
#undef PSI_TVM_MAKE_NEW_TERM

      /**
       * \brief Create a new placeholder term.
       *
       * If \c type is null, it will default to metatype.
       */
      Term* new_placeholder(Term *type=0);

      /**
       * \brief Create a new global term.
       */
      Term* new_global(Term *type);

      /**
       * \brief Just-in-time compile a term, and a get a pointer to
       * the result.
       */
      void* term_jit(Term *term);

    private:
      Context(const Context&);
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
      virtual std::size_t hash_internal() const;
      virtual ProtoTerm* clone() const;
      virtual LLVMFunctionBuilder::Result llvm_value_instruction(LLVMFunctionBuilder&, Term*) const;
      virtual LLVMConstantBuilder::Constant llvm_value_constant(LLVMConstantBuilder&, Term*) const;
      virtual LLVMConstantBuilder::Type llvm_type(LLVMConstantBuilder&, Term*) const;
    };

    class Parameter : public ProtoTerm {
    public:
      Parameter(const TermParameterRef& p);

    private:
      TermParameterRef m_p;
    };

    class Type : public ProtoTerm {
    public:
      virtual Term* type(Context& context, std::size_t n_parameters, Term *const* parameters) const;

    protected:
      Type(Source source);
    };

    class PrimitiveType : public Type {
    public:
      PrimitiveType();

    private:
      virtual LLVMFunctionBuilder::Result llvm_value_instruction(LLVMFunctionBuilder&, Term*) const;
    };

    class Value : public ProtoTerm {
    protected:
      Value(Source source);

    private:
      virtual LLVMConstantBuilder::Type llvm_type(LLVMConstantBuilder&, Term*) const;
    };

    class ConstantValue : public Value {
    public:
      ConstantValue(Source source=term_functional);

    private:
      virtual LLVMFunctionBuilder::Result llvm_value_instruction(LLVMFunctionBuilder&, Term*) const;
    };

    class GlobalVariable : public ConstantValue {
    protected:
      enum Slots {
	slot_initializer
      };

    public:
      GlobalVariable(bool read_only);

      static Term* create(bool read_only, Term *value);

      virtual Term* type(Context& context, std::size_t n_parameters, Term *const* parameters) const;

      /** \brief Whether this global will be placed in read-only memory. */
      bool read_only() const {return m_read_only;}

    private:
      bool m_read_only;

      virtual bool equals_internal(const ProtoTerm& other) const;
      virtual std::size_t hash_internal() const;
      virtual ProtoTerm* clone() const;
      virtual LLVMConstantBuilder::Constant llvm_value_constant(LLVMConstantBuilder&, Term*) const;
    };
  }
}

#endif
