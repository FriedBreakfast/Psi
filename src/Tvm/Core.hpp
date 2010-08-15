#ifndef HPP_PSI_TVM_CORE
#define HPP_PSI_TVM_CORE

#include "../Container.hpp"
#include "User.hpp"
#include "LLVMForward.hpp"

#include <tr1/cstdint>
#include <tr1/unordered_map>

namespace Psi {
  namespace Tvm {
    class Type;
    class TemplateType;
    class Term;
    class IntegerType;
    class RealType;
    class TermType;
    class Metatype;
    class Instruction;
    class InstructionValue;
    class PointerType;
    class Context;

    class LLVMBuilder : Noncopyable {
      friend class Context;

    public:
      LLVMBuilder();
      ~LLVMBuilder();

      /**
       * \brief Get the LLVM value of a term.
       *
       * For most types, the meaning of this is fairly obvious. #Type
       * objects also have a value, which has an LLVM type of <tt>{ i32,
       * i32 }</tt> giving the size and alignment of the type.
       */
      const llvm::Value* value(Term *term);

      /**
       * \brief Get the LLVM type of a term.
       *
       * Note that this is <em>not</em> the type of the value returned
       * by value(); rather <tt>value(t)->getType() ==
       * type(t->type())</tt>, so that type() returns the LLVM type of
       * terms whose type is this term.
       */
      const llvm::Type* type(Term *term);

      /**
       * \brief Get the LLVM context owned by this builder.
       */
      llvm::LLVMContext& context() {return *m_context;}

      /**
       * \brief Get the current module being used for compilation.
       */
      llvm::Module& module() {return *m_module;}

      /**
       * \brief Whether we are currently compiling at global
       * (constant) scope or not.
       */
      bool global() {return m_global;}

      typedef llvm::IRBuilder<true, llvm::ConstantFolder, llvm::IRBuilderDefaultInserter<true> > IRBuilder;

    private:
      typedef std::tr1::unordered_map<Term*, const llvm::Value*> ValueMap;
      typedef std::tr1::unordered_map<Term*, const llvm::Type*> TypeMap;
      ValueMap m_value_map;
      TypeMap m_type_map;
      bool m_global;
      UniquePtr<llvm::LLVMContext> m_context;
      UniquePtr<llvm::Module> m_module;
    };

    /**
     * \brief Base class of all objects managed by #Context.
     */
    class ContextObject : public Used, public User, public IntrusiveListNode<ContextObject> {
    public:
      friend class Context;
      virtual ~ContextObject();

      /**
       * \brief Get the context this object comes from.
       */
      Context *context() const {
	return m_context;
      }

    protected:
      ContextObject(const UserInitializer& ui, Context *context);

    private:
      Context *m_context;
    };

    class Context : Noncopyable {
      friend class ContextObject;
      friend class Instruction;

    public:
      Context();
      ~Context();

#define PSI_CONTEXT_TYPE(type,name)			\
      type* type_##name() {return m_type_##name;}	\
    private: type* m_type_##name; public:

      /**
       * \name Types
       *
       * Predefined types in this context.
       */
      //@{
      PSI_CONTEXT_TYPE(Type,void)
      PSI_CONTEXT_TYPE(IntegerType,int8)
      PSI_CONTEXT_TYPE(IntegerType,int16)
      PSI_CONTEXT_TYPE(IntegerType,int32)
      PSI_CONTEXT_TYPE(IntegerType,int64)
      PSI_CONTEXT_TYPE(IntegerType,uint8)
      PSI_CONTEXT_TYPE(IntegerType,uint16)
      PSI_CONTEXT_TYPE(IntegerType,uint32)
      PSI_CONTEXT_TYPE(IntegerType,uint64)
      PSI_CONTEXT_TYPE(RealType,real32)
      PSI_CONTEXT_TYPE(RealType,real64)
      PSI_CONTEXT_TYPE(RealType,real128)
      PSI_CONTEXT_TYPE(Type,label)
      PSI_CONTEXT_TYPE(PointerType,pointer)
      //@}

#undef PSI_CONTEXT_TYPE

      /**
       * \brief Return the unique Metatype object for this context.
       */
      Metatype *metatype() {return m_metatype;}

