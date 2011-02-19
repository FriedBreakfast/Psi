#include "Functional.hpp"
#include "Function.hpp"
#include "Number.hpp"

#include <boost/make_shared.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>

namespace Psi {
  namespace Tvm {
    class DisassemblerContext {
      enum DisassemblerMode {
        mode_module,
        mode_function,
        mode_block,
        mode_term
      };
      
      struct TermName {
        std::string name;
        FunctionTerm *context;
        bool anonymous;
        
        TermName(const std::string&,FunctionTerm*,bool);
      };
      
      struct TermNameSort {
        bool operator () (const boost::shared_ptr<TermName>&, const boost::shared_ptr<TermName>&) const;
      };
      
      DisassemblerMode m_mode;
      bool m_function_body;
      std::ostream *m_output;
      
      typedef std::tr1::unordered_map<Term*, boost::shared_ptr<TermName> > TermNameMap;
      TermNameMap m_names;
      std::tr1::unordered_set<Term*> m_visited_terms;
      typedef std::vector<Term*> TermDefinitionList;
      TermDefinitionList m_global_definitions;
      typedef std::tr1::unordered_map<BlockTerm*, TermDefinitionList> LocalTermDefinitionList;
      LocalTermDefinitionList m_local_definitions;
      unsigned m_parameter_name_index;
      typedef std::list<std::vector<std::string> > ParameterNameList;
      ParameterNameList m_parameter_names;
      
      static boost::shared_ptr<DisassemblerContext::TermName> make_term_name(Term*, FunctionTerm*);

      void setup_function(FunctionTerm*);
      void setup_function_body(FunctionTerm*);
      void setup_block_instructions(BlockTerm*);
      void setup_block_phis(BlockTerm*);
      void setup_term(Term*);
      void setup_term_definition(Term*);
      std::pair<bool, TermDefinitionList*> setup_term_name(Term *term);
      void build_unique_names();
      const std::string& name(Term*);

      void print_term(Term*,bool);
      void print_term_definition(Term*);
      void print_functional_term(FunctionalTerm*,bool);
      void print_instruction_term(InstructionTerm*);
      void print_phi_term(PhiTerm*);
      void print_function(FunctionTerm*);
      void print_function_type_term(FunctionTypeTerm*, FunctionTerm* =0);
      void print_definitions(const TermDefinitionList&, const char *line_prefix="");
      void print_block(BlockTerm*, const TermDefinitionList&);

    public:
      DisassemblerContext(std::ostream*);
      ~DisassemblerContext();
      
      void run_module(Module*);
      void run_function(FunctionTerm*);
      void run_block(BlockTerm*);
      void run_other(Term*);
    };

    DisassemblerContext::TermName::TermName(const std::string& name_, FunctionTerm *context_, bool anonymous_)
    : name(name_), context(context_), anonymous(anonymous_) {
    }
    
    DisassemblerContext::DisassemblerContext(std::ostream *output)
    : m_function_body(false), m_output(output), m_parameter_name_index(0) {
    }
    
    DisassemblerContext::~DisassemblerContext() {
    }
    
    boost::shared_ptr<DisassemblerContext::TermName> DisassemblerContext::make_term_name(Term *term, FunctionTerm *function) {
      if (function) {
        const FunctionTerm::TermNameMap& name_map = function->term_name_map();
        FunctionTerm::TermNameMap::const_iterator it = name_map.find(term);
        if (it != name_map.end())
          return boost::make_shared<TermName>(it->second, function, false);
        return boost::make_shared<TermName>("", function, true);
      } else if (GlobalTerm *global = dyn_cast<GlobalTerm>(term)) {
        return boost::make_shared<TermName>(global->name(), static_cast<FunctionTerm*>(0), false);
      } else {
        return boost::make_shared<TermName>("", function, true);
      }
    }
    
    bool DisassemblerContext::TermNameSort::operator () (const boost::shared_ptr<TermName>& lhs, const boost::shared_ptr<TermName>& rhs) const {
      if (!lhs->context && rhs->context)
        return true;
      else if (!rhs->context && lhs->context)
        return false;
      
      if (!lhs->anonymous && rhs->anonymous)
        return true;
      else if (!rhs->anonymous && lhs->anonymous)
        return false;
      
      return lhs->name < rhs->name;
    }

