#ifndef HPP_PSI_BOX
#define HPP_PSI_BOX

#include <functional>
#include <stdexcept>
#include <typeinfo>

namespace Psi {
  class Box {
  public:
    Box() : m_type(0), m_value(0) {}
    template<typename T>
    Box(T *ptr) : m_type(&typeid(*ptr)), m_value(ptr) {}

    template<typename T>
    struct Mismatch {
      T operator () () const {
	throw std::runtime_error("box type mismatch");
      }
    };

    template<typename T, typename U, typename W>
    typename std::result_of<U(T&)>::type visit(U&& visitor, W&& mismatch=Mismatch<typename std::result_of<U(T&)>::type>()) {
      if (typeid(T) == type()) {
	return visitor(*static_cast<T*>(m_value));
      } else {
	return mismatch();
      }
    }

    const std::type_info& type() const {return *m_type;}

    template<typename T>
    void reset(T *ptr) {
      if (ptr) {
	m_type = &typeid(*ptr);
	m_value = ptr;
      } else {
	reset();
      }
    }

    void reset() {
      m_type = 0;
      m_value = 0;
    }

  private:
    const std::type_info *m_type;
    void *m_value;
  };
}

#endif
