#include "LLVMBuilder.hpp"
#include "Core.hpp"

#include <stdexcept>

#include <llvm/LLVMContext.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Module.h>
#include <llvm/Support/IRBuilder.h>

namespace Psi {
  namespace Tvm {
    class LLVMBuilderInvoker {
    public:
      static LLVMConstantBuilder::Type type(LLVMConstantBuilder& builder, Term *term) {
	return term->proto().llvm_type(builder, term);
      }

      static LLVMConstantBuilder::Constant constant(LLVMConstantBuilder& builder, Term *term) {
	return term->proto().llvm_value_constant(builder, term);
      }

      static LLVMFunctionBuilder::Result instruction(LLVMFunctionBuilder& builder, Term *term) {
	return term->proto().llvm_value_instruction(builder, term);
      }
    };

    namespace {
      template<typename ValueMap, typename Callback>
      typename ValueMap::value_type::second_type
      build_term(Term *term, ValueMap& values, const Callback& cb) {
	std::pair<typename ValueMap::iterator, bool> itp =
	  values.insert(std::make_pair(term, typename ValueMap::value_type::second_type()));
	if (!itp.second) {
	  if (itp.first->second.valid()) {
	    return itp.first->second;
	  } else {
	    throw std::logic_error("Cyclical term found");
	  }
	}

	typename ValueMap::value_type::second_type r = cb(term);
	if (r.valid()) {
	  itp.first->second = r;
	} else {
	  values.erase(itp.first);
	  throw std::logic_error("LLVM term building failed");
	}

	return r;
      }

      struct TypeBuilderCallback {
	LLVMConstantBuilder *self;
	TypeBuilderCallback(LLVMConstantBuilder *self_) : self(self_) {}
	LLVMConstantBuilder::Type operator () (Term *term) const {
	  return LLVMBuilderInvoker::type(*self, term);
	}
      };

      struct ConstantBuilderCallback {
	LLVMConstantBuilder *self;
	ConstantBuilderCallback(LLVMConstantBuilder *self_) : self(self_) {}
	LLVMConstantBuilder::Constant operator () (Term *term) const {
	  return LLVMBuilderInvoker::constant(*self, term);
	}
      };
    }
      
    LLVMConstantBuilder::LLVMConstantBuilder(llvm::LLVMContext *context, llvm::Module *module)
      : m_parent(0), m_context(context), m_module(module) {
    }

    LLVMConstantBuilder::LLVMConstantBuilder(const LLVMConstantBuilder *parent)
      : m_parent(parent), m_context(parent->m_context), m_module(parent->m_module) {
    }

    LLVMConstantBuilder::~LLVMConstantBuilder() {
    }

    LLVMConstantBuilder::Type LLVMConstantBuilder::type(Term *term) {
      return build_term(term, m_type_terms, TypeBuilderCallback(this));
    }

    LLVMConstantBuilder::Constant LLVMConstantBuilder::constant(Term *term) {
      return build_term(term, m_constant_terms, ConstantBuilderCallback(this));
    }

    void LLVMConstantBuilder::set_module(llvm::Module *module) {
      m_module = module;
    }

    LLVMFunctionBuilder::LLVMFunctionBuilder(LLVMConstantBuilder *constant_builder, IRBuilder *irbuilder) 
      : m_constant_builder(constant_builder), m_irbuilder(irbuilder) {
    }

    LLVMFunctionBuilder::~LLVMFunctionBuilder() {
    }

    LLVMConstantBuilder::Type LLVMFunctionBuilder::type(Term *term) {
      return m_constant_builder->type(term);
    }

    LLVMFunctionBuilder::Result LLVMFunctionBuilder::value(Term *term) {
      if (TermParameter::global(term->term_context())) {
	return m_constant_builder->constant(term);
      } else {
	TermMap::iterator it = m_terms.find(term);
	if (it != m_terms.end())
	  return it->second;
	else
	  throw std::logic_error("Term not yet available");
      }
    }

    LLVMFunctionBuilder::Result::Result(const LLVMConstantBuilder::Constant& src)
      : m_category(from_const_category(src)), m_value(src.value()) {
    }
    
    LLVMFunctionBuilder::Result::Category LLVMFunctionBuilder::Result::from_const_category(const LLVMConstantBuilder::Constant& r) {
      if (r.known())
	return category_known;
      else if (r.empty())
	return category_empty;
      else
	return category_invalid;
    }
  }
}
