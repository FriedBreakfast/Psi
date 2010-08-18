#include "Type.hpp"

#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Support/IRBuilder.h>

#include <sstream>

namespace Psi {
  namespace Tvm {
    namespace {
      template<typename TermType>
      bool all_global(std::size_t n_parameters, TermType *const* parameters) {
	for (std::size_t i = 0; i < n_parameters; ++i) {
	  if (!parameters[i]->global())
	    return false;
	}
	return true;
      }
    }

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
	PSI_ASSERT(context() == parameters[i]->context(), "template parameter belongs to different context");

      return AppliedType::create(this, parameters);
    }

    class AppliedType::Initializer : public InitializerBase<AppliedType> {
    public:
      Initializer(TemplateType *template_, Term *const* parameters)
	: m_template(template_),
	  m_parameters(parameters) {
      }

      std::size_t slots() const {
	return AppliedType::slot_parameters_start + m_template->n_parameters();
      }

      AppliedType* operator() (void *p, const UserInitializer& ui, Context *context) const {
	return new (p) AppliedType(ui, context, m_template, m_parameters);
      }
      
    private:
      TemplateType *m_template;
      Term *const* m_parameters;
    };

    namespace {
      template<typename T>
      TermType* externalize_type_helper(const T& parameters, TermType *type) {
	if (AppliedType *a = dynamic_cast<AppliedType*>(type)) {
	  std::vector<Term*> child_parameters(a->n_parameters());
	  for (std::size_t i = 0; i < child_parameters.size(); ++i)
	    child_parameters[i] = externalize_type_helper(parameters, a->parameter(i));
	  return a->template_()->apply(child_parameters);
	} else if (TemplateParameterType *p = dynamic_cast<TemplateParameterType*>(type)) {
	  return parameters[p->index()];
	} else if (FunctionType *f = dynamic_cast<FunctionType*>(type)) {
	  std::vector<TermType*> f_params(f->n_quantified() + f->n_regular());
	  for (std::size_t i = 0; i < f_params.size(); ++i)
	    f_params[i] = externalize_type_helper(parameters, f->parameter(i));
	  return FunctionType::create(type->context(),
				      f->n_quantified(), &f_params[0],
				      f->n_regular(), &f_params[f->n_quantified()]);
	} else {
	  return type;
	}
      }

      struct AppliedTypeParameters {
	AppliedType *ty;
	AppliedTypeParameters(AppliedType *ty_) : ty(ty_) {}
	Term* operator [] (std::size_t n) const {return ty->parameter(n);}
      };
    }

    TermType *TemplateType::externalize_type(AppliedType *applied, TermType *type) {
      return externalize_type_helper(AppliedTypeParameters(applied), type);
    }

    TermType *TemplateType::externalize_type(Term *const* applied, TermType *type) {
      return externalize_type_helper(applied, type);
    }

    AppliedType* AppliedType::create(TemplateType *template_, Term *const* parameters) {
      return template_->context()->new_user(Initializer(template_, parameters));
    }

    AppliedType::AppliedType(const UserInitializer& ui, Context *context, TemplateType* template_, Term *const* parameters)
      : Type(ui, context,
	     template_->constant_for(parameters),
	     all_global(template_->n_parameters(), parameters)) {
      use_set(slot_template, template_);

      for (std::size_t i = 0; i < template_->n_parameters(); ++i)
	use_set(slot_parameters_start+i, parameters[i]);
    }

    bool AppliedType::is_aggregate() {
      return dynamic_cast<AggregateType*>(template_());
    }

    bool AggregateType::constant_for(Term *const* parameters) {
      std::vector<Term*> child_parameters;
      for (std::size_t i = 0; i < n_members(); ++i) {
	Term *m = member(i);
	if (AppliedType *ty = dynamic_cast<AppliedType*>(m)) {
	} else if (TemplateParameterType *ty = dynamic_cast<TemplateParameterType*>(m)) {
	  if (!parameters[ty->index()]->constant())
	    return false;
	}
      }

      return true;
    }

