#include "Type.hpp"

#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Support/IRBuilder.h>

#include <sstream>

namespace Psi {
  namespace Tvm {
    TemplateType::TemplateType(const UserInitializer& ui, Context *context, std::size_t n_parameters)
      : ContextObject(ui, context), m_n_parameters(n_parameters) {
    }

    TemplateType::~TemplateType() {
    }

    Type* TemplateType::apply(const std::vector<Term*>& parameters) {
      return apply(parameters.size(), &parameters[0]);
    }

    Type* TemplateType::apply(Term *t1) {Term* p[] = {t1}; return apply(1, p);}
    Type* TemplateType::apply(Term *t1, Term *t2) {Term* p[] = {t1, t2}; return apply(2, p);}

    Type* TemplateType::apply(std::size_t n_parameters, Term *const* parameters) {
      if (m_n_parameters != n_parameters)
	throw std::logic_error("Incorrect number of template parameters");

      for (std::size_t i = 0; i < n_parameters; i++)
	PSI_ASSERT(parameters[i], "Cannot instantiate template with null parameters");

      return AppliedType::create(this, n_parameters, parameters);
    }

    class AppliedType::Initializer : public InitializerBase<AppliedType> {
    public:
      Initializer(TemplateType *template_, std::size_t n_parameters, Term *const* parameters)
	: m_template(template_),
	  m_n_parameters(n_parameters),
	  m_parameters(parameters) {
      }

      std::size_t slots() const {
	return AppliedType::slot_parameters_start + m_n_parameters;
      }

      AppliedType* operator() (void *p, const UserInitializer& ui, Context *context) const {
	return new (p) AppliedType(ui, context, m_template, m_n_parameters, m_parameters);
      }
      
    private:
      TemplateType *m_template;
      std::size_t m_n_parameters;
      Term *const* m_parameters;
    };

    TermType *TemplateType::externalize_type(AppliedType *applied, TermType *type) {
      if (AppliedType *a = dynamic_cast<AppliedType*>(type)) {
	std::vector<Term*> child_parameters;
	bool changed = false;
	for (std::size_t i = 0; i < a->n_parameters(); ++i) {
	  Term *t = a->parameter(i);
	  if (ParameterType *p = dynamic_cast<ParameterType*>(t)) {
	    changed = true;
	    child_parameters.push_back(applied->parameter(p->index()));
	  } else {
	    child_parameters.push_back(t);
	  }
	}

	if (changed)
	  return a->template_()->apply(child_parameters);
	else
	  return a;
      } else {
	PSI_ASSERT(type->constant(), "Non-template type is not constant");
	return type;
      }
    }

    AppliedType* AppliedType::create(TemplateType *template_, std::size_t n_parameters, Term *const* parameters) {
      return template_->context()->new_user(Initializer(template_, n_parameters, parameters));
    }

    namespace {
      bool all_constant(std::size_t n_parameters, Term *const* parameters) {
	for (std::size_t i = 0; i < n_parameters; ++i) {
	  if (!parameters[i]->constant())
	    return false;
	}
	return true;
      }
    }

    AppliedType::AppliedType(const UserInitializer& ui, Context *context, TemplateType* template_, std::size_t n_parameters, Term *const* parameters)
      : Type(ui, context, all_constant(n_parameters, parameters)) {
      PSI_ASSERT(ui.n_uses() == slot_parameters_start + n_parameters, "incorrect number of slots allocated");

      use_set(slot_template, template_);

      for (std::size_t i = 0; i < n_parameters; ++i)
	use_set(slot_parameters_start+i, parameters[i]);
    }

    bool AppliedType::is_aggregate() {
      return dynamic_cast<AggregateType*>(template_());
    }

    const llvm::Value* AppliedType::build_llvm_value(LLVMBuilder& builder) {
      if (constant()) {
	const llvm::Type *ty = builder.type(this);
	PSI_ASSERT(ty, "a constant AppliedType did not have a fixed LLVM type");
	return Metatype::llvm_value(builder.context(), ty);
      } else {
	return template_()->build_llvm_value(builder, this);
      }
    }

