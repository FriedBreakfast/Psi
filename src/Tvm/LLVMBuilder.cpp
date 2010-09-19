#include "LLVMBuilder.hpp"
#include "Core.hpp"

#include <stdexcept>

#include <llvm/LLVMContext.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Module.h>
#include <llvm/Support/IRBuilder.h>

namespace Psi {
  namespace Tvm {
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
	  switch(term->term_type()) {
	  case term_recursive_parameter: {
	    PSI_FAIL("not implemented");
	  }

	  case term_global_variable:
	  case term_function:
	    return LLVMConstantBuilder::type_known(const_cast<llvm::PointerType*>(llvm::Type::getInt8PtrTy(self->context())));

	  case term_metatype:
	    return self->metatype_type();

	  case term_functional: {
	    FunctionalTerm *cast_term = boost::polymorphic_downcast<FunctionalTerm*>(term);
	    return cast_term->backend().llvm_type(*self, cast_term);
	  }

	  default:
	    /**
	     * term_function_type should not occur because only
	     * function pointers are valid variables.
	     *
	     * term_recursive should only occur inside term_apply.
	     */
	    PSI_FAIL("unexpected type term type");
	  }
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
	  switch(term->term_type()) {
	  case term_recursive_parameter: {
	    PSI_FAIL("not implemented");
	  }

	  case term_function_type:
	    return self->metatype_value(llvm::Type::getInt8PtrTy(self->context()));

	  case term_functional: {
	    FunctionalTerm *cast_term = boost::polymorphic_downcast<FunctionalTerm*>(term);
	    return cast_term->backend().llvm_value_constant(*self, cast_term);
	  }

	  default:
	    /*
	     * term_function_type_parameter should never be passed
	     * here because function types are defined simply by their
	     * parameters.
	     *
	     * term_metatype does not occur here because metatype has
	     * no type, hence its value does not exist.
	     *
	     * term_recursive needs to be nested inside term_apply.
	     */
	    PSI_FAIL("unexpected constant term type");
	  }
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
	  switch (term->term_type()) {
	  case term_global_variable: {
	    PSI_FAIL("not implemented");
	  }

	  case term_function: {
	    PSI_FAIL("not implemented");
	  }

	  default:
	    PSI_FAIL("unexpected global term type");
	  }
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
      PSI_ASSERT(m_context && m_module);
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
      switch (term->term_type()) {
      case term_function:
      case term_global_variable: {
	llvm::GlobalValue *gv = global(term);

	// Need to force global terms to be of type i8* since they are
	// pointers, but the Global term builder can't do this
	// because it needs to return a llvm::GlobalVariable
	const llvm::Type* i8ptr = llvm::Type::getInt8PtrTy(*m_context);
	return LLVMConstantBuilder::constant_value(llvm::ConstantExpr::getPointerCast(gv, i8ptr));
      }

      default:
	return build_term(term, m_constant_terms, ConstantBuilderCallback(this)).first;
      }
    }

    llvm::GlobalValue* LLVMConstantBuilder::global(Term *term) {
      if ((term->term_type() != term_function) && (term->term_type() != term_global_variable))
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

    LLVMConstantBuilder::Type LLVMConstantBuilder::metatype_type() {
      const llvm::Type* i64 = llvm::Type::getInt64Ty(context());
      return LLVMConstantBuilder::type_known(llvm::StructType::get(context(), i64, i64, NULL));
    }

    LLVMConstantBuilder::Constant LLVMConstantBuilder::metatype_value(const llvm::Type* ty) {
      llvm::Constant* values[2] = {
	llvm::ConstantExpr::getSizeOf(ty),
	llvm::ConstantExpr::getAlignOf(ty)
      };

      return LLVMConstantBuilder::constant_value(llvm::ConstantStruct::get(ty->getContext(), values, 2, false));
    }

    LLVMConstantBuilder::Constant LLVMConstantBuilder::metatype_value_empty() {
      const llvm::Type *i64 = llvm::Type::getInt64Ty(context());
      llvm::Constant* values[2] = {
	llvm::ConstantInt::get(i64, 0),
	llvm::ConstantInt::get(i64, 1)
      };

      return LLVMConstantBuilder::constant_value(llvm::ConstantStruct::get(context(), values, 2, false));
    }

    LLVMConstantBuilder::Constant LLVMConstantBuilder::metatype_value(llvm::Constant *size, llvm::Constant *align) {
      PSI_ASSERT(size->getType()->isIntegerTy(64) && align->getType()->isIntegerTy(64));
      PSI_ASSERT(!llvm::cast<llvm::ConstantInt>(align)->equalsInt(0));
      llvm::Constant* values[2] = {size, align};
      return LLVMConstantBuilder::constant_value(llvm::ConstantStruct::get(context(), values, 2, false));
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

    LLVMFunctionBuilder::Result LLVMFunctionBuilder::metatype_value(LLVMFunctionBuilder& builder, llvm::Value *size, llvm::Value *align) {
      LLVMFunctionBuilder::IRBuilder& irbuilder = builder.irbuilder();
      llvm::LLVMContext& context = builder.context();
      const llvm::Type* i64 = llvm::Type::getInt64Ty(context);
      llvm::Type *mtype = llvm::StructType::get(context, i64, i64, NULL);
      llvm::Value *first = irbuilder.CreateInsertValue(llvm::UndefValue::get(mtype), size, 0);
      llvm::Value *second = irbuilder.CreateInsertValue(first, align, 1);
      return LLVMFunctionBuilder::make_known(second);
    }
  }
}
