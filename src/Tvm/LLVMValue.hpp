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
  class Value;
  class Constant;
  class Twine;
  class Module;
  class Type;
  class IntegerType;
  class TargetMachine;
  class TargetData;
  class BasicBlock;
  class Instruction;
  class APFloat;
  class APInt;
  class fltSemantics;
  class ExecutionEngine;
  class GlobalValue;
  class GlobalVariable;
  class Function;
  class JITEventListener;
  class raw_ostream;
  class CallInst;
  class StringRef;

  class ConstantFolder;
  template<bool> class IRBuilderDefaultInserter;
  template<bool,typename,typename> class IRBuilder;
}

namespace Psi {
  namespace Tvm {
    class LLVMValue {
      enum Category {
	category_invalid,
	category_known,
	category_unknown
      };

    public:
      LLVMValue() : m_category(category_invalid), m_value(0) {}

      bool is_valid() const {return m_category != category_invalid;}
      bool is_known() const {return m_category == category_known;}
      bool is_unknown() const {return m_category == category_unknown;}
      llvm::Value *known_value() const {PSI_ASSERT(is_known()); return m_value;}
      llvm::Value *unknown_value() const {PSI_ASSERT(is_unknown()); return m_value;}

      static LLVMValue known(llvm::Value *value) {return LLVMValue(category_known, value);}
      static LLVMValue unknown(llvm::Value *value) {return LLVMValue(category_unknown, value);}

    private:
      LLVMValue(Category category, llvm::Value *value) : m_category(category), m_value(value) {}

      Category m_category;
      llvm::Value *m_value;
    };
  }
}

#endif
