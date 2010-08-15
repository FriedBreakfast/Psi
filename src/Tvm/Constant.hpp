#ifndef HPP_PSI_TVM_VALUE
#define HPP_PSI_TVM_VALUE

#include "Type.hpp"

namespace Psi {
  namespace Tvm {
    /**
     * \brief Base class for constant values.
     *
     * Note that not all constant Terms
     */
    class ConstantValue : public Value {
    protected:
      ConstantValue(const UserInitializer& ui, Context *context, Type *type);
    };

    class GlobalVariable : public ConstantValue {
    protected:
      enum Slots {
	slot_initializer=Value::slot_max,
	slot_max
      };

    public:
      /** \brief Whether this global will be placed in read-only memory. */
      bool read_only() const {return m_read_only;}
      Term *initializer() const {return use_get<Term>(slot_initializer);}
      void set_initializer(Term *v);

      static GlobalVariable* create(TermType *type, bool read_only, Term *initializer=0);

    private:
      bool m_read_only;

      class Initializer;
      GlobalVariable(const UserInitializer& ui, Context *context, TermType *type, bool read_only, Term *initializer);
      virtual const llvm::Value* build_llvm_value(LLVMBuilder& builder);
    };

    class ConstantInteger : public ConstantValue {
    public:
      const mpz_class& value() {return m_value;}

      static ConstantInteger* create(IntegerType *type, const mpz_class& value);

    private:
      class Initializer;
      ConstantInteger(const UserInitializer& ui, Context *context, IntegerType *type, const mpz_class& value);
      virtual const llvm::Value* build_llvm_value(LLVMBuilder&);

      mpz_class m_value;
    };

    class ConstantReal : public ConstantValue {
    public:
      const mpf_class& value() {return m_value;}

      static ConstantReal* create(RealType *type, const mpf_class& value);

    private:
      class Initializer;
      ConstantReal(const UserInitializer& ui, Context *context, RealType *type, const mpf_class& value);
      virtual const llvm::Value* build_llvm_value(LLVMBuilder&);

      mpf_class m_value;
    };

    class ConstantArray : public ConstantValue {
    protected:
      enum Slots {
	slot_value_base=ConstantValue::slot_max
      };

    public:
      std::size_t length() {return use_slots() - slot_value_base;}
      Value *element_value(std::size_t n) {return use_get<Value>(slot_value_base+n);}
      Type *element_type() {return applied_type()->array_element_type();}
    };

    class ConstantStruct : public ConstantValue {
    protected:
      enum Slots {
	slot_member_base=ConstantValue::slot_max
      };

    public:
      Value *member_value(std::size_t n) {return use_get<Value>(slot_member_base+n);}

      static ConstantValue* create(Context& context, const std::vector<Term*>& values);
      static ConstantValue* create(Context& context, std::size_t n_elements, ConstantValue *const * values);

      static ConstantValue* create(Context& context, Term*);
    };

    class ConstantUnion : public ConstantValue {
    protected:
      enum Slots {
	slot_member_value=ConstantValue::slot_max
      };

    public:
      int which() {return m_which;}
      Type *value_type() {return applied_type()->member_type(m_which);}
      Value *value() {return use_get<Value>(slot_member_value);}

    private:
      int m_which;
    };
  }
}

#endif
