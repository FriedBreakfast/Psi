#ifndef HPP_PSI_COMPILER_TREEMAP
#define HPP_PSI_COMPILER_TREEMAP

#include "TreeBase.hpp"

#include <boost/format.hpp>
#include <boost/make_shared.hpp>

namespace Psi {
namespace Compiler {
/**
 * A utility class used to store callbacks to rewrite terms.
 * 
 * \tparam TreeType Base type of trees being rewritten.
 * Usually this will also be a subtype of Term.
 * 
 * \tparam ResultType Result type returned by the user
 * supplied functions.
 * 
 * \tparam UserParameter Parameter type supplied by the user
 * and then passed to the callback functions.
 */
template<typename TreeType, typename ResultType, typename UserParameter>
class TreeOperationMap {
  struct Callback {
    virtual ResultType call(UserParameter parameter, const TreePtr<TreeType>& term) = 0;
  };
  
  template<typename TreeTagType, typename CbType>
  class CallbackImpl : public Callback {
    CbType m_cb;
    
  public:
    CallbackImpl(const CbType& cb) : m_cb(cb) {}
    
    virtual ResultType call(UserParameter parameter, const TreePtr<TreeType>& term) {
      return m_cb(parameter, treeptr_cast<TreeTagType>(term));
    }
  };
  
  template<typename CbType>
  struct DefaultCallbackImpl : public Callback {
    CbType m_cb;
    
  public:
    DefaultCallbackImpl(const CbType& cb) : m_cb(cb) {}
    
    virtual ResultType call(UserParameter parameter, const TreePtr<TreeType>& term) {
      return m_cb(parameter, term);
    }
  };
  
  static ResultType default_throw_callback(UserParameter&, const TreePtr<TreeType>& term) {
    term.compile_context().error_throw(term.location(), boost::format("Term lowering not implemented for %s") % si_vptr(term.get())->classname, CompileError::error_internal);
  }

  typedef boost::unordered_map<const SIVtable*, boost::shared_ptr<Callback> > CallbackMapType;
  CallbackMapType m_callback_map;
  boost::shared_ptr<Callback> m_default_callback;
  
  struct InitializerData {
    boost::shared_ptr<const InitializerData> next;
    const SIVtable *vptr;
    boost::shared_ptr<Callback> callback;
    
    InitializerData(const boost::shared_ptr<const InitializerData>& next_,
                    const SIVtable *vptr_,
                    const boost::shared_ptr<Callback>& callback_)
    : next(next_), vptr(vptr_), callback(callback_) {}
  };
  
public:
  ResultType call(UserParameter parameter, const TreePtr<TreeType>& term) const {
    PSI_ASSERT(term);
    typename CallbackMapType::const_iterator it = m_callback_map.find(si_vptr(term.get()));
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
    friend class TreeOperationMap;
    boost::shared_ptr<const InitializerData> m_ptr;
    
    Initializer(const boost::shared_ptr<Callback>& default_callback)
    : m_ptr(boost::make_shared<InitializerData>(boost::shared_ptr<const InitializerData>(), static_cast<const SIVtable*>(0), default_callback)) {
    }
    
    Initializer(const boost::shared_ptr<const InitializerData>& next, const SIVtable *vptr, const boost::shared_ptr<Callback>& callback)
    : m_ptr(boost::make_shared<InitializerData>(next, vptr, callback)) {
    }
    
  public:
    template<typename TreeTagType, typename CallbackType>
    Initializer add(CallbackType callback) {
      return Initializer(m_ptr, reinterpret_cast<const SIVtable*>(&TreeTagType::vtable),
                         boost::make_shared<CallbackImpl<TreeTagType, CallbackType> >(callback));
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
  explicit TreeOperationMap(const Initializer& initializer) {
    const InitializerData *init = initializer.m_ptr.get();
    for (; init->next; init = init->next.get())
      m_callback_map.insert(std::make_pair(init->vptr, init->callback));
    m_default_callback = init->callback;
  }
};
}
}

#endif
