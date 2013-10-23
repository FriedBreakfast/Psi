#ifndef HPP_PSI_UTILITY
#define HPP_PSI_UTILITY

#include <cstddef>
#include <string>
#include <cstring>
#include <utility>
#include <vector>

#include <boost/aligned_storage.hpp>
#include <boost/functional/hash.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/swap.hpp>

#include "Assert.hpp"
#include "CppCompiler.hpp"

namespace Psi {
  template<bool v> struct static_bool {static const bool value = v;};
  using boost::swap;
  
  /**
   * \brief Base class for types which should never be constructed.
   */
  class PSI_COMPILER_COMMON_EXPORT NonConstructible {
    NonConstructible();
  };

  /**
   * \brief Base class for non-copyable types.
   *
   * \internal This is used instead of boost::noncopyable so we can control Win32 DLL exports.
   */
  class PSI_COMPILER_COMMON_EXPORT NonCopyable {
    NonCopyable(const NonCopyable&);
    NonCopyable& operator = (const NonCopyable&);
  public:
    NonCopyable() {}
  };

  /**
   * \brief Allows easy access to default constructors without writing
   * out entire types.
   */
  struct DefaultConstructor {
    template<typename T> operator T () const {
      return T();
    }
  };

  namespace {
    PSI_ATTRIBUTE((PSI_UNUSED_ATTR)) DefaultConstructor default_ = {};
  }
  
  template<std::size_t size, std::size_t align>
  struct AlignedStorage {
    typedef typename boost::aligned_storage<size, align>::type type;
    type data;
    void *address() {return &data;}
    const void* address() const {return &data;}
  };
  
  /**
   * Aligned storage for a specific type.
   * 
   * Note that unlike boost::aligned_storage, this is a POD type.
   */
  template<typename T>
  struct AlignedStorageFor {
    AlignedStorage<sizeof(T), PSI_ALIGNOF(T)> data;
    T *ptr() {return static_cast<T*>(data.address());}
    const T *ptr() const {return static_cast<const T*>(data.address());}
  };

  /**
   * \brief Used to wrap C functions where a C++ class is constructed into a passed pointer.
   */
  template<typename T>
  class ResultStorage : boost::noncopyable {
    bool m_constructed;
    AlignedStorageFor<T> m_data;

  public:
    ResultStorage() : m_constructed(false) {}
    ~ResultStorage() {if (m_constructed) m_data.ptr()->~T();}
    T *ptr() {return m_data.ptr();}
    T& done() {m_constructed = true; return *m_data.ptr();}
  };
  
  template<typename T>
  class Maybe {
    AlignedStorageFor<T> m_storage;
    bool m_full;

    typedef void (Maybe::*safe_bool_type) () const;
    void safe_bool_true() const {}
    
  public:
    Maybe() : m_full(false) {}
    Maybe(const DefaultConstructor&) : m_full(false) {}
    template<typename U> Maybe(const U& value) : m_full(true) {new (m_storage.ptr()) T (value);}
    Maybe(const Maybe<T>& src) : m_full(src) {if (m_full) new (m_storage.ptr()) T (*src);}
    template<typename U> Maybe(const Maybe<U>& src) : m_full(src) {if (m_full) new (m_storage.ptr()) T (*src);}
    ~Maybe() {clear();}

    operator safe_bool_type () const {return m_full ? &Maybe::safe_bool_true : 0;}
    bool operator ! () const {return !m_full;}
    
    T* get() {PSI_ASSERT(m_full); return m_storage.ptr();}
    const T* get() const {PSI_ASSERT(m_full); return m_storage.ptr();}
    
    T& operator * () {return *get();}
    const T& operator * () const {return *get();}
    T* operator -> () {return get();}
    const T* operator -> () const {return get();}
    
    template<typename U>
    Maybe<T>& operator = (const U& value) {
      if (m_full) {
        *m_storage.ptr() = value;
      } else {
        new (m_storage.ptr()) T (value);
        m_full = true;
      }
      return *this;
    }
    
    void clear() {
      if (m_full) {
        m_storage.ptr()->~T();
        m_full = false;
      }
    }
    
    Maybe<T>& operator = (const Maybe<T>& value) {
      if (value)
        *this = *value;
      else
        clear();
      return *this;
    }
    
    template<typename U>
    Maybe<T>& operator = (const Maybe<U>& value) {
      if (value)
        *this = *value;
      else
        clear();
      return *this;
    }
  };

  template<typename T>
  class PointerBase {
    typedef void (PointerBase::*safe_bool_type) () const;
    void safe_bool_true() const {}

    PointerBase(const PointerBase&);
    PointerBase& operator = (const PointerBase&);
    
  protected:
    T *m_ptr;

    PointerBase() : m_ptr(0) {}
    PointerBase(T *p) : m_ptr(p) {}

