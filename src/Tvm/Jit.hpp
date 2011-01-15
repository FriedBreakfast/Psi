#ifndef HPP_PSI_TVM_JIT
#define HPP_PSI_TVM_JIT

#include "Core.hpp"

#include <boost/shared_ptr.hpp>

/**
 * \file
 *
 * Mapping to C types as used by the JIT compiler.
 */

namespace Psi {
  namespace Tvm {
    class JitFactory;

    /**
     * \brief Base class for JIT compilers.
     *
     * Includes typedefs, which should conform to what the JIT maps a
     * given type to.
     */
    class Jit {
      boost::shared_ptr<JitFactory> m_factory;

    public:
      typedef int8_t Boolean;

      typedef int8_t Int8;
      typedef int16_t Int16;
      typedef int32_t Int32;
      typedef int64_t Int64;
      typedef intptr_t IntPtr;

      typedef uint8_t UInt8;
      typedef uint16_t UInt16;
      typedef uint32_t UInt32;
      typedef uint64_t UInt64;
      typedef uintptr_t UIntPtr;

      typedef float Float;
      typedef double Double;

      /**
       * \brief Value type of Metatype.
       */
      struct Metatype {
	UIntPtr size;
	UIntPtr align;
      };

      Jit(const boost::shared_ptr<JitFactory>& factory);
      virtual ~Jit() {}
      
      /**
       * Add a module to this JIT.
       */
      virtual void add_module(Module *module) = 0;
      
      /**
       * Remove a module from this JIT.
       */
      virtual void remove_module(Module *module) = 0;
      
      /**
       * Rebuild a module which may have changed. This should
       * be equivalent to \c remove_module followed by
       * \c add_module.
       * 
       * \param incremental Whether to only build new symbols.
       * If this is false, throw away existing values of these
       * symbols.
       */
      virtual void rebuild_module(Module *module, bool incremental) = 0;

      /**
       * \brief Get a pointer to the given term, generating code or
       * global data as necessary.
       */
      virtual void* get_symbol(GlobalTerm *global) = 0;
    };
    
    /**
     * \brief Factory object for Jit instances.
     * 
     * This holds the reference to the JIT dynamic module and is
     * responsible for system-specific load and unload.
     */
    class JitFactory {
      std::string m_name;

    public:
      JitFactory(const std::string& name);
      virtual ~JitFactory();
      
      /// \brief Name under which this JIT was loaded
      const std::string& name() const {return m_name;}
      
      /// \brief Create a new Just-in-time compiler
      virtual boost::shared_ptr<Jit> create_jit() = 0;

      static boost::shared_ptr<JitFactory> get(const std::string& name);
    };
  }
}

#endif