    void DisassemblerContext::build_unique_names() {
      std::vector<boost::shared_ptr<TermName> > names;
      for (TermNameMap::const_iterator ii = m_names.begin(), ie = m_names.end(); ii != ie; ++ii)
        names.push_back(ii->second);
      std::sort(names.begin(), names.end(), TermNameSort());
      
      std::tr1::unordered_set<std::string> used_names;
      std::tr1::unordered_map<std::string, unsigned> name_indices;
      
      std::stringstream ss;
      
      for (std::vector<boost::shared_ptr<TermName> >::iterator ii = names.begin(), ie = names.end(); ii != ie; ++ii) {
        TermName& name = **ii;
        std::tr1::unordered_map<std::string, unsigned>::iterator ji = name_indices.insert(std::make_pair(name.name, 0)).first;
        
        while (true) {
          ss.str("");
          ss << name.name;
          if (name.anonymous || (ji->second > 0))
            ss << ji->second;
          
          ++ji->second;
          if (used_names.find(ss.str()) == used_names.end())
            break;
        }
        
        name.name = "%";
        name.name += ss.str();
        used_names.insert(ss.str());
      }
    }

    const std::string& DisassemblerContext::name(Term *term) {
      TermNameMap::iterator it = m_names.find(term);
      PSI_ASSERT(it != m_names.end());
      return it->second->name;
    }

    void DisassemblerContext::run_module(Module *module) {
      m_mode = mode_module;
        
      for (Module::ModuleMemberList::iterator ii = module->members().begin(), ie = module->members().end(); ii != ie; ++ii)
        setup_term_definition(&*ii);
      
      build_unique_names();
      
      for (TermDefinitionList::iterator ii = m_global_definitions.begin(), ie = m_global_definitions.end(); ii != ie; ++ii) {
        *m_output << name(*ii) << " = define ";
        print_term(*ii, false);
      }
      
      for (Module::ModuleMemberList::iterator ib = module->members().begin(), ii = module->members().begin(), ie = module->members().end(); ii != ie; ++ii) {
        if (ii != ib)
          *m_output << '\n';
        print_term_definition(&*ii);
      }
    }
    
    void DisassemblerContext::run_function(FunctionTerm *function) {
      m_mode = mode_function;
      setup_function(function);
      setup_function_body(function);
      build_unique_names();
      print_definitions(m_global_definitions);
      print_function(function);
    }
    
    void DisassemblerContext::run_block(BlockTerm *block) {
      m_mode = mode_block;
      setup_function(block->function());
      m_function_body = true;
      setup_block_instructions(block);
      setup_block_phis(block);
      m_function_body = false;
      build_unique_names();
      print_block(block, m_global_definitions);
    }
    
    void DisassemblerContext::run_other(Term *term) {
      m_mode = mode_term;
      setup_term_definition(term);
      build_unique_names();
      print_definitions(m_global_definitions);
      print_term_definition(term);
    }

    void DisassemblerContext::setup_function(FunctionTerm *function) {
      for (unsigned ii = 0, ie = function->n_parameters(); ii != ie; ++ii) {
        Term *param = function->parameter(ii);
        setup_term(param->type());
        m_names.insert(std::make_pair(param, make_term_name(param, function)));
      }
      
      setup_term(function->result_type());

      if (function->entry()) {
        std::vector<BlockTerm*> blocks = function->topsort_blocks();
        for (std::vector<BlockTerm*>::iterator ii = blocks.begin(), ie = blocks.end(); ii != ie; ++ii) {
          BlockTerm *block = *ii;

          m_names.insert(std::make_pair(*ii, make_term_name(*ii, function)));

          for (BlockTerm::PhiList::iterator ji = block->phi_nodes().begin(), je = block->phi_nodes().end(); ji != je; ++ji)
            m_names.insert(std::make_pair(&*ji, make_term_name(&*ji, function)));
          
          for (BlockTerm::InstructionList::iterator ji = block->instructions().begin(), je = block->instructions().end(); ji != je; ++ji)
            m_names.insert(std::make_pair(&*ji, make_term_name(&*ji, function)));
        }
      }
    }
    
    void DisassemblerContext::setup_function_body(FunctionTerm *function) {
      m_function_body = true;
      std::vector<BlockTerm*> blocks = function->topsort_blocks();
      for (std::vector<BlockTerm*>::iterator ii = blocks.begin(), ie = blocks.end(); ii != ie; ++ii)
        setup_block_instructions(*ii);
      for (std::vector<BlockTerm*>::iterator ii = blocks.begin(), ie = blocks.end(); ii != ie; ++ii)
        setup_block_phis(*ii);
      m_function_body = false;
    }
    
    void DisassemblerContext::setup_block_phis(BlockTerm *block) {
      for (BlockTerm::PhiList::iterator ii = block->phi_nodes().begin(), ie = block->phi_nodes().end(); ii != ie; ++ii) {
        for (unsigned ji = 0, je = ii->n_incoming(); ji != je; ++ji)
          setup_term_definition(ii->incoming_value(ji));
      }
    }
      
