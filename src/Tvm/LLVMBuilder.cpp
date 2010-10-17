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
      build_term(TermRef<> term, ValueMap& values, const Callback& cb) {
	std::pair<typename ValueMap::iterator, bool> itp =
	  values.insert(std::make_pair(term.get(), cb.invalid()));
	if (!itp.second) {
	  if (cb.valid(itp.first->second)) {
	    return std::make_pair(itp.first->second, false);
	  } else {
	    throw std::logic_error("Cyclical term found");
	  }
	}

	typename ValueMap::value_type::second_type r = cb.build(*term);
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

	LLVMConstantBuilder::Type build(Term& term) const {
	  switch(term.term_type()) {
	  case term_recursive_parameter: {
	    PSI_FAIL("not implemented");
	  }

	  case term_global_variable:
	  case term_function:
	    return LLVMConstantBuilder::type_known(const_cast<llvm::PointerType*>(llvm::Type::getInt8PtrTy(self->context())));

	  case term_metatype:
	    return self->metatype_type();

	  case term_functional: {
	    FunctionalTerm& cast_term = checked_cast<FunctionalTerm&>(term);
	    return cast_term.backend()->llvm_type(*self, cast_term);
	  }

	  case term_apply: {
	    TermPtr<> actual = checked_cast<ApplyTerm&>(term).unpack();
	    PSI_ASSERT(actual->term_type() != term_apply);
	    return build(*actual);
	  }

	  default:
	    /**
	     * term_function_type should not occur because only
	     * function pointers are valid variables.
	     *
	     * term_recursive should only occur inside term_apply.
	     *
	     * term_recursive_parameter should never be encountered
	     * since it should be expanded out by ApplyTerm::apply().
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

      template<typename Builder, typename Result, typename FunctionalCallback>
      struct ValueBuilderCallback {
	Builder *self;
	FunctionalCallback functional_callback;
	ValueBuilderCallback(Builder *self_) : self(self_) {}

	Result build(Term& term) const {
	  switch(term.term_type()) {
	  case term_recursive_parameter: {
	    PSI_FAIL("not implemented");
	  }

	  case term_function_type:
	    return self->metatype_value(llvm::Type::getInt8PtrTy(self->context()));

	  case term_functional: {
	    FunctionalTerm& cast_term = checked_cast<FunctionalTerm&>(term);
	    return functional_callback(*self, cast_term);
	  }

	  case term_apply: {
	    TermPtr<> actual = checked_cast<ApplyTerm&>(term).unpack();
	    PSI_ASSERT(actual->term_type() != term_apply);
	    return build(*actual);
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
	     *
	     * term_recursive_parameter should never be encountered
	     * since it should be expanded out by ApplyTerm::apply().
	     */
	    PSI_FAIL("unexpected constant term type");
	  }
	}

	Result invalid() const {
	  return Result();
	}

	bool valid(const Result& t) const {
	  return t.valid();
	}
      };

      struct ConstantFunctionalCallback {
	LLVMConstantBuilder::Constant operator () (LLVMConstantBuilder& self, FunctionalTerm& term) const {
	  return term.backend()->llvm_value_constant(self, term);
	}
      };

      typedef ValueBuilderCallback<LLVMConstantBuilder, LLVMConstantBuilder::Constant, ConstantFunctionalCallback> ConstantBuilderCallback;

      struct InstructionFunctionalCallback {
	LLVMFunctionBuilder::Result operator () (LLVMFunctionBuilder& self, FunctionalTerm& term) const {
	  return term.backend()->llvm_value_instruction(self, term);
	}
      };

      typedef ValueBuilderCallback<LLVMFunctionBuilder, LLVMFunctionBuilder::Result, InstructionFunctionalCallback> InstructionBuilderCallback;

      struct GlobalBuilderCallback {
	LLVMConstantBuilder *self;
	GlobalBuilderCallback(LLVMConstantBuilder *self_) : self(self_) {}

	llvm::GlobalValue* build(Term& term) const {
	  switch (term.term_type()) {
	  case term_global_variable: {
	    GlobalVariableTerm& global = checked_cast<GlobalVariableTerm&>(term);
	    LLVMConstantBuilder::Type ty = self->type(&term);
	    if (ty.known()) {
	      return new llvm::GlobalVariable(self->module(), ty.type(), global.constant(), llvm::GlobalValue::InternalLinkage, NULL, "");
	    } else if (ty.empty()) {
	      const llvm::Type *int8ty = llvm::Type::getInt8Ty(self->context());
	      llvm::Constant *init = llvm::ConstantInt::get(int8ty, 0);
	      return new llvm::GlobalVariable(self->module(), int8ty, true, llvm::GlobalValue::InternalLinkage, init, "");
	    } else {
	      PSI_ASSERT(ty.unknown());
	      throw std::logic_error("global variable has unknown type");
	    }
	  }

	  case term_function: {
	    FunctionTerm& func = checked_cast<FunctionTerm&>(term);
	    const llvm::Type *i8ptr = llvm::Type::getInt8PtrTy(self->context());
	    std::vector<const llvm::Type*> params(func.n_parameters()+1, i8ptr);
	    llvm::FunctionType *llvm_ty = llvm::FunctionType::get(i8ptr, params, false);
	    return llvm::Function::Create(llvm_ty, llvm::GlobalValue::InternalLinkage, "", &self->module());
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

    LLVMConstantBuilder::Type LLVMConstantBuilder::type(TermRef<> term) {
      return build_term(term, m_type_terms, TypeBuilderCallback(this)).first;
    }

    LLVMConstantBuilder::Constant LLVMConstantBuilder::constant(TermRef<> term) {
      switch (term->term_type()) {
      case term_function:
      case term_global_variable: {
	llvm::GlobalValue *gv = global(checked_cast<GlobalTerm*>(term.get()));

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

    llvm::GlobalValue* LLVMConstantBuilder::global(TermRef<GlobalTerm> term) {
      if ((term->term_type() != term_function) && (term->term_type() != term_global_variable))
	throw std::logic_error("cannot get global value for non-global variable");

      std::pair<llvm::GlobalValue*, bool> gv = build_term(term, m_global_terms, GlobalBuilderCallback(this));

      if (gv.second) {
	if (m_global_build_list.empty()) {
	  m_global_build_list.push_back(std::make_pair(term.get(), gv.first));
	  while (!m_global_build_list.empty()) {
	    const std::pair<Term*, llvm::GlobalValue*>& t = m_global_build_list.front();
	    if (t.first->term_type() == term_function) {
	      FunctionTerm *psi_func = checked_cast<FunctionTerm*>(t.first);
	      llvm::Function *llvm_func = llvm::cast<llvm::Function>(t.second);
	      build_function(psi_func, llvm_func);
	      return llvm_func;
	    } else {
	      PSI_ASSERT(t.first->term_type() == term_global_variable);
	      GlobalVariableTerm *psi_var = checked_cast<GlobalVariableTerm*>(t.first);
	      llvm::GlobalVariable *llvm_var = llvm::cast<llvm::GlobalVariable>(t.second);
	      build_global_variable(psi_var, llvm_var);
	      return llvm_var;
	    }
	    m_global_build_list.pop_front();
	  }
	} else {
	  m_global_build_list.push_back(std::make_pair(term.get(), gv.first));
	}
      }

      return gv.first;
    }

    void LLVMConstantBuilder::build_function(FunctionTerm *psi_func, llvm::Function *llvm_func) {
      LLVMFunctionBuilder::IRBuilder irbuilder(context());
      LLVMFunctionBuilder func_builder(this, &irbuilder);

      std::vector<std::pair<BlockTerm*, llvm::BasicBlock*> > blocks;
      blocks.push_back(std::make_pair(psi_func->entry().get(), static_cast<llvm::BasicBlock*>(0)));
      // can't use an iterator since it will be invalidated by adding elements
      for (std::size_t i = 0; i < blocks.size(); ++i) {
	BlockTerm *bl = blocks[i].first;
	for (TermIterator<BlockTerm> it = bl->term_users_begin<BlockTerm>();
	     it != bl->term_users_end<BlockTerm>(); ++it) {
	  if (bl == it->dominator().get())
	    blocks.push_back(std::make_pair(it.get_ptr(), static_cast<llvm::BasicBlock*>(0)));
	}
      }

      for (std::vector<std::pair<BlockTerm*, llvm::BasicBlock*> >::iterator it = blocks.begin();
	   it != blocks.end(); ++it) {
	llvm::BasicBlock *llvm_bb = llvm::BasicBlock::Create(context(), "", llvm_func);
	it->second = llvm_bb;
	func_builder.m_terms.insert(std::make_pair(it->first, LLVMFunctionBuilder::make_known(llvm_bb)));
      }

      for (std::vector<std::pair<BlockTerm*, llvm::BasicBlock*> >::iterator it = blocks.begin();
	   it != blocks.end(); ++it) {
	irbuilder.SetInsertPoint(it->second);
	for (BlockTerm::InstructionList::const_iterator jt = it->first->instructions().begin();
	     jt != it->first->instructions().end(); ++jt) {
	  InstructionTerm *insn = &const_cast<InstructionTerm&>(*jt);
	  LLVMFunctionBuilder::Result r = insn->backend()->llvm_value_instruction(func_builder, insn);
	  func_builder.m_terms.insert(std::make_pair(insn, r));
	}
      }
    }

    void LLVMConstantBuilder::build_global_variable(GlobalVariableTerm *psi_var, llvm::GlobalVariable *llvm_var) {
      TermPtr<> value = psi_var->value();
      if (!value)
	throw std::logic_error("global variable value not set");

      Constant llvm_value = constant(value);
      if (llvm_value.known()) {
	llvm_var->setInitializer(llvm_value.value());
      } else {
	PSI_ASSERT(llvm_value.empty());
      }
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

    LLVMConstantBuilder::Type LLVMFunctionBuilder::type(TermRef<> term) {
      return m_constant_builder->type(term);
    }

    LLVMFunctionBuilder::Result LLVMFunctionBuilder::value(TermRef<> term) {
      if (term->global()) {
	return m_constant_builder->constant(term);
      } else {
	TermMap::iterator it = m_terms.find(term.get());
	if (it != m_terms.end()) {
	  return it->second;
	} else if ((term->term_type() == term_instruction) || (term->term_type() == term_phi)) {
	  throw std::logic_error("Instruction or phi term not yet available");
	}

	return build_term(term, m_terms, InstructionBuilderCallback(this)).first;
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

    LLVMFunctionBuilder::Result LLVMFunctionBuilder::metatype_value(llvm::Value *size, llvm::Value *align) {
      const llvm::Type* i64 = llvm::Type::getInt64Ty(context());
      llvm::Type *mtype = llvm::StructType::get(context(), i64, i64, NULL);
      llvm::Value *first = irbuilder().CreateInsertValue(llvm::UndefValue::get(mtype), size, 0);
      llvm::Value *second = irbuilder().CreateInsertValue(first, align, 1);
      return LLVMFunctionBuilder::make_known(second);
    }

    LLVMFunctionBuilder::Result LLVMFunctionBuilder::metatype_value(const llvm::Type* ty) {
      return m_constant_builder->metatype_value(ty);
    }

    LLVMFunctionBuilder::Result LLVMFunctionBuilder::metatype_value_empty() {
      return m_constant_builder->metatype_value_empty();
    }
  }
}
