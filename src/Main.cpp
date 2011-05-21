/*
 * Main function and associated routines for the interpreter (well, dynamic compiler really).
 */

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

namespace {
  std::string find_program_name(const char *path) {
    std::string exe_name(path);
    std::size_t name_pos = exe_name.find_last_of("/\\");
    if (name_pos != std::string::npos)
      exe_name = exe_name.substr(name_pos + 1);
    return exe_name;
  }
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

  PhysicalSourceLocation core_physical_location;
  core_physical_location.file.reset(new SourceFile());
  core_physical_location.first_line = core_physical_location.first_column = 0;
  core_physical_location.last_line = core_physical_location.last_column = 0;
  SourceLocation core_location(core_physical_location, make_logical_location(SharedPtr<LogicalSourceLocation>(), "psi"));

  Parser::ParserLocation file_text;
  file_text.location.file.reset(new SourceFile());
  file_text.location.file->url = argv[1];
  file_text.location.first_line = file_text.location.first_column = 1;
  file_text.location.last_line = file_text.location.last_column = 0;
  file_text.begin = source_text.get();
  file_text.end = source_text.get() + source_length;
  
  ArrayList<SharedPtr<Parser::NamedExpression> > statements = Parser::parse_statement_list(file_text);

  CompileContext compile_context(&std::cerr);
  
  std::map<String, TreePtr<> > global_names;
  global_names["function"] = function_definition_object(compile_context);

  TreePtr<CompileImplementation> root_evaluate_context = evaluate_context_dictionary(compile_context, core_location, global_names);

  GCPtr<Tree> compiled_statements;
  try {
    compiled_statements = compile_statement_list(statements, root_evaluate_context, SourceLocation(file_text.location, SharedPtr<LogicalSourceLocation>()));
    compiled_statements->complete();
  } catch (CompileException&) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
