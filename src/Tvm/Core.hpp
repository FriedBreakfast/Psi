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
#include <boost/unordered_set.hpp>

#include "../SourceLocation.hpp"
#include "../Utility.hpp"
#include "../Array.hpp"

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
      term_recursive, ///< RecursiveType: \copybrief RecursiveType
      term_recursive_parameter, ///< RecursiveParameter: \copybrief RecursiveParameter
      term_block, ///< Block: \copybrief Block
      term_global_variable, ///< GlobalVariable: \copybrief GlobalVariable
      term_function, ///< Function: \copybrief Function
      term_function_parameter, ///< FunctionParameter: \copybrief FunctionParameter
      term_phi, ///< Phi: \copybrief Phi
      term_function_type, ///< FunctionType: \copybrief FunctionType
      term_parameter_placeholder, ///< ParameterPlaceholder: \copybrief ParameterPlaceholder
      term_functional, ///< Functional: \copybrief Functional
      term_exists ///< Exists: \copybrief Exists
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

    PSI_VISIT_SIMPLE(CallingConvention);
    
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
      
      friend std::size_t hash_value(const ValuePtr<T>& v) {
        return v ? v->hash_value() : 0;
      }
    };

    template<typename Derived>
    class ValuePtrVisitorBase {
      Derived& derived() {
        return static_cast<Derived&>(*this);
      }
      
    public:
      template<typename T>
      void visit_base(const boost::array<T*,1>& c) {
        if (derived().do_visit_base(visitor_tag<T>()))
          visit_members(derived(), c);
      }

      /// Simple types cannot hold references, so we aren't interested in them.
      template<typename T>
      void visit_simple(const char*, const boost::array<T*, 1>&) {
      }

      template<typename T>
      void visit_object(const char*, const boost::array<T*,1>& obj) {
        visit_members(*this, obj);
      }

      /// Simple pointers are assumed to be owned by this object
      template<typename T>
      void visit_object(const char*, const boost::array<T**,1>& obj) {
        if (*obj[0]) {
          boost::array<T*, 1> star = {{*obj[0]}};
          visit_callback(*this, NULL, star);
        }
      }

      template<typename T>
      void visit_object(const char*, const boost::array<ValuePtr<T>*,1>& ptr) {
        derived().visit_ptr(*ptr[0]);
      }

      template<typename T>
      void visit_object(const char*, const boost::array<const ValuePtr<T>*,1>& ptr) {
        derived().visit_ptr(*ptr[0]);
      }
      
      template<typename T>
      void visit_value_list(const char*, const boost::array<T*,1>& ptr) {
        derived().visit_ptr_list(*ptr[0]);
      }

      template<typename T>
      void visit_sequence (const char*, const boost::array<T*,1>& collections) {
        for (typename T::iterator ii = collections[0]->begin(), ie = collections[0]->end(); ii != ie; ++ii) {
          boost::array<typename T::value_type*, 1> m = {{&*ii}};
          visit_callback(*this, NULL, m);
        }
      }

      template<typename T>
      void visit_sequence (const char*, const boost::array<const T*,1>& collections) {
        for (typename T::const_iterator ii = collections[0]->begin(), ie = collections[0]->end(); ii != ie; ++ii) {
          boost::array<const typename T::value_type*, 1> m = {{&*ii}};
          visit_callback(*this, NULL, m);
        }
      }

      template<typename T>
      void visit_map(const char*, const boost::array<T*,1>& maps) {
        for (typename T::iterator ii = maps[0]->begin(), ie = maps[0]->end(); ii != ie; ++ii) {
#if 0
          boost::array<const typename T::key_type*, 1> k = {{&ii->first}};
          visit_object(NULL, k);
#endif
          boost::array<typename T::mapped_type*, 1> v = {{&ii->second}};
          visit_callback(*this, NULL, v);
        }
      }
    };
    
    struct CheckSourceParameter {
      enum Mode {
        mode_before_block,
        mode_after_block,
        mode_before_instruction,
        mode_global
      };
      
      Mode mode;
      Value *point;
      boost::unordered_set<Value*> available;
      
      CheckSourceParameter(Mode mode_, Value *point_) : mode(mode_), point(point_) {}
    };
    
    void check_phantom_available(CheckSourceParameter& parameter, Value *phantom);
    
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
      friend struct GCIncrementVisitor;
      friend struct GCDecerementVisitor;
      
      /// Disable general new operator
      static void* operator new (size_t) {PSI_FAIL("Value::new should never be called");}
      /// Disable placement new
      static void* operator new (size_t, void*) {PSI_FAIL("Value::new should never be called");}

    public:
      virtual ~Value();

      enum Category {
        category_metatype,
        category_type,
        category_value,
        category_recursive,
        category_undetermined ///< Used for hashable values whose category is determined when they are moved onto the heap
      };

      /// \brief The low level type of this term.
      TermType term_type() const {return static_cast<TermType>(m_term_type);}
      /// \brief Whether this term can be the type of another term
      bool is_type() const {return (m_category == category_metatype) || (m_category == category_type);}

      /// \brief Get the category of this value (whether it is a metatype, type, or value)
      Category category() const {return static_cast<Category>(m_category);}

      /** \brief Get the context this Term comes from. */
      Context& context() const {return *m_context;}

      /** \brief Get the term describing the type of this term. */
      const ValuePtr<>& type() const {return m_type;}
      
      /** \brief Get the location this value originated from */
      const SourceLocation& location() const {return m_location;}
      
      /**
       * \brief Get an approximate source for this term.
       *
       * This should ONLY be used in the disassembler.
       */
      virtual Value* disassembler_source() = 0;
      
      void check_source(CheckSourceParameter& parameter);
      
