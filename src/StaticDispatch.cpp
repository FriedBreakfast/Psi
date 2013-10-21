#include "StaticDispatch.hpp"
#include "Tree.hpp"
#include "TermBuilder.hpp"

#include <boost/format.hpp>

namespace Psi {
  namespace Compiler {
    OverloadType::OverloadType(const TreeVtable *vtable, CompileContext& compile_context, unsigned n_implicit_,
                               const PSI_STD::vector<TreePtr<Term> >& pattern_,
                               const PSI_STD::vector<TreePtr<OverloadValue> >& values_,
                               const SourceLocation& location)
    : Tree(vtable, compile_context, location),
    n_implicit(n_implicit_),
    pattern(pattern_),
    values(values_) {
    }
    
    const SIVtable OverloadType::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.OverloadType", Tree);

    /**
     * \param type_ May be NULL if this overload will be attached to an OverloadType.
     */
    OverloadValue::OverloadValue(const TreeVtable *vtable, CompileContext& compile_context, const TreePtr<OverloadType>& type_, unsigned n_wildcards_,
                                 const PSI_STD::vector<TreePtr<Term> >& pattern_, const SourceLocation& location)
    : Tree(vtable, compile_context, location),
    overload_type(type_),
    n_wildcards(n_wildcards_),
    pattern(pattern_) {
    }
    
    const SIVtable OverloadValue::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.OverloadValue", Tree);
    
    Interface::Interface(const PSI_STD::vector<InterfaceBase>& bases_,
                         const TreePtr<Term>& type_,
                         unsigned n_implicit,
                         const std::vector<TreePtr<Term> >& pattern,
                         const PSI_STD::vector<TreePtr<Implementation> >& values,
                         const std::vector<TreePtr<Term> >& derived_pattern_,
                         const SourceLocation& location)
    : OverloadType(&vtable, type_->compile_context(), n_implicit, pattern, PSI_STD::vector<TreePtr<OverloadValue> >(values.begin(), values.end()), location),
    derived_pattern(derived_pattern_),
    type(type_),
    bases(bases_) {
    }
    
    TreePtr<Interface> Interface::new_(const PSI_STD::vector<InterfaceBase>& bases,
                                       const TreePtr<Term>& type,
                                       unsigned n_implicit,
                                       const PSI_STD::vector<TreePtr<Term> >& pattern,
                                       const PSI_STD::vector<TreePtr<Implementation> >& values,
                                       const PSI_STD::vector<TreePtr<Term> >& derived_pattern,
                                       const SourceLocation& location) {
      return tree_from(::new Interface(bases, type, n_implicit, pattern, values, derived_pattern, location));
    }
    
    template<typename V>
    void Interface::visit(V& v) {
      visit_base<OverloadType>(v);
      v("derived_pattern", &Interface::derived_pattern)
      ("type", &Interface::type)
      ("bases", &Interface::bases);
    }

    /**
     * \brief Get the value type of this interface for a given set of parameters.
     */
    TreePtr<Term> Interface::type_after(const PSI_STD::vector<TreePtr<Term> >& parameters, const SourceLocation& result_location) const {
      if (parameters.size() != pattern.size() + derived_pattern.size())
        compile_context().error_throw(result_location, boost::format("Incorrect number of parameters to interface") % location().logical->error_name(result_location.logical));
      return type->specialize(result_location, parameters);
    }
    
    const TreeVtable Interface::vtable = PSI_COMPILER_TREE(Interface, "psi.compiler.Interface", OverloadType);
    
    Implementation::Implementation(CompileContext& compile_context, const PSI_STD::vector<TreePtr<Term> >& dependent_, const TreePtr<Term>& value_, const TreePtr<Interface>& interface,
                                   unsigned n_wildcards, const PSI_STD::vector<TreePtr<Term> >& pattern, bool dynamic_, const PSI_STD::vector<int>& path_, const SourceLocation& location)
    : OverloadValue(&vtable, compile_context, interface, n_wildcards, pattern, location),
    dependent(dependent_),
    value(value_),
    dynamic(dynamic_),
    path(path_) {
      PSI_ASSERT(!dynamic || path.empty());
    }
    
