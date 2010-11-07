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
    template<typename T> struct TermIteratorCheck;

    class ApplyTerm;
    class RecursiveTerm;

    class FunctionalTerm;
    class FunctionalTermBackend;
    template<typename> class FunctionalTermBackendImpl;

    class FunctionTerm;
    class FunctionTypeTerm;
    class FunctionTypeParameterTerm;
    class BlockTerm;
    class FunctionTypeResolverTerm;
    class FunctionTypeResolverParameter;

    class InstructionTerm;
    class InstructionTermBackend;
    template<typename> class InstructionTermBackendImpl;

    class Metatype;
    class EmptyType;
    class BlockType;
    class PointerType;
    class BooleanType;

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
      ///@}

      ///@{
      /// Hashable terms
      term_functional, ///< FunctionalTerm: \copybrief FunctionalTerm
      term_function_type_resolver, ///< FunctionTypeResolverTerm: \copybrief FunctionTypeResolverTerm
      ///@}
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
      void reset(Term *ptr=0) {reset_ptr(m_ptr, ptr);}

      template<typename T> static void reset_ptr(T*& value, T *src);

    private:
      Term *m_ptr;
    };

    template<typename T=Term>
    class TermPtr : public TermPtrCommon<T, TermPtrBackend> {
      typedef TermPtrCommon<T, TermPtrBackend> BaseType;

      template<typename V, typename W> friend TermPtr<V> checked_term_cast(const TermPtr<W>& ptr);
      template<typename V, typename W> friend TermPtr<V> dynamic_term_cast(const TermPtr<W>& ptr);

    public:
      TermPtr() {}
      template<typename U, typename V>
      TermPtr(const TermPtrCommon<U,V>& ptr) : BaseType(check_cast_type(ptr.get())) {}
      explicit TermPtr(T *p) : BaseType(p) {}

      template<typename U, typename V>
      TermPtr<T>& operator = (const TermPtrCommon<U,V>& src) {
        this->m_base.reset(check_cast_type(src.get()));
	return *this;
      }

    private:
      template<typename U> static T* check_cast_type(U* ptr) {
        return ptr;
      }
    };

    template<typename T=Term>
    class TermArrayCommon {
    public:
      typedef T TermType;

      TermType *const* get() const {return m_ptr;}
      TermType* operator [] (std::size_t n) const {return m_ptr[n];}
      std::size_t size() const {return m_size;}

    protected:
      TermArrayCommon(std::size_t size, TermType** ptr) : m_size(size), m_ptr(ptr) {}
      TermArrayCommon(const TermArrayCommon& o) : m_size(o.m_size), m_ptr(o.m_ptr) {}

      std::size_t m_size;
      TermType** m_ptr;
    };

    template<typename T=Term>
    class TermRefArray : public TermArrayCommon<T> {
    public:
      TermRefArray(std::size_t n, T*const* ptr) : TermArrayCommon<T>(n, const_cast<T**>(ptr)) {}
      TermRefArray(const TermArrayCommon<T>& o) : TermArrayCommon<T>(o) {}
    };

    template<typename T=Term>
    class TermPtrArray : public TermArrayCommon<T> {
    public:
      TermPtrArray(std::size_t size)
	: TermArrayCommon<T>(size, new T*[size]) {
	std::fill_n(this->m_ptr, size, static_cast<T*>(0));
      }

      ~TermPtrArray() {
	for (std::size_t n = 0; n < this->m_size; ++n)
	  TermPtrBackend::reset_ptr(this->m_ptr[n], static_cast<T*>(0));
	delete [] this->m_ptr;
      }

      template<typename U, typename V>
      void set(std::size_t n, const TermPtrCommon<U,V>& ptr) {
	T *ptr2 = ptr.get();
	TermPtrBackend::reset_ptr(this->m_ptr[n], ptr2);
      }
    };

    template<typename T, typename U>
    TermPtr<T> checked_term_cast(const TermPtr<U>& ptr) {
      return TermPtr<T>(checked_cast<T*>(ptr.get()));
    }

    template<typename T, typename U>
    TermPtr<T> dynamic_term_cast(const TermPtr<U>& ptr) {
      return TermPtr<T>(dynamic_cast<T*>(ptr.get()));
    }

    template<typename T, typename U, typename W>
    class BackendTermPtr : public TermPtrCommon<T, TermPtrBackend> {
      typedef TermPtrCommon<T, TermPtrBackend> BaseType;
      typedef BackendTermPtr<T, U, W> ThisType;

    public:
      typedef U BackendType;
      typedef typename BackendType::Access AccessType;
      AccessType backend() const {
	const T* ptr = this->get();
	const W* backend_impl = checked_cast<const W*>(ptr->backend());
	return AccessType(ptr, &backend_impl->impl());
      }
    protected:
      BackendTermPtr() {}
      BackendTermPtr(T* src) : BaseType(src) {}
    };

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

      /**
       * So FunctionTerm can manage FunctionParameterTerm and
       * BlockTerm references.
       */
      friend class FunctionTerm;

      friend class Context;
      template<typename> friend class TermIterator;

      friend class GlobalTerm;
      friend class FunctionParameterTerm;
      friend class BlockTerm;
      friend class FunctionTypeTerm;
      friend class FunctionTypeParameterTerm;
      friend class InstructionTerm;
      friend class PhiTerm;
      friend class HashTerm;
      friend class RecursiveTerm;
      friend class RecursiveParameterTerm;
      friend class ApplyTerm;
      friend class FunctionalTerm;
      friend class FunctionTypeResolverTerm;

    public:
      virtual ~Term();

      TermType term_type() const {return TermUser::term_type();}

      /// \brief If this term is abstract: it contains references to recursive term parameters which are unresolved.
      bool abstract() const {return m_abstract;}
      /// \brief If this term is parameterized: it contains references to function type parameters which are unresolved.
      bool parameterized() const {return m_parameterized;}
      /// \brief If this term is global: it only contains references to constant values and global addresses.
      bool global() const {return m_global;}

      /** \brief Get the context this Term comes from. */
      Context& context() const {return *m_context;}

      /** \brief Get the term describing the type of this term. */
      TermPtr<> type() const {return TermPtr<>(use_get(0));}

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

    template<>
    struct TermIteratorCheck<Term> {
      static bool check (TermType t) {
	return t != term_ptr;
      }
    };

    /**
     * \brief Change the term pointed to by this object.
     */
    template<typename T>
    inline void TermPtrBackend::reset_ptr(T*& ptr, T *value) {
      if (ptr == value)
        return;

      Term *old_ptr = ptr;
      if (value)
	value->term_add_ref();
      ptr = value;

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
      bool check_stop() const {return m_base.end() || TermIteratorCheck<T>::check(static_cast<TermUser&>(*m_base).term_type());}
      void search_forward() {while (!check_stop()) ++m_base;}
      void search_backward() {while (!check_stop()) --m_base;}
      void increment() {++m_base; search_forward();}
      void decrement() {--m_base; search_backward();}
    };

    template<typename T> TermIterator<T> Term::term_users_begin() {
      TermIterator<T> result(users_begin());
      result.search_forward();
      return result;
    }

    template<typename T> TermIterator<T> Term::term_users_end() {
      return TermIterator<T>(users_end());
    }

    class HashTerm : public Term {
      friend class Context;
      friend class Term;
      friend class FunctionalTerm;
      friend class FunctionTypeResolverTerm;

    private:
      HashTerm(const UserInitializer& ui, Context *context, TermType term_type, bool abstract, bool parameterized, bool global, TermRef<> type, std::size_t hash);
      virtual ~HashTerm();
      typedef boost::intrusive::unordered_set_member_hook<> TermSetHook;
      TermSetHook m_term_set_hook;
      std::size_t m_hash;
    };

    class RecursiveParameterTerm : public Term {
      friend class Context;

    private:
      class Initializer;
      RecursiveParameterTerm(const UserInitializer& ui, Context *context, TermRef<> type);
    };

    /**
     * \brief Base class for globals: these are GlobalVariableTerm and FunctionTerm.
     */
    class GlobalTerm : public Term {
      friend class GlobalVariableTerm;
      friend class FunctionTerm;

    public:
      TermPtr<> value_type() const;

    private:
      GlobalTerm(const UserInitializer& ui, Context *context, TermType term_type, TermRef<> type);
    };

    template<>
    struct TermIteratorCheck<GlobalTerm> {
      static bool check (TermType t) {
	return (t == term_global_variable) || (t == term_function);
      }
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

    template<>
    struct TermIteratorCheck<GlobalVariableTerm> {
      static bool check (TermType t) {
	return t == term_global_variable;
      }
    };

    class TermBackend {
    public:
      virtual ~TermBackend();
      virtual std::pair<std::size_t, std::size_t> size_align() const = 0;
    };

    class HashTermBackend : public TermBackend {
    public:
      std::size_t hash_value() const;
      virtual bool equals(const FunctionalTermBackend&) const = 0;

    private:
      virtual std::size_t hash_internal() const = 0;      
    };

    class Context {
      friend class HashTerm;
      friend class FunctionTerm;

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

      template<typename T>
      FunctionalTermPtr<T> get_functional(const T& proto, TermRefArray<> parameters);

      TermPtr<FunctionalTerm> get_functional_bare(const FunctionalTermBackend& backend, TermRefArray<> parameters);

      TermPtr<FunctionTypeTerm> get_function_type(CallingConvention calling_convention,
						  TermRef<> result,
						  TermRefArray<FunctionTypeParameterTerm> parameters);

      TermPtr<FunctionTypeTerm> get_function_type_fixed(CallingConvention calling_convention,
							TermRef<> result,
							TermRefArray<> parameter_types);

      TermPtr<FunctionTypeParameterTerm> new_function_type_parameter(TermRef<> type);

      TermPtr<ApplyTerm> apply_recursive(TermRef<RecursiveTerm> recursive,
					 TermRefArray<> parameters);

      TermPtr<RecursiveTerm> new_recursive(bool global,
					   TermRef<> result_type,
					   TermRefArray<> parameters);

      void resolve_recursive(TermRef<RecursiveTerm> recursive, TermRef<Term> to);

      TermPtr<GlobalVariableTerm> new_global_variable(TermRef<Term> type, bool constant);
      TermPtr<GlobalVariableTerm> new_global_variable_set(TermRef<Term> value, bool constant);

      TermPtr<FunctionTerm> new_function(TermRef<FunctionTypeTerm> type);

      void* term_jit(TermRef<GlobalTerm> term);

      FunctionalTermPtr<Metatype> get_metatype();
      FunctionalTermPtr<EmptyType> get_empty_type();
      FunctionalTermPtr<BlockType> get_block_type();
      FunctionalTermPtr<PointerType> get_pointer_type(TermRef<Term> type);
      FunctionalTermPtr<BooleanType> get_boolean_type();

#define PSI_TVM_VARARG_MAX 5

      //@{
      /// Vararg versions of functions above

#define PSI_TVM_VA(z,n,data) template<typename T> FunctionalTermPtr<T> get_functional_v(const T& proto BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,TermRef<> p)) {Term *ap[n] = {BOOST_PP_ENUM_BINARY_PARAMS_Z(z,n,p,.get() BOOST_PP_INTERCEPT)}; return get_functional(proto, TermRefArray<>(n, ap));}
      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

#define PSI_TVM_VA(z,n,data) TermPtr<FunctionTypeTerm> get_function_type_v(CallingConvention calling_convention, TermRef<> result BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,TermRef<FunctionTypeParameterTerm> p)) {FunctionTypeParameterTerm *ap[n] = {BOOST_PP_ENUM_BINARY_PARAMS_Z(z,n,p,.get() BOOST_PP_INTERCEPT)}; return get_function_type(calling_convention,result,TermRefArray<FunctionTypeParameterTerm>(n,ap));}

      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

#define PSI_TVM_VA(z,n,data) TermPtr<FunctionTypeTerm> get_function_type_v(TermRef<> result BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,TermRef<FunctionTypeParameterTerm> p)) {FunctionTypeParameterTerm *ap[n] = {BOOST_PP_ENUM_BINARY_PARAMS_Z(z,n,p,.get() BOOST_PP_INTERCEPT)}; return get_function_type(cconv_tvm,result,TermRefArray<FunctionTypeParameterTerm>(n,ap));}

      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

#define PSI_TVM_VA(z,n,data) TermPtr<FunctionTypeTerm> get_function_type_fixed_v(CallingConvention calling_convention, TermRef<> result BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,TermRef<> p)) {Term *ap[n] = {BOOST_PP_ENUM_BINARY_PARAMS_Z(z,n,p,.get() BOOST_PP_INTERCEPT)}; return get_function_type_fixed(calling_convention,result,TermRefArray<>(n,ap));}

      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

#define PSI_TVM_VA(z,n,data) TermPtr<FunctionTypeTerm> get_function_type_fixed_v(TermRef<> result BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,TermRef<> p)) {Term *ap[n] = {BOOST_PP_ENUM_BINARY_PARAMS_Z(z,n,p,.get() BOOST_PP_INTERCEPT)}; return get_function_type_fixed(cconv_tvm,result,TermRefArray<>(n,ap));}

      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

#define PSI_TVM_VA(z,n,data) TermPtr<ApplyTerm> apply_recursive_v(TermRef<RecursiveTerm> recursive BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,TermRef<> p)) {Term *ap[n] = {BOOST_PP_ENUM_BINARY_PARAMS_Z(z,n,p,.get() BOOST_PP_INTERCEPT)}; return apply_recursive(recursive,TermRefArray<>(n,ap));}
      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

#define PSI_TVM_VA(z,n,data) TermPtr<RecursiveTerm> new_recursive_v(bool global, TermRef<> result_type BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,TermRef<> p)) {Term *ap[n] = {BOOST_PP_ENUM_BINARY_PARAMS_Z(z,n,p,.get() BOOST_PP_INTERCEPT)}; return new_recursive(global,result_type,TermRefArray<>(n,ap));}
      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

      //@}

    private:
      Context(const Context&);

      template<typename T>
      TermPtr<typename T::TermType> hash_term_get(T& Setup);

      TermPtr<RecursiveParameterTerm> new_recursive_parameter(TermRef<> type);

      TermPtr<FunctionTypeResolverTerm> get_function_type_resolver(TermRef<> result, TermRefArray<> parameters, CallingConvention calling_convention);
      FunctionalTermPtr<FunctionTypeResolverParameter> get_function_type_resolver_parameter(TermRef<> type, std::size_t depth, std::size_t index);

      typedef std::tr1::unordered_map<FunctionTypeTerm*, std::size_t> CheckCompleteMap;
      bool check_function_type_complete(TermRef<> term, CheckCompleteMap& functions);

      class FunctionTypeResolverRewriter;
      class FunctionTypeRootParameterRewriter;
      bool search_for_abstract(Term *term, std::vector<Term*>& queue, std::tr1::unordered_set<Term*>& set);

      static void clear_and_queue_if_abstract(std::vector<Term*>& queue, TermRef<> t);

      void clear_abstract(Term *term, std::vector<Term*>& queue);
    };

    bool term_unique(TermRef<> term);
  }
}

#endif
