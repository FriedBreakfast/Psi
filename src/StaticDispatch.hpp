#ifndef HPP_PSI_COMPILER_STATICDISPATCH
#define HPP_PSI_COMPILER_STATICDISPATCH

#include "Term.hpp"

namespace Psi {
  namespace Compiler {
    class OverloadValue;
    class Implementation;
    class Metadata;
    class EvaluateContext;
    
    /**
     * \brief Common base class for types which are located by global pattern matching.
     */
    class OverloadType : public Tree {
      DelayedValue<PSI_STD::vector<TreePtr<OverloadValue> >, TreePtr<OverloadType> > m_values;
      
      TreePtr<OverloadType> ptr_get() const {return TreePtr<OverloadType>(this);}
      
    public:
      static const SIVtable vtable;
      
      template<typename ValuesCallback>
      OverloadType(const TreeVtable *vtable, CompileContext& compile_context, unsigned n_implicit_,
                   const PSI_STD::vector<TreePtr<Term> >& pattern_,
                   const ValuesCallback& values_,
                   const SourceLocation& location)
      : Tree(vtable, compile_context, location),
      m_values(compile_context, location, values_),
      n_implicit(n_implicit_),
      pattern(pattern_) {
      }
    
      /**
       * \brief The number of implicit parameters which are found by pattern matching
       * 
       * This facility basically exists to handle one case, which is when
       * \code a:Type, b:a \endcode
       * is used, and \c a is to be discovered by matching.
       */
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
      const PSI_STD::vector<TreePtr<OverloadValue> >& values() const {
        return m_values.get(*this, &OverloadType::ptr_get);
      }
      
      template<typename V>
      static void visit(V& v) {
        visit_base<Tree>(v);
        v("n_implicit", &OverloadType::n_implicit)
        ("pattern", &OverloadType::pattern)
        ("values", &OverloadType::m_values);
      }
    };
    
    template<typename DerivedType, typename DerivedValue, typename Callback>
    class OverloadCallbackWrapper {
      Callback m_callback;
      
    public:
      OverloadCallbackWrapper(const Callback& cb_) : m_callback(cb_) {}
      template<typename V> static void visit(V& v) {v("callback", &OverloadCallbackWrapper::m_callback);}
      
      PSI_STD::vector<TreePtr<OverloadValue> > evaluate(const TreePtr<OverloadType>& self_base) {
        TreePtr<DerivedType> self(treeptr_cast<DerivedType>(self_base));
        PSI_STD::vector<TreePtr<DerivedValue> > base_list = m_callback.evaluate(self);
        return PSI_STD::vector<TreePtr<OverloadValue> >(base_list.begin(), base_list.end());
      }
    };
    
    template<typename DerivedType, typename DerivedValue>
    struct OverloadValuesWrapper {
      template<typename T>
      static OverloadCallbackWrapper<DerivedType, DerivedValue, T> call(const T& cb, typename boost::disable_if<boost::is_convertible<T, PSI_STD::vector<TreePtr<DerivedValue> > > >::type* =0) {
        return OverloadCallbackWrapper<DerivedType, DerivedValue, T>(cb);
      }
      
      static PSI_STD::vector<TreePtr<OverloadValue> > call(const PSI_STD::vector<TreePtr<DerivedValue> >& cb) {
        return PSI_STD::vector<TreePtr<OverloadValue> >(cb.begin(), cb.end());
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
        /**
         * \brief Parameters to base interface, including derived parameters.
         * 
         * For base classes, the values of derived parameters must be explicitly given
         * because we cannot know which overload will be used.
         * This makes any interfaces with a base that uses derived parameters rather
         * closely coupled to the base since the value of derived parameters must be
         * known.
         */
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
        
        InterfaceBase(const TreePtr<Interface>& interface_,
                      const PSI_STD::vector<TreePtr<Term> >& parameters_,
                      const PSI_STD::vector<int>& path_)
        : interface(interface_), parameters(parameters_), path(path_) {}
      };
      
      static const TreeVtable vtable;
      template<typename V> static void visit(V& v);
      
      template<typename ValuesCallback>
      static TreePtr<Interface> new_(unsigned n_implicit,
                                     const PSI_STD::vector<TreePtr<Term> >& pattern,
                                     const ValuesCallback& values,
                                     const PSI_STD::vector<TreePtr<Term> >& derived_pattern,
                                     const TreePtr<Term>& type,
                                     const PSI_STD::vector<InterfaceBase>& bases,
                                     const SourceLocation& location) {
        return tree_from(::new Interface(n_implicit, pattern, values, derived_pattern, type, bases, location));
      }
      
      TreePtr<Term> type_after(const PSI_STD::vector<TreePtr<Term> >& parameters, const SourceLocation& location) const;
      
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
      
    private:
      template<typename ValuesCallback>
      Interface(unsigned n_implicit,
                const PSI_STD::vector<TreePtr<Term> >& pattern,
                const ValuesCallback& values,
                const PSI_STD::vector<TreePtr<Term> >& derived_pattern_,
                const TreePtr<Term>& type_,
                const PSI_STD::vector<InterfaceBase>& bases_,
                const SourceLocation& location)
      : OverloadType(&vtable, type_->compile_context(), n_implicit, pattern, OverloadValuesWrapper<Interface,Implementation>::call(values), location),
      derived_pattern(derived_pattern_),
      type(type_),
      bases(bases_) {
      }
    };
    
