#include "StaticDispatch.hpp"
#include "Tree.hpp"
#include "TermBuilder.hpp"

#include <boost/format.hpp>

namespace Psi {
  namespace Compiler {
    template<typename V>
    void OverloadType::visit(V& v) {
      visit_base<Tree>(v);
      v("n_implicit", &OverloadType::n_implicit)
      ("pattern", &OverloadType::pattern)
      ("values", &OverloadType::m_values);
    }
    
    void OverloadType::local_complete_impl(const OverloadType& self) {
      self.values();
    }
    
    const SIVtable OverloadType::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.OverloadType", Tree);
    
    void OverloadValue::local_complete_impl(const OverloadValue& self) {
      self.overload_pattern();
    }

    const SIVtable OverloadValue::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.OverloadValue", Tree);
    
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
    
    template<typename V>
    void Implementation::visit(V& v) {
      visit_base<OverloadValue>(v);
      v("dependent", &Implementation::m_dependent)
      ("implementation_value", &Implementation::m_implementation_value);
    }
    
    void Implementation::local_complete_impl(const Implementation& self) {
      OverloadValue::local_complete_impl(self);
      self.dependent();
      self.implementation_value();
    }
    
    const TreeVtable Implementation::vtable = PSI_COMPILER_TREE(Implementation, "psi.compiler.Implementation", OverloadValue);

    template<typename V>
    void MetadataType::visit(V& v) {
      visit_base<OverloadType>(v);
      v("type", &MetadataType::type);
    }
    
    const TreeVtable MetadataType::vtable = PSI_COMPILER_TREE(MetadataType, "psi.compiler.MetadataType", OverloadType);
    
    const SIVtable Metadata::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Metadata", OverloadValue);
    
    class ConstantMetadata : public Metadata {
    public:
      static const VtableType vtable;
      
      ConstantMetadata(const TreePtr<>& value_, const TreePtr<MetadataType>& type,
                       const OverloadPattern& pattern, const SourceLocation& location)
      : Metadata(&vtable, value_->compile_context(), type, pattern, location),
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
      return tree_from(::new ConstantMetadata(value, type, OverloadPattern(n_wildcards, pattern), location));
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
        
        if (!src[ii]->match(target[ii], Term::upref_match_ignore, match))
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
          if ((def->statement_mode == statement_mode_functional) && def->value->pure)
            my_term = def->value;
          else
            break;
        } else if (TreePtr<Statement> stmt = dyn_treeptr_cast<Statement>(my_term)) {
          if ((stmt->statement_mode == statement_mode_functional) && stmt->value->pure)
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
          if (v && (type == v->overload_type)) {
            const OverloadPattern& patt = v->overload_pattern();
            if (overload_pattern_match(patt.pattern, parameters, patt.n_wildcards, scratch))
              results.push_back(OverloadLookupResult(v, scratch));
          }
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
      const OverloadPattern& patt = overload->overload_pattern();
      bool s = overload_pattern_match(patt.pattern, parameters, patt.n_wildcards, match);
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
      const PSI_STD::vector<TreePtr<OverloadValue> >& type_values = type->values();
      for (unsigned ii = 0, ie = type_values.size(); ii != ie; ++ii) {
        const TreePtr<OverloadValue>& v = type_values[ii];
        PSI_ASSERT(v && (!v->overload_type || (v->overload_type == type)));
        const OverloadPattern& patt = v->overload_pattern();
        if(overload_pattern_match(patt.pattern, parameters, patt.n_wildcards, match_scratch))
          results.push_back(OverloadLookupResult(v, match_scratch));
      }
      
      for (PSI_STD::vector<TreePtr<OverloadValue> >::const_iterator ii = extra.begin(), ie = extra.end(); ii != ie; ++ii) {
        const TreePtr<OverloadValue>& v = *ii;
        PSI_ASSERT(v && (type == v->overload_type));
        const OverloadPattern& patt = v->overload_pattern();
        if(overload_pattern_match(patt.pattern, parameters, patt.n_wildcards, match_scratch))
          results.push_back(OverloadLookupResult(v, match_scratch));
      }
      
      for (unsigned ii = 0, ie = parameters.size(); ii != ie; ++ii) {
        if (parameters[ii]) {
          overload_lookup_search(type, parameters, parameters[ii], results, match_scratch);
          if (parameters[ii]->type)
            overload_lookup_search(type, parameters, parameters[ii]->type, results, match_scratch);
        }
      }
      
      if (results.empty()) {
        std::ostringstream overload_name;
        overload_name << type->location().logical->error_name(location.logical) << '(';
        for (unsigned ii = 0, ie = parameters.size(); ii != ie; ++ii) {
          if (ii) overload_name << ", ";
          overload_name << parameters[ii]->location().logical->error_name(location.logical);
        }
        overload_name << ')';
        
        CompileError err(type->compile_context().error_context(), location);
        err.info(boost::format("Could not find overload for %s") % overload_name.str());
        err.end_throw();
      }
      
      /* 
       * Attempt to select the most specific overload.
       * 
       * I require that this "most specific" overload 
       */
      unsigned best_idx = 0;
      for (unsigned ii = 1, ie = results.size(); ii < ie;) {
        const OverloadPattern& best_patt = results[best_idx].value->overload_pattern();
        const OverloadPattern& ii_patt = results[ii].value->overload_pattern();
        if (overload_pattern_match(best_patt.pattern, ii_patt.pattern, best_patt.n_wildcards, match_scratch)) {
          // best_idx matches anything ii matches, so ii is more specific
          best_idx = ii;
          ii++;
        } else if (overload_pattern_match(ii_patt.pattern, best_patt.pattern, ii_patt.n_wildcards, match_scratch)) {
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
        
        const OverloadPattern& best_patt = results[best_idx].value->overload_pattern();
        const OverloadPattern& ii_patt = results[ii].value->overload_pattern();
        if (!overload_pattern_match(ii_patt.pattern, best_patt.pattern, ii_patt.n_wildcards, match_scratch))
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