    const llvm::Type* AppliedType::build_llvm_type(LLVMBuilder& builder) {
      return template_()->build_llvm_type(builder, this);
    }

    struct PointerType::Initializer : InitializerBase<PointerType, PointerType::slot_max> {
      PointerType* operator () (void *ptr, const UserInitializer& ui, Context *context) const {
	return new (ptr) PointerType(ui, context);
      }
    };

    PointerType::PointerType(const UserInitializer& ui, Context *context)
      : TemplateType(ui, context, 1) {
    }

    PointerType* PointerType::create(Context *context) {
      return context->new_user(Initializer());
    }

    const llvm::Value* PointerType::build_llvm_value(LLVMBuilder& builder, AppliedType*) {
      return Metatype::llvm_value(builder.context(), llvm::Type::getInt8PtrTy(builder.context()));
    }

    const llvm::Type* PointerType::build_llvm_type(LLVMBuilder& builder, AppliedType *applied) {
      Term *term = applied->parameter(0);

      if (!term->constant())
	return llvm::Type::getInt8PtrTy(builder.context());

      const llvm::Type* ty = builder.type(term);
      PSI_ASSERT(ty, "a constant Term did not have a fixed LLVM type");
      return ty->getPointerTo();
    }

    void OpaqueType::unify(TemplateType *ty) {
      assert(n_parameters() == ty->n_parameters());
      replace_with(ty);
    }

    PrimitiveType::PrimitiveType(const UserInitializer& ui, Context *context)
      : Type(ui, context, true) {
    }

    class IntegerType::Initializer : public InitializerBase<IntegerType, IntegerType::slot_max> {
    public:
      Initializer(unsigned n_bits, bool is_signed)
	: m_n_bits(n_bits),
	  m_is_signed(is_signed) {
      }

      IntegerType* operator() (void* p, const UserInitializer& ui, Context *context) const {
	return new (p) IntegerType (ui, context, m_n_bits, m_is_signed);
      }

    private:
      unsigned m_n_bits;
      bool m_is_signed;
    };

    IntegerType* IntegerType::create(Context *context, unsigned n_bits, bool is_signed) {
      return context->new_user(Initializer(n_bits, is_signed));
    }

    IntegerType::IntegerType(const UserInitializer& ui, Context *context, unsigned n_bits, bool is_signed)
      : PrimitiveType(ui, context),
	m_n_bits(n_bits),
	m_is_signed(is_signed) {
    }

    IntegerType::~IntegerType() {
    }

    llvm::APInt IntegerType::mpl_to_llvm(bool is_signed, unsigned n_bits, const mpz_class& value) {
      std::size_t value_bits = mpz_sizeinbase(value.get_mpz_t(), 2);
      if (mpz_sgn(value.get_mpz_t()) < 0) {
	if (!is_signed)
	  throw std::logic_error("integer literal value of out range");
	value_bits++;
      }
      value_bits = std::max(value_bits, std::size_t(n_bits));

      std::string text = value.get_str(16);
      llvm::APInt ap(value_bits, text, 16);

      if (n_bits == value_bits)
	return ap;

      if (is_signed) {
	if (ap.isSignedIntN(n_bits))
	  return ap.sext(n_bits);
	else
	  throw std::logic_error("integer literal value of out range");
      } else {
	if (ap.isIntN(n_bits))
	  return ap.zext(n_bits);
	else
	  throw std::logic_error("integer literal value of out range");
      }
    }

    llvm::Value* IntegerType::constant_to_llvm(llvm::LLVMContext& context, const mpz_class& value) {
      const llvm::Type *ty = llvm::IntegerType::get(context, m_n_bits);
      return llvm::ConstantInt::get(ty, mpl_to_llvm(m_is_signed, m_n_bits, value));
    }

    const llvm::Type* IntegerType::build_llvm_type(LLVMBuilder& builder) {
      return llvm::IntegerType::get(builder.context(), m_n_bits);
    }

    const llvm::Value* IntegerType::build_llvm_value(LLVMBuilder& builder) {
      return Metatype::llvm_value(builder.context(), builder.type(this));
    }

