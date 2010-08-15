#ifndef HPP_PSI_TYPE
#define HPP_PSI_TYPE

#include <vector>

#include <gmpxx.h>

#include "User.hpp"
#include "Core.hpp"

namespace Psi {
  namespace Tvm {
    /**
     * \brief Type of functions.
     *
     * This does not derive from #TemplateType since functions handle
     * type parameters differently: type parameters are passed when the
     * function is called so values which contain quantifiers are
     * permitted, which #TemplateType does not allow.
     *
     * Functions take two types of parameter: regular parameters, which
     * are passed as normal, and quantified parameters, whose values are
     * not passed, so they are only suitable for computing return types
     * (and can be likewise forwarded by the type system inside the
     * function).
     */
    class FunctionType : public Type {
    protected:
      enum Slots {
	slot_result = Type::slot_max,
	slot_parameters_base
      };

    public:
      /**
       * \brief Get the result type.
       *
       * This returns a #Type rather than a #TermType since a function
       * always returns a #Value.
       */
      Type *result() {return use_get<Type>(slot_result);}

      /// \brief Number of quantified parameters
      std::size_t n_quantified() {return m_n_quantified;}
      /// \brief Number of regular parameters
      std::size_t n_regular() {return m_n_regular;}
      /// \brief Number of parameters (both quantified and regular)
      std::size_t n_parameters() {return m_n_quantified + m_n_regular;}
      /**
       * \brief Get the type of a particular parameter.
       *
       * This includes both quantified and regular parameters;
       * quantified parameters are first.
       *
       * The type may include #ParameterType instances; these refer to
       * parameters to this function, and can only be references to
       * earlier parameters.
       */
      TermType *parameter(std::size_t n) {return use_get<Type>(slot_parameters_base+n);}

    private:
      std::size_t m_n_quantified;
      std::size_t m_n_regular;
    };

    /**
     * \brief Base class for template types.
     *
     * Most types derive from this type (e.g. #StructType, #UnionType,
     * #ArrayType, #PointerType). Concrete #Type objects are created
     * using #AppliedType.
     */
    class TemplateType : public ContextObject {
    protected:
      enum Slots {
	slot_max=0
      };

      TemplateType(const UserInitializer& ui, Context *context, std::size_t n_parameters);

    public:
      virtual ~TemplateType();

      std::size_t n_parameters() {return m_n_parameters;}

      /**
       * \brief Instantiate this type with the given parameters.
       */
      Type* apply(const std::vector<Term*>& parameters);

      /**
       * \brief Instantiate this type with parameters specified by
       * varargs. Each parameter should be a Term*.
       */
      Type* apply(Term*);
      Type* apply(Term*, Term*);
      Type* apply(std::size_t n_parameters, Term *const* parameters);

    protected:
      /**
       * \brief Take a type defined inside this template and apply
       * arguments to it to move it to another context.
       *
       * \param applied Applied type to get parameters from.
       * \param type Type to convert.
       */
      static TermType *externalize_type(AppliedType *applied, TermType *type);

    private:
      std::size_t m_n_parameters;

      friend class AppliedType;
      virtual const llvm::Value* build_llvm_value(LLVMBuilder& builder, AppliedType *applied) = 0;
      virtual const llvm::Type* build_llvm_type(LLVMBuilder& builder, AppliedType *applied) = 0;
    };

    /**
     * \brief Produces a concrete type from a #TemplateType.
     */
    class AppliedType : public Type {
    protected:
      enum Slots {
	slot_template = Term::slot_max,
	slot_parameters_start
      };

    private:
      friend class TemplateType;
      static AppliedType* create(TemplateType *template_, std::size_t n_parameters, Term *const* parameters);
      class Initializer;
      AppliedType(const UserInitializer& ui, Context *context, TemplateType *template_, std::size_t n_parameters, Term *const* parameters);

      virtual const llvm::Value* build_llvm_value(LLVMBuilder& builder);
      virtual const llvm::Type* build_llvm_type(LLVMBuilder& builder);

    public:
      TemplateType *template_() {return use_get<TemplateType>(slot_template);}
      std::size_t n_parameters() {return template_()->n_parameters();}
      Term* parameter(std::size_t n) {return use_get<Term>(slot_parameters_start+n);}

      /// Whether this type is an aggregate or not.
      bool is_aggregate();
      /// \brief Get the specified member of this type. This type must
      /// be an aggregate.
      Type *member_type(std::size_t n);

      Type *array_element_type();
    };

    inline AppliedType* Value::applied_type() {
      return use_get<AppliedType>(slot_type);
    }

    /**
     * \brief A parameter type.
     *
     * Refers to a parameter passed to either a #TemplateType or a
     * #FunctionType.
     */
    class ParameterType : public Type {
    public:
      std::size_t index() {return m_index;}

    private:
      /// Index of this parameter in the parent context.
      std::size_t m_index;
    };

    class ArrayType : public TemplateType {
    private:
      friend class Context;
      struct Initializer;
      ArrayType(const UserInitializer&, Context*);
    };

