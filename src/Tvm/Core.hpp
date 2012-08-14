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

#include "../SourceLocation.hpp"
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
      term_recursive, ///< RecursiveType: \copybrief RecursiveType
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
    class ValuePtrVistorBase {
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
      Value* source() const {return m_source;}

      bool phantom() const;
      bool parameterized() const;

      /// \brief Get the category of this value (whether it is a metatype, type, or value)
      Category category() const {return static_cast<Category>(m_category);}

      /** \brief Get the context this Term comes from. */
      Context& context() const {return *m_context;}

      /** \brief Get the term describing the type of this term. */
      const ValuePtr<>& type() const {return m_type;}
      
      /** \brief Get the location this value originated from */
      const SourceLocation& location() const {return m_location;}
      
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
      Value *m_source;
      SourceLocation m_location;
      boost::intrusive::list_member_hook<> m_value_list_hook;
      
      void destroy();
      virtual void gc_increment() = 0;
      virtual void gc_decrement() = 0;
      virtual void gc_clear() = 0;
      
      friend void intrusive_ptr_add_ref(Value *self) {
        ++self->m_reference_count;
      }
      
      friend void intrusive_ptr_release(Value *self) {
        if (!--self->m_reference_count)
          self->destroy();
      }

    protected:
      Value(Context& context, TermType term_type, const ValuePtr<>& type,
            Value *source, const SourceLocation& location);
      
      void set_type(const ValuePtr<>& type, Value *source);
    };
    
