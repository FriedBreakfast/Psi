#ifndef HPP_PSI_TVM_CORE
#define HPP_PSI_TVM_CORE

#include "Config.h"

#include <exception>
#include <vector>
#include <stdint.h>

#include <boost/functional/hash.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/unordered_set.hpp>
#include <boost/iterator/iterator_adaptor.hpp>
#include <boost/intrusive_ptr.hpp>

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
    class Module;
    class Value;

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
      term_instruction, ///< Instruction: \copybrief Instruction
      term_apply, ///< Apply: \copybrief Apply
      term_recursive, ///< Recursive: \copybrief Recursive
      term_recursive_parameter, ///< RecursiveParameter: \copybrief RecursiveParameter
      term_block, ///< Block: \copybrief Block
      term_global_variable, ///< GlobalVariable: \copybrief GlobalVariable
      term_function, ///< Function: \copybrief Function
      term_function_parameter, ///< FunctionParameter: \copybrief FunctionParameter
      term_phi, ///< Phi: \copybrief Phi
      term_function_type, ///< FunctionType: \copybrief FunctionType
      term_function_type_parameter, ///< FunctionTypeParameter: \copybrief FunctionTypeParameter
      term_functional, ///< Functional: \copybrief Functional
      term_catch_clause ///< CatchClause: \copybrief CatchClause
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
    
    template<typename T=Value>
    class ValuePtr : public boost::intrusive_ptr<T> {
      typedef boost::intrusive_ptr<T> BaseType;
      
    public:
      ValuePtr() {}
      explicit ValuePtr(T *ptr) : BaseType(ptr) {}
      template<typename U>
      ValuePtr(const ValuePtr<U>& src) : BaseType(src) {}

      template<typename U> ValuePtr& operator = (const ValuePtr<U>& src) {
        BaseType::operator = (src);
        return *this;
      }
      
    private:
      Value *m_value;
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
    class Value {
      friend class Context;

    public:
      virtual ~Value();

      enum Category {
        category_metatype,
        category_type,
        category_value,
        category_recursive
      };

      /// \brief The low level type of this term.
      TermType term_type() const {return static_cast<TermType>(m_term_type);}
      /// \brief Whether this term can be the type of another term
      bool is_type() const {return (m_category == category_metatype) || (m_category == category_type);}

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
       * <li>Phantom parameter - a phantom value</li>
       * <li>Function type parameter - a parameterized value</li>
       * <li>Global term - uses values from this module</li>
       * <li>Block - non-constant values are phi nodes in this block.</li>
       * <li>Instruction - this is the last instruction contributing to
       * the resulting value</li>
       * </ol>
       */
      const ValuePtr<>& source() const {return m_source;}

      bool phantom() const;
      bool parameterized() const;

      /// \brief Get the category of this value (whether it is a metatype, type, or value)
      Category category() const {return static_cast<Category>(m_category);}

      /** \brief Get the context this Term comes from. */
      Context& context() const {return *m_context;}

      /** \brief Get the term describing the type of this term. */
      const ValuePtr<>& type() const {return m_type;}
      
      void dump();

      std::size_t hash_value() const;

    private:
      std::size_t m_reference_count;
      Context *m_context;
      unsigned char m_term_type;
      unsigned char m_category : 2;
      ValuePtr<> m_type;
      ValuePtr<> m_source;
      boost::intrusive::list_member_hook<> m_value_list_hook;
      
      void destroy();
      
      friend void intrusive_ptr_add_ref(Value *self) {
        ++self->m_reference_count;
      }
      
      friend void intrusive_ptr_release(Value *self) {
        if (!--self->m_reference_count)
          self->destroy();
      }

    protected:
      Value(Context *context, TermType term_type, const ValuePtr<>& type, Value *source);
    };
    
    template<typename T>
    T* value_cast(Value *ptr) {
      return checked_cast<T*>(ptr);
    }
    
    template<typename T, typename U>
    ValuePtr<T> value_cast(const ValuePtr<U>& ptr) {
      return ValuePtr<T>(value_cast<T>(ptr.get()));
    }

    class HashableValue : public Value {
      friend class Context;
      friend class Term;
      friend class Apply;
      friend class FunctionType;
      friend class FunctionalValue;

    private:
      HashableValue(Context *context, TermType term_type, const ValuePtr<>& type, std::size_t hash);
      virtual ~HashableValue();
      typedef boost::intrusive::unordered_set_member_hook<> TermSetHook;
      TermSetHook m_hashable_set_hook;
      std::size_t m_hash;
    };

    /**
     * \brief Base class for globals: these are GlobalVariableTerm and FunctionTerm.
     */
    class Global : public Value {
      friend class GlobalVariable;
      friend class Function;
      friend class Module;

    public:
      const ValuePtr<>& value_type() const;
      /// \brief Get the module this global belongs to.
      Module* module() const {return m_module;}
      /// \brief Get the name of this global within the module.
      const std::string& name() const {return m_name;}
      
      /// \brief Get the minumum alignment of this symbol
      const ValuePtr<>& alignment() const {return m_alignment;}
      /// \brief Set the minimum alignment of this symbol.
      void set_alignment(const ValuePtr<>& alignment) {m_alignment = alignment;}

    private:
      Global(Context *context, TermType term_type, const ValuePtr<>& type, const std::string& name, Module *module);
      boost::intrusive::unordered_set_member_hook<> m_module_member_hook;
      std::string m_name;
      Module *m_module;
      ValuePtr<> m_alignment;
    };

    /**
     * \brief Global variable.
     */
    class GlobalVariable : public Global {
      friend class Module;

    public:
      void set_value(const ValuePtr<>& value);
      /// \brief Get the initial value of this global.
      const ValuePtr<>& value() const {return m_value;}

      /**
       * \brief Whether this global is created in a read only section
       * 
       * By default, global variables are created in writable sections.
       */
      bool constant() const {return m_constant;}
      /// \brief Set whether this global is created in a read only section
      void set_constant(bool is_const) {m_constant = is_const;}
      
    private:
      class Initializer;
      GlobalVariable(Context *context, const ValuePtr<>& type, const std::string& name, Module *module);

      bool m_constant;
      ValuePtr<> m_value;
    };

    class FunctionalValue;
    class FunctionType;
    class FunctionTypeParameter;
    class Function;
    class Apply;
    class Recursive;
    class RecursiveParameter;
    
    /**
     * \brief Tvm module class.
     * 
     * A collection of functions and global variables which can be compiled
     * and linked to other modules.
     */
    class Module : public boost::noncopyable {
    public:
      struct GlobalEquals {bool operator () (const Global&, const Global&) const;};
      struct GlobalHasher {std::size_t operator () (const Global&) const;};
      
      typedef boost::intrusive::unordered_set<Global,
                                              boost::intrusive::member_hook<Global, boost::intrusive::unordered_set_member_hook<>, &Global::m_module_member_hook>,
                                              boost::intrusive::equal<GlobalEquals>,
                                              boost::intrusive::hash<GlobalHasher>,
                                              boost::intrusive::power_2_buckets<true> > ModuleMemberList;
                                              
    private:
      Context *m_context;
      std::string m_name;
      static const std::size_t initial_members_buckets = 64;
      UniqueArray<ModuleMemberList::bucket_type> m_members_buckets;
      ModuleMemberList m_members;
      
      void add_member(Global*);
      
    public:
      Module(Context*, const std::string&);
      ~Module();
      
      /// \brief Get the context this module belongs to.
      Context& context() {return *m_context;}
      /// \brief Get the map of members of this module
      ModuleMemberList& members() {return m_members;}
      /// \brief Get the name of this module
      const std::string& name() {return m_name;}
      
      void dump();
      
      ValuePtr<Global> get_member(const std::string& name);
      ValuePtr<GlobalVariable> new_global_variable(const std::string& name, const ValuePtr<>& type);
      ValuePtr<GlobalVariable> new_global_variable_set(const std::string&, const ValuePtr<>& value);
      ValuePtr<Function> new_function(const std::string& name, const ValuePtr<FunctionType>& type);
    };

    /**
     * \brief Tvm context class.
     * 
     * Manages memory for terms, and ensures that equivalent terms are
     * not duplicated.
     */
    class Context {
      friend class HashableValue;
      friend class Function;
      friend class Block;
      friend class Module;

      struct TermDisposer;
      struct HashableValueHasher {std::size_t operator () (const HashableValue&) const;};

      typedef boost::intrusive::unordered_set<HashableValue,
                                              boost::intrusive::member_hook<HashableValue, boost::intrusive::unordered_set_member_hook<>, &HashableValue::m_hashable_set_hook>,
                                              boost::intrusive::hash<HashableValueHasher>,
                                              boost::intrusive::power_2_buckets<true> > HashTermSetType;

      typedef boost::intrusive::list<Value,
                                     boost::intrusive::constant_time_size<false>,
                                     boost::intrusive::member_hook<Value, boost::intrusive::list_member_hook<>, &Value::m_value_list_hook> > TermListType;

      static const std::size_t initial_hash_term_buckets = 64;
      UniqueArray<HashTermSetType::bucket_type> m_hash_term_buckets;
      HashTermSetType m_hash_terms;

      TermListType m_all_terms;

#ifdef PSI_DEBUG
      void dump_hash_terms();
      void print_hash_terms(std::ostream& output);
#endif

    public:
      Context();
      ~Context();

      template<typename T> typename T::Ptr get_functional(const std::vector<ValuePtr<> >& parameters, const typename T::Data& data = typename T::Data());

      ValuePtr<FunctionType> get_function_type(CallingConvention calling_convention,
                                               const ValuePtr<>& result,
                                               const std::vector<ValuePtr<FunctionTypeParameter> >& phantom_parameters,
                                               const std::vector<ValuePtr<FunctionTypeParameter> >& parameters);

      ValuePtr<FunctionType> get_function_type_fixed(CallingConvention calling_convention,
                                                     const ValuePtr<>& result,
                                                     const std::vector<ValuePtr<> >& parameter_types);

      ValuePtr<FunctionTypeParameter> new_function_type_parameter(const ValuePtr<>& type);

      ValuePtr<Apply> apply_recursive(const ValuePtr<Recursive>& recursive,
                                      const std::vector<ValuePtr<> >& parameters);

      ValuePtr<Recursive> new_recursive(const ValuePtr<>&, const ValuePtr<>&,
                                        const std::vector<ValuePtr<> >&);

      void resolve_recursive(const ValuePtr<Recursive>& recursive, const ValuePtr<>& to);

    private:
      Context(const Context&);

      template<typename T> typename T::TermType* allocate_term(const T& initializer);
      template<typename T> typename T::TermType* hash_term_get(T& Setup);

      ValuePtr<RecursiveParameter> new_recursive_parameter(const ValuePtr<>& type);

      class FunctionTypeResolverRewriter;

      ValuePtr<FunctionalValue> get_functional_bare(const FunctionalValue& value);
    };

    bool term_unique(const ValuePtr<>& term);
    void print_module(std::ostream&, Module*);
    void print_term(std::ostream&, const ValuePtr<>&);
  }
}

#endif
