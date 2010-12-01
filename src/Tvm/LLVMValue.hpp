#ifndef HPP_PSI_TVM_LLVMVALUE
#define HPP_PSI_TVM_LLVMVALUE

/*
 * LLVM forward declarations
 *
 * This file contains forward declarations for LLVM. This avoids
 * external programs having to indirectly include LLVM header files
 * (and hence use/work around LLVM compilation flags). It's not really
 * a good idea from a proper C++ perspective, but it should make
 * working with Psi as a library easier.
 */

namespace llvm {
  class LLVMContext;
  class Twine;
  class Module;
  class Value;
  class Type;
  class TargetMachine;
  class TargetData;
  class BasicBlock;
  class Instruction;
  class APFloat;
  class APInt;
  class fltSemantics;
  class ExecutionEngine;
  class Constant;
  class GlobalValue;
  class GlobalVariable;
  class Function;
  class JITEventListener;
  class raw_ostream;

  class ConstantFolder;
  template<bool> class IRBuilderDefaultInserter;
  template<bool,typename,typename> class IRBuilder;
}

namespace Psi {
  namespace Tvm {
    class LLVMType {
      enum Category {
	/**
	 * Default constructor marker - means this result object is
	 * not valid.
	 */
	category_invalid,
	/**
	 * \brief Unknown type.
	 *
	 * Not enough information about the type is known at compile
	 * time to produce a full LLVM representation. Such types are
	 * stored as <tt>i8*</tt> to \c alloca memory when loaded onto
	 * the stack. See LLVMValue::category_unknown.
	 */
	category_unknown,
	/**
	 * \brief Known type.
	 *
	 * Enough information is known to produce an accurate LLVM
	 * representation. This means the type (or members of the type
	 * if it is an aggregate) is known exactly, except for
	 * pointers which are always <tt>i8*</tt>.
	 */
	category_known,
	/**
	 * \brief A type with no data.
	 *
	 * LLVM has no representation of such a type, so special
	 * handling is needed.
	 */
	category_empty
      };

    public:
      LLVMType() : m_category(category_invalid), m_type(0) {}
      bool is_valid() const {return m_category != category_invalid;}
      bool is_empty() const {return m_category == category_empty;}
      bool is_known() const {return m_category == category_known;}
      bool is_unknown() const {return m_category == category_unknown;}
      llvm::Type *type() const {return m_type;}

      static LLVMType known(llvm::Type *ty) {
	return LLVMType(category_known, ty);
      }

      static LLVMType unknown() {
	return LLVMType(category_unknown, 0);
      }

      static LLVMType empty() {
	return LLVMType(category_empty, 0);
      }

    private:
      LLVMType(Category category, llvm::Type *type) : m_category(category), m_type(type) {}
      Category m_category;
      llvm::Type *m_type;
    };

    class LLVMValue {
      enum Category {
	category_invalid,
	category_known,
	category_unknown,
	category_empty,
	/**
	 * \brief An unknown value.
	 *
	 * This occurs when a variable is existentially quantified -
	 * it has no value available, merely an assertion that such a
	 * value exists somewhere.
	 */
	category_phantom
      };

    public:
      LLVMValue() : m_category(category_invalid), m_value(0) {}

      bool is_valid() const {return m_category != category_invalid;}
      bool is_known() const {return m_category == category_known;}
      bool is_unknown() const {return m_category == category_unknown;}
      bool is_empty() const {return m_category == category_empty;}
      bool is_phantom() const {return m_category == category_phantom;}
      llvm::Value *value() const {return m_value;}
      llvm::Value *ptr_value() const {return m_ptr_value;}

      static LLVMValue known(llvm::Value *value) {return LLVMValue(category_known, value, 0);}
      static LLVMValue unknown(llvm::Value *value, llvm::Value *ptr_value) {return LLVMValue(category_unknown, value, ptr_value);}
      static LLVMValue empty() {return LLVMValue(category_empty, 0, 0);}
      static LLVMValue phantom() {return LLVMValue(category_phantom, 0, 0);}

    private:
      LLVMValue(Category category, llvm::Value *value, llvm::Value *ptr_value) : m_category(category), m_value(value), m_ptr_value(ptr_value) {}

      Category m_category;
      llvm::Value *m_value;
      llvm::Value *m_ptr_value;
    };
  }
}

#endif