#define PSI_TVM_VALUE_DECL(Type) \
  private: \
    virtual void gc_increment(); \
    virtual void gc_decrement(); \
    virtual void gc_clear();
    
    struct GCIncrementVisitor : ValuePtrVistorBase<GCIncrementVisitor> {
      template<typename T>
      void visit_ptr(const ValuePtr<T>& ptr) {
        ++ptr->m_reference_count;
      }
      
      template<typename T> bool do_visit_base(VisitorTag<T>) {return true;}
    };
    
    struct GCDecerementVisitor : ValuePtrVistorBase<GCDecerementVisitor> {
      template<typename T>
      void visit_ptr(const ValuePtr<T>& ptr) {
        --ptr->m_reference_count;
      }

      template<typename T> bool do_visit_base(VisitorTag<T>) {return true;}
    };
    
    struct GCClearVisitor : ValuePtrVistorBase<GCClearVisitor> {
      template<typename T>
      void visit_ptr(ValuePtr<T>& ptr) {
        ptr.reset();
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
    
    Value* common_source(Value *t1, Value *t2);
    bool source_dominated(Value *dominator, Value *dominated);
    
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
      return !ptr || T::isa_impl(*ptr);
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
       * \param context Context to create the new term in. This may be
       * different to the current context of this term.
       * 
       * \param callback Callback used to rewrite members.
       */
      virtual ValuePtr<HashableValue> rewrite(RewriteCallback& callback) const = 0;

      const char *operation_name() const {return m_operation;}
      
    private:
      typedef boost::intrusive::unordered_set_member_hook<> TermSetHook;
      TermSetHook m_hashable_set_hook;
      std::size_t m_hash;
      const char *m_operation;

      virtual ValuePtr<> check_type() const = 0;
      virtual bool equals_impl(const HashableValue& rhs) const = 0;
      virtual std::pair<const char*,std::size_t> hash_impl() const = 0;
      virtual Value* source_impl() const = 0;
      virtual HashableValue* clone() const = 0;
    };

#define PSI_TVM_HASHABLE_DECL(Type) \
    PSI_TVM_VALUE_DECL(Type) \
  private: \
    virtual ValuePtr<> check_type() const; \
  public: \
    static const char operation[]; \
    virtual ValuePtr<HashableValue> rewrite(RewriteCallback& callback) const; \
    template<typename V> static void visit(V& v); \
  private: \
    virtual bool equals_impl(const HashableValue& rhs) const; \
    virtual std::pair<const char*, std::size_t> hash_impl() const; \
    virtual Value* source_impl() const; \
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
    Value* Type::source_impl() const { \
      SourceVisitor v; \
      boost::array<const Type*,1> c = {{this}}; \
      visit_members(v, c); \
      return v.source(); \
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
      
      template<typename V> static void visit(V& v);
      static bool isa_impl(const Value& v) {return v.term_type() == term_global_variable;}
      
    private:
      GlobalVariable(Context& context, const ValuePtr<>& type, const std::string& name, Module *module, const SourceLocation& location);

      bool m_constant;
      ValuePtr<> m_value;
    };

    class FunctionalValue;
    class FunctionType;
    class FunctionTypeParameter;
    class Function;
    class ApplyValue;
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
                                              
    private:
      Context *m_context;
      SourceLocation m_location;
      std::string m_name;
      ModuleMemberList m_members;
      
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
      
#ifdef PSI_DEBUG
      void dump();
#endif
      
      ValuePtr<Global> get_member(const std::string& name);
      ValuePtr<GlobalVariable> new_global_variable(const std::string& name, const ValuePtr<>& type, const SourceLocation& location);
      ValuePtr<GlobalVariable> new_global_variable_set(const std::string&, const ValuePtr<>& value, const SourceLocation& location);
      ValuePtr<Function> new_function(const std::string& name, const ValuePtr<FunctionType>& type, const SourceLocation& location);
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
                                               const std::vector<ValuePtr<FunctionTypeParameter> >& parameters,
                                               unsigned n_phantom,
                                               const SourceLocation& location);

      ValuePtr<FunctionType> get_function_type_fixed(CallingConvention calling_convention,
                                                     const ValuePtr<>& result,
                                                     const std::vector<ValuePtr<> >& parameter_types,
                                                     const SourceLocation& location);

      ValuePtr<FunctionTypeParameter> new_function_type_parameter(const ValuePtr<>& type, const SourceLocation& location);

      ValuePtr<ApplyValue> apply_recursive(const ValuePtr<RecursiveType>& recursive,
                                           const std::vector<ValuePtr<> >& parameters,
                                           const SourceLocation& location);

      ValuePtr<RecursiveType> new_recursive(const ValuePtr<>& result,
                                            const std::vector<ValuePtr<> >& parameters,
                                            Value *source,
                                            const SourceLocation& location);

      void resolve_recursive(const ValuePtr<RecursiveType>& recursive, const ValuePtr<>& to);

    private:
      Context(const Context&);

      ValuePtr<RecursiveParameter> new_recursive_parameter(const ValuePtr<>& type, const SourceLocation& location);

      class FunctionTypeResolverRewriter;
    };

    bool term_unique(const ValuePtr<>& term);
    void print_module(std::ostream&, Module*);
    void print_term(std::ostream&, const ValuePtr<>&);

    class RewriteVisitor : public ValuePtrVistorBase<RewriteVisitor> {
      RewriteCallback *m_callback;
    public:
      RewriteVisitor(RewriteCallback *callback) : m_callback(callback) {}
      void visit_ptr(ValuePtr<>& ptr) {ptr = m_callback->rewrite(ptr);}
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

    template<typename T, std::size_t N>
    std::size_t hash_value(const boost::array<T,N>& x) {
      std::size_t h = 0;
      for (unsigned i = 0; i != N; ++i) {
        std::size_t c = hash_value(x[i]);
        boost::hash_combine(h,c);
      }
      return h;
    }
    
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

    class SourceVisitor : public ValuePtrVistorBase<SourceVisitor> {
      Value *m_source;
      
    public:
      SourceVisitor() : m_source(NULL) {}
      Value *source() const {return m_source;}
      
      void visit_ptr(const ValuePtr<>& ptr) {
        if (ptr)
          m_source = common_source(m_source, ptr->source());
      }

      template<typename T> bool do_visit_base(VisitorTag<T>) const {return !boost::is_same<T,HashableValue>::value;}
    };
  }
}

#endif
