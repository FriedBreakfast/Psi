#ifndef HPP_TVM_TERMOPERATIONMAP
#define HPP_TVM_TERMOPERATIONMAP

#include "Core.hpp"

#include <boost/format.hpp>
#include <boost/make_shared.hpp>

namespace Psi {
  namespace Tvm {
    /**
     * A utility class used to store callbacks to rewrite terms.
     * 
     * \tparam TermType Type of term being rewritten. This must
     * contain an operation() member, meaning it must be either
     * an InstructionTerm or a FunctionalTerm.
     * 
     * \tparam ResultType Result type returned by the user
     * supplied functions.
     * 
     * \tparam UserParameter Parameter type supplied by the user
     * and then passed to the callback functions.
     */
    template<typename TermType, typename ResultType, typename UserParameter>
    class TermOperationMap {
      struct Callback {
        virtual ResultType call(UserParameter parameter, const ValuePtr<TermType>& term) = 0;
      };
      
      template<typename TermTagType, typename CbType>
      class CallbackImpl : public Callback {
        CbType m_cb;
        
      public:
        CallbackImpl(const CbType& cb) : m_cb(cb) {}
        
        virtual ResultType call(UserParameter parameter, const ValuePtr<TermType>& term) {
          return m_cb(parameter, value_cast<TermTagType>(term));
        }
      };
      
      template<typename CbType>
      struct DefaultCallbackImpl : public Callback {
        CbType m_cb;
        
      public:
        DefaultCallbackImpl(const CbType& cb) : m_cb(cb) {}
        
        virtual ResultType call(UserParameter parameter, const ValuePtr<TermType>& term) {
          return m_cb(parameter, term);
        }
      };
      
      static ResultType default_throw_callback(UserParameter, const ValuePtr<TermType>& term) {
        term->error_context().error_throw(term->location(), boost::format("term type not supported: %s") % term->operation_name());
      }

      typedef boost::unordered_map<const char*, boost::shared_ptr<Callback> > CallbackMapType;
      CallbackMapType m_callback_map;
      boost::shared_ptr<Callback> m_default_callback;
      
      struct InitializerData {
        boost::shared_ptr<const InitializerData> next;
        const char *operation;
        boost::shared_ptr<Callback> callback;
        
        InitializerData(const boost::shared_ptr<const InitializerData>& next_,
                        const char *operation_,
                        const boost::shared_ptr<Callback>& callback_)
        : next(next_), operation(operation_), callback(callback_) {}
      };
      
    public:
      ResultType call(UserParameter parameter, const ValuePtr<TermType>& term) const {
        PSI_ASSERT(term);
        typename CallbackMapType::const_iterator it = m_callback_map.find(term->operation_name());
        if (it != m_callback_map.end()) {
          return it->second->call(parameter, term);
        } else {
          return m_default_callback->call(parameter, term);
        }
      }
      
      /**
       * Initializer for term callback maps.
       * 
       * This type should never be constructed or stored by the user -
       * it will not be valid beyond the current expression.
       */
      class Initializer {
        friend class TermOperationMap;
        boost::shared_ptr<const InitializerData> m_ptr;
        
        Initializer(const boost::shared_ptr<Callback>& default_callback)
        : m_ptr(boost::make_shared<InitializerData>(boost::shared_ptr<const InitializerData>(), static_cast<const char*>(0), default_callback)) {
        }
        
        Initializer(const boost::shared_ptr<const InitializerData>& next, const char *operation, const boost::shared_ptr<Callback>& callback)
        : m_ptr(boost::make_shared<InitializerData>(next, operation, callback)) {
        }
        
      public:
        template<typename TermTagType, typename CallbackType>
        Initializer add(CallbackType callback) {
          return Initializer(m_ptr, TermTagType::operation,
                             boost::make_shared<CallbackImpl<TermTagType, CallbackType> >(callback));
        }
      };
      
      /**
       * Returns an initializer object for a map.
       * 
       * \param default_callback A map initialized with this initializer
       * will call this function object when no callback matches.
       */
      template<typename DefaultCbType>
      static Initializer initializer(DefaultCbType default_callback) {
        return Initializer(boost::make_shared<DefaultCallbackImpl<DefaultCbType> >(default_callback));
      }
      
      /**
       * Returns an initializer object for a map.
       * 
       * This returns an initializer which will cause a map to throw an
       * exception if no callback matches.
       */
      static Initializer initializer() {
        return initializer(&default_throw_callback);
      }

      /**
       * Construct a callback map with a set of callback functions.
       */
      explicit TermOperationMap(const Initializer& initializer) {
        const InitializerData *init = initializer.m_ptr.get();
        for (; init->next; init = init->next.get())
          m_callback_map.insert(std::make_pair(init->operation, init->callback));
        m_default_callback = init->callback;
      }
    };
  }
}

#endif
