#ifndef HPP_PSI_TVM_LLVMBUILDER
#define HPP_PSI_TVM_LLVMBUILDER

#include <deque>
#include <tr1/unordered_map>

#include "LLVMForward.hpp"
#include "../Utility.hpp"

namespace Psi {
  namespace Tvm {
    class Term;
    class LLVMBuilderInvoker;

    class LLVMConstantBuilder {
    public:
      class Type {
	friend class LLVMConstantBuilder;

	enum Category {
	  /**
	   * Default constructor marker - means this result object is
	   * not valid.
	   */
	  category_invalid,
	  /**
	   * \brief Unknown type.
	   *
	   * Not enough information about the type is known at compile
	   * time to produce a full LLVM representation. Such types are
	   * stored as <tt>i8*</tt> to \c alloca memory when loaded onto
	   * the stack. See LLVMValueType::local_unknown.
	   */
	  category_unknown,
	  /**
	   * \brief Known type.
	   *
	   * Enough information is known to produce an accurate LLVM
	   * representation. This means the type (or members of the type
	   * if it is an aggregate) is known exactly, except for
	   * pointers which are always <tt>i8*</tt>.
	   */
	  category_known,
	  /**
	   * \brief A type with no data.
	   *
	   * LLVM has no representation of such a type, so special
	   * handling is needed.
	   */
	  category_empty
	};

      public:
	Type() : m_category(category_invalid), m_type(0) {}
	bool valid() const {return m_category != category_invalid;}
	bool empty() const {return m_category == category_empty;}
	bool known() const {return m_category == category_known;}
	bool unknown() const {return m_category == category_unknown;}

	llvm::Type *type() const {return m_type;}

      private:
	Type(Category category, llvm::Type *type) : m_category(category), m_type(type) {}
	Category m_category;
	llvm::Type *m_type;
      };

      static Type type_known(llvm::Type *ty) {return Type(Type::category_known, ty);}
      static Type type_unknown() {return Type(Type::category_unknown, 0);}
      static Type type_empty() {return Type(Type::category_empty, 0);}

      class Constant {
	friend class LLVMConstantBuilder;

	enum Category {
	  category_invalid,
	  category_empty,
	  category_known
	};

      public:
	Constant() : m_category(category_invalid), m_value(0) {}

	bool valid() const {return m_category != category_invalid;}
	bool empty() const {return m_category == category_empty;}
	bool known() const {return m_category == category_known;}
	llvm::Constant* value() const {return m_value;}

      private:
	Constant(Category category, llvm::Constant *value) : m_category(category), m_value(value) {}
	Category m_category;
	llvm::Constant *m_value;
      };

      static Constant constant_empty() {return Constant(Constant::category_empty, 0);}
      static Constant constant_value(llvm::Constant *value) {return Constant(Constant::category_known, value);}

      LLVMConstantBuilder(llvm::LLVMContext *context, llvm::Module *module);
      LLVMConstantBuilder(const LLVMConstantBuilder *parent);
      ~LLVMConstantBuilder();

      llvm::LLVMContext& context() {return *m_context;}
      llvm::Module& module() {return *m_module;}
      llvm::GlobalValue* global(Term *term);
      Constant constant(Term *term);
      Type type(Term *term);

      void set_module(llvm::Module *module);

    private:
      const LLVMConstantBuilder *m_parent;
      llvm::LLVMContext *m_context;
      llvm::Module *m_module;

      typedef std::tr1::unordered_map<Term*, Type> TypeTermMap;
      TypeTermMap m_type_terms;

      typedef std::tr1::unordered_map<Term*, Constant> ConstantTermMap;
      ConstantTermMap m_constant_terms;

      /// Global terms which have been encountered but not yet built
      typedef std::deque<std::pair<Term*, llvm::GlobalValue*> > GlobalTermBuildList;
      GlobalTermBuildList m_global_build_list;

      typedef std::tr1::unordered_map<Term*, llvm::GlobalValue*> GlobalTermMap;
      GlobalTermMap m_global_terms;
    };

    class LLVMFunctionBuilder {
    public:
      typedef llvm::IRBuilder<true, llvm::ConstantFolder, llvm::IRBuilderDefaultInserter<true> > IRBuilder;

      class Result {
	friend class LLVMFunctionBuilder;

	enum Category {
	  category_invalid,
	  category_known,
	  category_unknown,
	  category_empty
	};

      public:
	Result() : m_category(category_invalid), m_value(0) {}
	Result(const LLVMConstantBuilder::Constant& src);

	bool valid() const {return m_category != category_invalid;}
	bool known() const {return m_category == category_known;}
	bool unknown() const {return m_category == category_unknown;}
	bool empty() const {return m_category == category_empty;}
	llvm::Value *value() const {return m_value;}

      private:
	Result(Category category, llvm::Value *value) : m_category(category), m_value(value) {}
	static Category from_const_category(const LLVMConstantBuilder::Constant& r);

	Category m_category;
	llvm::Value *m_value;
      };

      static Result make_known(llvm::Value *value) {return Result(Result::category_known, value);}
      static Result make_unknown(llvm::Value *value) {return Result(Result::category_unknown, value);}
      static Result make_empty() {return Result(Result::category_empty, 0);}

      LLVMFunctionBuilder(LLVMConstantBuilder *constant_builder, IRBuilder *irbuilder);
      ~LLVMFunctionBuilder();

      Result value(Term *term);
      LLVMConstantBuilder::Type type(Term *term);
      llvm::LLVMContext& context() {return m_constant_builder->context();}
      IRBuilder& irbuilder() {return *m_irbuilder;}

    private:
      LLVMConstantBuilder *m_constant_builder;
      IRBuilder *m_irbuilder;
      typedef std::tr1::unordered_map<Term*, Result> TermMap;
      TermMap m_terms;
    };
  }
}

#endif
