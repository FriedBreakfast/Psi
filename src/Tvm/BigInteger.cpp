#include "BigInteger.hpp"
#include "Core.hpp"

#include <limits>


namespace Psi {
  namespace Tvm {
    BigInteger::BigInteger() : m_bits(0) {
    }
    
    BigInteger::BigInteger(unsigned bits, unsigned value) : m_bits(0) {
      resize(bits, false);
      assign(value);
    }
    
    BigInteger::BigInteger(unsigned bits, int value) : m_bits(0) {
      resize(bits, false);
      assign(value);
    }

    /**
     * Parse an integer and convert it to the internal byte array
     * format. Note that this function does not parse minus signs or
     * base-specific prefixes such as '0x' - these should be handled
     * externally and the \c negative and \c base parameters set
     * accordingly.
     *
     * Note that this does not currently detect numerical overflows,
     * i.e. numbers which are too large to represent in the number of
     * bytes a number currently uses.
     */
    void BigInteger::parse(const std::string& value, bool negative, unsigned base) {
      std::fill(m_words.get(), m_words.get()+m_words.size(), 0);

      if ((base < 2) || (base > 35))
        throw TvmUserError("Unsupported numerical base, must be between 2 and 35 inclusive");
      
      const unsigned half_word_bits = std::numeric_limits<WordType>::digits / 2;

      for (std::size_t pos = 0; pos < value.size(); ++pos) {
        if (pos > 0) {
          // multiply current value by the base - base is known to be
          // a small number so a simple algorithm can be used.
          WordType carry = 0;
          for (unsigned i = 0; i != m_words.size(); ++i) {
            WordType val = m_words[i]*base + carry;
            carry = ((m_words[i] >> half_word_bits) * base) >> half_word_bits;
            m_words[i] = val;
          }
        }

        unsigned char digit = value[pos], digit_value;
        if ((digit >= '0') && (digit <= '9'))
          digit_value = digit - '0';
        else if ((digit >= 'a') && (digit <= 'z'))
          digit_value = digit - 'a';
        else if ((digit >= 'A') && (digit <= 'A'))
          digit_value = digit - 'A';
        else
          throw TvmUserError("Unrecognised digit in parsing");

        if (digit_value >= base)
          throw TvmUserError("Digit out of range for base");

        WordType carry = digit_value;
        for (unsigned i = 0; i < m_words.size(); ++i) {
          m_words[i] += carry;
          if (m_words[i] > carry)
            break;
          else
            carry = 1;
        }
      }

      if (negative)
        this->negative(*this);
    }
    
    void BigInteger::assign(unsigned value) {
      m_words[0] = value;
      std::fill(m_words.get()+1, m_words.get()+m_words.size(), 0);
      m_words[m_words.size()-1] &= mask();
    }
    
    void BigInteger::assign(int value) {
      if (value >= 0) {
        assign(unsigned(value));
      } else {
        // damn... i'm feeling lazy
        assign(unsigned(-value));
        negative(*this);
      }
    }
    
    std::size_t hash_value(const BigInteger& self) {
      std::size_t h = 0;
      boost::hash_combine(h, self.m_bits);
      boost::hash_range(h, self.m_words.get(), self.m_words.get()+self.m_words.size());
      return h;
    }
      
    /**
     * Check operand size is nonzero and resize this integer.
     */
    void BigInteger::unary_resize(const BigInteger& param) {
      if (param.bits() == 0)
        throw TvmUserError("binary operation attempted on zero bit integer");
      resize(param.bits(), false);
    }

    /**
     * Check that the size of two binary operands are equal and resize this
     * integer to the correct size, in preparation for performing an operation.
     */
    void BigInteger::binary_resize(const BigInteger& lhs, const BigInteger& rhs) {
      if (lhs.bits() != rhs.bits())
        throw TvmUserError("bit width mismatch in large integer arithmetic");
      if (lhs.bits() == 0)
        throw TvmUserError("binary operation attempted on zero bit integer");
      resize(lhs.bits(), false);
    }
    
    /**
     * Get the mask for the high word.
     */
    BigInteger::WordType BigInteger::mask() const {
      return ~WordType(0) >> (m_words.size()*std::numeric_limits<WordType>::digits - m_bits);
    }
    
    /**
     * Get the number of words required to store the specified
     * number of bits.
     */
    unsigned BigInteger::words_for_bits(unsigned bits) {
      return (bits + std::numeric_limits<WordType>::digits - 1) / std::numeric_limits<WordType>::digits;
    }
    