#ifdef PSI_DEBUG
      void dump();
#endif

      std::size_t hash_value() const;
      
      template<typename V>
      static void visit(V& v) {
        v("type", &Value::m_type);
      }
      
    private:
      std::size_t m_reference_count;
      Context *m_context;
      unsigned char m_term_type;
      unsigned char m_category;
      ValuePtr<> m_type;
      SourceLocation m_location;
      boost::intrusive::list_member_hook<> m_value_list_hook;
      
      void destroy();
      virtual void gc_increment() = 0;
      virtual void gc_decrement() = 0;
      virtual void gc_clear() = 0;
      virtual void check_source_hook(CheckSourceParameter& parameter) = 0;
      
      friend void intrusive_ptr_add_ref(Value *self) {
        ++self->m_reference_count;
      }
      
      friend void intrusive_ptr_release(Value *self) {
        if (!--self->m_reference_count)
          self->destroy();
      }

    protected:
      Value(Context& context, TermType term_type, const ValuePtr<>& type, const SourceLocation& location);
      
      void set_type(const ValuePtr<>& type);
    };
    
#define PSI_TVM_VALUE_DECL(Type) \
  private: \
    virtual void gc_increment(); \
    virtual void gc_decrement(); \
    virtual void gc_clear();
    
    struct GCIncrementVisitor : ValuePtrVisitorBase<GCIncrementVisitor> {
      template<typename T>
      void visit_ptr(const ValuePtr<T>& ptr) {
        ++ptr->m_reference_count;
      }
      
      template<typename T>
      void visit_ptr_list(T& list) {
        for (typename T::const_iterator ii = list.begin(), ie = list.end(); ii != ie; ++ii)
          visit_ptr(*ii);
      }

      template<typename T> bool do_visit_base(VisitorTag<T>) {return true;}
    };
    
    struct GCDecerementVisitor : ValuePtrVisitorBase<GCDecerementVisitor> {
      template<typename T>
      void visit_ptr(const ValuePtr<T>& ptr) {
        --ptr->m_reference_count;
      }

      template<typename T>
      void visit_ptr_list(T& list) {
        for (typename T::const_iterator ii = list.begin(), ie = list.end(); ii != ie; ++ii)
          visit_ptr(*ii);
      }

      template<typename T> bool do_visit_base(VisitorTag<T>) {return true;}
    };
    
    struct GCClearVisitor : ValuePtrVisitorBase<GCClearVisitor> {
      template<typename T>
      void visit_ptr(ValuePtr<T>& ptr) {
        ptr.reset();
      }
      
      template<typename T>
      void visit_ptr_list(T& list) {
        list.clear();
      }

      template<typename T> bool do_visit_base(VisitorTag<T>) {return true;}
    };
    
