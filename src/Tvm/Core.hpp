#ifndef HPP_PSI_TVM_CORE
#define HPP_PSI_TVM_CORE

#include <vector>

#include <tr1/unordered_map>
#include <tr1/unordered_set>

#include <boost/functional/hash.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/unordered_set.hpp>
#include <boost/iterator/iterator_adaptor.hpp>
#include <boost/preprocessor/facilities/intercept.hpp>
#include <boost/preprocessor/repetition/repeat.hpp>
#include <boost/preprocessor/repetition/enum_binary_params.hpp>
#include <boost/preprocessor/repetition/enum_trailing_params.hpp>
#include <boost/type_traits/alignment_of.hpp>

#include "User.hpp"
#include "LLVMValue.hpp"
#include "../Utility.hpp"

namespace Psi {
  namespace Tvm {
    class LLVMValueBuilder;
    class LLVMFunctionBuilder;

    class Context;
    class Term;

    /**
     * \brief Identifies the Term subclass this object actually is.
     */
    enum TermType {
      term_ptr, ///<PersistentTermPtr: \copybrief PersistentTermPtr

      ///@{
      /// Non hashable terms
      term_instruction, ///< InstructionTerm: \copybrief InstructionTerm
      term_apply, ///< ApplyTerm: \copybrief ApplyTerm
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

    template<typename T> class TermIterator;
    class PersistentTermPtrBackend;

    class TermUser : User {
      friend class Context;
      friend class Term;
      friend class PersistentTermPtrBackend;
      template<typename> friend class TermIterator;

    public:
      TermType term_type() const {return static_cast<TermType>(m_term_type);}

    private:
      TermUser(const UserInitializer& ui, TermType term_type);
      ~TermUser();

      inline std::size_t n_uses() const {return User::n_uses();}
      inline Term* use_get(std::size_t n) const;
      inline void use_set(std::size_t n, Term *term);

      unsigned char m_term_type;
    };

    template<typename TermTypeP, typename Base>
    class TermPtrCommon {
      typedef void (TermPtrCommon<TermTypeP,Base>::*SafeBoolType)() const;
      void safe_bool_true() const {}
    public:
      typedef TermTypeP TermType;

      template<typename T, typename U>
      bool operator == (const TermPtrCommon<T,U>& o) const {return o.get() == get();}
      template<typename T, typename U>
      bool operator != (const TermPtrCommon<T,U>& o) const {return o.get() != get();}
      template<typename T, typename U>
      bool operator < (const TermPtrCommon<T,U>& o) const {return o.get() < get();}
      operator SafeBoolType () const {return get() ? &TermPtrCommon<TermTypeP,Base>::safe_bool_true : 0;}

      TermType* operator -> () const {return get();}
      TermType& operator * () const {return *get();}
      TermType* get() const {return checked_cast<TermType*>(m_base.get());}

    protected:
      TermPtrCommon() {}
      explicit TermPtrCommon(TermType* ptr) : m_base(ptr) {}
      Base m_base;
    };

    class PersistentTermPtrBackend : TermUser {
    public:
      PersistentTermPtrBackend();
      PersistentTermPtrBackend(const PersistentTermPtrBackend&);
      explicit PersistentTermPtrBackend(Term *ptr);
      ~PersistentTermPtrBackend();

      const PersistentTermPtrBackend& operator = (const PersistentTermPtrBackend& o) {reset(o.get()); return *this;}

      bool operator == (const PersistentTermPtrBackend& o) const {return get() == o.get();}
      bool operator != (const PersistentTermPtrBackend& o) const {return get() != o.get();}

      Term* get() const {return use_get(0);}
      void reset(Term *term=0);

    private:
      Use m_uses[2];
    };

    template<typename T=Term>
    class PersistentTermPtr : public TermPtrCommon<T, PersistentTermPtrBackend> {
      typedef TermPtrCommon<T, PersistentTermPtrBackend> BaseType;
    public:
      PersistentTermPtr() {}
      template<typename U, typename V>
      PersistentTermPtr(const TermPtrCommon<U,V>& ptr) : BaseType(check_cast_type(ptr.get())) {}

      template<typename U, typename V>
      PersistentTermPtr<T>& operator = (const TermPtrCommon<U,V>& src) {
        this->m_base.reset(check_cast_type(src.get()));
	return *this;
      }

    private:
      template<typename U> static T* check_cast_type(U* ptr) {
        return ptr;
      }
    };

