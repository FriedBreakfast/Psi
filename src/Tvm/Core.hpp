#ifndef HPP_PSI_TVM_CORE
#define HPP_PSI_TVM_CORE

#include <exception>
#include <vector>
#include <stdint.h>

#include <tr1/unordered_map>
#include <tr1/unordered_set>

#include <boost/functional/hash.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/unordered_set.hpp>
#include <boost/iterator/iterator_adaptor.hpp>

#include "User.hpp"
#include "../Utility.hpp"

namespace Psi {
  /**
   * A low level compiler system which should basically be
   * functionally equivalent to C with support for a polymorphic type
   * system, although the syntax and semantics for control flow are
   * lower level, more akin to assembler.
   */
  namespace Tvm {
    class Context;
    class Term;

    /**
     * Thrown when an error is caused by the users use of the library.
     */
    class TvmUserError : public std::exception {
    public:
      explicit TvmUserError(const std::string& msg);
      virtual ~TvmUserError() throw ();
      virtual const char* what() const throw();

    private:
      const char *m_str;
      std::string m_message;
    };

    /**
     * Thrown when an internal library error occurs, which should not
     * occur.
     */
    class TvmInternalError : public std::exception {
    public:
      explicit TvmInternalError(const std::string& msg);
      virtual ~TvmInternalError() throw ();
      virtual const char* what() const throw();

    private:
      const char *m_str;
      std::string m_message;
    };

    /**
     * \brief Identifies the Term subclass this object actually is.
     */
    enum TermType {
      term_ptr, ///<PersistentTermPtr: \copybrief PersistentTermPtr
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
      term_functional, ///< FunctionalTerm: \copybrief FunctionalTerm
      term_function_type_resolver, ///< FunctionTypeResolverTerm: \copybrief FunctionTypeResolverTerm
    };

    /**
     * \brief Function calling conventions.
     */
    enum CallingConvention {
      /// C convention, compatible with host system.
      cconv_c,
      /// MS __stdcall convention
      cconv_x86_stdcall,
      /// MS __thiscall convention
      cconv_x86_thiscall,
      /// MS __fastcall convention
      cconv_x86_fastcall
    };

    template<typename T> struct CastImplementation;

    template<typename T, typename U>
    typename CastImplementation<T>::Ptr cast(U *p) {
      return CastImplementation<T>::cast(p);
    }

    template<typename T, typename U>
    bool isa(U *p) {
      return CastImplementation<T>::isa(p);
    }

    template<typename T, typename U>
    typename CastImplementation<T>::Ptr dyn_cast(U *p) {
      return isa<T>(p) ? cast<T>(p) : CastImplementation<T>::null();
    }

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
#ifdef PSI_DEBUG
      virtual ~TermUser();
#else
      ~TermUser();
#endif

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
    class Term : public TermUser, Used {
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
      /**
       * \brief Get the term which generates this one.
       * 
       * The source can be several different types of term. A term type
       * further down the list overrides one further up since the higher
       * up item must be a parent of the lower one.
       * 
       * <ol>
       * <li>Null - this term is global.</li>
       * <li>Function - non-constant values are parameters to the
       * given function.</li>
       * <li>Block - non-constant values are phi nodes in this block.</li>
       * <li>Instruction - this is the last instruction contributing to
       * the resulting value</li>
       * </ol>
       */
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

    template<> struct CastImplementation<Term> {
      typedef Term* Ptr;
      typedef Term& Reference;

      static Ptr null() {
        return 0;
      }

      static Ptr cast(TermUser *t) {
        return checked_cast<Term*>(t);
      }

      static bool isa(TermUser *t) {
        return t->term_type() != term_ptr;
      }
    };

    template<typename T, TermType term_type>
    struct CoreCastImplementation {
      typedef T* Ptr;
      typedef T& Reference;

      static Ptr null() {
        return 0;
      }

      static Ptr cast(TermUser *t) {
        return checked_cast<T*>(t);
      }

      static bool isa(TermUser *t) {
        return t->term_type() == term_type;
      }
    };

