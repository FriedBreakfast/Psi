#include "BigInteger.hpp"

#include <stdexcept>
#include <vector>

namespace Psi {
  namespace Tvm {
    BigInteger::BigInteger() : m_value(0) {
    }

    BigInteger::BigInteger(long v) : m_value(0) {
      if ((v >= small_min()) && (v <= small_max()))
        set_no_mp(v);
      else
        set_mp_si(v);
    }

    BigInteger::BigInteger(const BigInteger& o) : m_value(0) {
      if (o.using_mp())
        set_mp(o.m_mp_value);
      else
        set_no_mp(o.m_value);
    }

    BigInteger::~BigInteger() {
      if (using_mp())
        mpz_clear(m_mp_value);
    }

    void BigInteger::swap(BigInteger& o) {
      std::swap(m_value, o.m_value);
      std::swap(m_mp_value, o.m_mp_value);
    }

    const BigInteger& BigInteger::operator = (const BigInteger& o) {
      if (o.using_mp()) {
        set_mp(o.m_mp_value);
      } else {
        set_no_mp(o.m_value);
      }

      return *this;
    }

    const BigInteger& BigInteger::operator = (long v) {
      if ((v >= small_min()) && (v <= small_max()))
        set_no_mp(v);
      else
        set_mp_si(v);

      return *this;
    }

    int BigInteger::compare(const BigInteger& o) const {
      if (using_mp()) {
        if (o.using_mp())
          return mpz_cmp(m_mp_value, o.m_mp_value);
        else
          return mpz_sgn(m_mp_value);
      } else {
        if (o.using_mp())
          return -mpz_sgn(o.m_mp_value);
        else
          return m_value < o.m_value ? -1 : m_value > o.m_value ? 1 : 0;
      }
    }

    void BigInteger::ensure_mp() {
      if (!using_mp()) {
        m_value = magic();
        mpz_init(m_mp_value);
      }
    }

    void BigInteger::set_no_mp(long v) {
      PSI_ASSERT(v != magic());
      if (using_mp())
        mpz_clear(m_mp_value);
      m_value = v;
    }

    void BigInteger::set_mp_si(long v) {
      if (using_mp()) {
        mpz_set_si(m_mp_value, v);
      } else {
        mpz_init_set_si(m_mp_value, v);
        m_value = magic();
      }
    }

    void BigInteger::set_mp(const mpz_t v) {
      if (using_mp()) {
        mpz_set(m_mp_value, v);
      } else {
        mpz_init_set(m_mp_value, v);
        m_value = magic();
      }
    }

    void BigInteger::simplify() {
      PSI_ASSERT(using_mp());
      if (!mpz_fits_slong_p(m_mp_value))
        return;

      long x =  mpz_get_si(m_mp_value);
      if (x == magic())
        return;

      mpz_clear(m_mp_value);
      m_value = x;
    }

    /**
     * Used to implement arithmetic operations when the MP function
     * requires one operand to be an mpz_t. This either returns the
     * mpz_t in \c rhs, or sets the one in \c out to the value of \c
     * rhs and returns that.
     */
    const mpz_t *BigInteger::rhs_to_mp(BigInteger& out, const BigInteger& rhs) {
      if (!rhs.using_mp()) {
        mpz_set_si(out.m_mp_value, rhs.m_value);
        return &out.m_mp_value;
      } else {
        return &rhs.m_mp_value;
      }
    }

    void BigInteger::add(BigInteger& out, const BigInteger& lhs, const BigInteger& rhs) {
      if (!lhs.using_mp() && !rhs.using_mp()) {
        bool overflow_high = (lhs.m_value > 0) && (rhs.m_value > 0) && (small_max() - lhs.m_value < rhs.m_value);
        bool overflow_low = (lhs.m_value < 0) && (rhs.m_value < 0) && (small_min() - lhs.m_value > rhs.m_value);
        if (!overflow_high && !overflow_low) {
          out.set_no_mp(lhs.m_value + rhs.m_value);
          return;
        }
      }

      out.ensure_mp();
      if (lhs.using_mp()) {
        if (rhs.using_mp())
          mpz_add(out.m_mp_value, lhs.m_mp_value, rhs.m_mp_value);
        else if (rhs.m_value >= 0)
          mpz_add_ui(out.m_mp_value, lhs.m_mp_value, rhs.m_value);
        else
          mpz_sub_ui(out.m_mp_value, lhs.m_mp_value, std::abs(rhs.m_value));
      } else {
        const mpz_t *rhs_mpz = rhs_to_mp(out, rhs);

        if (lhs.m_value >= 0)
          mpz_add_ui(out.m_mp_value, *rhs_mpz, lhs.m_value);
        else
          mpz_ui_sub(out.m_mp_value, std::abs(lhs.m_value), *rhs_mpz);
      }

      out.simplify();
    }

