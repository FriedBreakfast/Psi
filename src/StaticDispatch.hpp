#ifndef HPP_PSI_COMPILER_STATICDISPATCH
#define HPP_PSI_COMPILER_STATICDISPATCH

#include "Term.hpp"

namespace Psi {
  namespace Compiler {
    class OverloadValue;
    class Implementation;
    class Metadata;
    
    /**
     * \brief Common base class for types which are located by global pattern matching.
     */
    class OverloadType : public Tree {
    public:
      static const SIVtable vtable;
      OverloadType(const TreeVtable *vtable, CompileContext& compile_context, unsigned n_implicit,
                   const PSI_STD::vector<TreePtr<Term> >& pattern,
                   const PSI_STD::vector<TreePtr<OverloadValue> >& values,
                   const SourceLocation& location);
    
      /// \brief The number of implicit parameters which are found by pattern matching
      unsigned n_implicit;
      /// \brief Parameter type patterns.
      PSI_STD::vector<TreePtr<Term> > pattern;
      
      /**
       * \brief Values defined along with the overload definition.
       * 
       * These are used for overloads on existing types, because if the
       * user does not control the type they cannot add an overload to it
       * directly, so it may be added to the overload type as an alternative.
       */
      PSI_STD::vector<TreePtr<OverloadValue> > values;
      
      template<typename V>
      static void visit(V& v) {
        visit_base<Tree>(v);
        v("n_implicit", &OverloadType::n_implicit)
        ("pattern", &OverloadType::pattern)
        ("values", &OverloadType::values);
      }
    };

    /**
     * \brief Interface tree.
     * 
     * Interfaces are run-time values located by global pattern matching.
     */
    class Interface : public OverloadType {
    public:
      struct InterfaceBase {
        /// \brief Base interface
        TreePtr<Interface> interface;
        /// \brief Parameters to base interface
        PSI_STD::vector<TreePtr<Term> > parameters;

        /** \brief How to find the base interface in the derived interface value.
         * 
         * Path of ElementValue operations to use to find a pointer to the base
         * interface.
         */
        PSI_STD::vector<int> path;
        
        template<typename V>
        static void visit(V& v) {
          v("interface", &InterfaceBase::interface)
          ("parameters", &InterfaceBase::parameters)
          ("path", &InterfaceBase::path);
        }
      };
      
      static const TreeVtable vtable;
      Interface(const PSI_STD::vector<InterfaceBase>& bases,
                const TreePtr<Term>& type,
                unsigned n_implicit,
                const PSI_STD::vector<TreePtr<Term> >& pattern,
                const PSI_STD::vector<TreePtr<Implementation> >& values,
                const PSI_STD::vector<TreePtr<Term> >& derived_pattern,
                const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief The derived parameter pattern.
      PSI_STD::vector<TreePtr<Term> > derived_pattern;

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
      TreePtr<Term> type;
      
      /**
       * \brief Base interfaces.
       */
      PSI_STD::vector<InterfaceBase> bases;
    };
    
    /**
     * \brief Metadata type tree.
     *
     * Metadata is located by global pattern matching on a set of Term variables.
     */
    class MetadataType : public OverloadType {
    public:
      static const TreeVtable vtable;
      MetadataType(CompileContext& compile_context,
                   unsigned n_implicit,
                   const PSI_STD::vector<TreePtr<Term> >& pattern,
                   const PSI_STD::vector<TreePtr<Metadata> >& values,
                   const SIType& type,
                   const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /**
       * \brief Get the common base type of implementations of this interface.
       * 
       * Note that this is a Tree type rather than a run-time type.
       */
      SIType type;
    };
    
    /**
     * \brief Values associated with StaticDispatch instances.
     */
    class OverloadValue : public Tree {
    public:
      static const SIVtable vtable;
      OverloadValue(const TreeVtable *vtable, const TreePtr<OverloadType>& type, unsigned n_wildcards, const PSI_STD::vector<TreePtr<Term> >& pattern, const SourceLocation& location);
      
      /// \brief Get what this overloads.
      TreePtr<OverloadType> overload_type;
      
      /// \brief Number of wildcards to be matched.
      unsigned n_wildcards;

      /**
       * \brief Pattern which this value matches.
       * 
       * Implicit parameters are expected to have been filled in in this list.
       * This list should have the same length as that in \c overload_type.
       */
      PSI_STD::vector<TreePtr<Term> > pattern;
      
      template<typename V>
      static void visit(V& v) {
        visit_base<Tree>(v);
        v("overload_type", &OverloadValue::overload_type)
        ("n_wildcards", &OverloadValue::n_wildcards)
        ("pattern", &OverloadValue::pattern);
      }
    };
    
    /**
     * \brief Class for values associated with MetadataType.
     */
    class Metadata : public OverloadValue {
    public:
      static const TreeVtable vtable;
      Metadata(const TreePtr<>& value, const TreePtr<MetadataType>& type, unsigned n_wildcards, const PSI_STD::vector<TreePtr<Term> >& pattern, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief The value of this metadata
      TreePtr<> value;
    };
    
    /**
     * \brief Class for values associated with Interface.
     */
    class Implementation : public OverloadValue {
    public:
      static const TreeVtable vtable;
      Implementation(const PSI_STD::vector<TreePtr<Term> >& dependent, const TreePtr<Term>& value, const TreePtr<Interface>& interface,
                     unsigned n_wildcards, const PSI_STD::vector<TreePtr<Term> >& pattern, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /**
       * \brief Dependent values.
       * 
       * This list should be the length expected according to Interface::dependent
       */
      PSI_STD::vector<TreePtr<Term> > dependent;
      
      /**
       * \brief Get the value of this implementation.
       * 
       * Note that before being returned to the user, this value must be
       * rewritten according to the values of the interface parameters.
       */
      TreePtr<Term> value;
    };
    
    std::pair<PSI_STD::vector<TreePtr<Term> >, TreePtr<OverloadValue> >
    overload_lookup(const TreePtr<OverloadType>& type, const TreePtr<EvaluateContext>& context,
                    const PSI_STD::vector<TreePtr<Term> >& parameters, const SourceLocation& location);
    
    TreePtr<> metadata_lookup(const TreePtr<MetadataType>& interface, const TreePtr<EvaluateContext>& context,
                              const PSI_STD::vector<TreePtr<Term> >& parameters, const SourceLocation& location);

    template<typename T>
    TreePtr<T> metadata_lookup_as(const TreePtr<MetadataType>& interface, const TreePtr<EvaluateContext>& context,
                                  const PSI_STD::vector<TreePtr<Term> >& parameters, const SourceLocation& location) {
      return treeptr_cast<T>(metadata_lookup(interface, context, parameters, location));
    }

    template<typename T>
    TreePtr<T> metadata_lookup_as(const TreePtr<MetadataType>& interface, const TreePtr<EvaluateContext>& context, 
                                  const TreePtr<Term>& parameter, const SourceLocation& location) {
      PSI_STD::vector<TreePtr<Term> > parameters(1, parameter);
      return metadata_lookup_as<T>(interface, context, parameters, location);
    }
  }
}

#endif