    class TermPtrBackend {
    public:
      TermPtrBackend() : m_ptr(0) {}
      TermPtrBackend(Term *ptr) : m_ptr(0) {reset(ptr);}
      TermPtrBackend(const TermPtrBackend& src) : m_ptr(0) {reset(src.m_ptr);}
      ~TermPtrBackend() {reset();}
      const TermPtrBackend& operator = (const TermPtrBackend& src) {reset(src.m_ptr); return *this;}
      Term *get() const {return m_ptr;}
      void reset(Term *ptr=0);

    private:
      Term *m_ptr;
    };

    template<typename T=Term>
    class TermPtr : public TermPtrCommon<T, TermPtrBackend> {
      typedef TermPtrCommon<T, TermPtrBackend> BaseType;
      friend class Context;
      friend class Term;
      friend class FunctionTerm;

      template<typename V, typename W> friend TermPtr<V> checked_term_cast(const TermPtr<W>& ptr);

    public:
      TermPtr() {}
      template<typename U, typename V>
      TermPtr(const TermPtrCommon<U,V>& ptr) : BaseType(check_cast_type(ptr.get())) {}

      template<typename U, typename V>
      TermPtr<T>& operator = (const TermPtrCommon<U,V>& src) {
        this->m_base.reset(check_cast_type(src.get()));
	return *this;
      }

    private:
      TermPtr(T *p) : BaseType(p) {}

      template<typename U> static T* check_cast_type(U* ptr) {
        return ptr;
      }
    };

    template<typename T, typename U>
    TermPtr<T> checked_term_cast(const TermPtr<U>& ptr) {
      return TermPtr<T>(checked_cast<T*>(ptr.get()));
    }

    template<typename T, typename U, typename W>
    class BackendTermPtr : public TermPtrCommon<T, TermPtrBackend> {
      typedef TermPtrCommon<T, TermPtrBackend> BaseType;
      typedef BackendTermPtr<T, U, W> ThisType;

    public:
      typedef U BackendType;
      const BackendType& backend() const {return checked_cast<const W*>(this->get()->backend())->impl();}
    protected:
      BackendTermPtr() {}
      BackendTermPtr(T* src) : BaseType(src) {}
    };

    class FunctionalTerm;
    template<typename> class FunctionalTermBackendImpl;

    template<typename T>
    class FunctionalTermPtr : public BackendTermPtr<FunctionalTerm, T, FunctionalTermBackendImpl<T> > {
      friend class Context;
      typedef BackendTermPtr<FunctionalTerm, T, FunctionalTermBackendImpl<T> > BaseType;
      template<typename U, typename V, typename W> friend
      FunctionalTermPtr<U> checked_cast_functional(const TermPtrCommon<V,W>& src);

    private:
      FunctionalTermPtr() {}
      FunctionalTermPtr(FunctionalTerm* src) : BaseType(src) {}
    };

    class InstructionTerm;
    template<typename T> class InstructionTermBackendImpl;

    template<typename T>
    class InstructionTermPtr : public BackendTermPtr<InstructionTerm, T, InstructionTermBackendImpl<T> > {
      friend class Context;
      typedef BackendTermPtr<InstructionTerm, T,  InstructionTermBackendImpl<T> > BaseType;

    private:
      InstructionTermPtr() {}
      InstructionTermPtr(InstructionTerm* src) : BaseType(src) {}
    };

    template<typename T=Term>
    class TermRef {
    public:
      TermRef(T *ptr) : m_ptr(ptr) {}
      template<typename U>
      TermRef(const TermRef<U>& src) : m_ptr(src.get()) {}
      template<typename U, typename V>
      TermRef(const TermPtrCommon<U,V>& src) : m_ptr(src.get()) {}

      T* get() const {return m_ptr;}
      T* operator -> () const {return m_ptr;}
      T& operator * () const {return *m_ptr;}

    private:
      const TermRef& operator = (const TermRef&);

      T *m_ptr;
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
      friend class TermPtrBackend;
      friend class PersistentTermPtrBackend;

      friend class Context;
      template<typename> friend class TermIterator;

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
      friend class ApplyTerm;

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

      static bool term_iterator_check(TermType t) {return t != term_ptr;}
      TermType term_type() const {return TermUser::term_type();}

      /// \brief If this term is abstract: it contains references to recursive term parameters which are unresolved.
      bool abstract() const {return m_abstract;}
      /// \brief If this term is parameterized: it contains references to function type parameters which are unresolved.
      bool parameterized() const {return m_parameterized;}
      /// \brief If this term is global: it only contains references to constant values and global addresses.
      bool global() const {return m_global;}
      Category category() const {return static_cast<Category>(m_category);}

