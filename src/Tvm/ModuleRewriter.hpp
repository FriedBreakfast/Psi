#ifndef HPP_PSI_TVM_MODULEREWRITER
#define HPP_PSI_TVM_MODULEREWRITER

#include "Core.hpp"

#include <boost/shared_ptr.hpp>

namespace Psi {
  namespace Tvm {
    /**
     * Rewrite a term in two stages.
     * 
     * The first stage sets the term up, and the second initializes it.
     * This is effective for global and function terms, which can be
     * created in this way.
     */
    class GlobalTermRewriter {
    protected:
      GlobalTerm *m_new_term;
      
    public:
      GlobalTermRewriter() : m_new_term(0) {}
      virtual ~GlobalTermRewriter() {}
      
      /**
       * Pointer to rewritten term.
       */
      
      GlobalTerm *new_term() const {return m_new_term;}
      
      /**
       * Run the term rewrite.
       */
      virtual void run() = 0;
    };
    
    /**
     * Base class for types which completely rewrite functions.
     */
    struct ModuleRewriterPass {
      /**
       * Prepare a function rewrite.
       */
      virtual boost::shared_ptr<GlobalTermRewriter> rewrite_global(FunctionTerm *f) = 0;
    };
  }
}

#endif