    void BigInteger::resize(unsigned bits, bool sign_extend) {
      if (bits == 0) {
        m_words.resize(0);
        m_bits = 0;
        return;
      }
      
      WordType extend_value = 0;
      if ((m_bits > 0) && (m_bits < bits)) {
        WordType sign_bit = (mask() >> 1) + 1;
        if (sign_extend && (m_words[m_words.size() - 1] & sign_bit)) {
          extend_value = ~WordType(0);
          m_words[m_words.size() - 1] |= ~mask();
        }
      }
      
      m_words.resize(words_for_bits(bits), extend_value);
      m_bits = bits;
      m_words[m_words.size() - 1] &= mask();
    }
    
    bool BigInteger::sign_bit() const {
      if (!m_words.size())
        return false;
      
      unsigned hi_word_shift = m_bits - (m_words.size() - 1)*std::numeric_limits<WordType>::digits - 1;
      return m_words[m_words.size() - 1] & (1 << hi_word_shift);
    }
    
    bool BigInteger::zero() const {
      for (unsigned i = 0; i != m_words.size(); ++i)
        if (m_words[i])
          return false;
      return true;
    }
    
    /**
     * Check whether this integer value is the maximum representable value in
     * the current number of bits for a signed or unsigned value.
     */
    bool BigInteger::is_max(bool for_signed) const {
      for (unsigned i = 1; i != m_words.size(); ++i) {
        if (m_words[i-1] != ~WordType(0))
          return false;
      }
      WordType hi_word = for_signed ? (mask() >> 1) : mask();
      return m_words[m_words.size()-1] == hi_word;
    }
    
    /**
     * Check whether this integer value is the minimum representable value in
     * the current number of bits for a signed or unsigned value.
     */
    bool BigInteger::is_min(bool for_signed) const {
      for (unsigned i = 1; i != m_words.size(); ++i) {
        if (m_words[i-1] != 0)
          return false;
      }
      WordType hi_word = for_signed ? ((mask() >> 1) + 1) : 0;
      return m_words[m_words.size()-1] == hi_word;
    }
    
    void BigInteger::add(const BigInteger& lhs, const BigInteger& rhs) {
      binary_resize(lhs, rhs);

      WordType carry = 0;
      for (unsigned i = 0; i < m_words.size(); ++i) {
        if (carry) {
          WordType x = lhs.m_words[i] + rhs.m_words[i] + 1;
          carry = x <= lhs.m_words[i] ? 1 : 0;
          m_words[i] = x;
        } else {
          WordType x = lhs.m_words[i] + rhs.m_words[i];
          carry = x < lhs.m_words[i] ? 1 : 0;
          m_words[i] = x;
        }
      }
      
      m_words[m_words.size() - 1] &= mask();
    }

    void BigInteger::subtract(const BigInteger& lhs, const BigInteger& rhs) {
      binary_resize(lhs, rhs);
      
      WordType borrow = 0;
      for (unsigned i = 0; i < m_words.size(); ++i) {
        if (borrow) {
          WordType x = lhs.m_words[i] - rhs.m_words[i] - 1;
          borrow = x >= lhs.m_words[i];
          m_words[i] = x;
        } else {
          WordType x = lhs.m_words[i] - rhs.m_words[i];
          borrow = x > lhs.m_words[i];
          m_words[i] = x;
        }
      }
      
      m_words[m_words.size() - 1] &= mask();
    }

    void BigInteger::multiply(const BigInteger& lhs, const BigInteger& rhs) {
      binary_resize(lhs, rhs);
      
      ArrayPtr<WordType> words(m_words);
      bool self_arg = (this == &lhs) || (this == &rhs);
      SmallArray<WordType, 2> tmp_words;
      if (self_arg) {
        tmp_words.resize(m_words.size(), 0);
        words = tmp_words;
      }

      const unsigned half_word_bits = std::numeric_limits<WordType>::digits / 2;
      const WordType half_word_mask = ~WordType(0) >> half_word_bits;
      
      for (unsigned i = 0; i != words.size(); ++i) {
        for (unsigned j = 0, e = words.size() - i; j != e; ++j) {
          WordType lhs_hi = lhs.m_words[i] >> half_word_bits;
          WordType lhs_lo = lhs.m_words[i] & half_word_mask;
          WordType rhs_hi = rhs.m_words[i] >> half_word_bits;
          WordType rhs_lo = rhs.m_words[i] & half_word_mask;
          
          WordType hi_hi = lhs_hi * rhs_hi;
          WordType hi_lo = lhs_hi * rhs_lo + lhs_lo * rhs_hi;
          WordType lo_lo = lhs_lo * rhs_lo;
          
          WordType lo = lo_lo + (hi_lo << half_word_bits);
          WordType hi = hi_hi + (hi_lo >> half_word_bits);
          
          words[i+j] += lo;
          if (j+1 != e)
            words[i+j+1] += hi;
        }
      }
      
      words[m_words.size() - 1] &= mask();
      
      if (self_arg)
        std::copy(tmp_words.get(), tmp_words.get() + tmp_words.size(), m_words.get());
    }
    