    TreePtr<Implementation> Implementation::new_(const PSI_STD::vector<TreePtr<Term> >& dependent, const TreePtr<Term>& value, const TreePtr<Interface>& interface,
                                                 unsigned n_wildcards, const PSI_STD::vector<TreePtr<Term> >& pattern, bool dynamic, const PSI_STD::vector<int>& path,
                                                 const SourceLocation& location) {
      return tree_from(::new Implementation(value->compile_context(), dependent, value, interface, n_wildcards, pattern, dynamic, path, location));
    }
    
    template<typename V>
    void Implementation::visit(V& v) {
      visit_base<OverloadValue>(v);
      v("dependent", &Implementation::dependent)
      ("value", &Implementation::value)
      ("dynamic", &Implementation::dynamic)
      ("path", &Implementation::path);
    }
    
    const TreeVtable Implementation::vtable = PSI_COMPILER_TREE(Implementation, "psi.compiler.Implementation", OverloadValue);
   
    MetadataType::MetadataType(CompileContext& compile_context,
                               unsigned n_implicit,
                               const PSI_STD::vector<TreePtr<Term> >& pattern,
                               const PSI_STD::vector<TreePtr<Metadata> >& values,
                               const SIType& type_,
                               const SourceLocation& location)
    : OverloadType(&vtable, compile_context, n_implicit, pattern, PSI_STD::vector<TreePtr<OverloadValue> >(values.begin(), values.end()), location),
    type(type_) {
    }
    
    /**
     * MetadataType constructor function.
     */
    TreePtr<MetadataType> MetadataType::new_(CompileContext& compile_context, unsigned n_implicit, const PSI_STD::vector<TreePtr<Term> >& pattern,
                                             const PSI_STD::vector<TreePtr<Metadata> >& values, const SIType& type, const SourceLocation& location) {
      return TreePtr<MetadataType>(::new MetadataType(compile_context, n_implicit, pattern, values, type, location));
    }
    
    template<typename V>
    void MetadataType::visit(V& v) {
      visit_base<OverloadType>(v);
      v("type", &MetadataType::type);
    }
    
    const TreeVtable MetadataType::vtable = PSI_COMPILER_TREE(MetadataType, "psi.compiler.MetadataType", OverloadType);
    
    Metadata::Metadata(const MetadataVtable *vptr, CompileContext& compile_context, const TreePtr<MetadataType>& type,
                       unsigned n_wildcards, const PSI_STD::vector<TreePtr<Term> >& pattern, const SourceLocation& location)
    : OverloadValue(PSI_COMPILER_VPTR_UP(OverloadValue, vptr),
                    compile_context, type, n_wildcards, pattern, location) {
    }
    
    const SIVtable Metadata::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Metadata", OverloadValue);
    
    class ConstantMetadata : public Metadata {
    public:
      static const VtableType vtable;
      
      ConstantMetadata(const TreePtr<>& value_, const TreePtr<MetadataType>& type,
                       unsigned n_wildcards, const PSI_STD::vector<TreePtr<Term> >& pattern,
                       const SourceLocation& location)
      : Metadata(&vtable, value_->compile_context(), type, n_wildcards, pattern, location),
      value(value_) {
        if (type && !type->type.isa(value.get()))
          compile_context().error_throw(location, "Metadata tree has incorrect type");
      }
      
      TreePtr<> value;
      
      static TreePtr<> get_impl(const ConstantMetadata& self,
                                const PSI_STD::vector<TreePtr<Term> >& PSI_UNUSED(wildcards),
                                const SourceLocation& PSI_UNUSED(location)) {
        return self.value;
      }
      
      template<typename V>
      static void visit(V& v) {
        visit_base<Metadata>(v);
        v("value", &ConstantMetadata::value);
      }
    };
    
    const MetadataVtable ConstantMetadata::vtable = PSI_COMPILER_METADATA(ConstantMetadata, "psi.compiler.ConstantMetadata", Metadata);
    
    /**
     * Metadata constructor function for metadata which need not be specialized.
     */
    TreePtr<Metadata> Metadata::new_(const TreePtr<>& value, const TreePtr<MetadataType>& type, unsigned n_wildcards, const PSI_STD::vector<TreePtr<Term> >& pattern, const SourceLocation& location) {
      return tree_from(::new ConstantMetadata(value, type, n_wildcards, pattern, location));
    }
    