    void DisassemblerContext::setup_block_instructions(BlockTerm *block) {
      for (BlockTerm::InstructionList::iterator ii = block->instructions().begin(), ie = block->instructions().end(); ii != ie; ++ii) {
        for (unsigned ji = 0, je = ii->n_parameters(); ji != je; ++ji)
          setup_term_definition(ii->parameter(ji));
      }
    }
    
    std::pair<bool, DisassemblerContext::TermDefinitionList*> DisassemblerContext::setup_term_name(Term *term) {
      if (!m_visited_terms.insert(term).second)
        return std::make_pair(false, static_cast<TermDefinitionList*>(0));

      PSI_ASSERT(m_names.find(term) == m_names.end());
      
      // Should this term be named?
      bool nameable = true;
      BlockTerm *block = 0;
      FunctionTerm *function = 0;
      if (term->source()) {
        switch (term->source()->term_type()) {
        case term_global_variable:
        case term_function: break;
        case term_block: block = cast<BlockTerm>(term->source()); break;
        case term_phi: block = cast<PhiTerm>(term->source())->block(); break;
        case term_instruction: block = cast<InstructionTerm>(term->source())->block(); break;
        case term_function_type_parameter: nameable = false; break;
        case term_function_parameter:
          function = cast<FunctionParameterTerm>(term->source())->function();
          if (term->term_type() == term_function_parameter) {
            nameable = true;
          } else {
            block = function->entry();
            nameable = m_function_body && block;
          }
          break;
        default: PSI_FAIL("unexpected source term type");
        }
      }
      
      TermDefinitionList *definition_list = 0;
      if (nameable) {
        if (block)
          function = block->function();
        
        m_names.insert(std::make_pair(term, make_term_name(term, function)));

        if (block && (m_mode == mode_module)) {
          LocalTermDefinitionList::iterator it = m_local_definitions.find(block);
          PSI_ASSERT(it != m_local_definitions.end());
          definition_list = &it->second;
        } else {
          definition_list = &m_global_definitions;
        }
      }
      
      return std::make_pair(true, definition_list);
    }
    
    void DisassemblerContext::setup_term_definition(Term* term) {
      if (!setup_term_name(term).first)
        return;

      switch (term->term_type()) {
      case term_global_variable: {
      GlobalVariableTerm *gvar = cast<GlobalVariableTerm>(term);
        setup_term(gvar->value_type());
        if (gvar->value())
          setup_term(gvar->value());
        return;
      }
      
      case term_function: {
        FunctionTerm *function = cast<FunctionTerm>(term);
        setup_function(function);
        setup_function_body(function);
        return;
      }
      
      case term_instruction: {
        InstructionTerm *insn = cast<InstructionTerm>(term);
        for (unsigned ii = 0, ie = insn->n_parameters(); ii != ie; ++ii)
          setup_term(insn->parameter(ii));
        return;
      }
      
      case term_phi: {
        PhiTerm *phi = cast<PhiTerm>(term);
        for (unsigned ii = 0, ie = phi->n_incoming(); ii != ie; ++ii) {
          setup_term(phi->incoming_block(ii));
          setup_term(phi->incoming_value(ii));
        }
        return;
      }
      
      default:
        setup_term(term);
        return;
      }
    }
    
    void DisassemblerContext::setup_term(Term *term) {
      std::pair<bool, TermDefinitionList*> name_setup_result = setup_term_name(term);
      if (!name_setup_result.first)
        return;
      
      switch (term->term_type()) {
      case term_apply:
      case term_recursive:
      case term_recursive_parameter:
        PSI_FAIL("not implemented");
        
      case term_functional: {
        FunctionalTerm *cast_term = cast<FunctionalTerm>(term);
        for (unsigned ii = 0, ie = cast_term->n_parameters(); ii != ie; ++ii)
          setup_term(cast_term->parameter(ii));
        break;
      }
        
      case term_function_type: {
        FunctionTypeTerm *cast_term = cast<FunctionTypeTerm>(term);
        for (unsigned ii = 0, ie = cast_term->n_parameters(); ii != ie; ++ii)
          setup_term(cast_term->parameter_type(ii));
        setup_term(cast_term->result_type());
        break;
      }
      
      case term_function_type_parameter:
      case term_function_parameter:
      case term_phi:
      case term_function:
      case term_block:
      case term_global_variable:
        name_setup_result.second = 0;
        break;
      
      case term_instruction:
        break;
        
      default:
        PSI_FAIL("unknown term type");
      }
      
      if (name_setup_result.second)
        name_setup_result.second->push_back(term);
    }
    
