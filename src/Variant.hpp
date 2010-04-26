#ifndef HPP_PSI_VARIANT
#define HPP_PSI_VARIANT

#include <utility>
#include <type_traits>
#include <stdexcept>
#include <functional>

namespace Psi {
  /// \cond internal
  namespace VariantDetail {
    template<typename... Args> struct MaxSize;

    template<> struct MaxSize<> {
      static const std::size_t value = 0;
    };

    template<typename T, typename... Args> struct MaxSize<T, Args...> {
    private:
      static const std::size_t tail_value = MaxSize<Args...>::value;
    public:
      static const std::size_t value = (sizeof(T) > tail_value) ? sizeof(T) : tail_value;
    };

    template<typename... Args> struct MaxAlign;

    template<> struct MaxAlign<> {
      static const std::size_t value = 0;
    };

    template<typename T, typename... Args> struct MaxAlign<T, Args...> {
    private:
      static const std::size_t tail_value = MaxAlign<Args...>::value;
    public:
      static const std::size_t value = (alignof(T) > tail_value) ? alignof(T) : tail_value;
    };

    template<typename Arg> struct EnableWrapper {
      typedef void type;
    };

    template<typename... Functors> struct FunctorList;

    template<> struct FunctorList<> {
      static const bool empty = true;
    };

    template<typename Head, typename... Rest> struct FunctorList<Head, Rest...> {
      typedef FunctorList<Rest...> RestType;

      static const bool empty = false;
      Head&& head;
      RestType rest;

      FunctorList(Head&& head_, Rest&&... rest_) : head(std::forward<Head>(head_)), rest(std::forward<Rest>(rest_)...) {}
    };

    template<typename ResultType, typename Arg, typename Functors, typename Enable=void>
    struct SelectVisitor {
      static ResultType call(Arg&& arg, const Functors& functors) {
        static_assert(!Functors::empty, "No matching functor for given variant entry type");
        return SelectVisitor<ResultType, Arg, typename Functors::RestType>::call(std::forward<Arg>(arg), functors.rest);
      }
    };

    template<typename ResultType, typename Arg, typename Head, typename... Rest>
    struct SelectVisitor<ResultType, Arg, FunctorList<Head, Rest...>,
                         typename EnableWrapper<decltype(std::declval<Head>()(std::declval<Arg>()))>::type> {
      static ResultType call(Arg&& arg, const FunctorList<Head, Rest...>& functors) {
        return functors.head(std::forward<Arg>(arg));
      }
    };

    template<typename ResultType, typename... Functors>
    struct AutoVisitor {
      typedef FunctorList<Functors...> FunctorsType;
      FunctorsType functors;

      AutoVisitor(Functors&&... functors_) : functors(std::forward<Functors>(functors_)...) {
      }

      template<typename T>
      ResultType operator() (T&& arg) {
        return SelectVisitor<ResultType, T, FunctorsType>::call(std::forward<T>(arg), functors);
      }
    };

    struct AnyConvertible {
      template<typename T> operator T ();
    };

    template<typename F> struct SelectVisitorResultSingle {
      typedef typename std::result_of<F(AnyConvertible)>::type type;
    };

    template<typename... Functors> struct SelectVisitorResult {
      typedef typename std::common_type<typename SelectVisitorResultSingle<Functors>::type...>::type type;
    };

    template<typename ResultType, typename... Args>
    struct VisitImpl {
      template<typename Visitor>
      static ResultType call(Visitor&& visitor, const void *storage, int which, int current=1) {
        throw std::logic_error("Invalid variant state");
      };
    };

    template<typename ResultType, typename First, typename... Args>
    struct VisitImpl<ResultType, First, Args...> {
      template<typename Visitor>
      static ResultType call(Visitor&& visitor, void *storage, int which, int current=1) {
        if (which == current) {
          return visitor(*static_cast<First*>(storage));
        } else {
          return VisitImpl<ResultType, Args...>::call(visitor, storage, which, current+1);
        }
      };

      template<typename Visitor>
      static ResultType call(Visitor&& visitor, const void *storage, int which, int current=1) {
        if (which == current) {
          return visitor(*static_cast<const First*>(storage));
        } else {
          return VisitImpl<ResultType, Args...>::call(visitor, storage, which, current+1);
        }
      };
    };

    struct ClearVisitor {
      template<typename T> int operator () (T& value) {
        value.~T();
        return 0;
      }
    };

    struct CopyVisitor {
      void *data;

      template<typename T> int operator () (const T& value) {
        new (data) T(value);
        return 0;
      }
    };

    struct MoveVisitor {
      void *data;

      template<typename T> int operator () (T& value) {
        new (data) T(std::move(value));
        return 0;
      }
    };

    template<typename T, typename... Args> struct AssignHelper;

    template<typename T, typename... Args>
    struct AssignHelper<T, T, Args...> {
      template<typename V>
      static int call(V&& rhs, void *storage) {
        new (storage) T(std::forward<V>(rhs));
        return 1;
      }
    };

    template<typename T, typename U, typename... Args>
    struct AssignHelper<T, U, Args...> {
      template<typename V>
      static int call(V&& rhs, void *storage) {
        return AssignHelper<T, Args...>::call(rhs, storage) + 1;
      }
    };

    template<typename T>
    struct DefaultVisitor {
      T value;

      template<typename U> T operator () (U&&) {
        return std::move(value);
      }
    };

    template<typename T, typename... Args> struct IndexOf;