      /**
       * \brief Allocate and initializer a descendent of #User.
       *
       * \param initializer Initializer object. This must conform to:
       *
       * \verbatim
       * struct T {
       *   typedef ?? ResultType;
       *   std::size_t slots() const; //< Return the number of Use slots to be allocated
       *   std::size_t size() const;  //< Memory to be allocated for base object, usually sizeof(ResultType)
       *   ResultType* operator() (void*, const UserInitializer&, Context*) const; //< Construct the object
       * };
       * \endverbatim
       */
      template<typename T>
      typename T::ResultType* new_user(const T& initializer) {
	std::size_t slots = initializer.slots();
	std::pair<void*, UserInitializer> p = allocate_user(initializer.size(), slots);
	try {
	  return initializer(p.first, p.second, this);
	} catch (...) {
	  operator delete (p.first);
	  throw;
	}
      }

      /**
       * \brief Just-in-time compile a term, and a get a pointer to
       * the result.
       */
      void* term_jit(Term *term);

    private:
      void init();
      void init_types();

      /**
       * \brief Allocate memory for an object with an array of #Use
       * objects.
       *
       * These should normally be descendents of #User.
       */
      std::pair<void*, UserInitializer> allocate_user(std::size_t obj_size,
						      std::size_t n_uses);
      Metatype *m_metatype;
      IntrusiveList<ContextObject> m_gc_objects;

      LLVMBuilder m_builder;
      UniquePtr<llvm::ExecutionEngine> m_llvm_engine;
    };

    template<typename T, std::size_t NumSlots=-1>
    struct InitializerBase {
      typedef T ResultType;
      std::size_t size() const {
	return sizeof(T);
      }

      std::size_t slots() const {
	return NumSlots;
      }
    };

    template<typename T>
    struct InitializerBase<T, -1> {
      typedef T ResultType;
      std::size_t size() const {
	return sizeof(T);
      }
    };

    /**
     * \brief The root type of all terms in Psi.
     *
     * This allows for dependent types since both types and values
     * derive from #Term and so can be used as parameters to other
     * types.
     */
    class Term : public ContextObject {
    protected:
      enum Slots {
	slot_type = 0,
	slot_max
      };

      Term(const UserInitializer& ui,
	   Context *context,
	   TermType *type,
	   bool constant);

    public:
      /**
       * \brief Get the type of this term.
       *
       * The type of all terms derives from #Type.
       */
      TermType* type() const;

      /**
       * Whether or not this term is a constant.
       */
      bool constant() const {return m_constant;}

    private:
      bool m_constant;

      friend class Context;
      friend class LLVMBuilder;

      virtual const llvm::Value* build_llvm_value(LLVMBuilder& builder) = 0;
      virtual const llvm::Type* build_llvm_type(LLVMBuilder& builder) = 0;
    };

    /**
     * \brief The type of a term.
     *
     * This is distinct from #Type because #Type is the type of a
     * #Value, whereas #TermType may be the type of a #Value or a #Type.
     */
    class TermType : public Term {
    protected:
      TermType(const UserInitializer&, Context *context, bool constant);

    private:
      friend class Metatype;

      /**
       * Special constructor for use by #Metatype, since all #TermType
       * objects except #Metatype have type #Metatype.
       */
      TermType(const UserInitializer&, Context *context, Metatype*);
    };

    inline TermType* Term::type() const {
      return use_get<TermType>(slot_type);
    }

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
    class Metatype : public TermType {
      friend class Context;

    public:
      /**
       * \brief Get an LLVM value for Metatype for the given LLVM type.
       */
      static const llvm::Value* llvm_value(llvm::LLVMContext& context, const llvm::Type* ty);

    private:
      struct Initializer;
      static Metatype* create(Context *context);
      Metatype(const UserInitializer&, Context *context);

      virtual const llvm::Type* build_llvm_type(LLVMBuilder&);
      virtual const llvm::Value* build_llvm_value(LLVMBuilder&);
    };

    /**
     * \brief The type of a #Value term.
     */
    class Type : public TermType {
    protected:
      Type(const UserInitializer&, Context*, bool constant);

    public:
      Metatype* type() const {return use_get<Metatype>(slot_type);}
    };

    class AppliedType;

    /**
     * \brief The type of values in Psi.
     */
    class Value : public Term {
    protected:
      Value(const UserInitializer& ui, Context *context, Type *type, bool constant);

    private:
      virtual const llvm::Type* build_llvm_type(LLVMBuilder& builder);

    public:
      Type* type() const {return use_get<Type>(slot_type);}

    protected:
      AppliedType* applied_type();
    };
  }
}

#endif