    template<typename T>
    class TermIterator
      : public boost::iterator_facade<TermIterator<T>, T, boost::bidirectional_traversal_tag, typename CastImplementation<T>::Reference> {
      friend class Term;
      friend class boost::iterator_core_access;

    public:
      TermIterator() {}

      typename CastImplementation<T>::Ptr get_ptr() const {return cast<T>(static_cast<TermUser*>(&*m_base));}

    private:
      UserIterator m_base;
      TermIterator(const UserIterator& base) : m_base(base) {}
      bool equal(const TermIterator& other) const {return m_base == other.m_base;}
      typename CastImplementation<T>::Reference dereference() const {return *get_ptr();}
      bool check_stop() const {return m_base.end() || isa<T>(static_cast<TermUser*>(&*m_base));}
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

    /**
     * For use with \c PtrAdapter. This is used by functional and
     * instruction terms to allow access to term-type-specific
     * functionality at the same time as common functionality.
     */
    class TermPtrBase {
    public:
      TermPtrBase() : m_ptr(0) {}
      explicit TermPtrBase(Term *ptr) : m_ptr(ptr) {}

      /// \copydoc Term::term_type
      TermType term_type() const {return m_ptr->term_type();}
      /// \copydoc Term::is_type
      bool is_type() const {return m_ptr->is_type();}
      /// \copydoc Term::abstract
      bool abstract() const {return m_ptr->abstract();}
      /// \copydoc Term::parameterized
      bool parameterized() const {return m_ptr->parameterized();}
      /// \copydoc Term::global
      bool global() const {return m_ptr->global();}
      /// \copydoc Term::phantom
      bool phantom() const {return m_ptr->phantom();}
      /// \copydoc Term::source
      Term* source() const {return m_ptr->source();}
      /// \copydoc Term::category
      Term::Category category() const {return m_ptr->category();}
      /// \copydoc Term::context
      Context& context() const {return m_ptr->context();}
      /// \copydoc Term::type
      Term* type() const {return m_ptr->type();}
      /// \copydoc Term::term_users_begin
      template<typename T> TermIterator<T> term_users_begin() {return m_ptr->term_users_begin<T>();}
      /// \copydoc Term::term_users_end
      template<typename T> TermIterator<T> term_users_end() {return m_ptr->term_users_end<T>();}

    protected:
      Term *m_ptr;
    };

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
      const std::string& name() const {return m_name;}

    private:
      GlobalTerm(const UserInitializer& ui, Context *context, TermType term_type, Term* type, const std::string& name);
      std::string m_name;
    };

#ifndef PSI_DOXYGEN
    template<> struct CastImplementation<GlobalTerm> {
      typedef GlobalTerm *Ptr;
      typedef GlobalTerm& Reference;

      static Ptr null() {
        return 0;
      }

      static Ptr cast(TermUser *t) {
        return checked_cast<GlobalTerm*>(t);
      }

      static bool isa(TermUser *t) {
	return (t->term_type() == term_global_variable) || (t->term_type() == term_function);
      }
    };
#endif

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
      GlobalVariableTerm(const UserInitializer& ui, Context *context, Term* type, bool constant, const std::string& name);

      bool m_constant;
    };

#ifndef PSI_DOXYGEN
    template<> struct CastImplementation<GlobalVariableTerm> : CoreCastImplementation<GlobalVariableTerm, term_global_variable> {};
#endif

    class FunctionalTerm;
    class FunctionalTermSetup;
    class FunctionTypeTerm;
    class FunctionTypeParameterTerm;
    class FunctionTypeResolverTerm;
    class FunctionTerm;
    class ApplyTerm;
    class RecursiveTerm;
    class RecursiveParameterTerm;

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

#if PSI_DEBUG
      void dump_hash_terms();
      void print_hash_terms(std::ostream& output);
#endif

    public:
      Context();
      ~Context();

      template<typename T> typename T::Ptr get_functional(ArrayPtr<Term*const> parameters, const typename T::Data& data = typename T::Data());

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

      GlobalVariableTerm* new_global_variable(Term* type, bool constant, const std::string& name);
      GlobalVariableTerm* new_global_variable_set(Term* value, bool constant, const std::string& name);

      FunctionTerm* new_function(FunctionTypeTerm* type, const std::string& name);

    private:
      Context(const Context&);

      template<typename T> typename T::TermType* allocate_term(const T& initializer);
      template<typename T> typename T::TermType* hash_term_get(T& Setup);

      RecursiveParameterTerm* new_recursive_parameter(Term* type, bool phantom=false);

      FunctionTypeResolverTerm* get_function_type_resolver(Term* result, ArrayPtr<Term*const> parameters, std::size_t n_phantom, CallingConvention calling_convention);

      typedef std::tr1::unordered_map<FunctionTypeTerm*, std::size_t> CheckCompleteMap;
      bool check_function_type_complete(Term* term, CheckCompleteMap& functions);

      class FunctionTypeResolverRewriter;
      bool search_for_abstract(Term *term, std::vector<Term*>& queue, std::tr1::unordered_set<Term*>& set);

      static void clear_and_queue_if_abstract(std::vector<Term*>& queue, Term* t);

      void clear_abstract(Term *term, std::vector<Term*>& queue);

      FunctionalTerm* get_functional_bare(const FunctionalTermSetup& setup, ArrayPtr<Term*const> parameters);
    };

    bool term_unique(Term* term);
  }
}

#endif