    class PointerType : public TemplateType {
    private:
      friend class Context;
      struct Initializer;
      PointerType(const UserInitializer&, Context*);
      static PointerType* create(Context*);
      virtual const llvm::Value* build_llvm_value(LLVMBuilder& builder, AppliedType *applied);
      virtual const llvm::Type* build_llvm_type(LLVMBuilder& builder, AppliedType *applied);
    };

    class AggregateType : public TemplateType {
    protected:
      enum Slots {
	slot_members_start=TemplateType::slot_max
      };

    public:
      std::size_t n_members() {return use_slots() - slot_members_start;}
      TermType *member(std::size_t n) {return use_get<TermType>(slot_members_start+n);}

    private:
      template<typename Derived> class Initializer;
    protected:
      template<typename Derived> static Derived* create(Context *context, std::size_t n_parameters,
							std::size_t n_members, Type *const* members);
      AggregateType(const UserInitializer& ui, Context *context,
		    std::size_t n_parameters, std::size_t n_members, Type *const* members);

      std::vector<const llvm::Value*> build_llvm_member_values(LLVMBuilder& builder, AppliedType *applied);
      std::vector<const llvm::Type*> build_llvm_member_types(LLVMBuilder& builder, AppliedType *applied);
    };

    class StructType : public AggregateType {
    public:
      static StructType* create(Context *context, std::size_t n_parameters, std::size_t n_members, Type *const* members);

    private:
      friend class AggregateType::Initializer<StructType>;
      StructType(const UserInitializer& ui, Context *context,
		 std::size_t n_parameters, std::size_t n_members, Type *const* members);
      virtual const llvm::Value* build_llvm_value(LLVMBuilder& builder, AppliedType *applied);
      virtual const llvm::Type* build_llvm_type(LLVMBuilder& builder, AppliedType *applied);
    };

    class UnionType : public AggregateType {
    public:
      static UnionType* create(Context *context, std::size_t n_parameters, std::size_t n_members, Type *const* members);

    private:
      friend class AggregateType::Initializer<StructType>;
      UnionType(const UserInitializer& ui, Context *context,
		std::size_t n_parameters, std::size_t n_members, Type *const* members);
      virtual const llvm::Value* build_llvm_value(LLVMBuilder& builder, AppliedType *applied);
      virtual const llvm::Type* build_llvm_type(LLVMBuilder& builder, AppliedType *applied);
    };

    class OpaqueType : public TemplateType {
    public:
      void unify(TemplateType *t);
    };

    class PrimitiveType : public Type { 
    protected:
      PrimitiveType(const UserInitializer& ui, Context *context);
    };

    /**
     * \brief Categories of special floating point value.
     */
    class SpecialReal {
    public:
      enum Category {
	zero,
	nan,
	qnan,
	snan,
	largest,
	smallest,
	smallest_normalized
      };

      SpecialReal() {}
      SpecialReal(Category v) : m_v(v) {}
      /**
       * This is to enable \c switch functionality. #Category instances
       * should not be used directly.
       */
      operator Category () {return m_v;}

    private:
      Category m_v;
    };

    class RealType : public PrimitiveType {
    public:
      /**
       * \brief Convert an MPL real to an llvm::APFloat.
       */
      static llvm::APFloat mpl_to_llvm(const llvm::fltSemantics& semantics, const mpf_class& value);
      /**
       * \brief Get an llvm::APFloat for a special value (see #Value).
       */
      static llvm::APFloat special_to_llvm(const llvm::fltSemantics& semantics, SpecialReal which, bool negative);

      virtual llvm::Value* constant_to_llvm(llvm::LLVMContext& context, const mpf_class& value) = 0;
      virtual llvm::Value* special_to_llvm(llvm::LLVMContext& context, SpecialReal which, bool negative=false) = 0;
    };

    class IntegerType : public PrimitiveType {
    public:
      static llvm::APInt mpl_to_llvm(bool is_signed, unsigned n_bits, const mpz_class& value);

      llvm::Value* constant_to_llvm(llvm::LLVMContext& context, const mpz_class& value);

    private:
      friend class Context;
      class Initializer;
      static IntegerType* create(Context *context, unsigned n_bits, bool is_signed);
      IntegerType(const UserInitializer& ui, Context *context, unsigned n_bits, bool is_signed);
      virtual ~IntegerType();

      virtual const llvm::Type* build_llvm_type(LLVMBuilder&);
      virtual const llvm::Value* build_llvm_value(LLVMBuilder&);

      unsigned m_n_bits;
      bool m_is_signed;
    };

    class LabelType : public PrimitiveType {
    public:

    private:
      friend class Context;
      struct Initializer;
      static LabelType* create(Context *context);
      LabelType(const UserInitializer& ui, Context *context);

      virtual const llvm::Type* build_llvm_type(LLVMBuilder&);
      virtual const llvm::Value* build_llvm_value(LLVMBuilder&);
    };
  }
}

#endif