  public:
    T* get() const {return m_ptr;}
    T* operator -> () const {return get();}
    T& operator * () const {return *get();}
    operator safe_bool_type () const {return get() ? &PointerBase::safe_bool_true : 0;}
    bool operator ! () const {return !get();}
  };

  template<typename T, typename U>
  bool operator == (const PointerBase<T>& lhs, const PointerBase<U>& rhs) {
    return lhs.get() == rhs.get();
  }

  template<typename T, typename U>
  bool operator != (const PointerBase<T>& lhs, const PointerBase<U>& rhs) {
    return lhs.get() != rhs.get();
  }

  template<typename T, typename U>
  bool operator < (const PointerBase<T>& lhs, const PointerBase<U>& rhs) {
    return lhs.get() < rhs.get();
  }

  template<typename T>
  class IntrusivePointer : public PointerBase<T> {
  public:
    IntrusivePointer() {}
    explicit IntrusivePointer(T *ptr) {reset(ptr);}
    IntrusivePointer(T *ptr, bool add_ref) {reset(ptr, add_ref);}
    IntrusivePointer(const IntrusivePointer& src) : PointerBase<T>() {reset(src.get());}
    template<typename U> IntrusivePointer(const IntrusivePointer<U>& src) {reset(src.get());}
    ~IntrusivePointer() {reset();}

    T* release() {
      T *ptr = this->m_ptr;
      this->m_ptr = 0;
      return ptr;
    }

    void reset(T *ptr=0, bool add_ref=true) {
      if (ptr && add_ref)
	intrusive_ptr_add_ref(ptr);

      if (this->m_ptr)
	intrusive_ptr_release(this->m_ptr);

      this->m_ptr = ptr;
    }

    IntrusivePointer& operator = (const IntrusivePointer& src) {
      reset(src.get());
      return *this;
    }
  };
  
  /**
   * Thread safe reference count.
   */
  class ReferenceCount {
    AtomicCount m_count;
    
  public:
    ReferenceCount() : m_count(0) {}
    void acquire() {atomic_increment(m_count);}
    bool release() {return atomic_decrement(m_count) == 0;}
  };
  
  template<typename T>
  class UniquePtr : public PointerBase<T> {
    void clear() {
      if (this->m_ptr)
        delete this->m_ptr;
    }

    void swap(UniquePtr<T>& src) {
      std::swap(this->m_ptr, src.m_ptr);
    }

  public:
    template<typename> friend class UniquePtr;

    UniquePtr() {}
    explicit UniquePtr(T *ptr) : PointerBase<T>(ptr) {}
    ~UniquePtr() {clear();}

    void reset(T *ptr=0) {
      clear();
      this->m_ptr = ptr;
    }

    T* release() {
      T *p = this->m_ptr;
      this->m_ptr = 0;
      return p;
    }
    
    friend void swap(UniquePtr<T>& a, UniquePtr<T>& b) {
    }
  };
  
  /**
   * Pointer which copies by cloning the target object.
   */
  template<typename T>
  class ClonePtr : public UniquePtr<T> {
    template<typename U>
    static T* clone_ptr(U *ptr) {
      return ptr ? clone(*ptr) : static_cast<T*>(0);
    }

    void swap(ClonePtr<T>& src) {
      std::swap(this->m_ptr, src.m_ptr);
    }

  public:
    template<typename> friend class UniquePtr;

    ClonePtr() {}
    explicit ClonePtr(T *ptr) : UniquePtr<T>(ptr) {}
    ClonePtr(const ClonePtr<T>& src) : UniquePtr<T>(clone_ptr(src.get())) {}
    template<typename U> ClonePtr(const UniquePtr<U>& src) : UniquePtr<T>() {}
    
    ClonePtr<T>& operator = (const ClonePtr<T>& src) {
      ClonePtr<T>(src).swap(*this);
      return *this;
    }
    
    template<typename U>
    ClonePtr<T>& operator = (const UniquePtr<U>& src) {
      ClonePtr<T>(src).swap(*this);
      return *this;
    }

    friend void swap(ClonePtr<T>& a, ClonePtr<T>& b) {
      a.swap(b);
    }

#if PSI_USE_RVALUE_REF
    ClonePtr(ClonePtr<T>&& src) {this->m_ptr = src.m_ptr; src.m_ptr = NULL;}
    template<typename U> ClonePtr(const UniquePtr<U>&& src) {this->m_ptr = src.m_ptr; src.m_ptr = NULL;}
    
    ClonePtr<T>& operator = (ClonePtr<T>&& src) {
      ClonePtr<T>(move(src)).swap(*this);
      return *this;
    }
    
    template<typename U>
    ClonePtr<T>& operator = (UniquePtr<U>&& src) {
      ClonePtr<T>(move(src)).swap(*this);
      return *this;
    }
#endif
  };
  
