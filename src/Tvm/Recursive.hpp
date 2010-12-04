#ifndef HPP_PSI_TVM_RECURSIVE
#define HPP_PSI_TVM_RECURSIVE

#include "Core.hpp"

namespace Psi {
  namespace Tvm {
    class RecursiveParameterTerm : public Term {
      friend class Context;

    private:
      class Initializer;
      RecursiveParameterTerm(const UserInitializer& ui, Context *context, Term* type, bool phantom);
    };

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

      std::size_t n_parameters() const {return n_base_parameters() - 2;}
      RecursiveParameterTerm* parameter(std::size_t i) const {return checked_cast<RecursiveParameterTerm*>(get_base_parameter(i+2));}
      Term* result_type() const {return get_base_parameter(0);}
      Term* result() const {return get_base_parameter(1);}

    private:
      class Initializer;
      RecursiveTerm(const UserInitializer& ui, Context *context, Term* result_type,
                    Term *source, ArrayPtr<RecursiveParameterTerm*const> parameters,
                    bool phantom);
    };

    template<>
    struct TermIteratorCheck<RecursiveTerm> {
      static bool check (TermType t) {
	return t == term_recursive;
      }
    };

    class ApplyTerm : public HashTerm {
      friend class Context;

    public:
      std::size_t n_parameters() const {return n_base_parameters() - 1;}
      Term* unpack() const;

      RecursiveTerm* recursive() const {return checked_cast<RecursiveTerm*>(get_base_parameter(0));}
      Term* parameter(std::size_t i) const {return get_base_parameter(i+1);}

    private:
      class Setup;
      ApplyTerm(const UserInitializer& ui, Context *context, RecursiveTerm *recursive,
		ArrayPtr<Term*const> parameters, std::size_t hash);
    };

    template<>
    struct TermIteratorCheck<ApplyTerm> {
      static bool check (TermType t) {
	return t == term_apply;
      }
    };
  }
}

#endif
