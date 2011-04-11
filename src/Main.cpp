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
  
  std::vector<boost::shared_ptr<Psi::Parser::NamedExpression> > statements =
    Psi::Parser::parse_statement_list(source_text.get(), source_text.get() + source_length);
  
  std::cout << boost::format("Number of statements: %s\n") % statements.size();

  return EXIT_SUCCESS;
}