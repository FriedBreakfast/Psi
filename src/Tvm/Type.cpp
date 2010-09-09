#include "Type.hpp"

#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Support/IRBuilder.h>

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