  /**
   * \brief Memory pool which does not support free().
   * 
   * It also does not run destructors, so no object placed in this pool should have one.
   */
  class PSI_COMPILER_COMMON_EXPORT WriteMemoryPool {
    struct Page {
      Page *next;
      std::size_t offset, length;
      PSI_FLEXIBLE_ARRAY(char data);
    };

    std::size_t m_page_size;
    Page *m_pages;
    
  public:
    WriteMemoryPool();
    ~WriteMemoryPool();
    
    /// \brief Get the size which will be allocated for the next page
    std::size_t page_size() {return m_page_size;}
    /// \brief Set the size of the next allocated page
    void page_size(std::size_t n) {m_page_size = n;}
    
    template<typename T>
    T* alloc() {
      return new (alloc(sizeof(T), PSI_ALIGNOF(T))) T;
    }
    
    template<typename T, typename Var>
    T* alloc_varstruct(std::size_t n_extra) {
      T *ptr = new (alloc(sizeof(T) + sizeof(Var)*n_extra, std::max(PSI_ALIGNOF(T), PSI_ALIGNOF(Var)))) T;
      Var *extra = reinterpret_cast<Var*>(ptr+1);
      for (std::size_t i = 0; i != n_extra; ++i)
        new (extra + i) Var;
      return ptr;
    }
    
    template<typename T>
    T* alloc(const T& src) {
      return new (alloc(sizeof(T), PSI_ALIGNOF(T))) T(src);
    }
    
    void* alloc(std::size_t size, std::size_t align);
    char* str_alloc(std::size_t n);
    char* strdup(const char *s);
  };
  
  template<typename T>
  class WriteMemoryPoolAllocator {
    WriteMemoryPool *m_pool;
    
  public:
    typedef T value_type;
    typedef T* pointer;
    typedef T& reference;
    typedef const T* const_pointer;
    typedef const T& const_reference;
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;
    template<typename Other> struct rebind {typedef WriteMemoryPoolAllocator<Other> other;};
    
    WriteMemoryPoolAllocator(WriteMemoryPool *pool) : m_pool(pool) {}
    template<typename Other> WriteMemoryPoolAllocator(const WriteMemoryPoolAllocator<Other>& other) : m_pool(&other.pool()) {}
    
    pointer address(reference x) const {return &x;}
    const_pointer address(const_reference x) const {return &x;}
    pointer allocate(size_type n, const_pointer PSI_UNUSED(hint)=0) {return static_cast<T*>(m_pool->alloc(n*sizeof(value_type), PSI_ALIGNOF(value_type)));}
    void deallocate(pointer PSI_UNUSED(p), size_type PSI_UNUSED(n)) {}
    size_type max_size() const throw() {
      // Extra brackets avoid Windows.h max() macro
      return (std::numeric_limits<std::size_t>::max)() / sizeof(value_type);
    }
    void construct(pointer p, const_reference val) {new (p) T (val);}
    void destroy(pointer p) {p->~T();}
    
    WriteMemoryPool& pool() const {return *m_pool;}
  };

  template<typename T, typename U>
  struct checked_cast_impl;

  template<typename T, typename U>
  struct checked_cast_impl<T&,U&> {
    static T& cast(U& src) {
      PSI_ASSERT(&src == dynamic_cast<T*>(&src));
      return static_cast<T&>(src);
    }
  };

  template<typename T, typename U>
  struct checked_cast_impl<T*,U*> {
    static T* cast(U* src) {
      PSI_ASSERT(src == dynamic_cast<T*>(src));
      return static_cast<T*>(src);
    }
  };

  /**
   * \brief Checked cast function.
   *
   * This follows the design of boost::polymorphic downcast with two exceptions:
   *
   * <ul>
   * <li>It supports casts involving references</li>
   * <li>It uses PSI_ASSERT() rather than assert() to check for
   * validity so leaves defining assert() up to the user rather than
   * possibly violating the ODR.</li>
   * </ul>
   */
  template<typename T, typename U>
  T checked_cast(U& src) {
    return checked_cast_impl<T,U&>::cast(src);
  }

  template<typename T, typename U>
  T checked_cast(U* src) {
    return checked_cast_impl<T,U*>::cast(src);
  }

  /**
   * checked_pointer_cast implementation for boost::shared_ptr.
   */
  template<typename T, typename U>
  boost::shared_ptr<T> checked_pointer_cast(const boost::shared_ptr<U>& ptr) {
    PSI_ASSERT(ptr.get() == dynamic_cast<T*>(ptr.get()));
    return boost::static_pointer_cast<T>(ptr);
  }