      /** \brief Get the context this Term comes from. */
      Context& context() const {return *m_context;}

      /** \brief Get the term describing the type of this term. */
      TermPtr<> type() const {return use_get(0);}

      template<typename T> TermIterator<T> term_users_begin();
      template<typename T> TermIterator<T> term_users_end();

    private:
      Term(const UserInitializer& ui, Context *context, TermType term_type, bool abstract, bool parameterized, bool global, TermRef<> type);

      std::size_t hash_value() const;

      std::size_t* term_use_count() {
	if (m_use_count_ptr)
	  return m_use_count.ptr;
	else
	  return &m_use_count.value;
      }

      void term_add_ref() {
	++*term_use_count();
      }

      void term_release() {
	if (!--*term_use_count())
	  term_destroy(this);
      }

      static void term_destroy(Term *term);

      unsigned char m_category : 2;
      unsigned char m_abstract : 1;
      unsigned char m_parameterized : 1;
      unsigned char m_global : 1;
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

      void set_base_parameter(std::size_t n, TermRef<> t);

      template<typename T>
      T* get_base_parameter_ptr(std::size_t n) const {
        return checked_cast<T*>(use_get(n+1));
      }

      Term* get_base_parameter_ptr(std::size_t n) const {
        return get_base_parameter_ptr<Term>(n);
      }

      template<typename T>
      TermPtr<T> get_base_parameter(std::size_t n) const {
	return TermPtr<T>(get_base_parameter_ptr<T>(n));
      }

      TermPtr<Term> get_base_parameter(std::size_t n) const {
        return get_base_parameter<Term>(n);
      }
    };

    inline Term* TermUser::use_get(std::size_t n) const {
      return static_cast<Term*>(User::use_get(n));
    }

    inline void TermUser::use_set(std::size_t n, Term *term) {
      User::use_set(n, term);
    }

    /**
     * \brief Change the term pointed to by this object.
     */
    inline void TermPtrBackend::reset(Term *ptr) {
      if (m_ptr == ptr)
        return;

      Term *old_ptr = m_ptr;
      if (ptr)
	ptr->term_add_ref();
      m_ptr = ptr;

      if (old_ptr)
        old_ptr->term_release();
    }

    template<typename T>
    class TermIterator
      : public boost::iterator_facade<TermIterator<T>, T, boost::bidirectional_traversal_tag> {
      friend class Term;
      friend class boost::iterator_core_access;

    public:
      TermIterator() {}

      T* get_ptr() const {return checked_cast<T*>(static_cast<Term*>(&*m_base));}

    private:
      UserIterator m_base;
      TermIterator(const UserIterator& base) : m_base(base) {}
      bool equal(const TermIterator& other) const {return m_base == other.m_base;}
      T& dereference() const {return *get_ptr();}
      bool check_stop() const {return !m_base.end() && T::term_iterator_check(static_cast<TermUser&>(*m_base).term_type());}
      void increment() {do {++m_base;} while (check_stop());}
      void decrement() {do {--m_base;} while (check_stop());}
    };

    template<typename T> TermIterator<T> Term::term_users_begin() {
      return TermIterator<T>(users_begin());
    }

    template<typename T> TermIterator<T> Term::term_users_end() {
      return TermIterator<T>(users_end());
    }

    class HashTerm : public Term {
      friend class Context;
      friend class Term;
      friend class FunctionalTerm;
      friend class FunctionTypeInternalTerm;
      friend class FunctionTypeInternalParameterTerm;

    private:
      HashTerm(const UserInitializer& ui, Context *context, TermType term_type, bool abstract, bool parameterized, bool global, TermRef<> type, std::size_t hash);
      virtual ~HashTerm();
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
      virtual TermPtr<> type(Context& context, std::size_t n_parameters, Term *const* parameters) const = 0;
      virtual LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm&) const = 0;
      virtual LLVMValue llvm_value_constant(LLVMValueBuilder&, FunctionalTerm&) const = 0;
      virtual LLVMType llvm_type(LLVMValueBuilder&, FunctionalTerm&) const = 0;

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
      const FunctionalTermBackend* backend() const {return m_backend;}
      std::size_t n_parameters() const {return Term::n_base_parameters();}
      TermPtr<> parameter(std::size_t n) const {return get_base_parameter(n);}

