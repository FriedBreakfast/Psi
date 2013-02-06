#include "Functional.hpp"
#include "Function.hpp"
#include "Number.hpp"
#include "Instructions.hpp"
#include "Recursive.hpp"
#include "Parser.hpp"

#include <list>

#include <boost/make_shared.hpp>
#include <boost/format.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/unordered_set.hpp>
#include <boost/concept_check.hpp>

namespace Psi {
  namespace Tvm {
    class DisassemblerContext {
    public:
      struct TermNamePrinter {
        const std::string *name;
        TermNamePrinter(const std::string *name_) : name(name_) {}
      };

    private:
      struct TermName {
        std::string name;
        ValuePtr<Function> context;
        bool anonymous;
        
        TermName(const std::string&,const ValuePtr<Function>&,bool);
      };
      
      struct TermNameSort {
        bool operator () (const boost::shared_ptr<TermName>&, const boost::shared_ptr<TermName>&) const;
      };
      
      bool m_in_function_mode;
      bool m_function_body;
      std::ostream *m_output;
      
      typedef boost::unordered_map<ValuePtr<>, boost::shared_ptr<TermName> > TermNameMap;
      TermNameMap m_names;
      boost::unordered_set<ValuePtr<> > m_visited_terms;
      boost::unordered_set<ValuePtr<> > m_defined_terms;
      typedef std::vector<ValuePtr<> > TermDefinitionList;
      TermDefinitionList m_global_definitions;
      typedef boost::unordered_map<ValuePtr<Block>, TermDefinitionList> LocalTermDefinitionList;
      LocalTermDefinitionList m_local_definitions;
      unsigned m_parameter_name_index;
      typedef std::list<std::vector<std::string> > ParameterNameList;
      ParameterNameList m_parameter_names;
      
      static boost::shared_ptr<DisassemblerContext::TermName> make_term_name(const ValuePtr<>&, const ValuePtr<Function>&);

      void setup_function(const ValuePtr<Function>&);
      void setup_block_instructions(const ValuePtr<Block>&);
      void setup_block_phis(const ValuePtr<Block>&);
      void setup_term(const ValuePtr<>&);
      void setup_term_definition(const ValuePtr<>&);
      void setup_term_name(const ValuePtr<>&);
      TermDefinitionList* term_definition_list(const ValuePtr<>&);
      void build_unique_names();
      TermNamePrinter name(const ValuePtr<>&);

      void print_term(const ValuePtr<>&,bool);
      void print_term_definition(const ValuePtr<>&,bool=false);
      void print_functional_term(const ValuePtr<FunctionalValue>&,bool);
      void print_instruction_term(const ValuePtr<Instruction>&);
      void print_phi_term(const ValuePtr<Phi>&);
      void print_function(const ValuePtr<Function>&);
      void print_function_type_term(const ValuePtr<FunctionType>&, bool bracket, const ValuePtr<Function>& =ValuePtr<Function>());
      void print_apply_term(const ValuePtr<ApplyValue>& apply, bool bracket);
      void print_exists(const ValuePtr<Exists>& term, bool bracket);
      void print_recursive(const ValuePtr<RecursiveType>& recursive);
      void print_definitions(const TermDefinitionList&, const char* ="", bool=false);
      void print_block(const ValuePtr<Block>&, const TermDefinitionList&);

    public:
      DisassemblerContext(std::ostream*);
      ~DisassemblerContext();
      
      void run_module(Module*);
      void run_term(const ValuePtr<>&);
    };

    std::ostream& operator << (std::ostream& os, const DisassemblerContext::TermNamePrinter& printer) {
      os << '%';
      for (std::string::const_iterator ii = printer.name->begin(), ie = printer.name->end(); ii != ie; ++ii) {
        if (Parser::token_char(*ii))
          os << *ii;
        else
          os << '%' << ((unsigned(*ii) & 0xF0) >> 8) << (unsigned(*ii) & 0xF);
      }
      
      return os;
    }

    DisassemblerContext::TermName::TermName(const std::string& name_, const ValuePtr<Function>& context_, bool anonymous_)
    : name(name_), context(context_), anonymous(anonymous_) {
    }
    