    void BigInteger::divide_internal(BigInteger& lhs, BigInteger& rhs) {
      binary_resize(lhs, rhs);
      
      if (rhs.zero())
        throw TvmUserError("cannot divide integer by zero");
      
      const unsigned word_bits = std::numeric_limits<WordType>::digits;
      
      int shift = lhs.log2_unsigned() - rhs.log2_unsigned();
      unsigned word = shift / word_bits;
      WordType bit = 1 << (shift % word_bits);
      
      std::fill(m_words.get(), m_words.get() + m_words.size(), 0);
      
      rhs.shl(rhs, shift);
      while (true) {
        if (lhs.cmp_unsigned(rhs) > 0) {
          lhs.subtract(lhs, rhs);
          m_words[word] |= bit;
        }
        
        if (--shift == 0)
          break;
        
        rhs.lshr(rhs, 1);
        bit >>= 1;
        if (bit == 0) {
          --word;
          bit = WordType(1) << (std::numeric_limits<WordType>::digits - 1);
        }
      }
    }

    void BigInteger::divide_signed(const BigInteger& lhs, const BigInteger& rhs) {
      binary_resize(lhs, rhs);
      
      bool result_sign = lhs.sign_bit() != rhs.sign_bit();
      
      BigInteger lhs_copy, rhs_copy;
      if (lhs.sign_bit())
        lhs_copy.negative(lhs);
      else
        lhs_copy = lhs;
      
      if (rhs.sign_bit())
        rhs_copy.negative(rhs);
      else
        rhs_copy = rhs;
      
      divide_internal(lhs_copy, rhs_copy);
      
      if (result_sign)
        negative(*this);
    }

    void BigInteger::divide_unsigned(const BigInteger& lhs, const BigInteger& rhs) {
      BigInteger lhs_copy(lhs), rhs_copy(rhs);
      divide_internal(lhs_copy, rhs_copy);
    }

    void BigInteger::negative(const BigInteger& src) {
      unary_resize(src);

      unsigned i = 0;
      for (; (i < m_words.size()) && (src.m_words[i] == 0); ++i)
        m_words[i] = 0;
      
      if (i < m_words.size()) {
        m_words[i] = -src.m_words[i];
        for (++i; i < m_words.size(); ++i)
          m_words[i] = ~src.m_words[i];
      }
    }
    
    void BigInteger::bit_and(const BigInteger& lhs, const BigInteger& rhs) {
      binary_resize(lhs, rhs);
      for (unsigned i = 0; i < m_words.size(); ++i)
        m_words[i] = lhs.m_words[i] & rhs.m_words[i];
    }

    void BigInteger::bit_or(const BigInteger& lhs, const BigInteger& rhs) {
      binary_resize(lhs, rhs);
      for (unsigned i = 0; i < m_words.size(); ++i)
        m_words[i] = lhs.m_words[i] | rhs.m_words[i];
    }

    void BigInteger::bit_xor(const BigInteger& lhs, const BigInteger& rhs) {
      binary_resize(lhs, rhs);
      for (unsigned i = 0; i < m_words.size(); ++i)
        m_words[i] = lhs.m_words[i] ^ rhs.m_words[i];
    }

    void BigInteger::bit_not(const BigInteger& src) {
      unary_resize(src);
      for (unsigned i = 0; i < m_words.size(); ++i)
        m_words[i] = ~src.m_words[i];
    }
    
    void BigInteger::shl(const BigInteger& src, unsigned count) {
      unsigned shift_words = count / std::numeric_limits<WordType>::digits;
      unsigned shift_bits = count - shift_words*std::numeric_limits<WordType>::digits;
      unsigned in_shift_bits = std::numeric_limits<WordType>::digits - shift_bits;
      
      for (unsigned i = m_words.size() - 1; i != shift_words; --i)
        m_words[i] = (src.m_words[i-shift_words] << shift_bits) | (src.m_words[i-shift_words-1] >> in_shift_bits);
      m_words[shift_words] = src.m_words[0] << shift_bits;
      for (unsigned i = 0; i != shift_words; ++i)
        m_words[i] = 0;
    }
    