#define PSI_TVM_VALUE_IMPL(Type,Base) \
    void Type::gc_increment() { \
      GCIncrementVisitor v; \
      boost::array<Type*,1> c = {{this}}; \
      visit_members(v, c); \
    } \
    \
    void Type::gc_decrement() { \
      GCDecerementVisitor v; \
      boost::array<Type*,1> c = {{this}}; \
      visit_members(v, c); \
    } \
    \
    void Type::gc_clear() { \
      GCClearVisitor v; \
      boost::array<Type*,1> c = {{this}}; \
      visit_members(v, c); \
    }
    
    template<typename T>
    T* value_cast(Value *ptr) {
      return checked_cast<T*>(ptr);
    }

    template<typename T>
    const T* value_cast(const Value *ptr) {
      return checked_cast<const T*>(ptr);
    }
    
    template<typename T, typename U>
    ValuePtr<T> value_cast(const ValuePtr<U>& ptr) {
      return ValuePtr<T>(value_cast<T>(ptr.get()));
    }
    
    template<typename T>
    bool isa(const Value *ptr) {
      return ptr && T::isa_impl(*ptr);
    }
    
    template<typename T, typename U>
    bool isa(const ValuePtr<U>& ptr) {
      return isa<T>(ptr.get());
    }
    
    template<typename T, typename U>
    T* dyn_cast(U* ptr) {
      return isa<T>(ptr) ? value_cast<T>(ptr) : NULL;
    }
    
    template<typename T, typename U>
    const T* dyn_cast(const U* ptr) {
      return isa<T>(ptr) ? value_cast<const T>(ptr) : NULL;
    }
    
    template<typename T, typename U>
    ValuePtr<T> dyn_cast(const ValuePtr<U>& ptr) {
      return ValuePtr<T>(isa<T>(ptr) ? value_cast<T>(ptr.get()) : NULL);
    }

    /**
     * Class used to construct common data for functional operations
     * and instructions.
     */
    class OperationSetup {
      const char *m_operation;
      
    public:
      explicit OperationSetup(const char *operation) : m_operation(operation) {}
      const char *operation() const {return m_operation;}
    };
    
    template<typename T> OperationSetup operation_setup() {return OperationSetup(T::operation);}

    class RewriteCallback {
      Context *m_context;
    public:
      RewriteCallback(Context& context) : m_context(&context) {}
      /// \brief Get the context to create rewritten terms in.
      Context& context() {return *m_context;}
      virtual ValuePtr<> rewrite(const ValuePtr<>& value) = 0;
    };    

    class HashableValue : public Value {
      friend class Context;
      friend std::size_t Value::hash_value() const;

    protected:
      HashableValue(Context& context, TermType term_type, const SourceLocation& location);
      virtual ~HashableValue();

    public:
      HashableValue(const HashableValue& src);
      
      static bool isa_impl(const Value& v) {
        return (v.term_type() == term_functional) || (v.term_type() == term_function_type)
          || (v.term_type() == term_apply);
      }
      
      template<typename V> static void visit(V& v) {visit_base<Value>(v);}

      /**
       * \brief Build a copy of this term with a new set of parameters.
       * 
       * Note that the RecursiveType in ApplyTerm will not be rewritten,
       * since it is not a value in the usual sense.
       * 
       * \param context Context to create the new term in. This may be
       * different to the current context of this term.
       * 
       * \param callback Callback used to rewrite members.
       */
      virtual ValuePtr<HashableValue> rewrite(RewriteCallback& callback) const = 0;

      const char *operation_name() const {return m_operation;}

      template<typename T>
      static void hashable_check_source(T& obj, CheckSourceParameter& parameter);
      
    private:
      typedef boost::intrusive::unordered_set_member_hook<> TermSetHook;
      TermSetHook m_hashable_set_hook;
      std::size_t m_hash;
      const char *m_operation;

      virtual ValuePtr<> check_type() const = 0;
      virtual bool equals_impl(const HashableValue& rhs) const = 0;
      virtual std::pair<const char*,std::size_t> hash_impl() const = 0;
      virtual HashableValue* clone() const = 0;
    };

#define PSI_TVM_HASHABLE_DECL(Type) \
    PSI_TVM_VALUE_DECL(Type) \
  private: \
    virtual ValuePtr<> check_type() const; \
  public: \
    static const char operation[]; \
    virtual ValuePtr<HashableValue> rewrite(RewriteCallback& callback) const; \
    virtual Value* disassembler_source(); \
    template<typename V> static void visit(V& v); \
  private: \
    virtual void check_source_hook(CheckSourceParameter& parameter); \
    virtual bool equals_impl(const HashableValue& rhs) const; \
    virtual std::pair<const char*, std::size_t> hash_impl() const; \
    virtual HashableValue* clone() const;

