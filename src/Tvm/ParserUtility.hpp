#ifndef HPP_TVM_PSI_PARSER_UTILITY
#define HPP_TVM_PSI_PARSER_UTILITY

#include "../Utility.hpp"
#include "Utility.hpp"

#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/intrusive/list.hpp>


namespace Psi {
  namespace Tvm {
    template<typename T>
    class UniquePtr : public PointerBase<T> {
    public:
      explicit UniquePtr(T *p=0) : PointerBase<T>(p) {}
      ~UniquePtr() {delete this->m_ptr;}

      void reset(T *p=0) {
        delete this->m_ptr;
        this->m_ptr = p;
      }

      T* release() {
        T *p = this->m_ptr;
        this->m_ptr = 0;
        return p;
      }

      void swap(UniquePtr& o) {std::swap(this->m_ptr, o.m_ptr);}

    private:
      UniquePtr(const UniquePtr&);
    };

    template<typename T> void swap(UniquePtr<T>& a, UniquePtr<T>& b) {a.swap(b);}

    /**
     * Version of boost::intrusive::list which deletes all owned objects
     * on destruction.
     */
    template<typename T>
    class UniqueList : public boost::intrusive::list<T> {
    public:
      ~UniqueList() {
        this->clear_and_dispose(boost::checked_deleter<T>());
      }
    };

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
    }
  }
}

#endif
