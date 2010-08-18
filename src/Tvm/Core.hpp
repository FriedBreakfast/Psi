#ifndef HPP_PSI_TVM_CORE
#define HPP_PSI_TVM_CORE

#include <tr1/cstdint>

#include "../Container.hpp"
#include "User.hpp"
#include "LLVMBuilder.hpp"

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
      PSI_CONTEXT_TYPE(Type,empty)
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
	   bool constant,
	   bool global);

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

      /**
       * Whether or not this term is a global. This is distinct from a
       * constant because a parameterized type (with a local applied
       * parameter) which has a known size and alignment will be
       * constant, but not global.
       */
      bool global() const {return m_global;}

    private:
      bool m_constant;
      bool m_global;

      friend class Context;
      friend class LLVMBuilder;

      virtual LLVMBuilderValue build_llvm_value(LLVMBuilder& builder) = 0;
      virtual LLVMBuilderType build_llvm_type(LLVMBuilder& builder) = 0;
    };

    /**
     * \brief The type of a term.
     *
     * This is distinct from #Type because #Type is the type of a
     * #Value, whereas #TermType may be the type of a #Value or a #Type.
     */
    class TermType : public Term {
    protected:
      TermType(const UserInitializer&, Context *context, bool constant, bool global);

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
      static LLVMBuilderValue llvm_value(const llvm::Type* ty);

      /**
       * \brief Get an LLVM value for Metatype for an empty type.
       */
      static LLVMBuilderValue llvm_value_empty(llvm::LLVMContext& context);

      /**
       * \brief Get an LLVM value for a specified size and alignment.
       *
       * The result of this call will be a global constant.
       */
      static LLVMBuilderValue llvm_value_global(llvm::Constant *size, llvm::Constant *align);

      /**
       * \brief Get an LLVM value for a specified size and alignment.
       *
       * The result of this call will be a local value.
       */
      static LLVMBuilderValue llvm_value_local(LLVMBuilder& builder, llvm::Value *size, llvm::Value *align);

    private:
      struct Initializer;
      static Metatype* create(Context *context);
      Metatype(const UserInitializer&, Context *context);

      virtual LLVMBuilderType build_llvm_type(LLVMBuilder&);
      virtual LLVMBuilderValue build_llvm_value(LLVMBuilder&);
    };

    /**
     * \brief The type of a #Value term.
     */
    class Type : public TermType {
    protected:
      Type(const UserInitializer&, Context*, bool constant, bool global);

    public:
      Metatype* type() const {return use_get<Metatype>(slot_type);}
    };

    class AppliedType;

    /**
     * \brief The type of values in Psi.
     */
    class Value : public Term {
    protected:
      Value(const UserInitializer& ui, Context *context, Type *type, bool constant, bool global);

    private:
      virtual LLVMBuilderType build_llvm_type(LLVMBuilder& builder);

    public:
      Type* type() const {return use_get<Type>(slot_type);}

    protected:
      AppliedType* applied_type();
    };
  }
}

#endif
