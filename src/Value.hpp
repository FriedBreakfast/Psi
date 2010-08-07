#ifndef HPP_PSI_VALUE
#define HPP_PSI_VALUE

#include "Type.hpp"

namespace Psi {
  class ConstantValue;

  class GlobalVariable : public Value {
  protected:
    enum Slots {
      slot_value=Value::slot_max,
    };

  public:
    Value *initializer() {return use_get<Value>(slot_value);}
    void set_initializer(ConstantValue *v);
  };

  class ConstantValue : public Value {
  };

  class ConstantInteger : public ConstantValue {
  public:
    const mpz_class& value() {return m_value;}

  private:
    ConstantInteger(const mpz_class& value);

    mpz_class m_value;
  };

  class ConstantReal : public ConstantValue {
  public:
    const mpf_class& value() {return m_value;}

  private:
    ConstantReal(const mpf_class& value);

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

#endif
