#include "LLVMBuilder.hpp"
#include "Core.hpp"

#include <llvm/LLVMContext.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Module.h>
#include <llvm/Support/IRBuilder.h>

namespace Psi {
  namespace Tvm {
    bool LLVMBuilderValue::is_unknown_value(const llvm::Value *v) {
      return (v != NULL) &&
	v->getType()->isPointerTy() &&
	llvm::cast<llvm::PointerType>(v->getType())->getElementType()->isIntegerTy(8);
    }

    bool LLVMBuilderValue::is_global_value(const llvm::Value *v) {
      return (v != NULL) && llvm::isa<llvm::Constant>(v);
    }

    LLVMBuilder::LLVMBuilder()
      : m_context(new llvm::LLVMContext),
	m_module(new llvm::Module("", *m_context)),
	m_irbuilder(new IRBuilder(*m_context)) {
    }

    LLVMBuilder::~LLVMBuilder() {
    }

    LLVMBuilderValue LLVMBuilder::value(Term *term) {
      ValueMap::iterator it = m_value_map.find(term);
      if (it != m_value_map.end())
	return it->second;

      LLVMBuilderValue value = term->build_llvm_value(*this);
      m_value_map.insert(ValueMap::value_type(term, value));
      return value;
    }

    LLVMBuilderType LLVMBuilder::type(Term *term) {
      TypeMap::iterator it = m_type_map.find(term);
      if (it != m_type_map.end())
	return it->second;

      LLVMBuilderType type = term->build_llvm_type(*this);
      m_type_map.insert(TypeMap::value_type(term, type));
      return type;
    }
  }
}
