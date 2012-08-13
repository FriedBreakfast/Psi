#ifndef HPP_PSI_TVM_RECURSIVE
#define HPP_PSI_TVM_RECURSIVE

#include "Core.hpp"

namespace Psi {
  namespace Tvm {
    class ApplyValue;

    class RecursiveParameter : public Value {
      PSI_TVM_VALUE_DECL(RecursiveParameter);
      friend class Context;
      RecursiveParameter(const ValuePtr<>& type, const SourceLocation& location);
      ValuePtr<> m_recursive_parameter_type;
    public:
      template<typename V> static void visit(V& v);
    };

    /**
     * \brief Recursive term: usually used to create recursive types.
     *
     * To create a recursive type (or term), first create a
     * RecursiveTerm using Context::new_recursive, create the type as
     * normal and then call #resolve to finalize the type.
     */
    class RecursiveType : public Value {
      PSI_TVM_VALUE_DECL(RecursiveType)
      friend class Context;
      ValuePtr<> m_result;
      std::vector<ValuePtr<RecursiveParameter> > m_parameters;

    public:
      void resolve(const ValuePtr<>& term);
      ValuePtr<ApplyValue> apply(const std::vector<ValuePtr<> >& parameters, const SourceLocation& location);

      std::size_t n_parameters() {return m_parameters.size();}
      const ValuePtr<RecursiveParameter>& parameter(std::size_t i) {return m_parameters[i];}
      const ValuePtr<> result() {return m_result;}
      
      static bool isa_impl(const Value& v) {return v.term_type() == term_recursive;}
      template<typename V> static void visit(V& v);

    private:
      RecursiveType(const ValuePtr<>& result_type, Value *source,
                    const std::vector<ValuePtr<RecursiveParameter> >& parameters,
                    const SourceLocation& location);
    };

    class ApplyValue : public HashableValue {
      PSI_TVM_HASHABLE_DECL(ApplyValue)
      friend class Context;
      ValuePtr<> m_recursive;
      std::vector<ValuePtr<> > m_parameters;

    public:
      std::size_t n_parameters() {return m_parameters.size();}
      ValuePtr<> unpack();

      ValuePtr<RecursiveType> recursive() {return value_cast<RecursiveType>(m_recursive);}
      const ValuePtr<>& parameter(std::size_t i) {return m_parameters[i];}

    private:
      ApplyValue(Context& context,
                 const ValuePtr<>& recursive,
                 const std::vector<ValuePtr<> >& parameters,
                 const SourceLocation& location);
    };
  }
}

#endif
