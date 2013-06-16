#ifndef HPP_PSI_TVM_RECURSIVE
#define HPP_PSI_TVM_RECURSIVE

#include "Core.hpp"
#include "ValueList.hpp"
#include "Functional.hpp"

namespace Psi {
  namespace Tvm {
    class PSI_TVM_EXPORT RecursiveParameter : public Value {
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
      
      virtual Value* disassembler_source();

      template<typename V> static void visit(V& v);

      static ValuePtr<RecursiveParameter> create(const ValuePtr<>& type, bool phantom, const SourceLocation& location);

    private:
      RecursiveParameter(Context& context, const ValuePtr<>& type, bool phantom, const SourceLocation& location);
      bool m_phantom;

      void list_release() {m_recursive = NULL;}
      RecursiveType *m_recursive;
      boost::intrusive::list_member_hook<> m_parameter_list_hook;
      template<typename T, boost::intrusive::list_member_hook<> T::*> friend class ValueList;
      virtual void check_source_hook(CheckSourceParameter& parameter);
    };

    /**
     * \brief Recursive term: usually used to create recursive types.
     *
     * To create a recursive type (or term), first create a
     * RecursiveTerm using Context::new_recursive, create the type as
     * normal and then call #resolve to finalize the type.
     */
    class PSI_TVM_EXPORT RecursiveType : public Value {
      PSI_TVM_VALUE_DECL(RecursiveType)

    public:
      typedef ValueList<RecursiveParameter, &RecursiveParameter::m_parameter_list_hook> ParameterList;
      
      static ValuePtr<RecursiveType> create(Context& context,
                                            RecursiveType::ParameterList& parameters,
                                            const SourceLocation& location);

      void resolve(const ValuePtr<>& term);
      const ParameterList& parameters() const {return m_parameters;}
      const ValuePtr<>& result() {return m_result;}
      
      static bool isa_impl(const Value& v) {return v.term_type() == term_recursive;}
      template<typename V> static void visit(V& v);
      virtual Value* disassembler_source();
      
#if PSI_DEBUG
      void dump_parameters();
#endif

    private:
      RecursiveType(Context& context, ParameterList& parameters, const SourceLocation& location);
      
      virtual void check_source_hook(CheckSourceParameter& parameter);

      ValuePtr<> m_result;
      ParameterList m_parameters;
    };

    class PSI_TVM_EXPORT ApplyType : public HashableValue {
      PSI_TVM_HASHABLE_DECL(ApplyType)
      friend class Context;
      ValuePtr<RecursiveType> m_recursive;
      std::vector<ValuePtr<> > m_parameters;

    public:
      ApplyType(const ValuePtr<RecursiveType>& recursive,
                 const std::vector<ValuePtr<> >& parameters,
                 const SourceLocation& location);

      std::size_t n_parameters() {return m_parameters.size();}
      ValuePtr<> unpack();

      const ValuePtr<RecursiveType>& recursive() const {return m_recursive;}
      const std::vector<ValuePtr<> >& parameters() const {return m_parameters;}
      
      static bool isa_impl(const Value& v) {return v.term_type() == term_apply;}
      
    private:
      static void hashable_check_source(ApplyType& self, CheckSourceParameter& parameter);
    };
  }
}

#endif
