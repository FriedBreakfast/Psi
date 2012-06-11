#ifndef HPP_PSI_TVM_RECURSIVE
#define HPP_PSI_TVM_RECURSIVE

#include "Core.hpp"

namespace Psi {
  namespace Tvm {
    class ApplyValue;

    class RecursiveParameter : public Value {
      friend class Context;
      RecursiveParameter(Context *context, const ValuePtr<>& type);
    };

    /**
     * \brief Recursive term: usually used to create recursive types.
     *
     * To create a recursive type (or term), first create a
     * RecursiveTerm using Context::new_recursive, create the type as
     * normal and then call #resolve to finalize the type.
     */
    class RecursiveType : public Value {
      friend class Context;
      ValuePtr<> m_result;
      std::vector<ValuePtr<RecursiveParameter> > m_parameters;

    public:
      void resolve(const ValuePtr<>& term);
      ValuePtr<ApplyValue> apply(const std::vector<ValuePtr<> >& parameters);

      std::size_t n_parameters() {return m_parameters.size();}
      const ValuePtr<RecursiveParameter>& parameter(std::size_t i) {return m_parameters[i];}
      const ValuePtr<> result() {return m_result;}

    private:
      RecursiveType(Context *context, const ValuePtr<>& result_type,
                    const ValuePtr<>& source, const std::vector<ValuePtr<RecursiveParameter> >& parameters);
    };

    class ApplyValue : public HashableValue {
      friend class Context;
      ValuePtr<RecursiveType> m_recursive;
      std::vector<ValuePtr<> > m_parameters;

    public:
      std::size_t n_parameters() {return m_parameters.size();}
      ValuePtr<> unpack();

      const ValuePtr<RecursiveType>& recursive() {return m_recursive;}
      const ValuePtr<>& parameter(std::size_t i) {return m_parameters[i];}

    private:
      ApplyValue(Context *context, const ValuePtr<RecursiveType>& recursive,
                 const std::vector<ValuePtr<> >& parameters);
    };
  }
}

#endif
