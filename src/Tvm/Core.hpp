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
#include <boost/preprocessor/repetition/enum_params.hpp>
#include <boost/preprocessor/repetition/enum_trailing_params.hpp>
#include <boost/type_traits/alignment_of.hpp>
#include <boost/type_traits/remove_const.hpp>

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
    class RecursiveParameterTerm;

    class FunctionalTerm;
    class FunctionalTermBackend;
    struct FunctionalTypeResult;
    template<typename> class FunctionalTermBackendImpl;

    class FunctionTerm;
    class FunctionTypeTerm;
    class FunctionTypeParameterTerm;
    class BlockTerm;
    class FunctionTypeResolverTerm;
    class FunctionTypeResolverParameter;
    class PhiTerm;

    class InstructionTerm;
    class InstructionTermBackend;
    template<typename> class InstructionTermBackendImpl;

    class Metatype;
    class EmptyType;
    class BlockType;
    class PointerType;
    class ArrayType;
    class ArrayValue;
    class StructType;
    class StructValue;
    class UnionType;
    class UnionValue;
    class BooleanType;
    class IntegerType;

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

    template<typename> class TermIterator;
    class PersistentTermPtr;

    class TermUser : User {
      friend class Context;
      friend class Term;
      friend class PersistentTermPtr;
      template<typename> friend class TermIterator;

    public:
      TermType term_type() const {return static_cast<TermType>(m_term_type);}

    private:
      TermUser(const UserInitializer& ui, TermType term_type);
      ~TermUser();

      inline std::size_t n_uses() const {return User::n_uses();}
      inline Term* use_get(std::size_t n) const;
      inline void use_set(std::size_t n, Term *term);
      void resize_uses(std::size_t n);

      unsigned char m_term_type;
    };

    class PersistentTermPtr : TermUser {
    public:
      PersistentTermPtr();
      PersistentTermPtr(Term *term);
      PersistentTermPtr(const PersistentTermPtr&);
      ~PersistentTermPtr();

      const PersistentTermPtr& operator = (const PersistentTermPtr& o);
      Term* get() const {return use_get(0);}
      void reset(Term *term=0);

    private:
      Use m_uses[2];
    };

    template<typename T, typename U, typename W>
    class BackendTermPtr {
      typedef BackendTermPtr<T, U, W> ThisType;

      typedef void (ThisType::*SafeBoolType)() const;
      void safe_bool_true() const {}

    public:
      typedef U BackendType;
      typedef typename BackendType::Access AccessType;

      AccessType backend() const {
	const W* backend_impl = checked_cast<const W*>(m_ptr->backend());
	return AccessType(m_ptr, &backend_impl->impl());
      }

      T* get() const {return m_ptr;}
      T* operator -> () const {return m_ptr;}
      T& operator * () const {return *m_ptr;}

      template<typename X, typename Y, typename Z>
      bool operator == (const BackendTermPtr<X,Y,Z>& o) const {return o.get() == get();}
      template<typename X, typename Y, typename Z>
      bool operator != (const BackendTermPtr<X,Y,Z>& o) const {return o.get() != get();}
      template<typename X, typename Y, typename Z>
      bool operator < (const BackendTermPtr<X,Y,Z>& o) const {return o.get() < get();}
      operator SafeBoolType () const {return get() ? &ThisType::safe_bool_true : 0;}

    protected:
      BackendTermPtr() : m_ptr(0) {}
      BackendTermPtr(T* src) : m_ptr(src) {}

    private:
      T *m_ptr;
    };

    template<typename A, typename B, typename C> bool operator == (const BackendTermPtr<A,B,C>& lhs, Term *rhs) {return lhs.get() == rhs;}
    template<typename A, typename B, typename C> bool operator == (Term *lhs, const BackendTermPtr<A,B,C>& rhs) {return lhs == rhs.get();}
    template<typename A, typename B, typename C> bool operator != (const BackendTermPtr<A,B,C>& lhs, Term *rhs) {return lhs.get() != rhs;}
    template<typename A, typename B, typename C> bool operator != (Term *lhs, const BackendTermPtr<A,B,C>& rhs) {return lhs != rhs.get();}

    template<typename T>
    class FunctionalTermPtr : public BackendTermPtr<FunctionalTerm, T, FunctionalTermBackendImpl<T> > {
      friend class Context;
      typedef BackendTermPtr<FunctionalTerm, T, FunctionalTermBackendImpl<T> > BaseType;

      template<typename U> friend FunctionalTermPtr<U> checked_cast_functional(FunctionalTerm* src);
      template<typename U> friend FunctionalTermPtr<U> dynamic_cast_functional(FunctionalTerm* src);

    public:
      FunctionalTermPtr() {}

    private:
      FunctionalTermPtr(FunctionalTerm* src) : BaseType(src) {}
    };

    template<typename T>
    class InstructionTermPtr : public BackendTermPtr<InstructionTerm, T, InstructionTermBackendImpl<T> > {
      friend class BlockTerm;
      typedef BackendTermPtr<InstructionTerm, T,  InstructionTermBackendImpl<T> > BaseType;

    public:
      InstructionTermPtr() {}

    private:
      InstructionTermPtr(InstructionTerm* src) : BaseType(src) {}
    };

    template<typename T=Term>
    class ScopedTermPtrArray {
    public:
      ScopedTermPtrArray(std::size_t size)
        : m_size(size), m_ptr(new T*[size]) {
      }

      ~ScopedTermPtrArray() {
        delete [] m_ptr;
      }

      T*& operator[] (std::size_t n) {PSI_ASSERT(n < m_size); return m_ptr[n];}
      std::size_t size() const {return m_size;}
      T** get() {return m_ptr;}

      ArrayPtr<T*> array() const {
        return ArrayPtr<T*>(m_ptr, m_size);
      }

    private:
      ScopedTermPtrArray(const ScopedTermPtrArray&);
      const ScopedTermPtrArray& operator = (const ScopedTermPtrArray&);

      std::size_t m_size;
      T **m_ptr;
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

      enum Category {
        category_metatype,
        category_type,
        category_value,
        category_recursive
      };

      TermType term_type() const {return TermUser::term_type();}

      /// \brief Whether this term can be the type of another term
      bool is_type() const {return (m_category == category_metatype) || (m_category == category_type);}
      /// \brief If this term is abstract: it contains references to recursive term parameters which are unresolved.
      bool abstract() const {return m_abstract;}
      /// \brief If this term is parameterized: it contains references to function type parameters which are unresolved.
      bool parameterized() const {return m_parameterized;}
      /// \brief If this term is global: it only contains references to constant values and global addresses.
      bool global() const {return !m_source;}
      /// \brief Return true if the value of this term is not known
      //
      // What this means is somewhat type specific, for instance a
      // pointer type to phantom type is not considered phantom.
      bool phantom() const {return m_phantom;}
      /// \brief Get the term which generates this one - this will either be a function or a block
      Term* source() const {return m_source;}
      /// \brief Get the category of this value (whether it is a metatype, type, or value)
      Category category() const {return static_cast<Category>(m_category);}

      /** \brief Get the context this Term comes from. */
      Context& context() const {return *m_context;}

      /** \brief Get the term describing the type of this term. */
      Term* type() const {return use_get(0);}

      template<typename T> TermIterator<T> term_users_begin();
      template<typename T> TermIterator<T> term_users_end();

    private:
      Term(const UserInitializer& ui, Context *context, TermType term_type, bool abstract, bool parameterized, bool phantom, Term *source, Term* type);

      std::size_t hash_value() const;

      unsigned char m_category : 2;
      unsigned char m_abstract : 1;
      unsigned char m_parameterized : 1;
      unsigned char m_phantom : 1;
      Context *m_context;
      Term *m_source;
      boost::intrusive::list_member_hook<> m_term_list_hook;

    protected:
      std::size_t n_base_parameters() const {
	return n_uses() - 1;
      }

      void set_base_parameter(std::size_t n, Term *t);

      Term* get_base_parameter(std::size_t n) const {
        return use_get(n+1);
      }

      void resize_base_parameters(std::size_t n);
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
      friend class ApplyTerm;
      friend class FunctionalTerm;
      friend class FunctionTypeResolverTerm;

    private:
      HashTerm(const UserInitializer& ui, Context *context, TermType term_type, bool abstract, bool parameterized, bool phantom, Term *source, Term* type, std::size_t hash);
      virtual ~HashTerm();
      typedef boost::intrusive::unordered_set_member_hook<> TermSetHook;
      TermSetHook m_term_set_hook;
      std::size_t m_hash;
    };

    /**
     * \brief Base class for globals: these are GlobalVariableTerm and FunctionTerm.
     */
    class GlobalTerm : public Term {
      friend class GlobalVariableTerm;
      friend class FunctionTerm;

    public:
      Term* value_type() const;

    private:
      GlobalTerm(const UserInitializer& ui, Context *context, TermType term_type, Term* type);
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
      void set_value(Term* value);
      Term* value() const {return get_base_parameter(0);}

      bool constant() const {return m_constant;}

    private:
      class Initializer;
      /**
       * Need to add parameters for linkage and possibly thread
       * locality.
       */
      GlobalVariableTerm(const UserInitializer& ui, Context *context, Term* type, bool constant);

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
      friend class BlockTerm;

      struct TermDisposer;
      struct HashTermHasher {std::size_t operator () (const HashTerm&) const;};

      typedef boost::intrusive::unordered_set<HashTerm,
					      boost::intrusive::member_hook<HashTerm, boost::intrusive::unordered_set_member_hook<>, &HashTerm::m_term_set_hook>,
					      boost::intrusive::hash<HashTermHasher>,
					      boost::intrusive::power_2_buckets<true> > HashTermSetType;

      typedef boost::intrusive::list<Term,
                                     boost::intrusive::constant_time_size<false>,
                                     boost::intrusive::member_hook<Term, boost::intrusive::list_member_hook<>, &Term::m_term_list_hook > > TermListType;

      static const std::size_t initial_hash_term_buckets = 64;
      UniqueArray<HashTermSetType::bucket_type> m_hash_term_buckets;
      HashTermSetType m_hash_terms;

      TermListType m_all_terms;

      UniquePtr<llvm::LLVMContext> m_llvm_context;
      UniquePtr<llvm::Module> m_llvm_module;
      UniquePtr<llvm::ExecutionEngine> m_llvm_engine;

      std::tr1::unordered_set<llvm::JITEventListener*> m_llvm_jit_listeners;

#if PSI_DEBUG
      void dump_hash_terms();
      void print_hash_terms(std::ostream& output);
#endif

    public:
      Context();
      ~Context();

      template<typename T>
      FunctionalTermPtr<T> get_functional(const T& proto, ArrayPtr<Term*const> parameters);

      FunctionalTerm* get_functional_bare(const FunctionalTermBackend& backend, ArrayPtr<Term*const> parameters);

      FunctionTypeTerm* get_function_type(CallingConvention calling_convention,
                                          Term* result,
                                          ArrayPtr<FunctionTypeParameterTerm*const> phantom_parameters,
                                          ArrayPtr<FunctionTypeParameterTerm*const> parameters);

      FunctionTypeTerm* get_function_type_fixed(CallingConvention calling_convention,
                                                Term* result,
                                                ArrayPtr<Term*const> parameter_types);

      FunctionTypeParameterTerm* new_function_type_parameter(Term* type);

      ApplyTerm* apply_recursive(RecursiveTerm* recursive,
                                 ArrayPtr<Term*const> parameters);

      RecursiveTerm* new_recursive(Term *source,
                                   Term* result_type,
                                   ArrayPtr<Term*const> parameters,
                                   bool phantom=false);

      void resolve_recursive(RecursiveTerm* recursive, Term* to);

      GlobalVariableTerm* new_global_variable(Term* type, bool constant);
      GlobalVariableTerm* new_global_variable_set(Term* value, bool constant);

      FunctionTerm* new_function(FunctionTypeTerm* type);

      void* term_jit(GlobalTerm* term);

      FunctionalTermPtr<Metatype> get_metatype();
      FunctionalTermPtr<EmptyType> get_empty_type();
      FunctionalTermPtr<BlockType> get_block_type();
      FunctionalTermPtr<PointerType> get_pointer_type(Term* type);
      FunctionalTermPtr<BooleanType> get_boolean_type();
      FunctionalTermPtr<IntegerType> get_integer_type(std::size_t n_bits, bool is_signed=true);
      FunctionalTermPtr<ArrayType> get_array_type(Term* element_type, Term* length);
      FunctionalTermPtr<ArrayType> get_array_type(Term* element_type, std::size_t length);
      FunctionalTermPtr<ArrayValue> get_constant_array(Term* element_type, ArrayPtr<Term*const> elements);

      void register_llvm_jit_listener(llvm::JITEventListener *l);
      void unregister_llvm_jit_listener(llvm::JITEventListener *l);

#define PSI_TVM_VARARG_MAX 5

      //@{
      /// Vararg versions of functions above

#define PSI_TVM_VA(z,n,data) template<typename T> FunctionalTermPtr<T> get_functional_v(const T& proto BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,Term* p)) {Term *ap[n] = {BOOST_PP_ENUM_PARAMS_Z(z,n,p)}; return get_functional(proto, ArrayPtr<Term*const>(ap, n));}
      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

#define PSI_TVM_VA(z,n,data) FunctionTypeTerm* get_function_type_v(CallingConvention calling_convention, Term* result BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,FunctionTypeParameterTerm* p)) {FunctionTypeParameterTerm *ap[n] = {BOOST_PP_ENUM_PARAMS_Z(z,n,p)}; return get_function_type(calling_convention,result,ArrayPtr<FunctionTypeParameterTerm*const>(),ArrayPtr<FunctionTypeParameterTerm*const>(ap,n));}

      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

#define PSI_TVM_VA(z,n,data) FunctionTypeTerm* get_function_type_v(Term* result BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,FunctionTypeParameterTerm* p)) {FunctionTypeParameterTerm *ap[n] = {BOOST_PP_ENUM_PARAMS_Z(z,n,p)}; return get_function_type(cconv_tvm,result,ArrayPtr<FunctionTypeParameterTerm*const>(0,0),ArrayPtr<FunctionTypeParameterTerm*const>(ap,n));}

      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

#define PSI_TVM_VA(z,n,data) FunctionTypeTerm* get_function_type_fixed_v(CallingConvention calling_convention, Term* result BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,Term* p)) {Term *ap[n] = {BOOST_PP_ENUM_PARAMS_Z(z,n,p)}; return get_function_type_fixed(calling_convention,result,ArrayPtr<Term*const>(ap,n));}

      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

#define PSI_TVM_VA(z,n,data) FunctionTypeTerm* get_function_type_fixed_v(Term* result BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,Term* p)) {Term *ap[n] = {BOOST_PP_ENUM_PARAMS_Z(z,n,p)}; return get_function_type_fixed(cconv_tvm,result,ArrayPtr<Term*const>(ap,n));}

      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

#define PSI_TVM_VA(z,n,data) ApplyTerm* apply_recursive_v(RecursiveTerm* recursive BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,Term* p)) {Term *ap[n] = {BOOST_PP_ENUM_PARAMS_Z(z,n,p)}; return apply_recursive(recursive,ArrayPtr<Term*const>(ap,n));}
      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

#define PSI_TVM_VA(z,n,data) RecursiveTerm* new_recursive_v(Term *source, Term* result_type BOOST_PP_ENUM_TRAILING_PARAMS_Z(z,n,Term* p)) {Term *ap[n] = {BOOST_PP_ENUM_PARAMS_Z(z,n,p)}; return new_recursive(source,result_type,ArrayPtr<Term*const>(ap,n));}
      BOOST_PP_REPEAT(PSI_TVM_VARARG_MAX,PSI_TVM_VA,)
#undef PSI_TVM_VA

      //@}

    private:
      Context(const Context&);

      template<typename T> typename T::TermType* allocate_term(const T& initializer);
      template<typename T> typename T::TermType* hash_term_get(T& Setup);

      RecursiveParameterTerm* new_recursive_parameter(Term* type, bool phantom=false);

      FunctionTypeResolverTerm* get_function_type_resolver(Term* result, ArrayPtr<Term*const> parameters, std::size_t n_phantom, CallingConvention calling_convention);
      FunctionalTermPtr<FunctionTypeResolverParameter> get_function_type_resolver_parameter(Term* type, std::size_t depth, std::size_t index);

      typedef std::tr1::unordered_map<FunctionTypeTerm*, std::size_t> CheckCompleteMap;
      bool check_function_type_complete(Term* term, CheckCompleteMap& functions);

      class FunctionTypeResolverRewriter;
      bool search_for_abstract(Term *term, std::vector<Term*>& queue, std::tr1::unordered_set<Term*>& set);

      static void clear_and_queue_if_abstract(std::vector<Term*>& queue, Term* t);

      void clear_abstract(Term *term, std::vector<Term*>& queue);
    };

    bool term_unique(Term* term);
  }
}

#endif