    /**
     * \brief Metadata type tree.
     *
     * Metadata is located by global pattern matching on a set of Term variables.
     */
    class MetadataType : public OverloadType {
      template<typename ValuesCallback>
      MetadataType(CompileContext& compile_context,
                   unsigned n_implicit,
                   const PSI_STD::vector<TreePtr<Term> >& pattern,
                   const ValuesCallback& values,
                   const SIType& type_,
                   const SourceLocation& location)
      : OverloadType(&vtable, compile_context, n_implicit, pattern, OverloadValuesWrapper<MetadataType,Metadata>::call(values), location),
      type(type_) {
      }

    public:
      static const TreeVtable vtable;
      template<typename V> static void visit(V& v);
      
      /**
       * \brief Get the common base type of implementations of this interface.
       * 
       * Note that this is a Tree type rather than a run-time type.
       */
      SIType type;
      
      /**
       * MetadataType factory function.
       */
      template<typename ValuesCallback>
      static TreePtr<MetadataType> new_(CompileContext& compile_context, unsigned n_implicit, const PSI_STD::vector<TreePtr<Term> >& pattern,
                                        const ValuesCallback& values, const SIType& type, const SourceLocation& location) {
        return TreePtr<MetadataType>(::new MetadataType(compile_context, n_implicit, pattern, values, type, location));
      }
    };
    
    /**
     * \brief Values associated with StaticDispatch instances.
     */
    class OverloadValue : public Tree {
    public:
      static const SIVtable vtable;
      OverloadValue(const TreeVtable *vtable, CompileContext& compile_context, const TreePtr<OverloadType>& type, unsigned n_wildcards, const PSI_STD::vector<TreePtr<Term> >& pattern, const SourceLocation& location);
      
      /**
       * \brief Get what this overloads.
       * 
       * This may be NULL if this OverloadValue is attached to the OverloadType directly.
       */
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
    
    class Metadata;
    
    struct MetadataVtable {
      TreeVtable base;
      void (*get) (TreePtr<> *result, const Metadata *self, const PSI_STD::vector<TreePtr<Term> > *wildcards, const SourceLocation *location);
    };
    
    /**
     * \brief Class for values associated with MetadataType.
     */
    class Metadata : public OverloadValue {
    public:
      typedef MetadataVtable VtableType;
      static const SIVtable vtable;

      Metadata(const MetadataVtable *vptr, CompileContext& compile_context, const TreePtr<MetadataType>& type,
               unsigned n_wildcards, const PSI_STD::vector<TreePtr<Term> >& pattern, const SourceLocation& location);
      
      template<typename V>
      static void visit(V& v) {
        visit_base<OverloadValue>(v);
      }
      
      TreePtr<> get(const PSI_STD::vector<TreePtr<Term> >& wildcards, const SourceLocation& location) const {
        ResultStorage<TreePtr<> > rs;
        derived_vptr(this)->get(rs.ptr(), this, &wildcards, &location);
        return rs.done();
      }
      
      static TreePtr<Metadata> new_(const TreePtr<>& value, const TreePtr<MetadataType>& type, unsigned n_wildcards, const PSI_STD::vector<TreePtr<Term> >& pattern, const SourceLocation& location);
    };
    
    template<typename Derived, typename Impl=Derived>
    struct MetadataWrapper {
      static void get (TreePtr<> *result, const Metadata *self, const PSI_STD::vector<TreePtr<Term> > *wildcards, const SourceLocation *location) {
        new (result) TreePtr<> (Impl::get_impl(*static_cast<const Derived*>(self), *wildcards, *location));
      }
    };
    
#define PSI_COMPILER_METADATA(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &MetadataWrapper<derived>::get \
  }
    
    /**
     * \brief Class for values associated with Interface.
     */
    class Implementation : public OverloadValue {
      Implementation(CompileContext& compile_context, const PSI_STD::vector<TreePtr<Term> >& dependent, const TreePtr<Term>& value, const TreePtr<Interface>& interface,
                     unsigned n_wildcards, const PSI_STD::vector<TreePtr<Term> >& pattern, bool dynamic, const PSI_STD::vector<int>& path,
                     const SourceLocation& location);

    public:
      static const TreeVtable vtable;
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
      
      /**
       * \brief True if this implementation is in a dynamic rather than static scope.
       * 
       * This means that \c value is a direct reference to the correct value for the interface
       * rather than a template to be used to build such a value, which is what is done if
       * the implementation is global. If \c dynamic is true, \c path must be empty.
       */
      PsiBool dynamic;
      
      /**
       * \brief Path of ElementValue operations to use on \c value to get the correct value type of the interface.
       */
      PSI_STD::vector<int> path;

      static TreePtr<Implementation> new_(const PSI_STD::vector<TreePtr<Term> >& dependent, const TreePtr<Term>& value, const TreePtr<Interface>& interface,
                                          unsigned n_wildcards, const PSI_STD::vector<TreePtr<Term> >& pattern, bool dynamic, const PSI_STD::vector<int>& path,
                                          const SourceLocation& location);
    };
    
    PSI_STD::vector<TreePtr<Term> > overload_match(const TreePtr<OverloadValue>& overload, const PSI_STD::vector<TreePtr<Term> >& parameters, const SourceLocation& location);
    
    /// \brief Result of overload_lookup function
    struct OverloadLookupResult {
      OverloadLookupResult();
      OverloadLookupResult(const TreePtr<OverloadValue>& value_, const PSI_STD::vector<TreePtr<Term> >& wildcards_)
      : value(value_), wildcards(wildcards_) {}
      
      TreePtr<OverloadValue> value;
      PSI_STD::vector<TreePtr<Term> > wildcards;
    };
    
    OverloadLookupResult overload_lookup(const TreePtr<OverloadType>& type, const PSI_STD::vector<TreePtr<Term> >& parameters,
                                         const SourceLocation& location, const PSI_STD::vector<TreePtr<OverloadValue> >& extra);
    
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
