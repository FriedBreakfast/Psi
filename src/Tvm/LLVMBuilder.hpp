#ifndef HPP_PSI_TVM_LLVMBUILDER
#define HPP_PSI_TVM_LLVMBUILDER

#include <tr1/unordered_map>

#include "LLVMForward.hpp"
#include "../Utility.hpp"

namespace Psi {
  namespace Tvm {
    class Term;

    class LLVMBuilderValue {
    public:
      enum Category {
	/**
	 * \brief Global constant, which is created as a full LLVM value.
	 */
	global,
	/**
	 * \brief Local value with a type which is well-known to LLVM
	 *
	 * This will be an LLVM value with the appropriate type (as \c
	 * global).
	 */
	local_known,
	/**
	 * \brief Local value with a generic type
	 *
	 * This will be stored as <tt>i8*</tt> in LLVM, and be stack
	 * allocated using the \c alloca instruction.
	 */
	local_unknown,
	/**
	 * \brief A value of "empty" type.
	 *
	 * This has no data, and hence no variable associated with it.
	 */
	empty
      };

      static LLVMBuilderValue empty_value() {
	return LLVMBuilderValue(empty, NULL);
      }

      static LLVMBuilderValue global_value(const llvm::Value *value) {
	PSI_ASSERT(is_global_value(value), "");
	return LLVMBuilderValue(global, value);
      }

      static LLVMBuilderValue known_value(const llvm::Value *value) {
	PSI_ASSERT(value != NULL, "");
	return LLVMBuilderValue(local_known, value);
      }

      static LLVMBuilderValue unknown_value(const llvm::Value *value) {
	PSI_ASSERT(is_unknown_value(value), "");
	return LLVMBuilderValue(local_unknown, value);
      }

      Category category() const {return m_category;}
      const llvm::Value* value() const {return m_value;}

    private:
      LLVMBuilderValue(Category c, const llvm::Value *v) : m_category(c), m_value(v) {}
      static bool is_unknown_value(const llvm::Value *v);
      static bool is_global_value(const llvm::Value *v);

      Category m_category;
      const llvm::Value *m_value;
    };

    class LLVMBuilderType {
    public:
      enum Category {
	/**
	 * \brief Unknown type.
	 *
	 * Not enough information about the type is known at compile
	 * time to produce a full LLVM representation. Such types are
	 * stored as <tt>i8*</tt> to \c alloca memory when loaded onto
	 * the stack. See LLVMValueType::local_unknown.
	 */
	unknown,
	/**
	 * \brief Known type.
	 *
	 * Enough information is known to produce an accurate LLVM
	 * representation. This means the type (or members of the type
	 * if it is an aggregate) is known exactly, except for
	 * pointers which are always <tt>i8*</tt>.
	 */
	known,
	/**
	 * \brief A type with no data.
	 *
	 * LLVM has no representation of such a type, so special
	 * handling is needed.
	 */
	empty
      };

      LLVMBuilderType() : m_category(empty), m_type(NULL) {}

      static LLVMBuilderType empty_type() {return LLVMBuilderType(empty, NULL);}
      static LLVMBuilderType unknown_type() {return LLVMBuilderType(unknown, NULL);}
      static LLVMBuilderType known_type(const llvm::Type *ty) {return LLVMBuilderType(known, ty);}

      Category category() const {return m_category;}
      const llvm::Type* type() const {return m_type;}

    private:
      LLVMBuilderType(Category c, const llvm::Type *ty) : m_category(c), m_type(ty) {}

      Category m_category;
      const llvm::Type *m_type;
    };

    class LLVMBuilder : Noncopyable {
      friend class Context;

    public:
      typedef llvm::IRBuilder<true, llvm::ConstantFolder, llvm::IRBuilderDefaultInserter<true> > IRBuilder;

      LLVMBuilder();
      ~LLVMBuilder();

      /**
       * \brief Get the LLVM value of a term.
       *
       * For most types, the meaning of this is fairly obvious. #Type
       * objects also have a value, which has an LLVM type of <tt>{ i32,
       * i32 }</tt> giving the size and alignment of the type.
       */
      LLVMBuilderValue value(Term *term);

      /**
       * \brief Get the LLVM type of a term.
       *
       * Note that this is <em>not</em> the type of the value returned
       * by value(); rather <tt>value(t)->getType() ==
       * type(t->type())</tt>, so that type() returns the LLVM type of
       * terms whose type is this term.
       */
      LLVMBuilderType type(Term *term);

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

      IRBuilder& irbuilder() {return *m_irbuilder;}

    private:
      typedef std::tr1::unordered_map<Term*, LLVMBuilderValue> ValueMap;
      typedef std::tr1::unordered_map<Term*, LLVMBuilderType> TypeMap;
      ValueMap m_value_map;
      TypeMap m_type_map;
      bool m_global;
      UniquePtr<llvm::LLVMContext> m_context;
      UniquePtr<llvm::Module> m_module;
      UniquePtr<IRBuilder> m_irbuilder;
    };
  }
}

#endif
