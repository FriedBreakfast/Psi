#ifndef HPP_PSI_TVM_RECURSIVE
#define HPP_PSI_TVM_RECURSIVE

#include "Core.hpp"
#include "ValueList.hpp"
#include "Functional.hpp"

namespace Psi {
  namespace Tvm {
    class ApplyValue;
    
    class RecursiveParameter : public Value {
      PSI_TVM_VALUE_DECL(RecursiveParameter);
      friend class Context;
      friend class RecursiveType;

    public:
      ValuePtr<RecursiveType> recursive() {return ValuePtr<RecursiveType>(m_recursive);}
      RecursiveType *recursive_ptr() {return m_recursive;}
      bool parameter_phantom() {return m_phantom;}
      
      static bool isa_impl(const Value& ptr) {
        return ptr.term_type() == term_recursive_parameter;
      }

      template<typename V> static void visit(V& v);

      static ValuePtr<RecursiveParameter> create(const ValuePtr<>& type, bool phantom, const SourceLocation& location);

    private:
      RecursiveParameter(Context& context, const ValuePtr<>& type, bool phantom, const SourceLocation& location);
      bool m_phantom;

      void list_release() {m_recursive = NULL;}
      RecursiveType *m_recursive;
      boost::intrusive::list_member_hook<> m_parameter_list_hook;
      template<typename T, boost::intrusive::list_member_hook<> T::*> friend class ValueList;
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

    public:
      typedef ValueList<RecursiveParameter, &RecursiveParameter::m_parameter_list_hook> ParameterList;
      
      static ValuePtr<RecursiveType> create(const ValuePtr<>& result_type,
                                            RecursiveType::ParameterList& parameters,
                                            Value *source,
                                            const SourceLocation& location);

      void resolve(const ValuePtr<>& term);
      const ValuePtr<>& result_type() {return m_result_type;}
      const ParameterList& parameters() const {return m_parameters;}
      const ValuePtr<>& result() {return m_result;}
      
      static bool isa_impl(const Value& v) {return v.term_type() == term_recursive;}
      template<typename V> static void visit(V& v);

    private:
      RecursiveType(const ValuePtr<>& result_type, ParameterList& parameters, Value *source, const SourceLocation& location);

      ValuePtr<> m_result_type;
      ValuePtr<> m_result;
      ParameterList m_parameters;
    };

    class ApplyValue : public HashableValue {
      PSI_TVM_HASHABLE_DECL(ApplyValue)
      friend class Context;
      ValuePtr<> m_recursive;
      std::vector<ValuePtr<> > m_parameters;

    public:
      ApplyValue(const ValuePtr<>& recursive,
                 const std::vector<ValuePtr<> >& parameters,
                 const SourceLocation& location);

      std::size_t n_parameters() {return m_parameters.size();}
      ValuePtr<> unpack();

      ValuePtr<RecursiveType> recursive() {return value_cast<RecursiveType>(m_recursive);}
      const std::vector<ValuePtr<> >& parameters() const {return m_parameters;}
      
      static bool isa_impl(const Value& v) {return v.term_type() == term_apply;}
    };
    
    ValuePtr<> unrecurse(const ValuePtr<>& value);
    
    /**
     * \brief Combines unrecurse and dyn_cast.
     * 
     * Just because it's used to often.
     */
    template<typename T> ValuePtr<T> dyn_unrecurse(const ValuePtr<>& value) {
      return dyn_cast<T>(unrecurse(value));
    }
  }
}

#endif