    void BigInteger::ashr(const BigInteger& src, unsigned count) {
      shr(src, count, true);
    }
    
    void BigInteger::lshr(const BigInteger& src, unsigned count) {
      shr(src, count, false);
    }
    
    /**
     * \param arithmetic Whether negative integers remain negative.
     */
    void BigInteger::shr(const BigInteger& src, unsigned count, bool arithmetic) {
      unsigned shift_words = count / std::numeric_limits<WordType>::digits;
      unsigned shift_bits = count - shift_words*std::numeric_limits<WordType>::digits;
      unsigned in_shift_bits = std::numeric_limits<WordType>::digits - shift_bits;
      
      WordType high_bits = 0;
      WordType high_word = 0;
      if (arithmetic && sign_bit()) {
        high_bits = ~WordType(0) << in_shift_bits;
        high_word = ~WordType(0);
      }
      
      for (unsigned i = shift_words + 1; i != m_words.size(); ++i)
        m_words[i-shift_words-1] = (src.m_words[i-1] >> shift_bits) | (src.m_words[i] << in_shift_bits);
      m_words[m_words.size() - shift_words - 1] = (src.m_words[m_words.size() - 1] >> shift_bits) | high_bits;
      for (unsigned i = m_words.size() - shift_words; i != m_words.size(); ++i)
        m_words[i] = high_word;
      m_words[m_words.size() - 1] &= mask();
    }

    int BigInteger::cmp_signed(const BigInteger& other) const {
      if (m_words.size() != other.m_words.size())
        throw TvmUserError("cannot compare integers of different sizes");
      
      if (sign_bit()) {
        if (!other.sign_bit())
          return -1;
      } else {
        if (other.sign_bit())
          return 1;
      }
      
      return cmp_unsigned(other);      
    }
    
    int BigInteger::cmp_unsigned(const BigInteger& other) const {
      if (m_words.size() != other.m_words.size())
        throw TvmUserError("cannot compare integers of different sizes");
      
      for (unsigned i = m_words.size(); i != 0; --i) {
        WordType x = m_words[i-1], y = other.m_words[i-1];
        if (x < y)
          return -1;
        else if (x > y)
          return 1;
      }
      
      return 0;
    }
    
    /**
     * \brief Return the index of the leftmost set bit.
     * 
     * If the number is zero, return zero, one return one, two or three
     * return two, between four and seven return three etc.
     */
    unsigned BigInteger::log2_unsigned() const {
      for (unsigned i = m_words.size(); i != 0; --i) {
        WordType x = m_words[i-1];
        if (x) {
          unsigned r = (i-1) * std::numeric_limits<WordType>::digits;
          for (; x > 0; x >>= 1, ++r);
          return r;
        }
      }
      
      return 0;
    }
    
    /**
     * \brief Return the index of the leftmost nontrivial bit.
     * 
     * This should equal abs(*this) followed by log2_unsigned().
     */
    unsigned BigInteger::log2_signed() const {
      WordType trivial = sign_bit() ? ~WordType(0) : 0;
      
      for (unsigned i = m_words.size(); i != 0; --i) {
        WordType x = m_words[i-1];
        WordType cur_trivial = (i == m_words.size()) ? (trivial & mask()) : trivial;
        if (x != cur_trivial) {
          x ^= cur_trivial;
          unsigned r = (i-1) * std::numeric_limits<WordType>::digits;
          for (; x > 0; x >>= 1, ++r);
          return r;
        }
      }
      
      return 0;
    }
    
    /**
     * \brief Convert a BigInteger instance to an unsigned integer.
     * 
     * \param is_signed Whether the contents of \c data should be
     * treated as signed or unsigned.
     * 
     * \return Converted value, or \c boost::none if the converted
     * value is not within the range of an <tt>unsigned int</tt>.
     */
    boost::optional<unsigned> BigInteger::unsigned_value(bool is_signed) const {
      if (is_signed && sign_bit())
        // value is negative, so out of range of an unsigned int
        return boost::none;
      
      for (unsigned i = 1; i < m_words.size(); ++i) {
        if (m_words[i])
          return boost::none;
      }
      
      if (m_words[0] > std::numeric_limits<unsigned>::max())
        return boost::none;
      
      return unsigned(m_words[0]);
    }
  }
}