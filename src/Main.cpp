/*
 * Main function and associated routines for the interpreter (well, dynamic compiler really).
 */

#include "Config.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>
#include <string>

#if PSI_HAVE_READLINE
#include <readline.h>
#include <history.h>
#endif

#include <boost/format.hpp>
#include <boost/scoped_array.hpp>

#include "Parser.hpp"
#include "Compiler.hpp"
#include "Tree.hpp"
#include "Macros.hpp"
#include "TermBuilder.hpp"
#include "Platform/Platform.hpp"

#include "Configuration.hpp"
#include "OptionParser.hpp"

namespace {
  enum OptionKeys {
    opt_key_help,
    opt_key_config,
    opt_key_set,
    opt_key_nodefault,
    opt_key_testprompt
  };
  
  struct OptionSet {
    std::string program_name;
    Psi::PropertyValue configuration;
    boost::optional<std::string> filename;
    std::vector<std::string> arguments;
    bool test_prompt;
  };
  
  bool parse_options(int argc, const char **argv, OptionSet& options) {
    options.program_name = Psi::find_program_name(argv[0]);
    options.test_prompt = false;
    
    std::string help_extra = " [file] [args] ...";
    Psi::OptionsDescription desc;
    desc.allow_unknown = false;
    desc.allow_positional = true;
    desc.opts.push_back(Psi::option_description(opt_key_help, false, 'h', "help", "Print this help"));
    desc.opts.push_back(Psi::option_description(opt_key_config, true, 'c', "config", "Read a configuration file"));
    desc.opts.push_back(Psi::option_description(opt_key_set, true, 's', "set", "Set a configuration property"));
    desc.opts.push_back(Psi::option_description(opt_key_nodefault, false, '\0', "nodefault", "Disable loading of default configuration files"));
    desc.opts.push_back(Psi::option_description(opt_key_testprompt, false, '\0', "testprompt", "Disable interpreter prompt and print a null character to separate error logs. Used for automated testing."));
    
    bool read_default = true;
    std::vector<std::string> config_files;
    std::vector<std::string> extra_config;
    
    Psi::OptionParser parser(desc, argc, argv);
    while (!parser.empty()) {
      Psi::OptionValue val;
      try {
        val = parser.next();
      } catch (Psi::OptionParseError& ex) {
        std::cerr << ex.what() << '\n';
        Psi::options_usage(std::cerr, options.program_name, help_extra, "-h");
        return false;
      }
      
      switch (val.key) {
      case Psi::OptionValue::positional:
        options.filename = val.value;
        while (!parser.empty())
          options.arguments.push_back(parser.take());
        break;
        
      case opt_key_help:
        Psi::options_help(std::cerr, options.program_name, help_extra, desc);
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
        
      case opt_key_testprompt:
        options.test_prompt = true;
        break;
        
      default: PSI_FAIL("Unexpected option key");
      }
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
  global_names["number_value"] = number_value_macro(compile_context, psi_location.named_child("number_value"));
  
  global_names["type"] = compile_context.builtins().metatype;
  global_names["pointer"] = pointer_macro(compile_context, psi_location.named_child("pointer"));
  global_names["struct"] = struct_macro(compile_context, psi_location.named_child("struct"));
  
  global_names["bool"] = TermBuilder::boolean_type(compile_context);
  
  global_names["byte"] = TermBuilder::number_type(compile_context, NumberType::n_i8);
  global_names["short"] = TermBuilder::number_type(compile_context, NumberType::n_i16);
  global_names["int"] = TermBuilder::number_type(compile_context, NumberType::n_i32);
  global_names["long"] = TermBuilder::number_type(compile_context, NumberType::n_i64);
  global_names["size"] = TermBuilder::number_type(compile_context, NumberType::n_iptr);

  global_names["ubyte"] = TermBuilder::number_type(compile_context, NumberType::n_u8);
  global_names["ushort"] = TermBuilder::number_type(compile_context, NumberType::n_u16);
  global_names["uint"] = TermBuilder::number_type(compile_context, NumberType::n_u32);
  global_names["ulong"] = TermBuilder::number_type(compile_context, NumberType::n_u64);
  global_names["usize"] = TermBuilder::number_type(compile_context, NumberType::n_uptr);
  
  global_names["__init__"] = lifecycle_init_macro(compile_context, psi_location.named_child("__init__"));
  global_names["__fini__"] = lifecycle_fini_macro(compile_context, psi_location.named_child("__fini__"));
  global_names["__move__"] = lifecycle_move_macro(compile_context, psi_location.named_child("__move__"));
  global_names["__copy__"] = lifecycle_copy_macro(compile_context, psi_location.named_child("__copy__"));
  global_names["__no_move__"] = lifecycle_no_move_macro(compile_context, psi_location.named_child("__no_move__"));
  global_names["__no_copy__"] = lifecycle_no_copy_macro(compile_context, psi_location.named_child("__no_copy__"));

  global_names["new"] = new_macro(compile_context, psi_location.named_child("new"));
  global_names["interface"] = interface_define_macro(compile_context, psi_location.named_child("interface"));
  global_names["macro"] = macro_define_macro(compile_context, psi_location.named_child("macro"));
  global_names["library"] = library_macro(compile_context, psi_location.named_child("library"));
  global_names["function"] = function_macro(compile_context, psi_location.named_child("function"));

  return evaluate_context_dictionary(module, psi_location, global_names);
}

Psi::Parser::Text url_location(const Psi::String& url, const Psi::SharedPtrHandle& data_handle, const char *text_begin, const char *text_end, unsigned first_line=1) {
  using namespace Psi;
  using namespace Psi::Compiler;
  
  PhysicalSourceLocation loc;
  loc.file.reset(new SourceFile);
  loc.file->url = url;
  loc.first_line = first_line;
  loc.first_column = 1;
  loc.last_line = loc.last_column = 0;
  
  return Parser::Text(loc, data_handle, text_begin, text_end);
}

/**
 * Run a file.
 */
int psi_interpreter_run_file(const OptionSet& opts) {
  Psi::SharedPtr<std::vector<char> > source_text(new std::vector<char>);
  
  std::filebuf file_input_buffer;
  std::streambuf *input_buffer_ptr;
  if (*opts.filename == "-") {
    // Read from standard input
    input_buffer_ptr = std::cin.rdbuf();
  } else {
    input_buffer_ptr = file_input_buffer.open(opts.filename->c_str(), std::ios::in);
    if (!input_buffer_ptr) {
      std::cerr << boost::format("%s: cannot open %s: %s\n") % opts.program_name % *opts.filename % strerror(errno);
      return EXIT_FAILURE;
    }
  }
  
  std::copy(std::istreambuf_iterator<char>(input_buffer_ptr),
            std::istreambuf_iterator<char>(),
            std::back_inserter(*source_text));
  
  if (file_input_buffer.is_open())
    file_input_buffer.close();
  
  using namespace Psi;
  using namespace Psi::Compiler;

  CompileErrorContext error_context(&std::cerr);
  CompileContext compile_context(&error_context, opts.configuration);
  TreePtr<Module> global_module = Module::new_(compile_context, "psi", compile_context.root_location().named_child("psi"));
  TreePtr<Module> my_module = Module::new_(compile_context, "main", compile_context.root_location());
  TreePtr<EvaluateContext> root_evaluate_context = create_globals(global_module);
  TreePtr<EvaluateContext> module_evaluate_context = evaluate_context_module(my_module, root_evaluate_context, my_module->location());
  Parser::Text file_text = url_location(*opts.filename, source_text, Psi::vector_begin_ptr(*source_text), Psi::vector_end_ptr(*source_text));
  
  PSI_STD::vector<SharedPtr<Parser::Statement> > statements = Parser::parse_namespace(error_context, my_module->location().logical, file_text);
  
  // Code used to bootstrap into user program.
  std::string init = "main()";
  Parser::Text init_text = url_location("(init)", Psi::SharedPtrHandle(), init.c_str(), init.c_str() + init.length());

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

namespace {
#if PSI_HAVE_READLINE
  template<typename T>
  class ScopedMallocPtr : public boost::noncopyable {
    T *m_ptr;
    
  public:
    ScopedMallocPtr(T *ptr) : m_ptr(ptr) {}
    ~ScopedMallocPtr() {free(m_ptr);}
    T *get() {return m_ptr;}
  };
#endif
  
  boost::optional<std::string> interpreter_read_line(bool test_mode, const char *prompt) {
#if PSI_HAVE_READLINE
    if (!test_mode) {
      ScopedMallocPtr<char> line(readline(prompt));
      if (line.get()) {
        if (*line.get())
          add_history(line.get());
        return std::string(line.get());
      } else {
        std::cout << std::endl;
        return boost::none;
      }
    }
#endif

    if (!std::cin.good()) {
      std::cout << std::endl;
      return boost::none;
    }
    if (!test_mode)
      std::cout << prompt;
    std::string result;
    std::getline(std::cin, result);
    return result;
  }

  /**
   * \brief Check that there is a closing bracket for every opening bracket.
   * 
   * Note that in cases where there is a closing bracket without the corresponding opening bracket
   * the user must have made an error, and therefore \c true is returned so that the resulting
   * statement can be parsed immediately and the error reported, even though brackets are not
   * balanced.
   * 
   * If the input ends in a backslash, the backslash is removed and \c false is returned,
   * so that the user may explicitly continue from one line to the next.
   */
  bool input_finished(std::string& input) {
    int brace_depth = 0, square_bracket_depth = 0, bracket_depth = 0;
    
    for (std::string::const_iterator ii = input.begin(), ie = input.end(); ii != ie; ++ii) {
      char c = *ii;
      if (c == '\\') {
        ++ii;
        if (ii == ie) {
          if ((bracket_depth == 0) && (square_bracket_depth == 0) && (brace_depth == 0))
            input.erase(input.end() - 1);
          return false; // Explicit continuation
        }
      } else if (c == '{') {
        ++brace_depth;
      } else if (c == '}') {
        if (brace_depth == 0)
          return true;
        --brace_depth;
      } else if (brace_depth == 0) {
        if (c == '[') {
          ++square_bracket_depth;
        } else if (c == ']') {
          if (square_bracket_depth == 0)
            return true;
          --square_bracket_depth;
        } else if (square_bracket_depth == 0) {
          if (c == '(') {
            ++bracket_depth;
          } else if (c == ')') {
            if (bracket_depth == 0)
              return true;
            --bracket_depth;
          }
        }
      }

    }

    return (bracket_depth == 0) && (square_bracket_depth == 0) && (brace_depth == 0);
  }
}

namespace {  
  using namespace Psi::Compiler;
  
  class EvaluateCallback {
    unsigned m_statement_count;
    
  public:
    EvaluateCallback(unsigned statement_count) : m_statement_count(statement_count) {}
    
    TreePtr<Term> operator () (unsigned index, const TreePtr<Term>& value, const Psi::SourceLocation& location) const {
      if (index + 1 == m_statement_count) {
        /// \todo Print the result
        return TreePtr<Term>();
      } else {
        return TreePtr<Term>();
      }
    }
  };
}

/**
 * Read-eval-print loop.
 */
int psi_interpreter_repl(const OptionSet& opts) {
  using namespace Psi;
  using namespace Psi::Compiler;
  
  unsigned line_no = 0;
  
  CompileErrorContext error_context(&std::cerr);
  CompileContext compile_context(&error_context, opts.configuration);

  SourceLocation input_location = compile_context.root_location().named_child("_input");
  
  TreePtr<Module> global_module = Module::new_(compile_context, "psi", compile_context.root_location().named_child("psi"));
  TreePtr<EvaluateContext> root_evaluate_context = create_globals(global_module);
  
  std::map<String, TreePtr<Term> > names;
  
  while (true) {
    unsigned start_line = ++line_no;
    boost::optional<std::string> maybe_input = interpreter_read_line(opts.test_prompt, ">>> ");
    if (!maybe_input)
      return EXIT_SUCCESS;
    std::string input = *maybe_input;
    
    while (!input_finished(input)) {
      input += '\n';
      ++line_no;
      boost::optional<std::string> continuation = interpreter_read_line(opts.test_prompt, "... ");
      if (!continuation)
        return EXIT_FAILURE; // Quit mid-command
      input += *continuation;
    }
    
    try {
      std::ostringstream unique_ss;
      unique_ss << start_line;
      std::string unique = unique_ss.str();

      SourceLocation location = input_location.named_child(unique);
      location.physical.first_column = location.physical.last_column = 1;
      location.physical.first_line = start_line;
      location.physical.last_line = line_no;

      SharedPtr<std::vector<char> > data(new std::vector<char>(input.begin(), input.end()));
      Parser::Text text = url_location("(input)", data, Psi::vector_begin_ptr(*data), Psi::vector_end_ptr(*data), start_line);
      PSI_STD::vector<SharedPtr<Parser::Statement> > statements = Parser::parse_statement_list(error_context, location.logical, text);
      
      TreePtr<Module> my_module = Module::new_(compile_context, "input_" + unique, location);

      TreePtr<EvaluateContext> evaluate_context = evaluate_context_dictionary(my_module, location, names, root_evaluate_context);
      CompileScriptResult script = compile_script(statements, evaluate_context, EvaluateCallback(statements.size()), location);

      // Force immediate compilation and loading
      compile_context.jit_compile_many(script.globals);

      // Only add names to map if they compiled correctly
      for (PSI_STD::map<String, TreePtr<Term> >::const_iterator ii = script.names.begin(), ie = script.names.end(); ii != ie; ++ii)
        names[ii->first] = ii->second;
    } catch (CompileException&) {
      // Error details should already have been printed, so ignore error
    }
    
    if (opts.test_prompt) {
      std::cout << '\0' << std::flush;
      std::cerr << '\0' << std::flush;
    }
  }
  
  return EXIT_SUCCESS;
}

int main(int argc, const char **argv) {
  OptionSet opts;
  if (!parse_options(argc, argv, opts))
    return EXIT_FAILURE;
  
  if (opts.filename)
    return psi_interpreter_run_file(opts);
  else
    return psi_interpreter_repl(opts);
}
