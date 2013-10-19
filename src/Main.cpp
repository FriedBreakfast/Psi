/*
 * Main function and associated routines for the interpreter (well, dynamic compiler really).
 */

#include "Config.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>

#include <boost/format.hpp>
#include <boost/scoped_array.hpp>

#include "Parser.hpp"
#include "Compiler.hpp"
#include "Tree.hpp"
#include "Macros.hpp"
#include "TermBuilder.hpp"
#include "Platform.hpp"

#include "Configuration.hpp"
#include "OptionParser.hpp"

namespace {
  enum OptionKeys {
    opt_key_help,
    opt_key_config,
    opt_key_set,
    opt_key_nodefault
  };
  
  struct OptionSet {
    Psi::PropertyValue configuration;
    std::string filename;
    std::vector<std::string> arguments;
  };
  
  bool parse_options(int argc, const char **argv, OptionSet& options) {
    std::string help_extra = " [file] [args] ...";
    Psi::OptionsDescription desc;
    desc.allow_unknown = false;
    desc.allow_positional = true;
    desc.opts.push_back(Psi::option_description(opt_key_help, false, 'h', "help", "Print this help"));
    desc.opts.push_back(Psi::option_description(opt_key_config, true, 'c', "config", "Read a configuration file"));
    desc.opts.push_back(Psi::option_description(opt_key_set, true, 's', "set", "Set a configuration property"));
    desc.opts.push_back(Psi::option_description(opt_key_nodefault, false, '\0', "nodefault", "Disable loading of default configuration files"));
    
    bool read_default = true;
    bool file_given = false;
    std::vector<std::string> config_files;
    std::vector<std::string> extra_config;
    
    Psi::OptionParser parser(desc, argc, argv);
    while (!parser.empty()) {
      Psi::OptionValue val;
      try {
        val = parser.next();
      } catch (Psi::OptionParseError& ex) {
        std::cerr << ex.what() << '\n';
        Psi::options_usage(argv[0], help_extra, "-h");
        return false;
      }
      
      switch (val.key) {
      case Psi::OptionValue::positional:
        file_given = true;
        options.filename = val.value;
        while (!parser.empty())
          options.arguments.push_back(parser.take());
        break;
        
      case opt_key_help:
        Psi::options_help(argv[0], help_extra, desc);
        return false;

      case opt_key_nodefault:
        read_default = false;
        break;
        
      case opt_key_config:
        config_files.push_back(val.value);
        break;
        
      case opt_key_set:
        extra_config.push_back(val.value);
        break;
        
      default: PSI_FAIL("Unexpected option key");
      }
    }

    // Check that the user has specified a file
    if (!file_given) {
      Psi::options_usage(argv[0], help_extra, "-h");
      return false;
    }
    
    // Load configuration
    options.configuration = Psi::PropertyValue();
    // Always load built in configuration
    Psi::configuration_builtin(options.configuration);
    if (read_default)
      Psi::configuration_read_files(options.configuration);
    // Read configuration implied by environment variables
    Psi::configuration_environment(options.configuration);

    for (std::vector<std::string>::const_iterator ii = config_files.begin(), ie = config_files.end(); ii != ie; ++ii)
      options.configuration.parse_file(*ii);
    for (std::vector<std::string>::const_iterator ii = extra_config.begin(), ie = extra_config.end(); ii != ie; ++ii)
      options.configuration.parse_configuration(ii->c_str());
    
    return true;
  }
}

Psi::Compiler::TreePtr<Psi::Compiler::EvaluateContext> create_globals(const Psi::Compiler::TreePtr<Psi::Compiler::Module>& module) {
  using namespace Psi::Compiler;

  CompileContext& compile_context = module->compile_context();
  Psi::SourceLocation psi_location = module->location();
  
  PSI_STD::map<Psi::String, TreePtr<Term> > global_names;
  global_names["namespace"] = namespace_macro(compile_context, psi_location.named_child("namespace"));
  //global_names["__number__"] = TreePtr<Term>();
  global_names["__brace__"] = string_macro(compile_context, psi_location.named_child("cstring"));
  global_names["number_type"] = number_type_macro(compile_context, psi_location.named_child("number_type"));
  global_names["builtin_function"] = builtin_function_macro(compile_context, psi_location.named_child("builtin_function"));
  global_names["number_value"] = number_value_macro(compile_context, psi_location.named_child("number_value"));
  
  global_names["type"] = compile_context.builtins().metatype;
  global_names["pointer"] = pointer_macro(compile_context, psi_location.named_child("pointer"));
  global_names["struct"] = struct_macro(compile_context, psi_location.named_child("struct"));

  global_names["new"] = new_macro(compile_context, psi_location.named_child("new"));
  global_names["interface"] = interface_define_macro(compile_context, psi_location.named_child("interface"));
  global_names["implement"] = implementation_define_macro(compile_context, psi_location.named_child("implement"));
  global_names["macro"] = macro_define_macro(compile_context, psi_location.named_child("macro"));
  global_names["library"] = library_macro(compile_context, psi_location.named_child("library"));
  global_names["function"] = function_definition_macro(compile_context, psi_location.named_child("function"));
  global_names["function_type"] = function_type_macro(compile_context, psi_location.named_child("function_type"));

  return evaluate_context_dictionary(module, psi_location, global_names);
}