    DisassemblerContext::DisassemblerContext(std::ostream *output)
    : m_in_function_mode(false), m_function_body(false), m_output(output), m_parameter_name_index(0) {
    }
    
    DisassemblerContext::~DisassemblerContext() {
    }
    
    boost::shared_ptr<DisassemblerContext::TermName> DisassemblerContext::make_term_name(const ValuePtr<>& term, const ValuePtr<Function>& function) {
      if (function) {
        const Function::TermNameMap& name_map = function->term_name_map();
        Function::TermNameMap::const_iterator it = name_map.find(term);
        if (it != name_map.end())
          return boost::make_shared<TermName>(it->second, function, false);
        return boost::make_shared<TermName>("", function, true);
      } else if (ValuePtr<Global> global = dyn_cast<Global>(term)) {
        return boost::make_shared<TermName>(global->name(), ValuePtr<Function>(), false);
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
      std::stable_sort(names.begin(), names.end(), TermNameSort());
      
      boost::unordered_set<std::string> used_names;
      boost::unordered_map<std::string, unsigned> name_indices;
      
      boost::format name_formatter("%s"), number_formatter("%s%s");
      
      for (std::vector<boost::shared_ptr<TermName> >::iterator ii = names.begin(), ie = names.end(); ii != ie; ++ii) {
        TermName& name = **ii;
        boost::unordered_map<std::string, unsigned>::iterator ji = name_indices.insert(std::make_pair(name.name, 0)).first;

        std::string numbered_name;
        while (true) {
          if (name.anonymous || (ji->second > 0))
            numbered_name = str(number_formatter % name.name % ji->second);
          else
            numbered_name = name.name;
          
          ++ji->second;
          if (used_names.find(numbered_name) == used_names.end())
            break;
        }
        
        name.name = str(name_formatter % numbered_name);
        used_names.insert(numbered_name);
      }
    }

    DisassemblerContext::TermNamePrinter DisassemblerContext::name(const ValuePtr<>& term) {
      TermNameMap::iterator it = m_names.find(term);
      PSI_ASSERT(it != m_names.end());
      return TermNamePrinter(&it->second->name);
    }

    void DisassemblerContext::run_module(Module *module) {
      for (Module::ModuleMemberList::iterator ii = module->members().begin(), ie = module->members().end(); ii != ie; ++ii)
        setup_term_definition(ii->second);
      
      build_unique_names();
      
      print_definitions(m_global_definitions, "", true);
      
      for (Module::ModuleMemberList::iterator ib = module->members().begin(), ii = module->members().begin(), ie = module->members().end(); ii != ie; ++ii) {
        if (ii != ib)
          *m_output << '\n';
        print_term_definition(ii->second);
      }
    }
    
    void DisassemblerContext::run_term(const ValuePtr<>& term) {
      switch (term->term_type()) {
      case term_function: {
        ValuePtr<Function> function = value_cast<Function>(term);
        setup_term_name(function);
        setup_function(function);
        build_unique_names();
        print_definitions(m_global_definitions, "", true);
        print_function(function);
        break;
      }
      
      case term_block: {
        ValuePtr<Block> block = value_cast<Block>(term);
        m_in_function_mode = true;
        setup_term_name(block);
        setup_block_instructions(block);
        setup_block_phis(block);
        build_unique_names();
        print_block(block, m_global_definitions);
        break;
      }
      
      default:
        m_in_function_mode = true;
        setup_term_definition(term);
        build_unique_names();
        print_definitions(m_global_definitions);
        
        switch (term->term_type()) {
        case term_instruction:
        case term_phi:
        case term_function_parameter:
        case term_recursive:
          print_term_definition(term, true);
          break;
          
        default:
          break;
        }
        break;
      }
    }

    void DisassemblerContext::setup_function(const ValuePtr<Function>& function) {
      for (Function::ParameterList::const_iterator ii = function->parameters().begin(), ie = function->parameters().end(); ii != ie; ++ii)
        setup_term_definition(*ii);
      
      setup_term(function->result_type());

      for (Function::BlockList::const_iterator ii = function->blocks().begin(), ie = function->blocks().end(); ii != ie; ++ii) {
        const ValuePtr<Block>& block = *ii;

        setup_term_name(block);

        for (Block::PhiList::const_iterator ji = block->phi_nodes().begin(), je = block->phi_nodes().end(); ji != je; ++ji)
          m_names.insert(std::make_pair(*ji, make_term_name(*ji, function)));
        
        for (Block::InstructionList::const_iterator ji = block->instructions().begin(), je = block->instructions().end(); ji != je; ++ji)
          m_names.insert(std::make_pair(*ji, make_term_name(*ji, function)));

        for (Function::BlockList::const_iterator ji = function->blocks().begin(), je = function->blocks().end(); ji != je; ++ji) {
          setup_term_name(*ji);
          setup_block_instructions(*ji);
          setup_block_phis(*ji);
        }
      }
    }
    
    void DisassemblerContext::setup_block_phis(const ValuePtr<Block>& block) {
      for (Block::PhiList::const_iterator ii = block->phi_nodes().begin(), ie = block->phi_nodes().end(); ii != ie; ++ii)
        setup_term_definition(*ii);
    }
      
    void DisassemblerContext::setup_block_instructions(const ValuePtr<Block>& block) {
      for (Block::InstructionList::const_iterator ii = block->instructions().begin(), ie = block->instructions().end(); ii != ie; ++ii)
        setup_term_definition(*ii);
    }
    
    DisassemblerContext::TermDefinitionList* DisassemblerContext::term_definition_list(const ValuePtr<>& term) {
#ifdef PSI_DEBUG
      switch (term->term_type()) {
      case term_global_variable:
      case term_function:
      case term_block:
      case term_phi:
      case term_parameter_placeholder:
      case term_function_parameter:
        PSI_FAIL("term type should not go in definition lists");
        
      default:
        break;
      }
#endif

      // Should this term be named?
      ValuePtr<Block> block;
      ValuePtr<Function> function;
      if (Value *source = term->disassembler_source()) {
        switch (source->term_type()) {
        case term_global_variable:
        case term_function:
          return &m_global_definitions;

        case term_block: block.reset(value_cast<Block>(source)); break;
        case term_phi: block = value_cast<Phi>(source)->block(); break;
        case term_instruction: block = value_cast<Instruction>(source)->block(); break;

        case term_parameter_placeholder: return NULL;

        case term_function_parameter:
          function = value_cast<FunctionParameter>(source)->function();
          block = function->blocks().front();
          break;

        default: PSI_FAIL("unexpected source term type");
        }
      }

      if (m_in_function_mode || (!block && !function))
        return &m_global_definitions;
      
      if (block)
        return &m_local_definitions[block];
      
      return NULL;
    }
    
    void DisassemblerContext::setup_term_name(const ValuePtr<>& term) {
      // Should this term be named?
      ValuePtr<Function> function;
      if (Value *source = term->disassembler_source()) {
        switch (source->term_type()) {
        case term_global_variable:
        case term_function: break;
        case term_recursive: break;
        case term_block: function = value_cast<Block>(source)->function(); break;
        case term_phi: function = value_cast<Phi>(source)->block()->function(); break;
        case term_instruction: function = value_cast<Instruction>(source)->block()->function(); break;
        case term_function_parameter: function = value_cast<FunctionParameter>(source)->function(); break;
        case term_parameter_placeholder: return;
        default: PSI_FAIL("unexpected source term type");
        }
      }
      
      m_names.insert(std::make_pair(term, make_term_name(term, function)));
    }
    
    void DisassemblerContext::setup_term_definition(const ValuePtr<>& term) {
      if (!m_defined_terms.insert(term).second)
        return;
      
      setup_term_name(term);

      switch (term->term_type()) {
      case term_recursive: {
        ValuePtr<RecursiveType> recursive = value_cast<RecursiveType>(term);
        for (RecursiveType::ParameterList::const_iterator ii = recursive->parameters().begin(), ie = recursive->parameters().end(); ii != ie; ++ii)
          setup_term(*ii);
        setup_term(recursive->result_type());
        if (recursive->result())
          setup_term(recursive->result());
        break;
      }
        
      case term_global_variable: {
        ValuePtr<GlobalVariable> gvar = value_cast<GlobalVariable>(term);
        setup_term(gvar->value_type());
        if (gvar->value())
          setup_term(gvar->value());
        break;
      }
      
      case term_function: {
        ValuePtr<Function> function = value_cast<Function>(term);
        setup_function(function);
        break;
      }
      
      case term_function_parameter: {
        ValuePtr<FunctionParameter> param = value_cast<FunctionParameter>(term);
        setup_term(param->type());
        break;
      }
      
      case term_instruction: {
        class MyVisitor : public InstructionVisitor {
          DisassemblerContext *m_self;
        public:
          MyVisitor(DisassemblerContext *self) : m_self(self) {}
          virtual void next(ValuePtr<>& v) {if (v) m_self->setup_term(v);}
        };
        
        MyVisitor my_visitor(this);
        ValuePtr<Instruction> insn = value_cast<Instruction>(term);
        insn->instruction_visit(my_visitor);
        m_local_definitions[insn->block()].push_back(insn);
        break;
      }
      
      case term_phi: {
        ValuePtr<Phi> phi = value_cast<Phi>(term);
        const std::vector<PhiEdge>& edges = phi->edges();
        for (std::vector<PhiEdge>::const_iterator ii = edges.begin(), ie = edges.end(); ii != ie; ++ii) {
          setup_term(ii->block);
          setup_term(ii->value);
        }
        break;
      }
      
      default:
        setup_term(term);
        break;
      }
    }
    
    void DisassemblerContext::setup_term(const ValuePtr<>& term) {
      if (!m_visited_terms.insert(term).second)
        return;

      setup_term_name(term);
      
      switch (term->term_type()) {
      case term_apply: {
        ValuePtr<ApplyValue> apply = value_cast<ApplyValue>(term);
        setup_term(apply->recursive());
        for (std::vector<ValuePtr<> >::const_iterator ii = apply->parameters().begin(), ie = apply->parameters().end(); ii != ie; ++ii)
          setup_term(*ii);
        
        if (TermDefinitionList *dl = term_definition_list(term))
          dl->push_back(term);
        break;
      }
      
      case term_exists: {
        ValuePtr<Exists> exists = value_cast<Exists>(term);
        setup_term(exists->result());
        if (TermDefinitionList *dl = term_definition_list(term))
          dl->push_back(term);
        break;
      }
        
      case term_recursive_parameter: {
        setup_term(term->type());
        break;
      }
        
      case term_functional: {
        class MyVisitor : public FunctionalValueVisitor {
          DisassemblerContext *m_self;
        public:
          MyVisitor(DisassemblerContext *self) : m_self(self) {}
          virtual void next(const ValuePtr<>& v) {if (v) m_self->setup_term(v);}
        };
        
        MyVisitor my_visitor(this);
        value_cast<FunctionalValue>(term)->functional_visit(my_visitor);
        
        if (TermDefinitionList *dl = term_definition_list(term))
          dl->push_back(term);
        break;
      }
        
      case term_function_type: {
        ValuePtr<FunctionType> cast_term = value_cast<FunctionType>(term);
        const std::vector<ValuePtr<> >& parameter_types = cast_term->parameter_types();
        for (std::vector<ValuePtr<> >::const_iterator ii = parameter_types.begin(), ie = parameter_types.end(); ii != ie; ++ii)
          setup_term(*ii);
        setup_term(cast_term->result_type());

        if (TermDefinitionList *dl = term_definition_list(term))
          dl->push_back(term);
        break;
      }
      
      default:
        break;
      }
    }
    
    void DisassemblerContext::print_term(const ValuePtr<>& term, bool bracket) {
      if (!term) {
        *m_output << "NULL";
        return;
      }
      
      TermNameMap::iterator name_it = m_names.find(term);
      if (name_it != m_names.end()) {
        *m_output << TermNamePrinter(&name_it->second->name);
        return;
      }
      
      switch (term->term_type()) {
      case term_functional: {
        print_functional_term(value_cast<FunctionalValue>(term), bracket);
        break;
      }
      
      case term_function_type: {
        if (bracket)
          *m_output << '(';
        print_function_type_term(value_cast<FunctionType>(term), bracket);
        if (bracket)
          *m_output << ')';
        break;
      }
      
      case term_apply: {
        print_apply_term(value_cast<ApplyValue>(term), bracket);
        break;
      }

      case term_exists: {
        print_exists(value_cast<Exists>(term), bracket);
        return;
      }
        
      default:
        PSI_FAIL("unexpected term type - this term should have had a name assigned");
      }
    }
    
    void DisassemblerContext::print_term_definition(const ValuePtr<>& term, bool global) {
      *m_output << name(term) << " = ";
      
      switch (term->term_type()) {
      case term_functional: {
        if (global)
          *m_output << "define ";
        print_functional_term(value_cast<FunctionalValue>(term), false);
        *m_output << ";\n";
        break;
      }
      
      case term_function_type: {
        if (global)
          *m_output << "define ";
        print_function_type_term(value_cast<FunctionType>(term), false);
        *m_output << ";\n";
        break;
      }
      
      case term_instruction: {
        print_instruction_term(value_cast<Instruction>(term));
        break;
      }
      
      case term_phi: {
        print_phi_term(value_cast<Phi>(term));
        break;
      }
      
      case term_global_variable: {
        ValuePtr<GlobalVariable> gvar = value_cast<GlobalVariable>(term);
        *m_output << "global ";
        if (gvar->constant())
          *m_output << "const ";
        print_term(gvar->value_type(), true);
        if (gvar->value()) {
          *m_output << ' ';
          print_term(gvar->value(), true);
        }
        *m_output << ";\n";
        return;
      }
      
      case term_function: {
        print_function(value_cast<Function>(term));
        return;
      }
      
      case term_function_parameter: {
        ValuePtr<FunctionParameter> parameter = value_cast<FunctionParameter>(term);
        ValuePtr<Function> function = parameter->function();
        unsigned n = 0;
        for (Function::ParameterList::const_iterator ii = function->parameters().begin(), ie = function->parameters().end(); ii != ie; ++ii, ++n) {
          if (parameter == *ii) {
            *m_output << "[function parameter " << n << "]\n";
            return;
          }
        }
        *m_output << "[invalid function parameter]\n";
        return;
      }
      
      case term_apply: {
        if (global)
          *m_output << "define ";
        print_apply_term(value_cast<ApplyValue>(term), false);
        *m_output << ";\n";
        return;
      }
      
      case term_exists: {
        if (global)
          *m_output << "define ";
        print_exists(value_cast<Exists>(term), false);
        *m_output << ";\n";
        return;
      }
      
      case term_recursive: {
        print_recursive(value_cast<RecursiveType>(term));
        return;
      }
        
      default:
        PSI_FAIL("unexpected term type - cannot print a definition");
      }
    }
   
    void DisassemblerContext::print_function_type_term(const ValuePtr<FunctionType>& term, bool bracket, const ValuePtr<Function>& use_names) {
      PSI_ASSERT(!use_names || (term->parameter_types().size() == use_names->parameters().size()));
      
      if (bracket)
        *m_output << '(';

      *m_output << "function";
      switch (term->calling_convention()) {
      case cconv_c: break;
      case cconv_x86_stdcall: *m_output << " cc_x86_stdcall";
      case cconv_x86_thiscall: *m_output << " cc_x86_thiscall";
      case cconv_x86_fastcall: *m_output << " cc_x86_fastcall";
      default: PSI_FAIL("Unknown calling convention in disassembler");
      }
      if (term->sret())
        *m_output << " sret";
      *m_output << " (";
      
      unsigned n_parameters = term->parameter_types().size();
      unsigned parameter_name_base = m_parameter_name_index;
      m_parameter_name_index += n_parameters;
      
      boost::format name_formatter("%%%s");
      
      m_parameter_names.push_back(ParameterNameList::value_type());
      ParameterNameList::value_type& name_list = m_parameter_names.back();
      for (unsigned ii = 0; ii != n_parameters; ++ii) {
        if (ii)
          *m_output << ", ";
        
        std::string name;
        if (use_names) {
          *m_output << TermNamePrinter(&m_names.find(use_names->parameters().at(ii))->second->name);
        } else {
          *m_output << name_formatter % (parameter_name_base + ii);
        }
        
        *m_output << " : ";
        print_term(term->parameter_types()[ii], false);
        
        name_list.push_back(name);
      }
      
      *m_output << ") > ";
      
      print_term(term->result_type(), true);
      
      m_parameter_names.pop_back();
      m_parameter_name_index = parameter_name_base;
      
      if (bracket)
        *m_output << ')';
    }
    
    void DisassemblerContext::print_exists(const ValuePtr<Exists>& term, bool bracket) {
      if (bracket)
        *m_output << '(';
      *m_output << "exists (";
      
      unsigned n_parameters = term->parameter_types().size();
      unsigned parameter_name_base = m_parameter_name_index;
      m_parameter_name_index += n_parameters;
      
      boost::format name_formatter("%%%s");
      
      m_parameter_names.push_back(ParameterNameList::value_type());
      ParameterNameList::value_type& name_list = m_parameter_names.back();
      for (unsigned ii = 0; ii != n_parameters; ++ii) {
        if (ii)
          *m_output << ", ";
        
        std::string name = str(name_formatter % (parameter_name_base + ii));
        *m_output << name << " : ";
        print_term(term->parameter_types()[ii], false);
        
        name_list.push_back(name);
      }
      
      *m_output << ") > ";
      
      print_term(term->result(), true);
      
      m_parameter_names.pop_back();
      m_parameter_name_index = parameter_name_base;
      
      if (bracket)
        *m_output << ')';
    }

    void DisassemblerContext::print_functional_term(const ValuePtr<FunctionalValue>& term, bool bracket) {
      if (ValuePtr<BooleanValue> bool_value = dyn_cast<BooleanValue>(term)) {
        *m_output << (bool_value->value() ? "true" : "false");
      } else if (ValuePtr<IntegerType> int_type = dyn_cast<IntegerType>(term)) {
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
      } else if (ValuePtr<IntegerValue> int_value = dyn_cast<IntegerValue>(term)) {
        ValuePtr<IntegerType> type = int_value->type();
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
      } else if (ValuePtr<FloatType> float_type = dyn_cast<FloatType>(term)) {
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
      } else if (ValuePtr<ResolvedParameter> resolved_param = dyn_cast<ResolvedParameter>(term)) {
        ParameterNameList::reverse_iterator it = m_parameter_names.rbegin();
        if (resolved_param->depth() < m_parameter_names.size()) {
          std::advance(it, resolved_param->depth());
          PSI_ASSERT(resolved_param->index() < it->size());
          *m_output << (*it)[resolved_param->index()];
        } else {
          *m_output << "[unknown parameter]";
        }
      } else {
        class MyVisitor : public FunctionalValueVisitor {
          DisassemblerContext *m_self;
          const char *m_operation;
          bool m_bracket;
          bool m_first;
          
        public:
          MyVisitor(DisassemblerContext *self, const char *operation, bool bracket)
          : m_self(self), m_operation(operation), m_bracket(bracket), m_first(true) {}
          
          virtual void next(const ValuePtr<>& ptr) {
            if (m_first) {
              if (m_bracket)
                *m_self->m_output << '(';
              *m_self->m_output << m_operation;
              m_first = false;
            }
            
            *m_self->m_output << ' ';
            m_self->print_term(ptr, true);
          }
          
          bool empty() const {return m_first;}
        };
        
        MyVisitor my_visitor(this, term->operation_name(), bracket);
        term->functional_visit(my_visitor);
        
        if (my_visitor.empty()) {
          *m_output << term->operation_name();
        } else if (bracket) {
          *m_output << ')';
        }
      }
    }

    void DisassemblerContext::print_apply_term(const ValuePtr<ApplyValue>& apply, bool bracket) {
      if (bracket)
        *m_output << '(';
      
      *m_output << "apply ";
      print_term(apply->recursive(), true);
      for (std::vector<ValuePtr<> >::const_iterator ii = apply->parameters().begin(), ie = apply->parameters().end(); ii != ie; ++ii) {
        *m_output << ' ';
        print_term(*ii, true);
      }
        
      if (bracket)
        *m_output << ')';
    }

    void DisassemblerContext::print_instruction_term(const ValuePtr<Instruction>& term) {
      *m_output << term->operation_name();
      
      class MyVisitor : public InstructionVisitor {
        DisassemblerContext *m_self;
        
      public:
        MyVisitor(DisassemblerContext *self) : m_self(self) {}
        
        virtual void next(ValuePtr<>& ptr) {
          *m_self->m_output << ' ';
          m_self->print_term(ptr, true);
        }
      };
      
      MyVisitor my_visitor(this);
      term->instruction_visit(my_visitor);
      *m_output << ";\n";
    }
    
    void DisassemblerContext::print_phi_term(const ValuePtr<Phi>& term) {
      *m_output << "phi ";
      print_term(term->type(), true);
      *m_output << ": ";
      const std::vector<PhiEdge>& edges = term->edges();
      bool first = true;
      for (std::vector<PhiEdge>::const_iterator ii = edges.begin(), ie = edges.end(); ii != ie; ++ii) {
        if (first)
          first = false;
        else
          *m_output << ", ";
        *m_output << name(ii->block) << " > ";
        print_term(ii->value, true);
      }
      *m_output << ";\n";
    }
    
    void DisassemblerContext::print_recursive(const ValuePtr<RecursiveType>& term) {
      *m_output << "recursive (";
      
      unsigned parameter_name_base = m_parameter_name_index, parameter_name_idx = m_parameter_name_index;
      m_parameter_name_index += term->parameters().size();
      
      boost::format name_formatter("%%%s");
      
      m_parameter_names.push_back(ParameterNameList::value_type());
      ParameterNameList::value_type& name_list = m_parameter_names.back();
      for (RecursiveType::ParameterList::const_iterator ib = term->parameters().begin(), ii = term->parameters().begin(), ie = term->parameters().end(); ii != ie; ++ii, ++parameter_name_idx) {
        if (ii != ib)
          *m_output << ", ";
        
        std::string name = str(name_formatter % parameter_name_idx);
        
        *m_output << name << " : ";
        print_term(*ii, false);
        
        name_list.push_back(name);
      }
      
      *m_output << ") > ";
      
      print_term(term->result_type(), true);
      *m_output << " > ";
      if (term->result())
        print_term(term->result(), true);
      else
        *m_output << "NULL";
      
      m_parameter_names.pop_back();
      m_parameter_name_index = parameter_name_base;
      
      
      *m_output << ";\n";
    }

    void DisassemblerContext::print_function(const ValuePtr<Function>& term) {
      print_function_type_term(term->function_type(), term);
      if (!term->blocks().empty()) {
        *m_output << " {\n";
        
        for (Function::BlockList::const_iterator ii = term->blocks().begin(), ie = term->blocks().end(); ii != ie; ++ii)
          print_block(*ii, m_local_definitions[*ii]);
        
        *m_output << "}";
      }
      *m_output << ";\n";
    }

    void DisassemblerContext::print_block(const ValuePtr<Block>& block, const TermDefinitionList& definitions) {
      *m_output << "block " << name(block) << ":\n";
      for (Block::PhiList::const_iterator ii = block->phi_nodes().begin(), ie = block->phi_nodes().end(); ii != ie; ++ii) {
        *m_output << "  ";
        print_term_definition(*ii);
      }
      print_definitions(definitions, "  ");
    }
    
    void DisassemblerContext::print_definitions(const TermDefinitionList& definitions, const char *line_prefix, bool global) {
      for (TermDefinitionList::const_iterator ii = definitions.begin(), ie = definitions.end(); ii != ie; ++ii) {
        *m_output << line_prefix;
        print_term_definition(*ii, global);
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
    void print_term(std::ostream& os, const ValuePtr<>& term) {
      DisassemblerContext context(&os);
      context.run_term(term);
    }
  }
}
