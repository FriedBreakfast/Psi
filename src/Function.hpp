#ifndef HPP_PSI_FUNCTION
#define HPP_PSI_FUNCTION

#include "Tree.hpp"

namespace Psi {
  namespace Compiler {
    class ArgumentHandler;

    struct ArgumentPassingInfo {
      enum Category {
        category_positional,
        category_keyword,
        category_automatic
      };

      char category;
      String keyword;

      /// \brief List of additional C function arguments
      PSI_STD::vector<std::pair<ParameterMode, TreePtr<Anonymous> > > extra_arguments;
      /// \brief Main argument mode
      ParameterMode argument_mode;
      /// \brief Main argument.
      TreePtr<Anonymous> argument;
      /// \brief Handler used to interpret the argument.
      TreePtr<ArgumentHandler> handler;
      
      template<typename V>
      static void visit(V& v) {
        v("category", &ArgumentPassingInfo::category)
        ("keyword", &ArgumentPassingInfo::keyword)
        ("extra_arguments", &ArgumentPassingInfo::extra_arguments)
        ("argument_mode", &ArgumentPassingInfo::argument_mode)
        ("argument", &ArgumentPassingInfo::argument)
        ("handler", &ArgumentPassingInfo::handler);
      }
    };

    class ArgumentPassingInfoCallback;
    
    struct ArgumentPassingInfoCallbackVtable {
      TreeVtable base;
      void (*argument_passing_info) (const ArgumentPassingInfoCallback*, ArgumentPassingInfo*);
    };

    class ArgumentPassingInfoCallback : public Tree {
    public:
      typedef ArgumentPassingInfoCallbackVtable VtableType;
      static const SIVtable vtable;
      
      ArgumentPassingInfo argument_passing_info() const {
        ResultStorage<ArgumentPassingInfo> result;
        derived_vptr(this)->argument_passing_info(this, result.ptr());
        return result.done();
      }
    };
    
    struct ReturnPassingInfo {
      /// \brief Return type
      TreePtr<Term> type;
      /// \brief Return mode
      ResultMode mode;

      template<typename V>
      static void visit(V& v) {
        v("type", &ReturnPassingInfo::type)
        ("mode", &ReturnPassingInfo::mode);
      }
    };
    
    class ReturnPassingInfoCallback;
    
    struct ReturnPassingInfoCallbackVtable {
      TreeVtable base;
      void (*return_passing_info) (const ReturnPassingInfoCallback*, ReturnPassingInfo*);
    };
    
    class ReturnPassingInfoCallback : public Tree {
    public:
      typedef ReturnPassingInfoCallbackVtable VtableType;
      static const SIVtable vtable;
      
      ReturnPassingInfo return_passing_info() const {
        ResultStorage<ReturnPassingInfo> result;
        derived_vptr(this)->return_passing_info(this, result.ptr());
        return result.done();
      }
    };
  }
}

#endif
