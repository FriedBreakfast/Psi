#ifndef HPP_PSI_PARSER_UTILITY
#define HPP_PSI_PARSER_UTILITY

#include "Array.hpp"
#include "Utility.hpp"

namespace Psi {
  template<typename YYS, typename YYV, typename YYL>
  class BisonHelper {
  public:
    BisonHelper() : m_stack_size(0) {
    }

    void overflow(YYS **yysp, std::size_t yyssize,
                  YYV **yyvp, std::size_t yyvsize,
                  YYL **yylp, std::size_t yylsize,
                  std::size_t *yystacksize) {

      std::size_t new_stack_size;

      if (m_stack_size) {
        PSI_ASSERT(&m_yys[0] == *yysp);
        PSI_ASSERT(&m_yyv[0] == *yyvp);
        PSI_ASSERT(&m_yyl[0] == *yylp);
        PSI_ASSERT(*yystacksize == m_stack_size);

        new_stack_size = m_stack_size * 2;
      } else {
        new_stack_size = *yystacksize * 2;
      }

      UniqueArray<YYS> new_yys(new_stack_size);
      UniqueArray<YYV> new_yyv(new_stack_size);
      UniqueArray<YYL> new_yyl(new_stack_size);

      std::copy(*yysp, *yysp + yyssize / sizeof(**yysp), new_yys.get());
      std::copy(*yyvp, *yyvp + yyvsize / sizeof(**yyvp), new_yyv.get());
      std::copy(*yylp, *yylp + yylsize / sizeof(**yylp), new_yyl.get());

      swap(m_yys, new_yys);
      swap(m_yyv, new_yyv);
      swap(m_yyl, new_yyl);
      m_stack_size = new_stack_size;

      *yysp = m_yys.get();
      *yyvp = m_yyv.get();
      *yylp = m_yyl.get();
      *yystacksize = new_stack_size;
    }

  private:
    std::size_t m_stack_size;
    UniqueArray<YYS> m_yys;
    UniqueArray<YYV> m_yyv;
    UniqueArray<YYL> m_yyl;
  };
}

#endif