  /**
   * A base class for types which want to work with checked_cast but
   * would not ordinarily have any virtual members (and hence RTTI
   * would not be available). This class defines a virtual destructor
   * if PSI_DEBUG is defined, so checked_cast will be able to verify
   * casts.
   */
#if PSI_DEBUG
  struct PSI_COMPILER_COMMON_EXPORT CheckedCastBase {
    virtual ~CheckedCastBase();
  };
#else
  struct CheckedCastBase {
  };
#endif

  /**
   * \brief Does nothing.
   *
   * This can be used to require the existence of a conversion to type T.
   */
  template<typename T> void no_op(const T&) {}

  /**
   * \brief Check value is convertible to type.
   */
#define PSI_REQUIRE_CONVERTIBLE(value,type) (false ? no_op<type>(value) : void())

  /// \brief isdigit() fixed to the C locale.
  inline bool c_isdigit(char c) {
    return (c >= '0') && (c <= '9');
  }
  
  /// \brief isalpha() fixed to the C locale.
  inline bool c_isalpha(char c) {
    return ((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z'));
  }
  
  /// \brief isalnum() fixed to the C locale.
  inline bool c_isalnum(char c) {
    return c_isdigit(c) || c_isalpha(c);
  }

  template<typename T>
  PSI_STD::vector<T> vector_of(const T& t1) {
    PSI_STD::vector<T> x;
    x.reserve(1);
    x.push_back(t1);
    return x;
  }
  
  template<typename T>
  PSI_STD::vector<T> vector_of(const T& t1, const T& t2) {
    PSI_STD::vector<T> x;
    x.reserve(2);
    x.push_back(t1);
    x.push_back(t2);
    return x;
  }
  
  template<typename T>
  PSI_STD::vector<T> vector_of(const T& t1, const T& t2, const T& t3) {
    PSI_STD::vector<T> x;
    x.reserve(3);
    x.push_back(t1);
    x.push_back(t2);
    x.push_back(t3);
    return x;
  }
  
  template<typename T, typename U>
  PSI_STD::vector<T> vector_from(const U& u) {
    return PSI_STD::vector<T>(u.begin(), u.end());
  }
  
  /**
   * Sort the elements of a container and remove any duplicates.
   */
  template<typename T, typename Ord, typename Eq>
  void container_sort_unique(T& container, const Ord& ord=Ord(), const Eq& eq=Eq()) {
    std::sort(container.begin(), container.end(), ord);
    container.erase(std::unique(container.begin(), container.end(), eq), container.end());
  }

  template<typename T>
  void container_sort_unique(T& container) {
    container_sort_unique(container, std::less<typename T::value_type>(), std::equal_to<typename T::value_type>());
  }
  
  /**
   * \brief Advance an iterator a certain number of steps, but not beyond an endpoint.
   * 
   * \return True if \c iter was advanced \c n steps. Note that if end is n steps from
   * iter, true is returned and iter is end, so this does not guarantee iter has not
   * reached end.
   */
  template<typename T>
  bool safe_advance(T& iter, std::size_t n, const T& end) {
    for (std::size_t i = 0; i != n; ++i) {
      if (iter == end)
        return false;
      ++iter;
    }
    return true;
  }
  
  /**
   * \brief Get a pointer to the first element of a vector.
   * 
   * If the vector is empty, then front() and the subscript operator are undefined
   * behaviour and hence the C++ implementation may consider it an error. This
   * checks for an empty vector and returns a NULL pointer in this case.
   */
  template<typename Container>
  typename Container::pointer vector_begin_ptr(Container& container) {
    return container.empty() ? typename Container::pointer(0) : &container.front();
  }
  
  /**
   * \brief Get a pointer "one past" the last element of a vector.
   * 
   * See vector_begin_ptr for rationale.
   */
  template<typename Container>
  typename Container::pointer vector_end_ptr(Container& container) {
    return container.empty() ? typename Container::pointer(0) : (&container.back() + 1);
  }
  
  /**
   * Function which returns the length of an array.
   */
  template<typename T, std::size_t N>
  std::size_t array_size(T(&)[N]) {
    return N;
  }

  /**
   * RAII for an array of C strings.
   * 
   * Each string should be allocated with malloc() since it is freed
   * with free().
   */
  class PSI_COMPILER_COMMON_EXPORT CStringArray : public NonCopyable {
    std::size_t m_length;
    char **m_strings;
  
  public:
    CStringArray(std::size_t n);
    ~CStringArray();
    char** data() {return m_strings;}
    char*& operator [] (std::size_t n) {return m_strings[n];}
    static char* checked_strdup(const std::string& s);
  };
  
  /**
   * Return the smallest value greater than \c size which is a
   * multiple of \c align, which must be a power of two.
   */
  inline std::size_t align_to(std::size_t size, std::size_t align) {
    PSI_ASSERT(align && !(align & (align - 1)));
    return (size + align - 1) & ~(align - 1);
  }
}

#endif