#define PSI_TVM_HASHABLE_IMPL(Type,Base,Name) \
    PSI_TVM_VALUE_IMPL(Type,Base) \
    \
    const char Type::operation[] = #Name; \
    \
    HashableValue* Type::clone() const { \
      return ::new Type(*this); \
    } \
    \
    ValuePtr<HashableValue> Type::rewrite(RewriteCallback& callback) const { \
      Type copy(*this); \
      boost::array<Type*,1> c = {{&copy}}; \
      RewriteVisitor v(&callback); \
      visit_members(v, c); \
      return callback.context().get_functional(copy); \
    } \
    \
    bool Type::equals_impl(const HashableValue& rhs) const { \
      EqualsVisitor<Type> v(this, &checked_cast<const Type&>(rhs)); \
      visit(v); \
      return v.is_equal(); \
    } \
    \
    std::pair<const char*, std::size_t> Type::hash_impl() const { \
      HashVisitor<Type> v(Type::operation, this); \
      visit(v); \
      return std::make_pair(Type::operation, v.hash()); \
    } \
    \
    Value* Type::disassembler_source() { \
      DisassemblerSourceVisitor v; \
      boost::array<Type*,1> c = {{this}}; \
      visit_members(v, c); \
      return v.source(); \
    } \
    \
    void Type::check_source_hook(CheckSourceParameter& parameter) { \
      hashable_check_source(*this, parameter); \
    }
    
    /**
     * \brief Base class for globals: these are GlobalVariableTerm and FunctionTerm.
     */
    class Global : public Value {
      friend class GlobalVariable;
      friend class Function;
      friend class Module;

    public:
      ValuePtr<> value_type() const;
      /// \brief Get the module this global belongs to.
      Module* module() const {return m_module;}
      /// \brief Get the name of this global within the module.
      const std::string& name() const {return m_name;}
      
      /// \brief Get the minumum alignment of this symbol
      const ValuePtr<>& alignment() const {return m_alignment;}
      /// \brief Set the minimum alignment of this symbol.
      void set_alignment(const ValuePtr<>& alignment) {m_alignment = alignment;}
      
      /// \brief Whether this variable should not be visible outside of the module in which it is defined.
      bool private_() const {return m_private;}
      void set_private(bool b) {m_private = b;}
      
      virtual Value* disassembler_source();

      static bool isa_impl(const Value& x) {
        return (x.term_type() == term_global_variable) ||
          (x.term_type() == term_function);
      }

      template<typename V>
      static void visit(V& v) {
        visit_base<Value>(v);
        v("name", &Global::m_name)
        ("alignment", &Global::m_alignment);
      }

    private:
      Global(Context& context, TermType term_type, const ValuePtr<>& type, const std::string& name, Module *module, const SourceLocation& location);
      boost::intrusive::unordered_set_member_hook<> m_module_member_hook;
      std::string m_name;
      Module *m_module;
      ValuePtr<> m_alignment;
      bool m_private;
      
      virtual void check_source_hook(CheckSourceParameter& parameter);
    };

    /**
     * \brief Global variable.
     */
    class GlobalVariable : public Global {
      PSI_TVM_VALUE_DECL(GlobalVariable);
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
      
      /// \brief Whether this variable can be merged with others which have the same value.
      bool merge() const {return m_merge;}
      void set_merge(bool m) {m_merge = m;}
      
      template<typename V> static void visit(V& v);
      static bool isa_impl(const Value& v) {return v.term_type() == term_global_variable;}
      
    private:
      GlobalVariable(Context& context, const ValuePtr<>& type, const std::string& name, Module *module, const SourceLocation& location);

      bool m_constant;
      bool m_merge;
      ValuePtr<> m_value;
    };

    class FunctionalValue;
    class FunctionType;
    class Exists;
    class ParameterPlaceholder;
    class Function;
    class ApplyType;
    class RecursiveType;
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
      
      typedef std::map<std::string, ValuePtr<Global> > ModuleMemberList;
      typedef std::vector<std::pair<ValuePtr<Function>, unsigned> > ConstructorList;
                                              
    private:
      Context *m_context;
      SourceLocation m_location;
      std::string m_name;
      ModuleMemberList m_members;
      ConstructorList m_constructors, m_destructors;
      
      void add_member(const ValuePtr<Global>& global);
      
    public:
      Module(Context*, const std::string& name, const SourceLocation& location);
      ~Module();
      
      /// \brief Get the context this module belongs to.
      Context& context() {return *m_context;}
      /// \brief Get the location this module originated from.
      const SourceLocation& location() const {return m_location;}
      /// \brief Get the map of members of this module
      ModuleMemberList& members() {return m_members;}
      /// \brief Get the name of this module
      const std::string& name() {return m_name;}
      /// \brief List of constructor functions
      ConstructorList& constructors() {return m_constructors;}
      /// \brief List of destructor functions
      ConstructorList& destructors() {return m_destructors;}
      
