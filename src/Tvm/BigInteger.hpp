#ifndef HPP_PSI_TVM_BIGINTEGER
#define HPP_PSI_TVM_BIGINTEGER

#include <functional>
#include <limits>
#include <sstream>

#include <gmp.h>

#include "../Utility.hpp"

namespace Psi {
  namespace Tvm {
    class BigInteger {
      typedef void (BigInteger::*SafeBoolType)() const;
      void safe_bool_true() const {}

    public:
      BigInteger();
      BigInteger(long v);
      BigInteger(const BigInteger& o);
      ~BigInteger();
      void swap(BigInteger& o);
      const BigInteger& operator = (const BigInteger& o);
      const BigInteger& operator = (long v);
      int compare(const BigInteger& o) const;
      static void add(BigInteger& out, const BigInteger& lhs, const BigInteger& rhs);
      static void sub(BigInteger& out, const BigInteger& lhs, const BigInteger& rhs);
      static void mul(BigInteger& out, const BigInteger& lhs, const BigInteger& rhs);
      static void div(BigInteger& out, const BigInteger& lhs, const BigInteger& rhs);
      static void rem(BigInteger& out, const BigInteger& lhs, const BigInteger& rhs);

    private:
      template<typename T> static void bit_op(BigInteger& out, const BigInteger& lhs, const BigInteger& rhs, T op, void (*mpz_op)(mpz_t,const mpz_t,const mpz_t));

    public:
      static void bit_and(BigInteger& out, const BigInteger& lhs, const BigInteger& rhs);
      static void bit_or(BigInteger& out, const BigInteger& lhs, const BigInteger& rhs);
      static void bit_xor(BigInteger& out, const BigInteger& lhs, const BigInteger& rhs);
      static void bit_com(BigInteger& out, const BigInteger& in);

#define PSI_TVM_BIGINT_OP(op,fun)                                       \
      const BigInteger& operator op (const BigInteger& o) {fun(*this, *this, o); return *this;} \
      const BigInteger& operator op (long o) {fun(*this, *this, o); return *this;}

      PSI_TVM_BIGINT_OP(+=,add)
      PSI_TVM_BIGINT_OP(-=,sub)
      PSI_TVM_BIGINT_OP(*=,mul)
      PSI_TVM_BIGINT_OP(/=,div)
      PSI_TVM_BIGINT_OP(%=,rem)
      PSI_TVM_BIGINT_OP(&=,bit_and)
      PSI_TVM_BIGINT_OP(|=,bit_or)
      PSI_TVM_BIGINT_OP(^=,bit_xor)

#undef PSI_TVM_BIGINT_OP

      BigInteger operator ~ () const {BigInteger x; bit_com(x, *this); return x;}
      operator SafeBoolType () const {return m_value ? &BigInteger::safe_bool_true : 0;}
      bool operator ! () const {return m_value == 0;}

      std::string get_str() const;
      std::string get_str_hex() const;
      std::string get_str_oct() const;

      void set_str(const std::string& s);

      bool using_mp() const {return m_value == magic();}
      long value_long() const {PSI_ASSERT(!using_mp()); return m_value;}
      const mpz_t& value_mp() const {PSI_ASSERT(using_mp()); return m_mp_value;}
      void set_mp_swap(mpz_t mp);
      std::size_t hash() const;

    private:
      static long small_max() {return std::numeric_limits<long>::max();}
      static long small_min() {return 1 + std::numeric_limits<long>::min();}
      static long magic() {return std::numeric_limits<long>::min();}

      void ensure_mp();
      void set_no_mp(long v);
      void set_mp_si(long v);
      void set_mp(const mpz_t v);
      void simplify();
      static const mpz_t *rhs_to_mp(BigInteger& out, const BigInteger& rhs);
      template<typename T> std::string get_str_impl(int base, T manip) const;
      void set_str_impl(int base, const char *s);

      long m_value;
      mpz_t m_mp_value;
    };

#define PSI_TVM_BIGINT_CMP(op)                                          \
    inline bool operator op (const BigInteger& lhs, const BigInteger& rhs) {return lhs.compare(rhs) op 0;} \
    inline bool operator op (long lhs, const BigInteger& rhs) {return 0 op rhs.compare(lhs);} \
    inline bool operator op (const BigInteger& lhs, long rhs) {return lhs.compare(rhs) op 0;}

    PSI_TVM_BIGINT_CMP(==)
    PSI_TVM_BIGINT_CMP(!=)
    PSI_TVM_BIGINT_CMP(<=)
    PSI_TVM_BIGINT_CMP(<)
    PSI_TVM_BIGINT_CMP(>=)
    PSI_TVM_BIGINT_CMP(>)

#undef PSI_TVM_BIGINT_CMP

#define PSI_TVM_BIGINT_OP(op,fun)                                       \
    inline BigInteger operator op (const BigInteger& lhs, const BigInteger& rhs) {BigInteger x; BigInteger::fun(x, lhs, rhs); return x;} \
    inline BigInteger operator op (long lhs, const BigInteger& rhs) {BigInteger x; BigInteger::fun(x, lhs, rhs); return x;} \
    inline BigInteger operator op (const BigInteger& lhs, long rhs) {BigInteger x; BigInteger::fun(x, lhs, rhs); return x;}

    PSI_TVM_BIGINT_OP(+,add)
    PSI_TVM_BIGINT_OP(-,sub)
    PSI_TVM_BIGINT_OP(*,mul)
    PSI_TVM_BIGINT_OP(/,div)
    PSI_TVM_BIGINT_OP(%,rem)
    PSI_TVM_BIGINT_OP(&,bit_and)
    PSI_TVM_BIGINT_OP(|,bit_or)
    PSI_TVM_BIGINT_OP(^,bit_xor)

#undef PSI_TVM_BIGINT_OP
  }
}

#endif