Psi::Parser::Text url_location(const Psi::String& url, const char *text_begin, const char *text_end) {
  using namespace Psi;
  using namespace Psi::Compiler;
  
  PhysicalSourceLocation loc;
  loc.file.reset(new SourceFile);
  loc.file->url = url;
  loc.first_line = loc.first_column = 1;
  loc.last_line = loc.last_column = 0;
  
  return Parser::Text(loc, text_begin, text_end);
}

int main(int argc, const char **argv) {
  std::string prog_name = Psi::find_program_name(argv[0]);
  
  OptionSet opts;
  if (!parse_options(argc, argv, opts))
    return EXIT_FAILURE;
  
  // argv[1] is the script name
  std::ifstream source_file(opts.filename.c_str());
  if (source_file.fail()) {
    std::cerr << boost::format("%s: cannot open %s: %s\n") % prog_name % opts.filename % strerror(errno);
    return EXIT_FAILURE;
  }
  
  source_file.seekg(0, std::ios::end);
  std::streampos source_length = source_file.tellg();
  boost::scoped_array<char> source_text(new char[source_length]);
  source_file.seekg(0);
  source_file.read(source_text.get(), source_length);
  source_file.close();

  using namespace Psi;
  using namespace Psi::Compiler;

  CompileErrorContext error_context(&std::cerr);
  CompileContext compile_context(&error_context, opts.configuration);
  TreePtr<Module> global_module = Module::new_(compile_context, "psi", compile_context.root_location().named_child("psi"));
  TreePtr<Module> my_module = Module::new_(compile_context, "main", compile_context.root_location());
  TreePtr<EvaluateContext> root_evaluate_context = create_globals(global_module);
  TreePtr<EvaluateContext> module_evaluate_context = evaluate_context_module(my_module, root_evaluate_context, my_module->location());
  Parser::Text file_text = url_location(argv[1], source_text.get(), source_text.get() + source_length);
  
  PSI_STD::vector<SharedPtr<Parser::Statement> > statements = Parser::parse_namespace(error_context, my_module->location().logical, file_text);
  
  // Code used to bootstrap into user program.
  std::string init = "main()";
  Parser::Text init_text = url_location("(init)", init.c_str(), init.c_str() + init.length());

  LogicalSourceLocationPtr root_location = compile_context.root_location().logical;
  try {
    TreePtr<Namespace> ns = compile_namespace(statements, module_evaluate_context, SourceLocation(file_text.location, root_location));
    ns->complete();

    SourceLocation init_location(init_text.location, root_location);

    // Create only statement in main function
    SharedPtr<Parser::Expression> init_expr = Parser::parse_expression(error_context, compile_context.root_location().logical, init_text);
    TreePtr<EvaluateContext> init_evaluate_context = evaluate_context_dictionary(my_module, init_location, ns->members);
    TreePtr<Term> init_tree = compile_term(init_expr, init_evaluate_context, root_location);
    init_tree->complete();
    
    // Create main function
    TreePtr<FunctionType> main_type = TermBuilder::function_type(result_mode_functional, compile_context.builtins().empty_type, default_, default_, init_location);
    TreePtr<ModuleGlobal> main_function = TermBuilder::function(my_module, main_type, link_public, default_, default_, init_location, init_tree, "_Y_jit_entry");
    
    void (*main_ptr) ();
    *reinterpret_cast<void**>(&main_ptr) = compile_context.jit_compile(main_function);
    main_ptr();
  } catch (CompileException&) {
    return EXIT_FAILURE;
  }
  
  return EXIT_SUCCESS;
}
