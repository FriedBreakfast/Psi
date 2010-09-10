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

      static llvm::GlobalValue* build_global(LLVMConstantBuilder& builder, Term *term) {
	Global& global = checked_reference_static_cast<Global>(term->proto());
	return global.llvm_build_global(builder, term);
      }

      static void init_global(LLVMConstantBuilder& builder, Term *term, llvm::GlobalValue *llvm_global) {
	Global& global = checked_reference_static_cast<Global>(term->proto());
	global.llvm_init_global(builder, llvm_global, term);
      }
    };

    namespace {
      template<typename ValueMap, typename Callback>
      std::pair<typename ValueMap::value_type::second_type, bool>
      build_term(Term *term, ValueMap& values, const Callback& cb) {
	std::pair<typename ValueMap::iterator, bool> itp =
	  values.insert(std::make_pair(term, cb.invalid()));
	if (!itp.second) {
	  if (cb.valid(itp.first->second)) {
	    return std::make_pair(itp.first->second, false);
	  } else {
	    throw std::logic_error("Cyclical term found");
	  }
	}

	typename ValueMap::value_type::second_type r = cb.build(term);
	if (cb.valid(r)) {
	  itp.first->second = r;
	} else {
	  values.erase(itp.first);
	  throw std::logic_error("LLVM term building failed");
	}

	return std::make_pair(r, true);
      }

      struct TypeBuilderCallback {
	LLVMConstantBuilder *self;
	TypeBuilderCallback(LLVMConstantBuilder *self_) : self(self_) {}

	LLVMConstantBuilder::Type build(Term *term) const {
	  return LLVMBuilderInvoker::type(*self, term);
	}

	LLVMConstantBuilder::Type invalid() const {
	  return LLVMConstantBuilder::Type();
	}

	bool valid(const LLVMConstantBuilder::Type& t) const {
	  return t.valid();
	}
      };

      struct ConstantBuilderCallback {
	LLVMConstantBuilder *self;
	ConstantBuilderCallback(LLVMConstantBuilder *self_) : self(self_) {}

	LLVMConstantBuilder::Constant build(Term *term) const {
	  return LLVMBuilderInvoker::constant(*self, term);
	}

	LLVMConstantBuilder::Constant invalid() const {
	  return LLVMConstantBuilder::Constant();
	}

	bool valid(const LLVMConstantBuilder::Constant& t) const {
	  return t.valid();
	}
      };

      struct GlobalBuilderCallback {
	LLVMConstantBuilder *self;
	GlobalBuilderCallback(LLVMConstantBuilder *self_) : self(self_) {}

	llvm::GlobalValue* build(Term *term) const {
	  return LLVMBuilderInvoker::build_global(*self, term);
	}

	llvm::GlobalValue* invalid() const {
	  return NULL;
	}

	bool valid(llvm::GlobalValue *p) const {
	  return p;
	}
      };
    }
      
    LLVMConstantBuilder::LLVMConstantBuilder(llvm::LLVMContext *context, llvm::Module *module)
      : m_parent(0), m_context(context), m_module(module) {
      PSI_ASSERT(m_context && m_module, "builder context and module cannot be null");
    }

    LLVMConstantBuilder::LLVMConstantBuilder(const LLVMConstantBuilder *parent)
      : m_parent(parent), m_context(parent->m_context), m_module(parent->m_module) {
    }

    LLVMConstantBuilder::~LLVMConstantBuilder() {
    }

    LLVMConstantBuilder::Type LLVMConstantBuilder::type(Term *term) {
      return build_term(term, m_type_terms, TypeBuilderCallback(this)).first;
    }

    LLVMConstantBuilder::Constant LLVMConstantBuilder::constant(Term *term) {
      if (term->proto().source() == ProtoTerm::term_global) {
	llvm::GlobalValue *gv = global(term);

	// Need to force global terms to be of type i8* since they are
	// pointers, but the Global term builder can't do this
	// because it needs to return a llvm::GlobalVariable
	const llvm::Type* i8ptr = llvm::Type::getInt8PtrTy(*m_context);
	return LLVMConstantBuilder::constant_value(llvm::ConstantExpr::getPointerCast(gv, i8ptr));
      } else {
	return build_term(term, m_constant_terms, ConstantBuilderCallback(this)).first;
      }
    }

    llvm::GlobalValue* LLVMConstantBuilder::global(Term *term) {
      if (term->proto().source() != ProtoTerm::term_global)
	throw std::logic_error("cannot get global value for non-global variable");

      std::pair<llvm::GlobalValue*, bool> gv = build_term(term, m_global_terms, GlobalBuilderCallback(this));

      if (gv.second) {
	if (m_global_build_list.empty()) {
	  m_global_build_list.push_back(std::make_pair(term, gv.first));
	  while (!m_global_build_list.empty()) {
	    const std::pair<Term*, llvm::GlobalValue*>& t = m_global_build_list.front();
	    LLVMBuilderInvoker::init_global(*this, t.first, t.second);
	    m_global_build_list.pop_front();
	  }
	} else {
	  m_global_build_list.push_back(std::make_pair(term, gv.first));
	}
      }

      return gv.first;
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
