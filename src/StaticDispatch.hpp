#ifndef HPP_PSI_COMPILER_STATICDISPATCH
#define HPP_PSI_COMPILER_STATICDISPATCH

#include "Term.hpp"

namespace Psi {
  namespace Compiler {
    /**
     * \brief Common base class for types which are located by global pattern matching.
     */
    class OverloadType : public Tree {
      unsigned m_n_implicit;
      unsigned m_n_derived;
      PSI_STD::vector<TreePtr<Term> > m_pattern;

    public:
      static const SIVtable vtable;
      OverloadType(CompileContext& compile_context, unsigned n_implicit,
                   const PSI_STD::vector<TreePtr<Term> >& pattern, const SourceLocation& location);
    
      /// \brief The number of implicit parameters which are found by pattern matching
      unsigned n_implicit() const {return m_n_implicit;}
      /// \brief Parameter patterns.
      const PSI_STD::vector<TreePtr<Term> >& pattern() const {return m_pattern;}
    };

    /**
     * \brief Interface tree.
     * 
     * Interfaces are run-time values located by global pattern matching.
     */
    class Interface : public OverloadType {
    public:
      struct InterfaceBase {
        PSI_STD::vector<TreePtr<Term> > parameters;
        TreePtr<Interface> interface;
        
        template<typename V>
        static void visit(V& v) {
          v("parameters", &InterfaceBase::parameters)
          ("interface", &InterfaceBase::interface);
        }
      };
      
    private:
      TreePtr<Term> m_type;
      PSI_STD::vector<InterfaceBase> m_bases;
      PSI_STD::vector<TreePtr<Term> > m_derived_pattern;

    public:
      static const TreeVtable vtable;
      Interface(const PSI_STD::vector<InterfaceBase>& bases, const TreePtr<Term>& type,
                unsigned n_implicit, const PSI_STD::vector<TreePtr<Term> >& pattern,
                const PSI_STD::vector<TreePtr<Term> >& derived_pattern, const SourceLocation& location);
      
      /// \brief The derived parameter pattern.
      const PSI_STD::vector<TreePtr<Term> >& derived_pattern() const {return m_derived_pattern;}

      /**
       * \brief Get the expected type of implementations of this interface.
       * 
       * Note that this will often depend on the parameters given to the interface.
       * Dependent parameters are numbered in the following order:
       * 
       * <ol>
       * <li>Implicit then explicit parameters to this interface.</li>
       * <li>Dependent parameters to this interface</li>
       * <li>Dependent parameters to base interfaces, sequentially</li>
       * </ol>
       */
      const TreePtr<Term>& type() const {return m_type;}
      
      /**
       * \brief Base interfaces.
       */
      const PSI_STD::vector<InterfaceBase>& bases() const {return m_bases;}
    };
    
    /**
     * \brief Metadata type tree.
     *
     * Metadata is located by global pattern matching on a set of Term variables.
     */
    class MetadataType : public OverloadType {
      SIType m_type;
      
    public:
      static const TreeVtable vtable;
      MetadataType(CompileContext& compile_context, unsigned n_implicit, const PSI_STD::vector<TreePtr<Term> >& pattern,
                   const SIType& type, const SourceLocation& location);
      
      /**
       * \brief Get the common base type of implementations of this interface.
       * 
       * Note that this is a Tree type rather than a run-time type.
       */
      const SIType& type() const {return m_type;}
    };
    
    /**
     * \brief Values associated with StaticDispatch instances.
     */
    class OverloadValue : public Tree {
      
    public:
      static const SIVtable vtable;
      OverloadValue(const TreePtr<OverloadType>& type, const PSI_STD::vector<TreePtr<Term> >& pattern, const SourceLocation& location);
      
      /// \brief Get what this overloads.
      TreePtr<OverloadType> overload_type;

      /**
       * \brief Pattern which this value matches.
       * 
       * Implicit parameters are expected to have been filled in in this list.
       * Note that this list will be longer than the number of parameters for OverloadType when
       * this value has wildcards, and these wildcards are filled in during pattern matching.
       * Wildcards all occur at the start of the list.
       */
      PSI_STD::vector<TreePtr<Term> > pattern;
      
      template<typename V>
      static void visit(V& v) {
        visit_base<Tree>(v);
        v("overload_type", &OverloadValue::overload_type)
        ("pattern", &OverloadValue::pattern);
      }
    };
    
    /**
     * \brief Class for values associated with MetadataType.
     */
    class Metadata : public OverloadValue {
      TreePtr<> m_value;
      
    public:
      static const TreeVtable vtable;
      Metadata(const PSI_STD::vector<TreePtr<Term> >& pattern, const TreePtr<>& value, const SourceLocation& location);
      
      /// \brief The value of this metadata
      const TreePtr<>& value() const {return m_value;}
    };
    
    /**
     * \brief Class for values associated with Interface.
     */
    class Implementation : public OverloadValue {
      PSI_STD::vector<TreePtr<Term> > m_dependent;
      TreePtr<Term> m_value;

    public:
      static const TreeVtable vtable;
      Implementation(const PSI_STD::vector<TreePtr<Term> >& pattern, const PSI_STD::vector<TreePtr<Term> >& dependent,
                     const TreePtr<>& value, const SourceLocation& location);
      
      /**
       * \brief Dependent values.
       * 
       * This list should be the length expected according to Interface::dependent
       */
      const PSI_STD::vector<TreePtr<Term> >& dependent() const {return m_dependent;}
      
      /**
       * \brief Get the value of this implementation.
       * 
       * Note that before being returned to the user, this value must be
       * rewritten according to the values of the interface parameters.
       */
      const TreePtr<Term>& value() const {return m_value;}
    };
  }
}

#endif
