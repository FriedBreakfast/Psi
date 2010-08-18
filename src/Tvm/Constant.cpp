#include "Constant.hpp"
#include "../Utility.hpp"

#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/GlobalVariable.h>

namespace Psi {
  namespace Tvm {
    ConstantValue::ConstantValue(const UserInitializer& ui, Context *context, Type *type)
      : Value(ui, context, type, true, true) {
    }

    class GlobalVariable::Initializer : public InitializerBase<GlobalVariable, GlobalVariable::slot_max> {
    public:
      Initializer(TermType *type, bool read_only, Term *initializer)
	: m_type(type), m_read_only(read_only), m_initializer(initializer) {
      }

      GlobalVariable* operator () (void *p, const UserInitializer& ui, Context *con) const {
	return new (p) GlobalVariable(ui, con, m_type, m_read_only, m_initializer);
      }

    private:
      TermType *m_type;
      bool m_read_only;
      Term *m_initializer;
    };

    GlobalVariable* GlobalVariable::create(TermType *type, bool read_only, Term *initializer) {
      return type->context()->new_user(Initializer(type, read_only, initializer));
    }

    GlobalVariable::GlobalVariable(const UserInitializer& ui, Context *context, TermType *type, bool read_only, Term *initializer)
      : ConstantValue(ui, context, context->type_pointer()->apply(type)),
	m_read_only(read_only) {
      use_set(slot_initializer, initializer);
    }

    LLVMBuilderValue GlobalVariable::build_llvm_value(LLVMBuilder& builder) {
      AppliedType *ap = checked_pointer_static_cast<AppliedType>(type());
      LLVMBuilderType ty = builder.type(ap->parameter(0));
      switch(ty.category()) {
      case LLVMBuilderType::known: {
	LLVMBuilderValue v = builder.value(initializer());
	PSI_ASSERT(llvm::isa<llvm::Constant>(v.value()), "Global initializer is not constant");
	return LLVMBuilderValue::known_value
	  (new llvm::GlobalVariable(builder.module(), ty.type(), m_read_only,
				    llvm::GlobalValue::ExternalLinkage,
				    const_cast<llvm::Constant*>(llvm::cast<llvm::Constant>(v.value())),
				    ""));
      }

      case LLVMBuilderType::empty: {
	const llvm::Type *i8 = llvm::Type::getInt8Ty(builder.context());
	llvm::Constant *v = llvm::ConstantInt::get(i8, 0);
	return LLVMBuilderValue::known_value
	  (new llvm::GlobalVariable(builder.module(), i8, true,
				    llvm::GlobalValue::ExternalLinkage,
				    v, ""));
      }

      default:
	PSI_FAIL("Type of global variable must be known (or empty)");
      }
    }

    class ConstantInteger::Initializer : public InitializerBase<ConstantInteger, ConstantInteger::slot_max> {
    public:
      Initializer(IntegerType *type, const mpz_class *value) : m_type(type), m_value(value) {
      }

      ConstantInteger* operator () (void *p, const UserInitializer& ui, Context *con) const {
	return new (p) ConstantInteger(ui, con, m_type, *m_value);
      }

    private:
      IntegerType *m_type;
      const mpz_class *m_value;
    };

    ConstantInteger* ConstantInteger::create(IntegerType *type, const mpz_class& value) {
      return type->context()->new_user(Initializer(type, &value));
    }

    ConstantInteger::ConstantInteger(const UserInitializer& ui, Context *context, IntegerType *type, const mpz_class& value)
      : ConstantValue(ui, context, type), m_value(value) {
    }

    LLVMBuilderValue ConstantInteger::build_llvm_value(LLVMBuilder& builder) {
      return LLVMBuilderValue::global_value(checked_pointer_static_cast<IntegerType>(type())->constant_to_llvm(builder.context(), m_value));
    }

    class ConstantReal::Initializer : public InitializerBase<ConstantReal, ConstantReal::slot_max> {
    public:
      Initializer(RealType *type, const mpf_class *value) : m_type(type), m_value(value) {
      }

      ConstantReal* operator () (void *p, const UserInitializer& ui, Context *con) const {
	return new (p) ConstantReal(ui, con, m_type, *m_value);
      }

    private:
      RealType *m_type;
      const mpf_class *m_value;
    };

    ConstantReal* ConstantReal::create(RealType *type, const mpf_class& value) {
      return type->context()->new_user(Initializer(type, &value));
    }

    ConstantReal::ConstantReal(const UserInitializer& ui, Context *context, RealType *type, const mpf_class& value)
      : ConstantValue(ui, context, type), m_value(value) {
    }

    LLVMBuilderValue ConstantReal::build_llvm_value(LLVMBuilder& builder) {
      return LLVMBuilderValue::global_value(checked_pointer_static_cast<RealType>(type())->constant_to_llvm(builder.context(), m_value));
    }
  }
}