    llvm::APFloat RealType::mpl_to_llvm(const llvm::fltSemantics& semantics, const mpf_class& value) {
      mp_exp_t exp;

      std::stringstream r;
      if (mpf_sgn(value.get_mpf_t()) < 0)
	r << "-";

      r << "0." << value.get_str(exp);
      r << "e" << exp;

      return llvm::APFloat(semantics, r.str());
    }

    llvm::APFloat RealType::special_to_llvm(const llvm::fltSemantics& semantics, SpecialReal v, bool negative) {
      switch (v) {
      case SpecialReal::zero: return llvm::APFloat::getZero(semantics, negative);
      case SpecialReal::nan: return llvm::APFloat::getNaN(semantics, negative);
      case SpecialReal::qnan: return llvm::APFloat::getQNaN(semantics, negative);
      case SpecialReal::snan: return llvm::APFloat::getSNaN(semantics, negative);
      case SpecialReal::largest: return llvm::APFloat::getLargest(semantics, negative);
      case SpecialReal::smallest: return llvm::APFloat::getSmallest(semantics, negative);
      case SpecialReal::smallest_normalized: return llvm::APFloat::getSmallestNormalized(semantics, negative);

      default:
	throw std::logic_error("unknown special floating point value");
      }
    }

    struct LabelType::Initializer : InitializerBase<LabelType, LabelType::slot_max> {
      LabelType* operator() (void *p, const UserInitializer& ui, Context *context) const {
	return new (p) LabelType(ui, context);
      }
    };

    LabelType* LabelType::create(Context *context) {
      return context->new_user(Initializer());
    }

    LabelType::LabelType(const UserInitializer& ui, Context *context)
      : PrimitiveType(ui, context) {
    }

    const llvm::Value* LabelType::build_llvm_value(LLVMBuilder& builder) {
      return Metatype::llvm_value(builder.context(), builder.type(this));
    }

    const llvm::Type* LabelType::build_llvm_type(LLVMBuilder& builder) {
      return llvm::Type::getLabelTy(builder.context());
    }

    template<typename Derived> class AggregateType::Initializer : public InitializerBase<Derived> {
    public:
      Initializer(std::size_t n_parameters, std::size_t n_members, Type *const* members)
	: m_n_parameters(n_parameters), m_n_members(n_members), m_members(members) {
      }

      std::size_t slots() const {
	return slot_members_start + m_n_parameters;
      }

      Derived* operator () (void *ptr, const UserInitializer& ui, Context *context) const {
	return new (ptr) Derived (ui, context, m_n_parameters, m_n_members, m_members);
      }

    private:
      std::size_t m_n_parameters;
      std::size_t m_n_members;
      Type *const* m_members;
    };

    AggregateType::AggregateType(const UserInitializer& ui, Context *context,
				 std::size_t n_parameters, std::size_t n_members, Type *const* members)
      : TemplateType(ui, context, n_parameters) {
      for (std::size_t i = 0; i < n_members; ++i)
	use_set(slot_members_start+i, members[i]);
    }
    
    template<typename Derived>
    Derived* AggregateType::create(Context *context, std::size_t n_parameters, std::size_t n_members, Type *const* members) {
      return context->new_user(Initializer<Derived>(n_parameters, n_members, members));
    }

    std::vector<const llvm::Value*> AggregateType::build_llvm_member_values(LLVMBuilder& builder, AppliedType *applied) {
      std::vector<const llvm::Value*> llvm_members;
      for (std::size_t i = 0; i < n_members(); ++i) {
	TermType *m = externalize_type(applied, member(i));
	const llvm::Value *v = builder.value(m);
	if (v)
	  llvm_members.push_back(v);
      }

      return llvm_members;
    }

    std::vector<const llvm::Type*> AggregateType::build_llvm_member_types(LLVMBuilder& builder, AppliedType *applied) {
      std::vector<const llvm::Type*> llvm_members;
      for (std::size_t i = 0; i < n_members(); ++i) {
	TermType *m = externalize_type(applied, member(i));
	const llvm::Type *ty = builder.type(m);
	if (ty)
	  llvm_members.push_back(ty);
      }

      return llvm_members;
    }