    void DisassemblerContext::print_term(Term *term, bool bracket) {
      TermNameMap::iterator name_it = m_names.find(term);
      if (name_it != m_names.end()) {
        *m_output << name_it->second->name;
        return;
      }
      
      switch (term->term_type()) {
      case term_functional: {
        print_functional_term(cast<FunctionalTerm>(term), bracket);
        break;
      }
      
      case term_function_type: {
        if (bracket)
          *m_output << '(';
        print_function_type_term(cast<FunctionTypeTerm>(term));
        if (bracket)
          *m_output << ')';
        break;
      }
      
      case term_apply:
        PSI_FAIL("not supported");
        
      default:
        PSI_FAIL("unexpected term type - this term should have had a name assigned");
      }
    }
    
    void DisassemblerContext::print_term_definition(Term *term) {
      *m_output << name(term) << " = ";
      
      switch (term->term_type()) {
      case term_functional: {
        print_functional_term(cast<FunctionalTerm>(term), false);
        *m_output << '\n';
        break;
      }
      
      case term_function_type: {
        print_function_type_term(cast<FunctionTypeTerm>(term));
        *m_output << '\n';
        break;
      }
      
      case term_instruction: {
        print_instruction_term(cast<InstructionTerm>(term));
        break;
      }
      
      case term_phi: {
        print_phi_term(cast<PhiTerm>(term));
        break;
      }
      
      case term_global_variable: {
        GlobalVariableTerm *gvar = cast<GlobalVariableTerm>(term);
        *m_output << name(gvar) << " = global ";
        if (gvar->constant())
          *m_output << "const ";
        print_term(gvar->value_type(), true);
        if (gvar->value())
          print_term(gvar->value(), true);
        *m_output << ";\n";
        return;
      }
      
      case term_function: {
        print_function(cast<FunctionTerm>(term));
        return;
      }
      
      case term_apply:
        PSI_FAIL("not supported");
        
      default:
        PSI_FAIL("unexpected term type - cannot print a definition");
      }
    }
   
    void DisassemblerContext::print_function_type_term(FunctionTypeTerm *term, FunctionTerm *use_names) {
      PSI_ASSERT(term->n_parameters() == use_names->n_parameters());

      *m_output << "function (";
      
      unsigned n_parameters = term->n_parameters();
      unsigned parameter_name_base = m_parameter_name_index;
      m_parameter_name_index += n_parameters;
      
      m_parameter_names.push_back(ParameterNameList::value_type());
      ParameterNameList::value_type& name_list = m_parameter_names.back();
      for (unsigned ii = 0; ii != n_parameters; ++ii) {
        if (ii)
          *m_output << ", ";
        *m_output << " : ";
        print_term(term->parameter_type(ii), false);
        
        std::string name;
        if (use_names) {
          name = m_names.find(use_names->parameter(ii))->second->name;
        } else {
          name = boost::lexical_cast<std::string>(parameter_name_base + ii);
        }
        name_list.push_back(boost::lexical_cast<std::string>(parameter_name_base + ii));
      }
      
      *m_output << ") > ";
      
      print_term(term->result_type(), false);
      
      m_parameter_names.pop_back();
      m_parameter_name_index = parameter_name_base;
    }
    
