#include "StaticDispatch.hpp"
#include "Tree.hpp"

#include <boost/format.hpp>

namespace Psi {
  namespace Compiler {
    OverloadType::OverloadType(const TreeVtable *vtable, CompileContext& compile_context, unsigned n_implicit_,
                               const PSI_STD::vector<TreePtr<Term> >& pattern_, const SourceLocation& location)
    : Tree(vtable, compile_context, location),
    n_implicit(n_implicit_),
    pattern(pattern_) {
    }
    
    const SIVtable OverloadType::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.OverloadType", Tree);
    
    OverloadValue::OverloadValue(const TreeVtable *vtable, const TreePtr<OverloadType>& type_, unsigned n_wildcards_,
                                 const PSI_STD::vector<TreePtr<Term> >& pattern_, const SourceLocation& location)
    : Tree(vtable, type_.compile_context(), location),
    overload_type(type_),
    n_wildcards(n_wildcards_),
    pattern(pattern_) {
    }
    
    const SIVtable OverloadValue::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.OverloadValue", Tree);
    
    Interface::Interface(const PSI_STD::vector<InterfaceBase>& bases_, const TreePtr<Term>& type_, unsigned n_implicit,
                         const std::vector<TreePtr<Term> >& pattern, const std::vector<TreePtr<Term> >& derived_pattern_,
                         const SourceLocation& location)
    : OverloadType(&vtable, type.compile_context(), n_implicit, pattern, location),
    derived_pattern(derived_pattern_),
    type(type_),
    bases(bases_) {
    }
    
    template<typename V>
    void Interface::visit(V& v) {
      visit_base<OverloadType>(v);
      v("derived_pattern", &Interface::derived_pattern)
      ("type", &Interface::type)
      ("bases", &Interface::bases);
    }
    
    const TreeVtable Interface::vtable = PSI_COMPILER_TREE(Interface, "psi.compiler.Interface", OverloadType);
    
    Implementation::Implementation(const PSI_STD::vector<TreePtr<Term> >& dependent_, const TreePtr<Term>& value_, const TreePtr<Interface>& interface,
                                   unsigned n_wildcards, const PSI_STD::vector<TreePtr<Term> >& pattern, const SourceLocation& location)
    : OverloadValue(&vtable, interface, n_wildcards, pattern, location),
    dependent(dependent_),
    value(value_) {
    }
    
    template<typename V>
    void Implementation::visit(V& v) {
      visit_base<OverloadValue>(v);
      v("dependent", &Implementation::dependent)
      ("value", &Implementation::value);
    }
    
    const TreeVtable Implementation::vtable = PSI_COMPILER_TREE(Implementation, "psi.compiler.Implementation", OverloadValue);
   
    MetadataType::MetadataType(CompileContext& compile_context, unsigned int n_implicit, const PSI_STD::vector<TreePtr<Term> >& pattern,
                               const SIType& type_, const SourceLocation& location)
    : OverloadType(&vtable, compile_context, n_implicit, pattern, location),
    type(type_) {
    }
    
    template<typename V>
    void MetadataType::visit(V& v) {
      visit_base<OverloadType>(v);
      v("type", &MetadataType::type);
    }
    
    const TreeVtable MetadataType::vtable = PSI_COMPILER_TREE(MetadataType, "psi.compiler.MetadataType", OverloadType);
    
    Metadata::Metadata(const TreePtr<>& value_, const TreePtr<MetadataType>& type, unsigned n_wildcards, const PSI_STD::vector<TreePtr<Term> >& pattern, const SourceLocation& location)
    : OverloadValue(&vtable, type, n_wildcards, pattern, location),
    value(value_) {
    }
    
    template<typename V>
    void Metadata::visit(V& v) {
      visit_base<OverloadValue>(v);
      v("value", &Metadata::value);
    }
    
    const TreeVtable Metadata::vtable = PSI_COMPILER_TREE(Metadata, "psi.compiler.Metadata", OverloadValue);
    
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
        
        if (!src[ii]->match(target[ii], match, 0))
          return false;
        
        match.push_back(target[ii]);
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
    void overload_lookup_search(const TreePtr<OverloadType>& type, const PSI_STD::vector<TreePtr<Term> >& parameters, const TreePtr<Term>& term,
                                std::vector<std::pair<PSI_STD::vector<TreePtr<Term> >, TreePtr<OverloadValue> > >& results,
                                PSI_STD::vector<TreePtr<Term> >& scratch) {
      TreePtr<Term> my_term = term;
      while (TreePtr<PointerType> ptr = dyn_treeptr_cast<PointerType>(term))
        my_term = ptr->target_type;
        
      if (TreePtr<TypeInstance> instance = dyn_treeptr_cast<TypeInstance>(my_term)) {
        const PSI_STD::vector<TreePtr<OverloadValue> >& overloads = instance->generic->overloads;
        for (unsigned ii = 0, ie = overloads.size(); ii != ie; ++ii) {
          if ((type == overloads[ii]->overload_type) && overload_pattern_match(overloads[ii]->pattern, parameters, overloads[ii]->n_wildcards, scratch))
            results.push_back(std::make_pair(scratch, overloads[ii]));
        }
        
        const PSI_STD::vector<TreePtr<Term> >& parameters = instance->parameters;
        for (unsigned ii = 0, ie = parameters.size(); ii != ie; ++ii)
          overload_lookup_search(type, parameters, parameters[ii], results, scratch);
      }
    }
    
    /**
     * \brief Get an overloaded value.
     */
    std::pair<PSI_STD::vector<TreePtr<Term> >, TreePtr<OverloadValue> >
    overload_lookup(const TreePtr<OverloadType>& type, const PSI_STD::vector<TreePtr<Term> >& parameters, const SourceLocation& location) {
      std::vector<std::pair<PSI_STD::vector<TreePtr<Term> >, TreePtr<OverloadValue> > > results;
      PSI_STD::vector<TreePtr<Term> > match_scratch;

      // Find all possible matching overloads
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
      for (unsigned ii = 1, ie = results.size(); ii != ie; ++ii) {
      }
      
      // Now check that the selected overload is more specific than all other overloads
      for (unsigned ii = 0, ie = results.size(); ii != ie; ++ii) {
        if (ii == best_idx)
          continue;
      }
      
      return results[best_idx];
    }

    /**
     * \brief Locate an interface implementation for a given set of parameters.
     *
     * \param metadata_type Metadata type to look up.
     * \param parameters Parameters.
     */    
    TreePtr<> metadata_lookup(const TreePtr<MetadataType>& metadata_type, const PSI_STD::vector<TreePtr<Term> >& parameters, const SourceLocation& location) {
      TreePtr<Metadata> md = treeptr_cast<Metadata>(overload_lookup(metadata_type, parameters, location).second);
      if (!metadata_type->type.isa(md->value.get()))
        metadata_type.compile_context().error_throw(location, boost::format("Value of metadata does not have the expected type: %s") % metadata_type->type->classname);
      return md->value;
    }
  }
}