    template<typename T, typename... Args> struct IndexOf<T, T, Args...> {
      static const int value = 0;
    };

    template<typename T, typename U, typename... Args> struct IndexOf<T, U, Args...> {
      static const int value = IndexOf<T, Args...>::value + 1;
    };
  }
  /// \endcond

  struct None {
    None() = default;
    None(None&&) = default;
  };

  template<typename... Args> class Variant {
  public:
    typedef Variant<Args...> this_type;

    Variant() : m_which(0) {
    }

    Variant(const Variant& rhs) {
      copy_assign(rhs);
    }

    Variant(Variant&& rhs)  {
      move_assign(rhs);
    }

    template<typename T>
    Variant(T&& rhs) {
      entry_assign(std::forward<T>(rhs));
    }

    ~Variant() {
      clear();
    }

    bool empty() const {
      return m_which == 0;
    }

    void clear() {
      if (m_which != 0) {
        VariantDetail::VisitImpl<int, Args...>::call(VariantDetail::ClearVisitor(), &m_storage, m_which);
        m_which = 0;
      }
    }

    const Variant& operator = (const Variant& rhs) {
      clear();
      copy_assign(rhs);
      return *this;
    }

    const Variant& operator = (Variant&& rhs) {
      clear();
      move_assign(rhs);
      return *this;
    }

    template<typename T> const Variant& operator = (T&& rhs) {
      clear();
      entry_assign(std::forward<T>(rhs));
      return *this;
    }

    template<typename... Functors> typename VariantDetail::SelectVisitorResult<Functors...>::type visit(Functors&&... fs) {
      typedef typename VariantDetail::SelectVisitorResult<Functors...>::type ResultType;
      return visit_internal<ResultType>(std::forward<Functors>(fs)...);
    }

    template<typename... Functors> typename VariantDetail::SelectVisitorResult<Functors...>::type visit(Functors&&... fs) const {
      typedef typename VariantDetail::SelectVisitorResult<Functors...>::type ResultType;
      return visit_internal<ResultType>(std::forward<Functors>(fs)...);
    }

    template<typename... Functors> typename VariantDetail::SelectVisitorResult<Functors...>::type visit2(Functors&&... fs) {
      typedef typename VariantDetail::SelectVisitorResult<Functors...>::type ResultType;
      return visit_internal<ResultType>(std::forward<Functors>(fs)..., [] (None) -> ResultType {throw std::runtime_error("visited empty variant");});
    }

    template<typename... Functors> typename VariantDetail::SelectVisitorResult<Functors...>::type visit2(Functors&&... fs) const {
      typedef typename VariantDetail::SelectVisitorResult<Functors...>::type ResultType;
      return visit_internal<ResultType>(std::forward<Functors>(fs)..., [] (None) -> ResultType {throw std::runtime_error("visited empty variant");});
    }

    template<typename T, typename... Functors> T visit_default(T def, Functors&&... fs) {
      return visit_internal<T>(std::forward<Functors>(fs)..., VariantDetail::DefaultVisitor<T>{std::move(def)});
    }

    template<typename T, typename... Functors> T visit_default(T def, Functors&&... fs) const {
      return visit_internal<T>(std::forward<Functors>(fs)..., VariantDetail::DefaultVisitor<T>{std::move(def)});
    }

    template<typename T>
    bool contains() const {
      return m_which == VariantDetail::IndexOf<T, Args...>::value + 1;
    }

    template<typename T>
    T* get() {
      return contains<T>() ? static_cast<T*>(static_cast<void*>(&m_storage)) : 0;
    }

    template<typename T>
    const T* get() const {
      return contains<T>() ? static_cast<const T*>(static_cast<const void*>(&m_storage)) : 0;
    }

  private:
    typedef typename std::aligned_storage<VariantDetail::MaxSize<Args...>::value, VariantDetail::MaxAlign<Args...>::value>::type storage_type;

    void copy_assign(const Variant& rhs) {
      if (rhs.m_which != 0) {
        VariantDetail::VisitImpl<int, Args...>::call(VariantDetail::CopyVisitor{&m_storage}, &rhs.m_storage, rhs.m_which);
      }
      m_which = rhs.m_which;
    }

    void move_assign(Variant& rhs) {
      if (rhs.m_which != 0) {
        VariantDetail::VisitImpl<int, Args...>::call(VariantDetail::MoveVisitor{&m_storage}, &rhs.m_storage, rhs.m_which);
      }
      m_which = rhs.m_which;
    }

    template<typename T>
    void entry_assign(T&& rhs) {
      m_which = VariantDetail::AssignHelper<typename std::remove_reference<T>::type, Args...>::call(rhs, &m_storage);
    }

    template<typename ResultType, typename... Functors> ResultType visit_internal(Functors&&... fs) {
      VariantDetail::AutoVisitor<ResultType, Functors...> visitor(std::forward<Functors>(fs)...);
      if (m_which != 0) {
        return VariantDetail::VisitImpl<ResultType, Args...>::call(visitor, &m_storage, m_which);
      } else {
        return visitor(None());
      }
    }

    template<typename ResultType, typename... Functors> ResultType visit_internal(Functors&&... fs) const {
      VariantDetail::AutoVisitor<ResultType, Functors...> visitor(std::forward<Functors>(fs)...);
      if (m_which != 0) {
        return VariantDetail::VisitImpl<ResultType, Args...>::call(visitor, &m_storage, m_which);
      } else {
        return visitor(None());
      }
    }

    int m_which;
    storage_type m_storage;
  };
}

#endif
