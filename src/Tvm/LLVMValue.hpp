#ifndef HPP_PSI_TVM_LLVMVALUE
#define HPP_PSI_TVM_LLVMVALUE

#include <llvm/Value.h>

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
