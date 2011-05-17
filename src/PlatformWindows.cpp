#include <Dbghelp.h>
#include <Windows.h>

#include "Runtime.hpp"
#include "Platform.hpp"

#include <boost/noncopyable.hpp>

namespace Psi {
  namespace Platform {
    namespace Windows {
      CRITICAL_SECTION symbol_mutex;
      
      /**
       * \brief RAII wrapper for locally allocated memory.
       */
      template<typename T>
      struct LocalPtr : boost::noncopyable {
        T *ptr;
        
        LocalPtr() : ptr(NULL) {}
        ~LocalPtr() {LocalFree(ptr);}
      };
      
      void throw_error(DWORD error) PSI_ATTRIBUTE((PSI_NORETURN)) {
        LocalPtr<CHAR> message;

        DWORD result = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                                     NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), &message.ptr, 0, NULL);
        
        if (!result)
          throw PlatformError("Windows error: cannot get error message");
        
        throw PlatformError(message.ptr);
      }

      void throw_last_error() PSI_ATTRIBUTE((PSI_NORETURN)) {
        throw_error(GetLastError());
      }

      /**
       * This type is for use with Windows structures which tail pack dynamically
       * sized data members.
       */
      template<typename T, typename Len, Len T::*LenPtr, unsigned static_length=256>
      class DynamicLengthBuffer : public boost::noncopyable {
        char m_static[sizeof(T) + static_length] PSI_ATTRIBUTE((PSI_ALIGNED(PSI_ALIGNOF(T))));
        Len m_length;
        T *m_ptr;
        
        void clear() {
          if (m_ptr != m_static)
            checked_free(m_ptr);
        }
        
      public:
        DynamicLengthBuffer() {
          m_ptr = static_cast<T*>(m_static);
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
    }
    
    void platform_initialize() {
      if (!SymInitialize(GetCurrentProcess(), NULL, TRUE))
        Windows::throw_last_error();
      if (!InitializeCriticalSectionAndSpinCount(&symbol_mutex, 0))
        Windows::throw_last_error();
    }
    
    String address_to_symbol(void *addr, void **base) {
      HANDLE proc = GetCurrentProcess();
      DWORD64 cast_addr = reinterpret_cast<DWORD64>(addr);

      Windows::DynamicLengthBuffer<SYMBOL_INFO, ULONG, &SYMBOL_INFO::MaxNameLen> sym_buffer;
      SYMBOL_INFO *sym = buffer.get();
      EnterCriticalSection(&Windows::symbol_mutex);
      BOOL success = SymFromAddr(proc, cast_addr, NULL, sym);
      LeaveCriticalSection(&Windows::symbol_mutex);

      if (!success)
        Windows::throw_last_error();
      
      if (sym->NameLen > sym->MaxNameLen) {
        sym_buffer.resize(sym->NameLen);
        sym = sym_buffer.get();
        EnterCriticalSection(&Windows::symbol_mutex);
        BOOL success = SymFromAddr(proc, cast_addr, NULL, sym);
        LeaveCriticalSection(&Windows::symbol_mutex);
        
        if (!success)
          Windows::throw_last_error();
      }
      
      if (base)
        *base = reinterpret_cast<void*>(sym->Address);
      
      return String(sym->Name, sym->MaxNameLen);
    }
  }
}