    StructType::StructType(const UserInitializer& ui, Context *context,
			   std::size_t n_parameters, std::size_t n_members, Type *const* members)
      : AggregateType(ui, context, n_parameters, n_members, members) {
    }

    const llvm::Value* StructType::build_llvm_value(LLVMBuilder& builder, AppliedType *applied) {
      if (const llvm::Type *ty = builder.type(applied)) {
	return Metatype::llvm_value(builder.context(), ty);
      } else {
	std::vector<const llvm::Value*> members = build_llvm_member_values(builder, applied);
      }
    }

    const llvm::Type* StructType::build_llvm_type(LLVMBuilder& builder, AppliedType *applied) {
      return llvm::StructType::get(builder.context(), build_llvm_member_types(builder, applied));
    }

    UnionType::UnionType(const UserInitializer& ui, Context *context,
			 std::size_t n_parameters, std::size_t n_members, Type *const* members)
      : AggregateType(ui, context, n_parameters, n_members, members) {
    }

    namespace {
      std::pair<llvm::Constant*, llvm::Constant*> constant_size_align(const llvm::Value *value) {
	PSI_ASSERT(llvm::isa<llvm::Constant>(value), "value is not constant");
	llvm::Constant *c = const_cast<llvm::Constant*>(llvm::cast<llvm::Constant>(value));
	unsigned zero = 0, one = 1;
	return std::make_pair(llvm::ConstantExpr::getExtractValue(c, &zero, 1),
			      llvm::ConstantExpr::getExtractValue(c, &one, 1));
      }

      llvm::Constant* constant_max(llvm::Constant* left, llvm::Constant* right) {
	llvm::Constant* cmp = llvm::ConstantExpr::getCompare(llvm::CmpInst::ICMP_ULT, left, right);
	return llvm::ConstantExpr::getSelect(cmp, left, right);
      }

      std::pair<llvm::Value*, llvm::Value*> runtime_size_align(LLVMBuilder::IRBuilder& irbuilder, const llvm::Value *value) {
	llvm::Value *v = const_cast<llvm::Value*>(value);
	llvm::Value *size = irbuilder.CreateExtractValue(v, 0);
	llvm::Value *align = irbuilder.CreateExtractValue(v, 1);
	return std::make_pair(size, align);
      }

      llvm::Value* runtime_max(LLVMBuilder::IRBuilder& irbuilder, llvm::Value *left, llvm::Value *right) {
	llvm::Value *cmp = irbuilder.CreateICmpULT(left, right);
	return irbuilder.CreateSelect(cmp, left, right);
      }
    }

    const llvm::Value* UnionType::build_llvm_value(LLVMBuilder& builder, AppliedType *applied) {
      if (const llvm::Type *ty = builder.type(applied)) {
	return Metatype::llvm_value(builder.context(), ty);
      } else {
	std::vector<const llvm::Value*> members = build_llvm_member_values(builder, applied);
	if (members.empty())
	  return NULL;

	if (builder.global()) {
	  std::vector<const llvm::Value*>::iterator it = members.begin();
	  std::pair<llvm::Constant*, llvm::Constant*> size_align = constant_size_align(*it);
	  llvm::Constant *size = size_align.first;
	  llvm::Constant *align = size_align.second;
	  ++it;

	  for (; it != members.end(); ++it) {
	    size_align = constant_size_align(*it);
	    size = constant_max(size, size_align.first);
	    align = constant_max(align, size_align.second);
	  }
	} else {
	}
      }
    }

    const llvm::Type* UnionType::build_llvm_type(LLVMBuilder& builder, AppliedType *applied) {
      if (n_members() > 0) {
	std::vector<const llvm::Type*> m = build_llvm_member_types(builder, applied);
	PSI_ASSERT(n_members() == m.size(), "Wrong number of members returned");
	return llvm::UnionType::get(&m[0], m.size());
      } else {
	return NULL;
      }
    }
  }
}