    /**
     * \brief Match a pattern.
     * 
     * \param src Pattern.
     * \param target Parameters.
     * 
     * \param n_wildcards Number of wildcards in \c src. \c match will
     * be resized to this size.
     * 
     * \param match Holds values of inferred initial parameters after a
     * successful match. Note that this will be modified whether the
     * match is successful or not.
     * 
     * \c src may be longer than \c target, in which case the extra
     * initial values are assumed to be wildcards which will be found during
     * pattern matching.
     * 
     * \return Whether the match was successful.
     */
    bool overload_pattern_match(const PSI_STD::vector<TreePtr<Term> >& src,
                                const PSI_STD::vector<TreePtr<Term> >& target,
                                unsigned n_wildcards,
                                PSI_STD::vector<TreePtr<Term> >& match) {
      if (target.size() != src.size())
        return false;

      match.assign(n_wildcards, TreePtr<Term>());
      
      for (unsigned ii = 0, ie = src.size(); ii != ie; ++ii) {
        PSI_ASSERT(target[ii]);
        PSI_ASSERT(src[ii]);
        
        if (!src[ii]->match(target[ii], match, 0, Term::upref_match_ignore))
          return false;
      }
      
      // Check all wildcards are set and match expected pattern
      for (unsigned ii = 0; ii != n_wildcards; ++ii) {
        if (!match[ii])
          return false;
      }
      
      return true;
    }
    
    /**
     * \brief Search for a suitable overload.
     */
    void overload_lookup_search(const TreePtr<OverloadType>& type, const PSI_STD::vector<TreePtr<Term> >& parameters,
                                const TreePtr<Term>& term, PSI_STD::vector<OverloadLookupResult>& results,
                                PSI_STD::vector<TreePtr<Term> >& scratch) {
      TreePtr<Term> my_term = term;
      while (true) {
        if (TreePtr<PointerType> ptr = dyn_treeptr_cast<PointerType>(my_term))
          my_term = ptr->target_type;
        else if (TreePtr<Exists> exists = dyn_treeptr_cast<Exists>(my_term))
          my_term = exists->result;
        else if (TreePtr<GlobalStatement> def = dyn_treeptr_cast<GlobalStatement>(my_term)) {
          if ((def->mode == statement_mode_functional) && def->value->pure)
            my_term = def->value;
          else
            break;
        } else if (TreePtr<Statement> stmt = dyn_treeptr_cast<Statement>(my_term)) {
          if ((stmt->mode == statement_mode_functional) && stmt->value->pure)
            my_term = stmt->value;
          else
            break;
        } else
          break;
      }
        
      if (TreePtr<TypeInstance> instance = dyn_treeptr_cast<TypeInstance>(my_term)) {
        const PSI_STD::vector<TreePtr<OverloadValue> >& overloads = instance->generic->overloads();
        for (unsigned ii = 0, ie = overloads.size(); ii != ie; ++ii) {
          const TreePtr<OverloadValue>& v = overloads[ii];
          if (v && (type == v->overload_type) && overload_pattern_match(v->pattern, parameters, v->n_wildcards, scratch))
            results.push_back(OverloadLookupResult(v, scratch));
        }
        
        const PSI_STD::vector<TreePtr<Term> >& parameters = instance->parameters;
        for (unsigned ii = 0, ie = parameters.size(); ii != ie; ++ii)
          overload_lookup_search(type, parameters, parameters[ii], results, scratch);
      }
    }
    
    /**
     * \brief Match parameter pattern to an overload and return the inferred wildcards.
     */
    PSI_STD::vector<TreePtr<Term> > overload_match(const TreePtr<OverloadValue>& overload, const PSI_STD::vector<TreePtr<Term> >& parameters, const SourceLocation& location) {
      PSI_STD::vector<TreePtr<Term> > match;
      bool s = overload_pattern_match(overload->pattern, parameters, overload->n_wildcards, match);
      if (!s)
        overload->compile_context().error_throw(location, "Failed to match overload pattern", CompileError::error_internal);
      return match;
    }
    