#ifdef PSI_DEBUG
      void dump();
#endif
      
      ValuePtr<Global> get_member(const std::string& name);
      ValuePtr<Global> new_member(const std::string& name, const ValuePtr<>& type, const SourceLocation& location);
      ValuePtr<GlobalVariable> new_global_variable(const std::string& name, const ValuePtr<>& type, const SourceLocation& location);
      ValuePtr<GlobalVariable> new_global_variable_set(const std::string&, const ValuePtr<>& value, const SourceLocation& location);
      ValuePtr<Function> new_function(const std::string& name, const ValuePtr<FunctionType>& type, const SourceLocation& location);
      ValuePtr<Function> new_constructor(const std::string& name, const SourceLocation& location);
    };

    /**
     * \brief Tvm context class.
     * 
     * Manages memory for terms, and ensures that equivalent terms are
     * not duplicated.
     */
    class Context {
      friend class Value;
      friend class HashableValue;
      friend class Function;
      friend class Block;
      friend class Module;

      struct ValueDisposer;
      struct HashableSetupEquals;
      struct HashableValueHasher {std::size_t operator () (const HashableValue& h) const {return h.m_hash;}};

      typedef boost::intrusive::unordered_set<HashableValue,
                                              boost::intrusive::member_hook<HashableValue, boost::intrusive::unordered_set_member_hook<>, &HashableValue::m_hashable_set_hook>,
                                              boost::intrusive::hash<HashableValueHasher>,
                                              boost::intrusive::power_2_buckets<true> > HashTermSetType;

      typedef boost::intrusive::list<Value,
                                     boost::intrusive::constant_time_size<false>,
                                     boost::intrusive::member_hook<Value, boost::intrusive::list_member_hook<>, &Value::m_value_list_hook> > TermListType;

      static const std::size_t initial_hash_term_buckets = 64;
      UniqueArray<HashTermSetType::bucket_type> m_hash_term_buckets;
      HashTermSetType m_hash_value_set;

      TermListType m_value_list;

#ifdef PSI_DEBUG
      void dump_hash_terms();
#endif

    public:
      Context();
      ~Context();

      /**
       * \brief Get a pointer to a functional term.
       * 
       * Casts the result back to the incoming type.
       */
      template<typename T>
      ValuePtr<T> get_functional(const T& value) {
        return value_cast<T>(get_hash_term(value));
      }

      /**
       * \brief Get a pointer to a functional term given that term's value.
       */
      ValuePtr<HashableValue> get_hash_term(const HashableValue& value);
      
      ValuePtr<FunctionType> get_function_type(CallingConvention calling_convention,
                                               const ValuePtr<>& result,
                                               const std::vector<ValuePtr<ParameterPlaceholder> >& parameters,
                                               unsigned n_phantom,
                                               bool sret,
                                               const SourceLocation& location);

      ValuePtr<Exists> get_exists(const ValuePtr<>& result,
                                  const std::vector<ValuePtr<ParameterPlaceholder> >& parameters,
                                  const SourceLocation& location);

      ValuePtr<ParameterPlaceholder> new_placeholder_parameter(const ValuePtr<>& type, const SourceLocation& location);

      ValuePtr<ApplyType> apply_recursive(const ValuePtr<RecursiveType>& recursive,
                                           const std::vector<ValuePtr<> >& parameters,
                                           const SourceLocation& location);

    private:
      Context(const Context&);
    };

    bool term_unique(const ValuePtr<>& term);
    void print_module(std::ostream&, Module*);
    void print_term(std::ostream&, const ValuePtr<>&);

    class RewriteVisitor : public ValuePtrVisitorBase<RewriteVisitor> {
      RewriteCallback *m_callback;
    public:
      RewriteVisitor(RewriteCallback *callback) : m_callback(callback) {}
      void visit_ptr(ValuePtr<>& ptr) {ptr = m_callback->rewrite(ptr);}
      void visit_ptr(ValuePtr<RecursiveType>&) {}
      template<typename T> bool do_visit_base(VisitorTag<T>) {return !boost::is_same<T,HashableValue>::value;}
    };
    
    template<typename T>
    class EqualsVisitor {
      bool m_is_equal;
      const T *m_first, *m_second;
      
    public:
      EqualsVisitor(const T *first, const T *second) : m_is_equal(true), m_first(first), m_second(second) {}
      bool is_equal() const {return m_is_equal;}
      
      template<typename U>
      EqualsVisitor<T>& operator () (const char*, U T::*ptr) {
        if (m_is_equal)
          m_is_equal = (m_first->*ptr == m_second->*ptr);
        return *this;
      }
      
      friend void visit_base_hook(EqualsVisitor<T>&, VisitorTag<HashableValue>) {}
      
      template<typename Base>
      friend void visit_base_hook(EqualsVisitor<T>& v, VisitorTag<Base>) {
        if (v.m_is_equal) {
          EqualsVisitor<Base> nv(v.m_first, v.m_second);
          Base::visit(nv);
          if (!nv.is_equal())
            v.m_is_equal = false;
        }
      }
    };
    
    using boost::hash_value;

