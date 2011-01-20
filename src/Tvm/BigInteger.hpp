#ifndef HPP_PSI_TVM_BIGINTEGER
#define HPP_PSI_TVM_BIGINTEGER

#include <stdint.h>
#include <boost/optional.hpp>

#include "../Utility.hpp"

namespace Psi {
  namespace Tvm {
    /**
     * \brief A wide integer class for integers in two's complement representation
     * with a fixed number of bits.
     */
    class BigInteger {
      /**
       * Word type used to store large integers. This cannot be changed freely,
       * the implementation assumes that this type is at least as large as an
       * <tt>unsigned int</tt>.
       */
      typedef uint64_t WordType;
      
      unsigned m_bits;
      /// \brief Array of words, least significant word first (little endian)
      SmallArray<WordType, 2> m_words;
      
      static unsigned words_for_bits(unsigned bits);
      WordType mask() const;
      
      void unary_resize(const BigInteger&);
      void binary_resize(const BigInteger&, const BigInteger&);
      void divide_internal(BigInteger&, BigInteger&);
      
    public:
      BigInteger();
      BigInteger(unsigned bits, unsigned value=0);
      BigInteger(unsigned bits, int value);

      /**
       * Numer of words in the internal array. Just to facilitate LLVM
       * APInt creation, do not use anywhere else.
       */
      unsigned num_words() const {return m_words.size();}
      
      /**
       * Direct access to the internal array. Just to facilitate LLVM
       * APInt creation, do not use anywhere else.
       */
      const uint64_t* words() const {return m_words.get();}
      
      void parse(const std::string&, bool=false, unsigned=10);
      void assign(unsigned);
      void assign(int);
      
      /// \brief The number of bits in this integer.
      unsigned bits() const {return m_bits;}
      
      void resize(unsigned bits, bool sign_extend);
      
      bool sign_bit() const;
      bool zero() const;
      
      bool is_max(bool for_signed) const;
      bool is_min(bool for_signed) const;
      
      void add(const BigInteger&, const BigInteger&);
      void subtract(const BigInteger&, const BigInteger&);
      void multiply(const BigInteger&, const BigInteger&);
      void divide_unsigned(const BigInteger&, const BigInteger&);
      void divide_signed(const BigInteger&, const BigInteger&);
      void negative(const BigInteger&);
      
      void bit_and(const BigInteger&, const BigInteger&);
      void bit_or(const BigInteger&, const BigInteger&);
      void bit_xor(const BigInteger&, const BigInteger&);
      void bit_not(const BigInteger&);
      
      void shl(const BigInteger&, unsigned);
      void ashr(const BigInteger&, unsigned);
      void lshr(const BigInteger&, unsigned);
      void shr(const BigInteger&, unsigned, bool);
      
      int cmp_signed(const BigInteger&) const;
      int cmp_unsigned(const BigInteger&) const;
      
      unsigned log2_unsigned() const;
      unsigned log2_signed() const;
      
      bool operator == (const BigInteger& x) const {return (bits() == x.bits()) && (cmp_unsigned(x) == 0);}
      bool operator != (const BigInteger& x) const {return (bits() == x.bits()) && (cmp_unsigned(x) != 0);}
      friend std::size_t hash_value(const BigInteger&);
      
      boost::optional<unsigned> unsigned_value(bool=false) const;
    };
  }
}

#endif
