#ifndef HPP_PSI_TVM_RECURSIVE
#define HPP_PSI_TVM_RECURSIVE

#include "Core.hpp"

namespace Psi {
  namespace Tvm {
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
      void resolve(TermRef<> term);
      TermPtr<ApplyTerm> apply(TermRefArray<> parameters);

      std::size_t n_parameters() const {return n_base_parameters() - 2;}
      TermPtr<RecursiveParameterTerm> parameter(std::size_t i) const {return get_base_parameter<RecursiveParameterTerm>(i+2);}
      TermPtr<> result_type() const {return get_base_parameter(0);}
      TermPtr<> result() const {return get_base_parameter(1);}

    private:
      class Initializer;
      RecursiveTerm(const UserInitializer& ui, Context *context, TermRef<> result_type,
                    Term *source, TermRefArray<RecursiveParameterTerm> parameters);
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
      TermPtr<> unpack() const;

      TermPtr<RecursiveTerm> recursive() const {return get_base_parameter<RecursiveTerm>(0);}
      TermPtr<> parameter(std::size_t i) const {return get_base_parameter(i+1);}

    private:
      class Setup;
      ApplyTerm(const UserInitializer& ui, Context *context, RecursiveTerm *recursive,
		TermRefArray<> parameters, std::size_t hash);
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