#if BOOST_VERSION < 105000
    template<typename T, std::size_t N>
    std::size_t hash_value(const boost::array<T,N>& x) {
      std::size_t h = 0;
      for (unsigned i = 0; i != N; ++i) {
        std::size_t c = hash_value(x[i]);
        boost::hash_combine(h,c);
      }
      return h;
    }
#endif
    
    template<typename T>
    class HashVisitor {
      std::size_t m_hash;
      const T *m_ptr;
      
    public:
      HashVisitor(const char *operation, const T *ptr) : m_hash(boost::hash_value(operation)), m_ptr(ptr) {}
      std::size_t hash() const {return m_hash;}
      
      template<typename Base, typename U>
      HashVisitor<T>& operator () (const char*, U Base::*ptr) {
        std::size_t c = hash_value(m_ptr->*ptr);
        boost::hash_combine(m_hash, c);
        return *this;
      }
      
      friend void visit_base_hook(HashVisitor<T>&, VisitorTag<HashableValue>) {}
      
      template<typename Base>
      friend void visit_base_hook(HashVisitor<T>& v, VisitorTag<Base>) {
        Base::visit(v);
      }
    };
    
    Value* disassembler_merge_source(Value *lhs, Value *rhs);
    
    class DisassemblerSourceVisitor : public ValuePtrVisitorBase<DisassemblerSourceVisitor> {
      Value *m_source;
      
    public:
      DisassemblerSourceVisitor() : m_source(NULL) {}
      
      void visit_ptr(const ValuePtr<>& ptr) {
        if (ptr)
          m_source = disassembler_merge_source(m_source, ptr->disassembler_source());
      }
      
      /// Ignore recursive type reference in ApplyTerm.
      void visit_ptr(const ValuePtr<RecursiveType>&) {}
      
      Value *source() const {return m_source;}
      template<typename T> bool do_visit_base(VisitorTag<T>) const {return !boost::is_same<T,HashableValue>::value;}
    };

    class CheckSourceVisitor : public ValuePtrVisitorBase<CheckSourceVisitor> {
      CheckSourceParameter *m_parameter;
      
    public:
      CheckSourceVisitor(CheckSourceParameter *parameter) : m_parameter(parameter) {}
      
      template<typename T>
      void visit_ptr(const ValuePtr<T>& ptr) {
        if (ptr)
          ptr->check_source(*m_parameter);
      }
      
      template<typename T> bool do_visit_base(VisitorTag<T>) const {return !boost::is_same<T,HashableValue>::value;}
    };

    template<typename T>
    void HashableValue::hashable_check_source(T& obj, CheckSourceParameter& parameter) {
      CheckSourceVisitor v(&parameter);
      boost::array<const T*,1> c = {{&obj}};
      visit_members(v, c);
    }
  }
}

#endif