    void BigInteger::sub(BigInteger& out, const BigInteger& lhs, const BigInteger& rhs) {
      if (!lhs.using_mp() && !rhs.using_mp()) {
        bool overflow_high = (lhs.m_value > 0) && (rhs.m_value < 0) && (small_max() + rhs.m_value < lhs.m_value);
        bool overflow_low = (lhs.m_value < 0) && (rhs.m_value > 0) && (small_min() - lhs.m_value + rhs.m_value > 0);
        if (!overflow_high && !overflow_low) {
          out.set_no_mp(lhs.m_value - rhs.m_value);
          return;
        }
      }

      out.ensure_mp();
      if (lhs.using_mp()) {
        if (rhs.using_mp())
          mpz_sub(out.m_mp_value, lhs.m_mp_value, rhs.m_mp_value);
        else if (rhs.m_value >= 0)
          mpz_sub_ui(out.m_mp_value, lhs.m_mp_value, rhs.m_value);
        else
          mpz_add_ui(out.m_mp_value, lhs.m_mp_value, std::abs(rhs.m_value));
      } else {
        const mpz_t *rhs_mpz = rhs_to_mp(out, rhs);

        if (lhs.m_value >= 0) {
          mpz_ui_sub(out.m_mp_value, lhs.m_value, *rhs_mpz);
        } else {
          mpz_add_ui(out.m_mp_value, *rhs_mpz, std::abs(lhs.m_value));
          mpz_neg(out.m_mp_value, out.m_mp_value);
        }
      }

      out.simplify();
    }

    void BigInteger::mul(BigInteger& out, const BigInteger& lhs, const BigInteger& rhs) {
      if (!lhs.using_mp() && !rhs.using_mp()) {
        long min_lhs_rhs = std::min(std::abs(lhs.m_value), std::abs(rhs.m_value));
        long max_lhs_rhs = std::min(std::abs(lhs.m_value), std::abs(rhs.m_value));
        if (small_max() / min_lhs_rhs < max_lhs_rhs + 1) {
          out.set_no_mp(lhs.m_value * rhs.m_value);
          return;
        }
      }

      out.ensure_mp();
      if (lhs.using_mp()) {
        if (rhs.using_mp())
          mpz_mul(out.m_mp_value, lhs.m_mp_value, rhs.m_mp_value);
        else
          mpz_mul_si(out.m_mp_value, lhs.m_mp_value, rhs.m_value);
      } else {
        const mpz_t *rhs_mpz = rhs_to_mp(out, rhs);
        mpz_mul_si(out.m_mp_value, *rhs_mpz, lhs.m_value);
      }

      out.simplify();
    }

    void BigInteger::div(BigInteger& out, const BigInteger& lhs, const BigInteger& rhs) {
      if (!lhs.using_mp()) {
        if (!rhs.using_mp())
          out.set_no_mp(lhs.m_value / rhs.m_value);
        else
          out.set_no_mp(0);
        return;
      } else {
        out.ensure_mp();
        if (rhs.using_mp())
          mpz_tdiv_q(out.m_mp_value, lhs.m_mp_value, rhs.m_mp_value);
        else if (rhs.m_value > 0)
          mpz_tdiv_q_ui(out.m_mp_value, lhs.m_mp_value, rhs.m_value);
        else {
          mpz_tdiv_q_ui(out.m_mp_value, lhs.m_mp_value, std::abs(rhs.m_value));
          mpz_neg(out.m_mp_value, out.m_mp_value);
        }

        out.simplify();
      }
    }

