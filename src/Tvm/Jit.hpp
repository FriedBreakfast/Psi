#ifndef HPP_PSI_TVM_JIT
#define HPP_PSI_TVM_JIT

#include "Core.hpp"
#include "../PropertyValue.hpp"

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

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
    class PSI_TVM_EXPORT Jit {
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

      /**
       * Add a module to this JIT.
       */
      virtual void add_module(Module *module) = 0;
      
      /**
       * \brief Remove a module from this JIT.
       * 
       * Note that it is an error to call this method if other loaded modules depend
       * on the specified one.
       */
      virtual void remove_module(Module *module) = 0;

      /**
       * \brief Get a pointer to the given term, generating code or
       * global data as necessary.
       */
      virtual void* get_symbol(const ValuePtr<Global>& global) = 0;
      
      /**
       * \brief Destroy this JIT.
       */
      virtual void destroy() = 0;
    };
    
    /**
     * \brief Factory object for Jit instances.
     * 
     * This holds the reference to the JIT dynamic module and is
     * responsible for system-specific load and unload.
     */
    class PSI_TVM_EXPORT JitFactory {
      CompileErrorPair m_error_handler;
      std::string m_name;

    public:
      JitFactory(const CompileErrorPair& error_handler, const std::string& name);
      virtual ~JitFactory();
      
      /// \brief Name under which this JIT was loaded
      const std::string& name() const {return m_name;}
      
      /// \brief Get error reporting location
      const CompileErrorPair& error_handler() const {return m_error_handler;}
      
      /// \brief Create a new Just-in-time compiler
      virtual boost::shared_ptr<Jit> create_jit() = 0;

      /// \brief Get a JIT factory for a named JIT compiler.
      static boost::shared_ptr<JitFactory> get(const CompileErrorPair& error_handler, const std::string& name, const PropertyValue& config);

      static boost::shared_ptr<JitFactory> get(const CompileErrorPair& error_handler);
    };
    
    class JitFactoryCommon : public JitFactory, public boost::enable_shared_from_this<JitFactoryCommon> {
    public:
      typedef Jit* (*JitFactoryCallback) (const CompileErrorPair& error_handler, const PropertyValue& config);
      virtual boost::shared_ptr<Jit> create_jit();
      JitFactoryCommon(const CompileErrorPair& error_handler, const std::string& name, const PropertyValue& config);
      
    protected:
      JitFactoryCallback m_callback;
      PropertyValue m_config;
    };
  }
}

#endif
