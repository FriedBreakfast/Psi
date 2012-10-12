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
    
    OverloadValue::OverloadValue(const TreeVtable *vtable, const TreePtr<OverloadType>& type_,
                                 const PSI_STD::vector<TreePtr<Term> >& pattern_, const SourceLocation& location)
    : Tree(vtable, type_.compile_context(), location),
    overload_type(type_),
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
    
    Implementation::Implementation(const PSI_STD::vector<TreePtr<Term> >& dependent_, const TreePtr<Term>& value_,
                                   const TreePtr<Interface>& interface, const PSI_STD::vector<TreePtr<Term> >& pattern, const SourceLocation& location)
    : OverloadValue(&vtable, interface, pattern, location),
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
    
    Metadata::Metadata(const TreePtr<>& value_, const TreePtr<MetadataType>& type, const PSI_STD::vector<TreePtr<Term> >& pattern, const SourceLocation& location)
    : OverloadValue(&vtable, type, pattern, location),
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
     * \param src Pattern to be matched. This may be longer than \c target,
     * in which case it is assumed that the initial parameters are to be
     * found by pattern matching.
     * \param target Wildcard parameters in the target pattern which should
     * be found by pattern matching.
     * \param match Holds values of inferred initial parameters after a
     * successful match. Note that this will be modified whether the
     * match is successful or not.
     * 
     * \return Whether the match was successful.
     */
    bool overload_pattern_match(const PSI_STD::vector<TreePtr<Term> >& src,
                                const PSI_STD::vector<TreePtr<Term> >& target,
                                PSI_STD::vector<TreePtr<Term> >& match) {
      if (target.size() > src.size())
        return false;
      
      unsigned offset = src.size() - target.size();
      match.reserve(src.size());
      match.assign(offset, TreePtr<Term>());
      
      for (unsigned ii = 0, ie = src.size(); ii != ie; ++ii) {
        PSI_ASSERT(target[ii]);
        PSI_ASSERT(src[ii+offset]);
        
        if (!src[ii+offset]->match(target[ii], match, 0))
          return false;
        
        match.push_back(target[ii]);
      }
      
      // Check all wildcards are set and match expected pattern
      match.resize(offset);
      for (unsigned ii = 0; ii != offset; ++ii) {
        if (!match[ii])
          return false;
      }
      
      return true;
    }
    
    void overload_lookup_search(const TreePtr<OverloadType>& type, const PSI_STD::vector<TreePtr<Term> >& pattern, const TreePtr<Term>& term,
                                std::vector<std::pair<PSI_STD::vector<TreePtr<Term> >, TreePtr<OverloadValue> > >& results,
                                PSI_STD::vector<TreePtr<Term> >& scratch) {
      TreePtr<Term> my_term = term;
      while (TreePtr<PointerType> ptr = dyn_treeptr_cast<PointerType>(term))
        my_term = ptr->target_type;
        
      if (TreePtr<TypeInstance> instance = dyn_treeptr_cast<TypeInstance>(my_term)) {
        const PSI_STD::vector<TreePtr<OverloadValue> >& overloads = instance->generic->overloads;
        for (unsigned ii = 0, ie = overloads.size(); ii != ie; ++ii) {
          if ((type == overloads[ii]->overload_type) && overload_pattern_match(overloads[ii]->pattern, pattern, scratch))
            results.push_back(std::make_pair(scratch, overloads[ii]));
        }
        
        const PSI_STD::vector<TreePtr<Term> >& parameters = instance->parameters;
        for (unsigned ii = 0, ie = parameters.size(); ii != ie; ++ii)
          overload_lookup_search(type, pattern, parameters[ii], results, scratch);
      }
    }
    
    /**
     * \brief Get an overloaded value.
     */
    std::pair<PSI_STD::vector<TreePtr<Term> >, TreePtr<OverloadValue> >
    overload_lookup(const TreePtr<OverloadType>& type, const PSI_STD::vector<TreePtr<Term> >& pattern, const SourceLocation& location) {
      std::vector<std::pair<PSI_STD::vector<TreePtr<Term> >, TreePtr<OverloadValue> > > results;
      PSI_STD::vector<TreePtr<Term> > match_scratch;

      // Find all possible matching overloads
      for (unsigned ii = 0, ie = pattern.size(); ii != ie; ++ii) {
        if (pattern[ii]) {
          overload_lookup_search(type, pattern, pattern[ii], results, match_scratch);
          if (pattern[ii]->type)
            overload_lookup_search(type, pattern, pattern[ii]->type, results, match_scratch);
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
    
    /**
     * \brief Check whether this tree, which is a pattern, matches a given value.
     *
     * \param value Tree to match to.
     * \param wildcards Substitutions to be identified.
     * \param depth Number of parameter-enclosing terms above this match.
     */
    bool Term::match(const TreePtr<Term>& value, PSI_STD::vector<TreePtr<Term> >& wildcards, unsigned depth) const {
      // Unwrap any Statements involved
      const Term *self = this;
      while (const Statement *stmt = dyn_tree_cast<Statement>(self))
        self = stmt->value.get();
      
      const Term *other = value.get();
      while (const Statement *stmt = dyn_tree_cast<Statement>(other))
        other = stmt->value.get();
      
      if (self == other)
        return true;

      if (!self)
        return false;

      if (const Parameter *parameter = dyn_tree_cast<Parameter>(self)) {
        if (parameter->depth == depth) {
          // Check type also matches
          if (!parameter->type->match(other->type, wildcards, depth))
            return false;

          TreePtr<Term>& wildcard = wildcards[parameter->index];
          if (wildcard) {
            return wildcard->match(TreePtr<Term>(other), wildcards, depth);
          } else {
            wildcards[parameter->index].reset(other);
            return true;
          }
        }
      }

      if (si_vptr(self) == si_vptr(other)) {
        // Trees are required to have the same static type to work with pattern matching.
        return derived_vptr(this)->match(this, other, &wildcards, depth);
      } else {
        return false;
      }
    }
  }
}
