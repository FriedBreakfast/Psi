#ifndef HPP_PSI_PARSER_UTILITY
#define HPP_PSI_PARSER_UTILITY

#include "Utility.hpp"

#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>

namespace Psi {
  namespace ParserUtility {
    /**
     * Create a new empty list.
     */
    template<typename T>
    boost::shared_ptr<UniqueList<T> > list_empty() {
      return boost::make_shared<UniqueList<T> >();
    }

    /**
     * Create a one-element list containing the given type.
     */
    template<typename T, typename U>
    boost::shared_ptr<UniqueList<T> >
    list_one(U *t) {
      UniquePtr<U> ptr(t);
      boost::shared_ptr<UniqueList<T> > l = list_empty<T>();
      l->push_back(*ptr.release());
      return l;
    }  

    /**
     * Append two lists and return the result.
     */
    template<typename T>
    boost::shared_ptr<UniqueList<T> >
    list_append(boost::shared_ptr<UniqueList<T> >& source,
		boost::shared_ptr<UniqueList<T> >& append) {
      PSI_ASSERT(append->size() == 1);
      source->splice(source->end(), *append);
      boost::shared_ptr<UniqueList<T> > result;
      result.swap(source);
      append.reset();
      PSI_ASSERT(!source);
      return result;
    }

    /**
     * Ensure a list has one element, remove the element from the list
     * and return it.
     */
    template<typename T>
    T* list_to_ptr(boost::shared_ptr<UniqueList<T> >& ptr) {
      T *result = &ptr->front();
      ptr->pop_front();
      PSI_ASSERT(ptr->empty());
      ptr.reset();
      return result;
    }

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

	UniqueArray<YYS> new_yys(new YYS[new_stack_size]);
	UniqueArray<YYV> new_yyv(new YYV[new_stack_size]);
	UniqueArray<YYL> new_yyl(new YYL[new_stack_size]);

	std::copy(*yysp, *yysp + yyssize / sizeof(**yysp), new_yys.get());
	std::copy(*yyvp, *yyvp + yyvsize / sizeof(**yyvp), new_yyv.get());
	std::copy(*yylp, *yylp + yylsize / sizeof(**yylp), new_yyl.get());

	m_yys.swap(new_yys);
	m_yyv.swap(new_yyv);
	m_yyl.swap(new_yyl);
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
}

#endif