    void DisassemblerContext::print_functional_term(FunctionalTerm *term, bool bracket) {
      if (BooleanValue::Ptr bool_value = dyn_cast<BooleanValue>(term)) {
        *m_output << (bool_value->value() ? "true" : "false");
      } else if (IntegerType::Ptr int_type = dyn_cast<IntegerType>(term)) {
        if (!int_type->is_signed())
          *m_output << 'u';
        *m_output << 'i';
        const char *width;
        switch (int_type->width()) {
        case IntegerType::i8: width = "8"; break;
        case IntegerType::i16: width = "16"; break;
        case IntegerType::i32: width = "32"; break;
        case IntegerType::i64: width = "64"; break;
        case IntegerType::i128: width = "128"; break;
        case IntegerType::iptr: width = "ptr"; break;
        default: PSI_FAIL("unknown integer width");
        }
        *m_output << width;
      } else if (IntegerValue::Ptr int_value = dyn_cast<IntegerValue>(term)) {
        IntegerType::Ptr type = int_value->type();
        *m_output << '#';
        if (!type->is_signed())
          *m_output << 'u';
        char width;
        switch (type->width()) {
        case IntegerType::i8: width = 'b'; break;
        case IntegerType::i16: width = 's'; break;
        case IntegerType::i32: width = 'i'; break;
        case IntegerType::i64: width = 'l'; break;
        case IntegerType::i128: width = 'q'; break;
        case IntegerType::iptr: width = 'p'; break;
        default: PSI_FAIL("unknown integer width");
        }
        *m_output << width;
        int_value->value().print(*m_output, type->is_signed());
      } else if (FloatType::Ptr float_type = dyn_cast<FloatType>(term)) {
        const char *width;
        switch (float_type->width()) {
        case FloatType::fp32: width = "fp32"; break;
        case FloatType::fp64: width = "fp64"; break;
        case FloatType::fp128: width = "fp128"; break;
        case FloatType::fp_x86_80: width = "fp-x86-80"; break;
        case FloatType::fp_ppc_128: width = "fp-ppc-128"; break;
        default: PSI_FAIL("unknown integer width");
        }
        *m_output << width;
      } else if (FunctionTypeResolvedParameter::Ptr resolved_param = dyn_cast<FunctionTypeResolvedParameter>(term)) {
        ParameterNameList::reverse_iterator it = m_parameter_names.rbegin();
        PSI_ASSERT(resolved_param->depth() < m_parameter_names.size());
        std::advance(it, resolved_param->depth());
        PSI_ASSERT(resolved_param->index() < it->size());
        *m_output << (*it)[resolved_param->index()];
      } else {
        unsigned n_parameters = term->n_parameters();
        if (n_parameters == 0) {
          *m_output << term->operation();
        } else {
          if (bracket)
            *m_output << '(';
          
          *m_output << term->operation();
          for (unsigned ii = 0; ii != n_parameters; ++ii) {
            *m_output << ' ';
            print_term(term->parameter(ii), true);
          }
          
          if (bracket)
            *m_output << ')';
        }
      }
    }
    
    void DisassemblerContext::print_instruction_term(InstructionTerm *term) {
      unsigned n_parameters = term->n_parameters();
      *m_output << term->operation();
      for (unsigned ii = 0; ii != n_parameters; ++ii) {
        *m_output << ' ';
        print_term(term->parameter(ii), true);
      }
      *m_output << ";\n";
    }
    
    void DisassemblerContext::print_phi_term(PhiTerm* term) {
      *m_output << "phi ";
      print_term(term->type(), true);
      *m_output << ": ";
      for (unsigned ii = 0, ie = term->n_incoming(); ii != ie; ++ii) {
        if (ii)
          *m_output << ", ";
        *m_output << name(term->incoming_block(ii)) << " > ";
        print_term(term->incoming_value(ii), true);
      }
      *m_output << ";\n";
    }
    
    void DisassemblerContext::print_function(FunctionTerm *term) {
      *m_output << name(term) << " = ";
      print_function_type_term(term->function_type(), term);
      *m_output << " {\n";
      
      std::vector<BlockTerm*> blocks = term->topsort_blocks();
      for (std::vector<BlockTerm*>::iterator ii = blocks.begin(), ie = blocks.end(); ii != ie; ++ii)
        print_block(*ii, m_local_definitions.find(*ii)->second);
      
      *m_output << "};\n";
    }

    void DisassemblerContext::print_block(BlockTerm *block, const TermDefinitionList& definitions) {
      *m_output << "block " << name(block) << ":\n";
      for (BlockTerm::PhiList::iterator ii = block->phi_nodes().begin(), ie = block->phi_nodes().end(); ii != ie; ++ii) {
        *m_output << "  ";
        print_term_definition(&*ii);
      }
      print_definitions(definitions, "  ");
    }
    
    void DisassemblerContext::print_definitions(const TermDefinitionList& definitions, const char *line_prefix) {
      for (TermDefinitionList::const_iterator ii = definitions.begin(), ie = definitions.end(); ii != ie; ++ii) {
        *m_output << line_prefix;
        print_term_definition(*ii);
      }
    }
    
    /**
     * \brief Print the entire contents of a module.
     */
    void print_module(std::ostream& os, Module *module) {
      DisassemblerContext context(&os);
      context.run_module(module);
    }
    
    /**
     * \brief Print a term to an output stream.
     * 
     * The format of the term is dependent on its type.
     */
    void print_term(std::ostream& os, Term *term) {
      DisassemblerContext context(&os);
      if (FunctionTerm *function = dyn_cast<FunctionTerm>(term)) {
        context.run_function(function);
      } else if (BlockTerm *block = dyn_cast<BlockTerm>(term)) {
        context.run_block(block);
      } else {
        context.run_other(term);
      }
    }
  }
}
