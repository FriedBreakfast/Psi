#ifndef HPP_PSI_TVM_RECURSIVE
#define HPP_PSI_TVM_RECURSIVE

#include "Core.hpp"

namespace Psi {
  namespace Tvm {
    class RecursiveParameterTerm : public Term {
      friend class Context;

    private:
      class Initializer;
      RecursiveParameterTerm(const UserInitializer& ui, Context *context, Term* type);
    };

#ifndef PSI_DOXYGEN
    template<> struct CastImplementation<RecursiveParameterTerm> : CoreCastImplementation<RecursiveParameterTerm, term_recursive_parameter> {};
#endif

    /**
     * \brief Recursive term: usually used to create recursive types.
     *
     * To create a recursive type (or term), first create a
     * RecursiveTerm using Context::new_recursive, create the type as
     * normal and then call #resolve to finalize the type.
     */
    class RecursiveTerm : public Term {
      friend class Context;

    public:
      void resolve(Term* term);
      ApplyTerm* apply(ArrayPtr<Term*const> parameters);

      std::size_t n_parameters() {return n_base_parameters() - 1;}
      RecursiveParameterTerm* parameter(std::size_t i);
      Term* result() {return get_base_parameter(0);}

    private:
      class Initializer;
      RecursiveTerm(const UserInitializer& ui, Context *context, Term* result_type,
                    Term *source, ArrayPtr<RecursiveParameterTerm*const> parameters);
    };

#ifndef PSI_DOXYGEN
    template<> struct CastImplementation<RecursiveTerm> : CoreCastImplementation<RecursiveTerm, term_recursive> {};
#endif

    class ApplyTerm : public HashTerm {
      friend class Context;

    public:
      std::size_t n_parameters() {return n_base_parameters() - 1;}
      Term* unpack();

      RecursiveTerm* recursive() {return cast<RecursiveTerm>(get_base_parameter(0));}
      Term* parameter(std::size_t i) {return get_base_parameter(i+1);}

    private:
      class Setup;
      ApplyTerm(const UserInitializer& ui, Context *context, RecursiveTerm *recursive,
		ArrayPtr<Term*const> parameters, std::size_t hash);
    };

#ifndef PSI_DOXYGEN
    template<> struct CastImplementation<ApplyTerm> : CoreCastImplementation<ApplyTerm, term_apply> {};
#endif

    inline RecursiveParameterTerm* RecursiveTerm::parameter(std::size_t i) {
      return cast<RecursiveParameterTerm>(get_base_parameter(i+2));
    }
  }
}

#endif