    /**
     * \brief Perform a generic overloaded value search.
     * 
     * This is the base implementation for both metadata_lookup and implementation_lookup,
     * and should be used for anything else which subclasses OverloadType.
     */
    OverloadLookupResult overload_lookup(const TreePtr<OverloadType>& type, const PSI_STD::vector<TreePtr<Term> >& parameters,
                                         const SourceLocation& location, const PSI_STD::vector<TreePtr<OverloadValue> >& extra) {
      std::vector<OverloadLookupResult> results;
      PSI_STD::vector<TreePtr<Term> > match_scratch;

      // Find all possible matching overloads
      for (unsigned ii = 0, ie = type->values.size(); ii != ie; ++ii) {
        const TreePtr<OverloadValue>& v = type->values[ii];
        PSI_ASSERT(v && (!v->overload_type || (v->overload_type == type)));
        if(overload_pattern_match(v->pattern, parameters, v->n_wildcards, match_scratch))
          results.push_back(OverloadLookupResult(v, match_scratch));
      }
      
      for (PSI_STD::vector<TreePtr<OverloadValue> >::const_iterator ii = extra.begin(), ie = extra.end(); ii != ie; ++ii) {
        const TreePtr<OverloadValue>& v = *ii;
        PSI_ASSERT(v && (type == v->overload_type));
        if(overload_pattern_match(v->pattern, parameters, v->n_wildcards, match_scratch))
          results.push_back(OverloadLookupResult(v, match_scratch));
      }
      
      for (unsigned ii = 0, ie = parameters.size(); ii != ie; ++ii) {
        if (parameters[ii]) {
          overload_lookup_search(type, parameters, parameters[ii], results, match_scratch);
          if (parameters[ii]->type)
            overload_lookup_search(type, parameters, parameters[ii]->type, results, match_scratch);
        }
      }
      
      if (results.empty())
        type->compile_context().error_throw(location, boost::format("Could not find overload for %s") % type->location().logical->error_name(location.logical));
      
      /* 
       * Attempt to select the most specific overload.
       * 
       * I require that this "most specific" overload 
       */
      unsigned best_idx = 0;
      for (unsigned ii = 1, ie = results.size(); ii < ie;) {
        if (overload_pattern_match(results[best_idx].value->pattern, results[ii].value->pattern, results[best_idx].value->n_wildcards, match_scratch)) {
          // best_idx matches anything ii matches, so ii is more specific
          best_idx = ii;
          ii++;
        } else if (overload_pattern_match(results[ii].value->pattern, results[best_idx].value->pattern, results[ii].value->n_wildcards, match_scratch)) {
          // ii matches anything best_idx matches, so ii is more specific
          ii++;
        } else {
          // Ordering is ambiguous, so neither can be the "best"
          // Make next in list the "best" candidate and skip matching to the one after
          best_idx = ii + 1;
          ii += 2;
        }
      }
      
      if (best_idx == results.size()) {
      ambiguous_match:
        CompileError err(type->compile_context().error_context(), location);
        for (unsigned ii = 0, ie = results.size(); ii != ie; ++ii)
          err.info(results[ii].value->location(), "Ambiguous overload candidate");
        err.end();
        throw CompileException();
      }

      // Check best candidate is indeed more specific than all others
      // Now check that the selected overload is more specific than all other overloads
      for (unsigned ii = 0, ie = results.size(); ii != ie; ++ii) {
        if (ii == best_idx)
          continue;
        
        if (!overload_pattern_match(results[ii].value->pattern, results[best_idx].value->pattern, results[ii].value->n_wildcards, match_scratch))
          goto ambiguous_match;
      }
      
      return results[best_idx];
    }

    /**
     * \brief Locate an interface implementation for a given set of parameters.
     *
     * \param metadata_type Metadata type to look up.
     * \param context Context to perform search in, which may supply additional interfaces to be
     * searched, particularly in case virtual functions.
     * \param parameters Parameters.
     */    
    TreePtr<> metadata_lookup(const TreePtr<MetadataType>& metadata_type, const TreePtr<EvaluateContext>& context,
                              const PSI_STD::vector<TreePtr<Term> >& parameters, const SourceLocation& location) {
      PSI_STD::vector<TreePtr<OverloadValue> > context_list;
      context->overload_list(metadata_type, context_list);
      OverloadLookupResult lookup = overload_lookup(metadata_type, parameters, location, context_list);
      TreePtr<> value = treeptr_cast<Metadata>(lookup.value)->get(lookup.wildcards, location);
      if (!metadata_type->type.isa(value.get()))
        metadata_type->compile_context().error_throw(location, boost::format("Value of metadata does not have the expected type: %s") % metadata_type->type->classname);
      return value;
    }
  }
}
