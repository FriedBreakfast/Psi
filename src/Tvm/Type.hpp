#ifndef HPP_PSI_TYPE
#define HPP_PSI_TYPE

#include <vector>

#include <gmpxx.h>

#include "User.hpp"
#include "Core.hpp"

#if 0

namespace Psi {
  namespace Tvm {
    class ParameterType;

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

      TemplateType(const UserInitializer& ui, Context *context,
		   std::size_t n_parameters, ParameterType *const* parameters);

    public:
      virtual ~TemplateType();

      std::size_t n_parameters() {return m_n_parameters;}

      Term* apply(std::size_t n_parameters, Term *const* parameters);

      /**
       * \brief Instantiate this type with the given parameters.
       */
      Term* apply(const std::vector<Term*>& parameters);

      Term* apply(Term*);
      Term* apply(Term*, Term*);

    protected:
      /**
       * \brief Take a type defined inside this template and apply
       * arguments to it to move it to another context.
       *
       * \param applied Applied type to get parameters from.
       * \param type Type to convert.
       */
      static Term *externalize_term(AppliedType *applied, Term *term);
      static Term *externalize_term(Term *const* applied, Term *term);

    private:
      std::size_t m_n_parameters;

      friend class AppliedType;
      virtual bool constant_for(Term *const* parameters) = 0;
      virtual LLVMBuilderValue build_llvm_value(LLVMBuilder& builder, AppliedType *applied) = 0;
      virtual LLVMBuilderType build_llvm_type(LLVMBuilder& builder, AppliedType *applied) = 0;
    };

    /**
     * \brief Produces a concrete type from a #TemplateType.
     */
    class AppliedType : public Expression {
    protected:
      enum Slots {
	slot_template = Term::slot_max,
	slot_parameters_start
      };

    private:
      friend class TemplateType;
      static AppliedType* create(TemplateType *template_, Term *const* parameters);
      class Initializer;
      AppliedType(const UserInitializer& ui, Context *context, TemplateType *template_, Term *const* parameters);

      virtual LLVMBuilderValue build_llvm_value(LLVMBuilder& builder);
      virtual LLVMBuilderType build_llvm_type(LLVMBuilder& builder);

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
     * Refers to a parameter passed to a #TemplateType (via
     * #AppliedType), or #FunctionType.
     */
    class ParameterType : public Type {
    public:
      //std::size_t index() {return m_index;}

    private:
      /// Index of this parameter in the parent context.
      //std::size_t m_index;
    };

    class OpaqueType : public TemplateType {
    public:
      static OpaqueType* create(Context *context, std::size_t n_parameters);
      void unify(TemplateType *t);

    private:
      struct Initializer;
      OpaqueType(const UserInitializer& ui, Context *context, std::size_t n_parameters);
      virtual bool constant_for(Term *const* parameters);
      virtual LLVMBuilderValue build_llvm_value(LLVMBuilder& builder, AppliedType *applied);
      virtual LLVMBuilderType build_llvm_type(LLVMBuilder& builder, AppliedType *applied);
    };

    class PrimitiveType : public Type { 
    protected:
      PrimitiveType(const UserInitializer& ui, Context *context);

    private:
      virtual LLVMBuilderValue build_llvm_value(LLVMBuilder& builder);
    };

    /**
     * \brief Empty type.
     *
     * This type has size zero and alignment one.
     */
    class EmptyType : public PrimitiveType {
    public:

    private:
      friend class Context;
      struct Initializer;
      static EmptyType* create(Context *context);
      EmptyType(const UserInitializer& ui, Context *context);

      virtual LLVMBuilderType build_llvm_type(LLVMBuilder&);
    };

    class LabelType : public PrimitiveType {
    public:

    private:
      friend class Context;
      struct Initializer;
      static LabelType* create(Context *context);
      LabelType(const UserInitializer& ui, Context *context);

      virtual LLVMBuilderType build_llvm_type(LLVMBuilder&);
    };

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
       * \brief Create a \c FunctionType object.
       */
      static FunctionType* create(Context *context,
				  std::size_t n_quantified, TermType *const* quantified,
				  std::size_t n_regular, TermType *const* regular);

      /**
       * \brief Get the result type.
       *
       * This returns a #Type rather than a #TermType since a function
       * always returns a #Value.
       */
      TermType *result() {return use_get<TermType>(slot_result);}

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
      class Initializer;
      FunctionType(const UserInitializer& ui, Context *context,
		   std::size_t n_quantified, TermType *const* quantified,
		   std::size_t n_regular, TermType *const* regular);

      std::size_t m_n_quantified;
      std::size_t m_n_regular;

      virtual LLVMBuilderValue build_llvm_value(LLVMBuilder& builder);
      virtual LLVMBuilderType build_llvm_type(LLVMBuilder& builder);
    };
  }
}

#endif

#endif
