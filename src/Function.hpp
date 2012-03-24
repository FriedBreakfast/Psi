#ifndef HPP_PSI_FUNCTION
#define HPP_PSI_FUNCTION

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
      PSI_STD::vector<TreePtr<Anonymous> > extra_arguments;
      /// \brief Main argument.
      TreePtr<Anonymous> argument;
      /// \brief Handler used to interpret the argument.
      TreePtr<ArgumentHandler> handler;
      
      template<typename V>
      static void visit(V& v) {
        v("category", &ArgumentPassingInfo::category)
        ("keyword", &ArgumentPassingInfo::keyword)
        ("extra_arguments", &ArgumentPassingInfo::extra_arguments)
        ("argument", &ArgumentPassingInfo::argument)
        ("handler", &ArgumentPassingInfo::handler);
      }
    };

    class ArgumentPassingInfoCallback;
    
    struct ArgumentPassingInfoCallbackVtable {
      TreeVtable base;
      void (*argument_passing_info) (ArgumentPassingInfo*, const ArgumentPassingInfoCallback*);
    };

    class ArgumentPassingInfoCallback : public Tree {
    public:
      typedef ArgumentPassingInfoCallbackVtable VtableType;
      static const SIVtable vtable;
      
      ArgumentPassingInfo argument_passing_info() const {
        ResultStorage<ArgumentPassingInfo> result;
        derived_vptr(this)->argument_passing_info(result.ptr(), this);
        return result.done();
      }
    };
  }
}

#endif