    private:
      class Setup;
      FunctionalTerm(const UserInitializer& ui, Context *context, TermRef<> type,
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
     * \brief Function calling conventions.
     */
    enum CallingConvention {
      /**
       * \brief TVM internal calling convention.
       *
       * This allows parameters to be passed whether or not their type
       * is known, and works as follows:
       *
       * <ul>
       * <li>All parameters are pointers</li>
       * <li>The first parameter is the address to write the return value to.</li>
       * <li>The remaining parameters are pointers to the values of each parameter.</li>
       * <li>If a parameter type is a pointer, the pointer is passed, not the address
       * of a variable holding the pointer.</li>
       * <li>The return value is a pointer. If the return type is a pointer, the
       * value of the pointer is returned, but must also be written to the return
       * value address passed to the function. Otherwise the return value will be
       * the same as the address passed in to write the result to.</li>
       * </ul>
       */
      cconv_tvm,
      /// C convention, compatible with host system.
      cconv_c,
      /// MS __stdcall convention
      cconv_x86_stdcall,
      /// MS __thiscall convention
      cconv_x86_thiscall,
      /// MS __fastcall convention
      cconv_x86_fastcall
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

    class RecursiveParameterTerm : public Term {
      friend class Context;

    private:
      class Initializer;
      RecursiveParameterTerm(const UserInitializer& ui, Context *context, TermRef<> type);
    };

    class ApplyTerm;

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
      void resolve(TermRef<> term);
      TermPtr<ApplyTerm> apply(std::size_t n_parameters, Term *const* values);

      std::size_t n_parameters() const {return n_base_parameters() - 2;}
      TermPtr<RecursiveParameterTerm> parameter(std::size_t i) const {return get_base_parameter<RecursiveParameterTerm>(i+2);}
      TermPtr<> result_type() const {return get_base_parameter(0);}
      TermPtr<> result() const {return get_base_parameter(1);}

    private:
      class Initializer;
      RecursiveTerm(const UserInitializer& ui, Context *context, TermRef<> result_type, bool global,
		    std::size_t n_parameters, RecursiveParameterTerm *const* parameters);
    };

    class ApplyTerm : public Term {
      friend class Context;

    public:
      std::size_t n_parameters() const {return n_base_parameters() - 1;}
      TermPtr<> unpack() const;

      TermPtr<RecursiveTerm> recursive() const {return get_base_parameter<RecursiveTerm>(0);}
      TermPtr<> parameter(std::size_t i) const {return get_base_parameter(i+1);}

    private:
      class Initializer;
      ApplyTerm(const UserInitializer& ui, Context *context, RecursiveTerm *recursive,
		std::size_t n_parameters, Term *const* parameters);
    };

    /**
     * \brief Base class for globals: these are GlobalVariableTerm and FunctionTerm.
     */
    class GlobalTerm : public Term {
      friend class GlobalVariableTerm;
      friend class FunctionTerm;

    private:
      GlobalTerm(const UserInitializer& ui, Context *context, TermType term_type, TermRef<> type);
    };

    /**
     * \brief Global variable.
     */
    class GlobalVariableTerm : public GlobalTerm {
      friend class Context;

    public:
      void set_value(TermRef<> value);
      TermPtr<> value() const {return get_base_parameter(0);}

      bool constant() const {return m_constant;}

    private:
      class Initializer;
      /**
       * Need to add parameters for linkage and possibly thread
       * locality.
       */
      GlobalVariableTerm(const UserInitializer& ui, Context *context, TermRef<> type, bool constant);

      bool m_constant;
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

    /**
     * \brief Implementation of FunctionalTermBackend.
     *
     * Actual implementations of this type should be created by
     * creating a class that this can wrap and getting a context to
     * make the appropriate term.
     */
    template<typename T>
    class FunctionalTermBackendImpl : public FunctionalTermBackend {
    public:
      typedef T ImplType;
      typedef FunctionalTermBackendImpl<T> ThisType;

      FunctionalTermBackendImpl(const ImplType& impl) : m_impl(impl) {
      }

      virtual ~FunctionalTermBackendImpl() {
      }

      virtual std::pair<std::size_t, std::size_t> size_align() const {
        return std::make_pair(sizeof(ThisType), boost::alignment_of<ThisType>::value);
      }

      virtual bool equals(const FunctionalTermBackend& other) const {
	return m_impl == checked_cast<const ThisType&>(other).m_impl;
      }

      virtual FunctionalTermBackend* clone(void *dest) const {
        return new (dest) ThisType(*this);
      }

      virtual TermPtr<> type(Context& context, std::size_t n_parameters, Term *const* parameters) const {
        return m_impl.type(context, n_parameters, parameters);
      }

      virtual LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
        return m_impl.llvm_value_instruction(builder, term);
      }

      virtual LLVMValue llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const {
        return m_impl.llvm_value_constant(builder, term);
      }

      virtual LLVMType llvm_type(LLVMValueBuilder& builder, FunctionalTerm& term) const {
        return m_impl.llvm_type(builder, term);
      }

      const ImplType& impl() const {
        return m_impl;
      }

    private:
      virtual std::size_t hash_internal() const {
        boost::hash<ImplType> hasher;
        return hasher(m_impl);
      }

      ImplType m_impl;
    };

    /**
     * \brief Perform a checked cast to a FunctionalTermPtr. This
     * checks both the term type and the backend type.
     */
    template<typename T, typename U, typename V>
    FunctionalTermPtr<T> checked_cast_functional(const TermPtrCommon<U,V>& src) {
      FunctionalTerm *t = checked_cast<FunctionalTerm*>(src.get());
      checked_cast<const FunctionalTermBackendImpl<T>*>(t->backend());
      return FunctionalTermPtr<T>(t);
    }

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

    class PointerType;
    class BlockType;

    class Context {
      friend class HashTerm;
      friend class FunctionTerm;

      TermPtr<MetatypeTerm> m_metatype;

      struct HashTermHasher {std::size_t operator () (const HashTerm&) const;};

      typedef boost::intrusive::unordered_set<HashTerm,
					      boost::intrusive::member_hook<HashTerm, boost::intrusive::unordered_set_member_hook<>, &HashTerm::m_term_set_hook>,
					      boost::intrusive::hash<HashTermHasher>,
					      boost::intrusive::power_2_buckets<true> > HashTermSetType;

      static const std::size_t initial_hash_term_buckets = 64;
      UniqueArray<HashTermSetType::bucket_type> m_hash_term_buckets;
      HashTermSetType m_hash_terms;

      UniquePtr<llvm::LLVMContext> m_llvm_context;
      UniquePtr<llvm::Module> m_llvm_module;
      UniquePtr<llvm::ExecutionEngine> m_llvm_engine;

    public:
      Context();
      ~Context();

      const TermPtr<MetatypeTerm>& get_metatype() {return m_metatype;}

      template<typename T>
      FunctionalTermPtr<T> get_functional(const T& proto, std::size_t n_parameters, Term *const* parameters) {
	return FunctionalTermPtr<T>(get_functional_internal(FunctionalTermBackendImpl<T>(proto), n_parameters, parameters).get());
      }

      TermPtr<FunctionTypeTerm> get_function_type(CallingConvention calling_convention,
						  TermRef<> result,
						  std::size_t n_parameters,
						  FunctionTypeParameterTerm *const* parameters);

      TermPtr<FunctionTypeTerm> get_function_type_fixed(CallingConvention calling_convention,
							TermRef<> result,
							std::size_t n_parameters,
							Term *const* parameter_types);

      TermPtr<FunctionTypeParameterTerm> new_function_type_parameter(TermRef<> type);

      TermPtr<ApplyTerm> apply_recursive(TermRef<RecursiveTerm> recursive,
					 std::size_t n_parameters,
					 Term *const* parameters);

      TermPtr<RecursiveTerm> new_recursive(bool global,
					   TermRef<> result_type,
					   std::size_t n_parameters,
					   Term *const* parameter_types);

      void resolve_recursive(TermRef<RecursiveTerm> recursive, TermRef<Term> to);

      TermPtr<GlobalVariableTerm> new_global_variable(TermRef<Term> type, bool constant);
      TermPtr<GlobalVariableTerm> new_global_variable_set(TermRef<Term> value, bool constant);

      TermPtr<FunctionTerm> new_function(TermRef<FunctionTypeTerm> type);

      void* term_jit(TermRef<GlobalTerm> term);

      FunctionalTermPtr<PointerType> get_pointer_type(TermRef<Term> type);
      FunctionalTermPtr<BlockType> get_block_type();

#define PSI_TVM_VARARG_MAX 5

      //@{
      /// Vararg versions of functions above

#define PSI_TVM_VA(z,n,data) template<typename T> FunctionalTermPtr<T> get_functional_v(const T& proto BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,TermRef<> p)) {Term *ap[n] = {BOOST_PP_ENUM_BINARY_PARAMS_Z(z,n,p,.get() BOOST_PP_INTERCEPT)}; return get_functional(proto, n, ap);}
      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

#define PSI_TVM_VA(z,n,data) TermPtr<FunctionTypeTerm> get_function_type_v(CallingConvention calling_convention, TermRef<> result BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,TermRef<FunctionTypeParameterTerm> p)) {FunctionTypeParameterTerm *ap[n] = {BOOST_PP_ENUM_BINARY_PARAMS_Z(z,n,p,.get() BOOST_PP_INTERCEPT)}; return get_function_type(calling_convention,result,n,ap);}

      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

#define PSI_TVM_VA(z,n,data) TermPtr<FunctionTypeTerm> get_function_type_v(TermRef<> result BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,TermRef<FunctionTypeParameterTerm> p)) {FunctionTypeParameterTerm *ap[n] = {BOOST_PP_ENUM_BINARY_PARAMS_Z(z,n,p,.get() BOOST_PP_INTERCEPT)}; return get_function_type(cconv_tvm,result,n,ap);}

      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

#define PSI_TVM_VA(z,n,data) TermPtr<FunctionTypeTerm> get_function_type_fixed_v(CallingConvention calling_convention, TermRef<> result BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,TermRef<> p)) {Term *ap[n] = {BOOST_PP_ENUM_BINARY_PARAMS_Z(z,n,p,.get() BOOST_PP_INTERCEPT)}; return get_function_type_fixed(calling_convention,result,n,ap);}

      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

#define PSI_TVM_VA(z,n,data) TermPtr<FunctionTypeTerm> get_function_type_fixed_v(TermRef<> result BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,TermRef<> p)) {Term *ap[n] = {BOOST_PP_ENUM_BINARY_PARAMS_Z(z,n,p,.get() BOOST_PP_INTERCEPT)}; return get_function_type_fixed(cconv_tvm,result,n,ap);}

      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

#define PSI_TVM_VA(z,n,data) TermPtr<ApplyTerm> apply_recursive_v(TermRef<RecursiveTerm> recursive BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,TermRef<> p)) {Term *ap[n] = {BOOST_PP_ENUM_BINARY_PARAMS_Z(z,n,p,.get() BOOST_PP_INTERCEPT)}; return apply_recursive(recursive, n, ap);}
      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

#define PSI_TVM_VA(z,n,data) TermPtr<RecursiveTerm> new_recursive_v(bool global, TermRef<> result_type BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,TermRef<> p)) {Term *ap[n] = {BOOST_PP_ENUM_BINARY_PARAMS_Z(z,n,p,.get() BOOST_PP_INTERCEPT)}; return new_recursive(global, result_type, n, ap);}
      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

      //@}

    private:
      Context(const Context&);

      template<typename T>
      typename T::TermType* hash_term_get(T& Setup);

      TermPtr<RecursiveParameterTerm> new_recursive_parameter(TermRef<> type);

      TermPtr<FunctionalTerm> get_functional_internal(const FunctionalTermBackend& backend,
                                                      std::size_t n_parameters, Term *const* parameters);
      TermPtr<FunctionalTerm> get_functional_internal_with_type(const FunctionalTermBackend& backend, TermRef<> type,
                                                                std::size_t n_parameters, Term *const* parameters);

      TermPtr<FunctionTypeInternalTerm> get_function_type_internal(TermRef<> result, std::size_t n_parameters, Term *const* parameter_types, CallingConvention calling_convention);
      TermPtr<FunctionTypeInternalParameterTerm> get_function_type_internal_parameter(TermRef<> type, std::size_t depth, std::size_t index);

      bool check_function_type_complete(TermRef<> term, std::tr1::unordered_set<FunctionTypeTerm*>& functions);

      struct FunctionResolveStatus {
	/// Depth of this function
	std::size_t depth;
	/// Index of parameter currently being resolved
	std::size_t index;
      };
      typedef std::tr1::unordered_map<FunctionTypeTerm*, FunctionResolveStatus> FunctionResolveMap;
      TermPtr<> build_function_type_resolver_term(std::size_t depth, TermRef<> term, FunctionResolveMap& functions);

      bool search_for_abstract(Term *term, std::vector<Term*>& queue, std::tr1::unordered_set<Term*>& set);

      static void clear_and_queue_if_abstract(std::vector<Term*>& queue, TermRef<> t);

      void clear_abstract(Term *term, std::vector<Term*>& queue);

#if 0
      InstructionTerm* get_instruction_internal(const InstructionProtoTerm& proto, std::size_t n_parameters,
						Term *const* parameters);
#endif
    };
  }
}

#endif