    void BigInteger::rem(BigInteger& out, const BigInteger& lhs, const BigInteger& rhs) {
      if (!lhs.using_mp()) {
        if (!rhs.using_mp())
          out.set_no_mp(lhs.m_value % rhs.m_value);
        else
          out.set_no_mp(lhs.m_value);
        return;
      } else {
        out.ensure_mp();
        if (rhs.using_mp())
          mpz_tdiv_r(out.m_mp_value, lhs.m_mp_value, rhs.m_mp_value);
        else
          mpz_tdiv_r_ui(out.m_mp_value, lhs.m_mp_value, std::abs(rhs.m_value));

        out.simplify();
      }
    }

    template<typename T>
    void BigInteger::bit_op(BigInteger& out, const BigInteger& lhs, const BigInteger& rhs, T op, void (*mpz_op)(mpz_t,const mpz_t,const mpz_t)) {
      if (!lhs.using_mp() && !rhs.using_mp()) {
        long l = op(lhs.m_value, rhs.m_value);
        if (l == magic())
          out.set_mp_si(l);
        else
          out.set_no_mp(l);
      }

      out.ensure_mp();
      if (lhs.using_mp()) {
        if (rhs.using_mp())
          mpz_op(out.m_mp_value, lhs.m_mp_value, rhs.m_mp_value);
        else {
          mpz_set_si(out.m_mp_value, rhs.m_value);
          mpz_op(out.m_mp_value, out.m_mp_value, lhs.m_mp_value);
        }
      } else {
        PSI_ASSERT(rhs.using_mp());
        mpz_set_si(out.m_mp_value, lhs.m_value);
        mpz_op(out.m_mp_value, out.m_mp_value, rhs.m_mp_value);
      }

      out.simplify();
    }

    void BigInteger::bit_and(BigInteger& out, const BigInteger& lhs, const BigInteger& rhs) {
      bit_op(out, lhs, rhs, std::bit_and<long>(), mpz_and);
    }

    void BigInteger::bit_or(BigInteger& out, const BigInteger& lhs, const BigInteger& rhs) {
      bit_op(out, lhs, rhs, std::bit_or<long>(), mpz_ior);
    }

    void BigInteger::bit_xor(BigInteger& out, const BigInteger& lhs, const BigInteger& rhs) {
      bit_op(out, lhs, rhs, std::bit_xor<long>(), mpz_xor);
    }

    void BigInteger::bit_com(BigInteger& out, const BigInteger& in) {
      if (!in.using_mp()) {
        out.set_no_mp(~in.m_value);
      } else {
        out.ensure_mp();
        mpz_com(out.m_mp_value, in.m_mp_value);
      }
    }

    /**
     * Implementation of various get_str functions - I would like to
     * have a function for an arbitrary base but the standard
     * library only supports octal, decimal and hex.
     */
    template<typename T>
    std::string BigInteger::get_str_impl(int base, T manip) const {
      if (using_mp()) {
        std::vector<char> s(mpz_sizeinbase(m_mp_value, base) + 2);
        mpz_get_str(&s[0], base, m_mp_value);
        return std::string(s.begin(), s.end());
      } else {
        std::stringstream ss;
        ss << manip << m_value;
        return ss.str();
      }
    }

    std::string BigInteger::get_str() const {
      return get_str_impl(10, std::dec);
    }

    std::string BigInteger::get_str_hex() const {
      return get_str_impl(16, std::hex);
    }

    std::string BigInteger::get_str_oct() const {
      return get_str_impl(8, std::oct);
    }

    void BigInteger::set_str_impl(int base, const char *s) {
      ensure_mp();
      if (mpz_set_str(m_mp_value, s, base) != 0)
        throw std::invalid_argument("integer string value not valid");
      simplify();
    }

    void BigInteger::set_str(const std::string& s) {
      set_str_impl(10, s.c_str());
    }

    void BigInteger::set_mp_swap(mpz_t mp) {
      ensure_mp();
      mpz_swap(m_mp_value, mp);
      simplify();
    }

    std::size_t BigInteger::hash() const {
      if (using_mp())
        return mpz_get_ui(m_mp_value);
      else
        return m_value;
    }
  }
}