    LLVMBuilderValue AppliedType::build_llvm_value(LLVMBuilder& builder) {
      return template_()->build_llvm_value(builder, this);
    }

    LLVMBuilderType AppliedType::build_llvm_type(LLVMBuilder& builder) {
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

    bool PointerType::constant_for(Term*const*) {
      return true;
    }

    LLVMBuilderValue PointerType::build_llvm_value(LLVMBuilder& builder, AppliedType*) {
      llvm::LLVMContext& context = builder.context();
      return Metatype::llvm_value(llvm::Type::getInt8PtrTy(context));
    }

    LLVMBuilderType PointerType::build_llvm_type(LLVMBuilder& builder, AppliedType*) {
      return LLVMBuilderType::known_type(llvm::Type::getInt8PtrTy(builder.context()));
    }

    PrimitiveType::PrimitiveType(const UserInitializer& ui, Context *context)
      : Type(ui, context, true, true) {
    }

    LLVMBuilderValue PrimitiveType::build_llvm_value(LLVMBuilder& builder) {
      LLVMBuilderType ty = builder.type(this);
      switch(ty.category()) {
      case LLVMBuilderType::known:
	return Metatype::llvm_value(ty.type());

      case LLVMBuilderType::empty:
	return Metatype::llvm_value_empty(builder.context());

      default:
	PSI_FAIL("Primitive value does not have a global type");
      }
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

    LLVMBuilderType IntegerType::build_llvm_type(LLVMBuilder& builder) {
      return LLVMBuilderType::known_type(llvm::IntegerType::get(builder.context(), m_n_bits));
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

    LLVMBuilderType LabelType::build_llvm_type(LLVMBuilder& builder) {
      return LLVMBuilderType::known_type(llvm::Type::getLabelTy(builder.context()));
    }

    struct EmptyType::Initializer : InitializerBase<EmptyType, EmptyType::slot_max> {
      EmptyType* operator() (void *p, const UserInitializer& ui, Context *context) const {
	return new (p) EmptyType(ui, context);
      }
    };

    EmptyType* EmptyType::create(Context *context) {
      return context->new_user(Initializer());
    }

    EmptyType::EmptyType(const UserInitializer& ui, Context *context)
      : PrimitiveType(ui, context) {
    }

    LLVMBuilderType EmptyType::build_llvm_type(LLVMBuilder&) {
      return LLVMBuilderType::empty_type();
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

    struct OpaqueType::Initializer : InitializerBase<OpaqueType, OpaqueType::slot_max> {
      std::size_t m_n_parameters;

      Initializer(std::size_t n_parameters) : m_n_parameters(n_parameters) {}

      OpaqueType* operator () (void *ptr, const UserInitializer& ui, Context *context) const {
	return new (ptr) OpaqueType(ui, context, m_n_parameters);
      }
    };

    OpaqueType::OpaqueType(const UserInitializer& ui, Context *context, std::size_t n_parameters)
      : TemplateType(ui, context, n_parameters) {
    }

    OpaqueType* OpaqueType::create(Context *context, std::size_t n_parameters) {
      return context->new_user(Initializer(n_parameters));
    }

    void OpaqueType::unify(TemplateType *ty) {
      PSI_ASSERT(n_parameters() == ty->n_parameters(), "Wrong number of parameters in type unification");
      PSI_ASSERT(dynamic_cast<AggregateType*>(ty) == ty, "Unification should always be with aggregate types");
      replace_with(ty);
    }

    bool OpaqueType::constant_for(Term *const*) {
      throw std::logic_error("Opaque template type should not be queried for const-ness");
    }

    LLVMBuilderValue OpaqueType::build_llvm_value(LLVMBuilder&, AppliedType*) {
      throw std::logic_error("Opaque type has not been resolved when LLVM value is built");
    }

    LLVMBuilderType OpaqueType::build_llvm_type(LLVMBuilder&, AppliedType*) {
      throw std::logic_error("Opaque type has not been resolved when LLVM type is built");
    }

    AggregateType::AggregateType(const UserInitializer& ui, Context *context,
				 std::size_t n_parameters, std::size_t n_members, Type *const* members)
      : TemplateType(ui, context, n_parameters) {
      for (std::size_t i = 0; i < n_members; ++i)
	use_set(slot_members_start+i, members[i]);
    }
    
    template<typename Derived>
    Derived* AggregateType::create(Context *context, std::size_t n_parameters, std::size_t n_members, Type *const* members) {
      for (std::size_t i = 0; i < n_members; ++i) {
	PSI_ASSERT(context == members[i]->context(), "aggregate member belongs to another context");
	if (AppliedType *ty = dynamic_cast<AppliedType*>(members[i])) {
	  for (std::size_t j = 0; j < ty->n_parameters(); ++j) {
	    if (TemplateParameterType *pty = dynamic_cast<TemplateParameterType*>(ty->parameter(j))) {
	      PSI_ASSERT(pty->index() < n_parameters, "aggregate member template parameter index out of range");
	    }
	  }
	} else if (FunctionType *ty = dynamic_cast<FunctionType*>(members[i])) {
	  for (std::size_t j = 0; j < ty->n_parameters(); ++j) {
	    if (TemplateParameterType *pty = dynamic_cast<TemplateParameterType*>(ty->parameter(j))) {
	      PSI_ASSERT(pty->index() < n_parameters, "aggregate member template parameter index out of range");
	    }
	  }
	} else if (dynamic_cast<OpaqueType*>(members[i])) {
	  PSI_FAIL("aggregate type members cannot be opaque");
	}
      }

      return context->new_user(Initializer<Derived>(n_parameters, n_members, members));
    }

    std::vector<LLVMBuilderValue> AggregateType::build_llvm_member_values(LLVMBuilder& builder, AppliedType *applied) {
      std::vector<LLVMBuilderValue> llvm_members;
      for (std::size_t i = 0; i < n_members(); ++i) {
	TermType *m = externalize_type(applied, member(i));
	llvm_members.push_back(builder.value(m));
      }

      return llvm_members;
    }

    std::vector<LLVMBuilderType> AggregateType::build_llvm_member_types(LLVMBuilder& builder, AppliedType *applied) {
      std::vector<LLVMBuilderType> llvm_members;
      for (std::size_t i = 0; i < n_members(); ++i) {
	TermType *m = externalize_type(applied, member(i));
	llvm_members.push_back(builder.type(m));
      }

      return llvm_members;
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

      /*
       * Align a size to a boundary. The formula is: <tt>(size + align
       * - 1) & ~align</tt>. <tt>align</tt> must be a power of two.
       */
      llvm::Constant* constant_align(llvm::Constant* size, llvm::Constant* align) {
	llvm::Constant* one = llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(size->getType()), 1);
	llvm::Constant* a = llvm::ConstantExpr::getSub(align, one);
	llvm::Constant* b = llvm::ConstantExpr::getAdd(size, a);
	llvm::Constant* c = llvm::ConstantExpr::getNot(align);
	return llvm::ConstantExpr::getAnd(b, c);
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

      /* See constant_align */
      llvm::Value* runtime_align(LLVMBuilder::IRBuilder& irbuilder, llvm::Value* size, llvm::Value* align) {
	llvm::Constant* one = llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(size->getType()), 1);
	llvm::Value* a = irbuilder.CreateSub(align, one);
	llvm::Value* b = irbuilder.CreateAdd(size, a);
	llvm::Value* c = irbuilder.CreateNot(align);
	return irbuilder.CreateAnd(b, c);
      }
    }

    StructType::StructType(const UserInitializer& ui, Context *context,
			   std::size_t n_parameters, std::size_t n_members, Type *const* members)
      : AggregateType(ui, context, n_parameters, n_members, members) {
    }

    LLVMBuilderValue StructType::build_llvm_value(LLVMBuilder& builder, AppliedType *applied) {
      const llvm::Type *i64 = llvm::Type::getInt64Ty(builder.context());
      llvm::Constant *zero = llvm::ConstantInt::get(i64, 0);
      llvm::Constant *one = llvm::ConstantInt::get(i64, 1);

      std::vector<LLVMBuilderValue> members = build_llvm_member_values(builder, applied);
      if (builder.global()) {
	llvm::Constant *size = zero, *align = one;

	for (std::vector<LLVMBuilderValue>::iterator it = members.begin(); it != members.end(); ++it) {
	  PSI_ASSERT(it->category() == LLVMBuilderValue::global, "Member of global type is not global");
	  std::pair<llvm::Constant*, llvm::Constant*> size_align = constant_size_align(it->value());
	  size = llvm::ConstantExpr::getAdd(constant_align(size, size_align.second), size);
	  align = constant_max(align, size_align.second);
	}

	// size should always be a multiple of align
	size = constant_align(size, align);

	return Metatype::llvm_value_global(size, align);
      } else {
	LLVMBuilder::IRBuilder& irbuilder = builder.irbuilder();
	llvm::Value *size = zero, *align = one;

	for (std::vector<LLVMBuilderValue>::iterator it = members.begin(); it != members.end(); ++it) {
	  PSI_ASSERT((it->category() == LLVMBuilderValue::global) || (it->category() == LLVMBuilderValue::local_known),
		     "Value of metatype is not global or local_known");
	  std::pair<llvm::Value*, llvm::Value*> size_align = runtime_size_align(irbuilder, it->value());
	  size = irbuilder.CreateAdd(runtime_align(irbuilder, size, size_align.second), size);
	  align = runtime_max(irbuilder, align, size_align.second);
	}

	// size should always be a multiple of align
	size = runtime_align(irbuilder, size, align);

	return Metatype::llvm_value_local(builder, size, align);
      }
    }

    LLVMBuilderType StructType::build_llvm_type(LLVMBuilder& builder, AppliedType *applied) {
      std::vector<LLVMBuilderType> m = build_llvm_member_types(builder, applied);
      std::vector<const llvm::Type*> lm;
      for (std::vector<LLVMBuilderType>::iterator it = m.begin(); it != m.end(); ++it) {
	switch (it->category()) {
	case LLVMBuilderType::known:
	  lm.push_back(it->type());
	  break;

	case LLVMBuilderType::empty:
	  break;

	case LLVMBuilderType::unknown:
	  return LLVMBuilderType::unknown_type();
	}
      }

      if (lm.empty())
	return LLVMBuilderType::empty_type();
      else
	return LLVMBuilderType::known_type(llvm::StructType::get(builder.context(), lm));
    }

    UnionType::UnionType(const UserInitializer& ui, Context *context,
			 std::size_t n_parameters, std::size_t n_members, Type *const* members)
      : AggregateType(ui, context, n_parameters, n_members, members) {
    }

    LLVMBuilderValue UnionType::build_llvm_value(LLVMBuilder& builder, AppliedType *applied) {
      const llvm::Type *i64 = llvm::Type::getInt64Ty(builder.context());
      llvm::Constant *zero = llvm::ConstantInt::get(i64, 0);
      llvm::Constant *one = llvm::ConstantInt::get(i64, 1);

      std::vector<LLVMBuilderValue> members = build_llvm_member_values(builder, applied);
      if (builder.global()) {
	llvm::Constant *size = zero, *align = one;

	for (std::vector<LLVMBuilderValue>::iterator it = members.begin(); it != members.end(); ++it) {
	  PSI_ASSERT(it->category() == LLVMBuilderValue::global, "Member of global type is not global");
	  std::pair<llvm::Constant*, llvm::Constant*> size_align = constant_size_align(it->value());
	  size = constant_max(size, size_align.first);
	  align = constant_max(align, size_align.second);
	}

	return Metatype::llvm_value_global(size, align);
      } else {
	LLVMBuilder::IRBuilder& irbuilder = builder.irbuilder();
	llvm::Value *size = zero, *align = one;

	for (std::vector<LLVMBuilderValue>::iterator it = members.begin(); it != members.end(); ++it) {
	  PSI_ASSERT((it->category() == LLVMBuilderValue::global) || (it->category() == LLVMBuilderValue::local_known),
		     "Value of metatype is not global or local_known");
	  std::pair<llvm::Value*, llvm::Value*> size_align = runtime_size_align(irbuilder, it->value());
	  size = size_align.first;
	  align = size_align.second;
	}

	return Metatype::llvm_value_local(builder, size, align);
      }
    }

    LLVMBuilderType UnionType::build_llvm_type(LLVMBuilder& builder, AppliedType *applied) {
      std::vector<LLVMBuilderType> m = build_llvm_member_types(builder, applied);
      std::vector<const llvm::Type*> lm;
      for (std::vector<LLVMBuilderType>::iterator it = m.begin(); it != m.end(); ++it) {
	switch (it->category()) {
	case LLVMBuilderType::known:
	  lm.push_back(it->type());
	  break;

	case LLVMBuilderType::empty:
	  break;

	case LLVMBuilderType::unknown:
	  return LLVMBuilderType::unknown_type();
	}
      }

      if (lm.empty())
	return LLVMBuilderType::empty_type();
      else
	return LLVMBuilderType::known_type(llvm::UnionType::get(&lm[0], lm.size()));
    }

    class FunctionType::Initializer : public InitializerBase<FunctionType> {
    public:
      Initializer(std::size_t n_quantified, TermType *const* quantified,
		  std::size_t n_regular, TermType *const* regular)
	: m_n_quantified(n_quantified), m_n_regular(n_regular),
	  m_quantified(quantified), m_regular(regular) {
      }

      std::size_t slots() const {
	return FunctionType::slot_parameters_base + m_n_quantified + m_n_regular;
      }

      FunctionType* operator () (void *ptr, const UserInitializer& ui, Context *context) const {
	return new (ptr) FunctionType(ui, context,
				      m_n_quantified, m_quantified,
				      m_n_regular, m_regular);
      }

    private:
      std::size_t m_n_quantified, m_n_regular;
      TermType *const* m_quantified, *const* m_regular;
    };

    FunctionType::FunctionType(const UserInitializer& ui, Context *context,
			       std::size_t n_quantified, TermType *const* quantified,
			       std::size_t n_regular, TermType *const* regular)
      : Type(ui, context, true,
	     all_global(n_quantified, quantified) && all_global(n_regular, regular)),
	m_n_quantified(n_quantified),
	m_n_regular(n_regular) {
      for (std::size_t i = 0; i < n_quantified; ++i)
	use_set(i, quantified[i]);
      for (std::size_t i = 0; i < n_regular; ++i)
	use_set(n_quantified+i, regular[i]);
    }

    FunctionType* FunctionType::create(Context *context,
				       std::size_t n_quantified, TermType *const* quantified,
				       std::size_t n_regular, TermType *const* regular) {
      return context->new_user(Initializer(n_quantified, quantified,
					   n_regular, regular));
    }

    LLVMBuilderValue FunctionType::build_llvm_value(LLVMBuilder&) {
      throw std::logic_error("Function types do not have a value (can only be used through pointers)");
    }

    LLVMBuilderType FunctionType::build_llvm_type(LLVMBuilder& builder) {
      const llvm::Type *i8ptr = llvm::Type::getInt8PtrTy(builder.context());
      const llvm::Type *voidty = llvm::Type::getVoidTy(builder.context());
      std::vector<const llvm::Type*> params(m_n_regular+1, i8ptr);
      return LLVMBuilderType::known_type(llvm::FunctionType::get(voidty, params, false));
    }
  }
}
