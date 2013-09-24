#ifndef HPP_PSI_PLATFORM_WINDOWS
#define HPP_PSI_PLATFORM_WINDOWS

#define WIN32_LEAN_AND_MEAN

// Need to set these to pick up full Windows XP API in MinGW. See w32api.h for values
#define WINVER 0x0500 // Windows 2000
#define _WIN32_WINNT 0x0500 // Windows 2000
#define _WIN32_IE 0x0500 // IE5
#include <Windows.h>

#include <string>
#include <boost/noncopyable.hpp>

#include "CppCompiler.hpp"
#include "Export.hpp"
#include "Platform.hpp"
#include "Utility.hpp"

namespace Psi {
  namespace Platform {
    namespace Windows {
      /**
       * \brief RAII wrapper for locally allocated memory.
       */
      template<typename T>
      struct LocalPtr : boost::noncopyable {
        T *ptr;
        
        LocalPtr() : ptr(NULL) {}
        ~LocalPtr() {LocalFree(ptr);}
      };
      
      /**
       * This type is for use with Windows structures which tail pack dynamically
       * sized data members.
       */
      template<typename T, typename Len, Len T::*LenPtr, unsigned static_length=256>
      class DynamicLengthBuffer : public NonCopyable {
        AlignedStorage<sizeof(T) + static_length, PSI_ALIGNOF(T)> m_static;
        Len m_length;
        T *m_ptr;
        
        T *static_ptr() {
          return static_cast<T*>(m_static.address());
        }
        
        void clear() {
          if (m_ptr != static_ptr())
            checked_free(sizeof(T) + m_length, m_ptr);
        }
        
      public:
        DynamicLengthBuffer() {
          m_ptr = static_ptr();
          m_length = static_length;
          m_ptr->*LenPtr = m_length;
        }
        
        ~DynamicLengthBuffer() {
          clear();
        }
        
        void expand() {
          resize(m_length * 2);
        }
        
        void resize(Len new_length) {
          void *new_ptr = checked_alloc(sizeof(T) + new_length);
          clear();
          m_length = new_length;
          m_ptr = static_cast<T*>(new_ptr);
          m_ptr->*LenPtr = m_length;
        }
        
        T *get() {
          return m_ptr;
        }
      };

      /**
       * RAII wrapper around FreeLibrary.
       */
      class PSI_COMPILER_COMMON_EXPORT LibraryHandle : public NonCopyable {
        HMODULE m_handle;
        
      public:
        LibraryHandle();
        LibraryHandle(HMODULE handle);
        ~LibraryHandle();
        HMODULE get() {return m_handle;}
        void swap(LibraryHandle& other);
      };

      PSI_COMPILER_COMMON_EXPORT std::string error_string(DWORD error);
      PSI_COMPILER_COMMON_EXPORT std::string last_error_string();
      PSI_COMPILER_COMMON_EXPORT PSI_ATTRIBUTE((PSI_NORETURN)) void throw_last_error();

      class PSI_COMPILER_COMMON_EXPORT LibraryWindows : public PlatformLibrary {
        std::vector<HMODULE> m_handles;

      public:
        LibraryWindows(unsigned hint=0);
        virtual ~LibraryWindows();
        virtual boost::optional<void*> symbol(const std::string& symbol);
        void add_handle(HMODULE handle);
      };
    }
  }
}

#endif
