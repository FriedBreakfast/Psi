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

namespace {
  std::string find_program_name(const char *path) {
    std::string exe_name(path);
    std::size_t name_pos = exe_name.find_last_of("/\\");
    if (name_pos != std::string::npos)
      exe_name = exe_name.substr(name_pos + 1);
    return exe_name;
  }
}

Psi::Compiler::TreePtr<Psi::Compiler::EvaluateContext> create_globals(const Psi::Compiler::TreePtr<Psi::Compiler::Module>& module) {
  using namespace Psi::Compiler;

  CompileContext& compile_context = module.compile_context();
  Psi::SourceLocation psi_location = module.location();
  
  PSI_STD::map<Psi::String, TreePtr<Term> > global_names;
  global_names["namespace"] = namespace_macro(compile_context, psi_location.named_child("namespace"));
  global_names["__none__"] = none_macro(compile_context, psi_location.named_child("__none__"));
  global_names["__number__"] = TreePtr<Term>();
  global_names["builtin_type"] = builtin_type_macro(compile_context, psi_location.named_child("builtin_type"));
  global_names["builtin_function"] = builtin_function_macro(compile_context, psi_location.named_child("builtin_function"));
  global_names["builtin_value"] = builtin_value_macro(compile_context, psi_location.named_child("builtin_function"));
  
  global_names["interface"] = interface_define_macro(compile_context, psi_location.named_child("interface"));
  global_names["implement"] = implementation_define_macro(compile_context, psi_location.named_child("implement"));
  global_names["macro"] = macro_define_macro(compile_context, psi_location.named_child("macro"));
  global_names["library"] = library_macro(compile_context, psi_location.named_child("library"));
  global_names["function"] = function_definition_macro(compile_context, psi_location.named_child("function"));

  return evaluate_context_dictionary(module, psi_location, global_names);
}

Psi::Parser::ParserLocation url_location(const Psi::String& url, const char *text_begin, const char *text_end) {
  using namespace Psi;
  using namespace Psi::Compiler;
  
  Parser::ParserLocation file_text;
  file_text.location.file.reset(new SourceFile());
  file_text.location.file->url = url;
  file_text.location.first_line = file_text.location.first_column = 1;
  file_text.location.last_line = file_text.location.last_column = 0;
  file_text.begin = text_begin;
  file_text.end = text_end;
  return file_text;
}

int main(int argc, char *argv[]) {
  std::string prog_name = find_program_name(argv[0]);
  
  if (argc < 2) {
    std::cerr << boost::format("Usage: %s [file] [arg] ...\n") % prog_name;
    return EXIT_FAILURE;
  }
  
  // argv[1] is the script name
  std::ifstream source_file(argv[1]);
  if (source_file.fail()) {
    std::cerr << boost::format("%s: cannot open %s: %s\n") % prog_name % argv[1] % strerror(errno);
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

  CompileContext compile_context(&std::cerr);
  TreePtr<Module> global_module(new Module(compile_context, "psi", compile_context.root_location().named_child("psi")));
  TreePtr<Module> my_module(new Module(compile_context, "main", compile_context.root_location()));
  TreePtr<EvaluateContext> root_evaluate_context = create_globals(global_module);
  TreePtr<EvaluateContext> module_evaluate_context = evaluate_context_module(my_module, root_evaluate_context, my_module.location());
  Parser::ParserLocation file_text = url_location(argv[1], source_text.get(), source_text.get() + source_length);
  
  PSI_STD::vector<SharedPtr<Parser::NamedExpression> > statements = Parser::parse_statement_list(file_text);
  
  // Code used to bootstrap into user program.
  std::string init = "main()";
  Parser::ParserLocation init_text = url_location("(init)", init.c_str(), init.c_str() + init.length());

  LogicalSourceLocationPtr root_location = LogicalSourceLocation::new_root_location();
  try {
    NamespaceCompileResult ns = compile_namespace(statements, module_evaluate_context, SourceLocation(file_text.location, root_location));
    ns.ns->complete();
    
    SourceLocation init_location(init_text.location, root_location);

    // Create only statement in main function
    SharedPtr<Parser::Expression> init_expr = Parser::parse_expression(init_text);
    TreePtr<EvaluateContext> init_evaluate_context = evaluate_context_dictionary(my_module, init_location, ns.entries);
    TreePtr<Term> init_tree = compile_expression(init_expr, init_evaluate_context, root_location);
    init_tree->complete();
    
    // Create main function
    TreePtr<FunctionType> main_type(new FunctionType(result_mode_by_value, compile_context.builtins().empty_type, default_, default_, init_location));
    TreePtr<Function> main_function(new Function(my_module, main_type, default_, init_tree, TreePtr<JumpTarget>(), init_location));
    
    void (*main_ptr) ();
    *reinterpret_cast<void**>(&main_ptr) = compile_context.jit_compile(main_function);
    main_ptr();
  } catch (CompileException&) {
    return EXIT_FAILURE;
  }
  
  return EXIT_SUCCESS;
}
