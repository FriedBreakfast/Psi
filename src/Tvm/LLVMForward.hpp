#ifndef HPP_PSI_TVM_LLVMFORWARD
#define HPP_PSI_TVM_LLVMFORWARD

/**
 * \file LLVM forward declarations
 *
 * This file contains forward declarations for LLVM. This avoids
 * external programs having to indirectly include LLVM header files
 * (and hence use/work around LLVM compilation flags). It's not really
 * a good idea from a proper C++ perspective, but it should make
 * working with Psi as a library easier.
 */

namespace llvm {
  class LLVMContext;
  class Module;
  class Value;
  class Type;
  class TargetMachine;
  class TargetData;
  class BasicBlock;
  class Instruction;
  class APFloat;
  class APInt;
  class fltSemantics;
  class ExecutionEngine;
  class Constant;
  class GlobalValue;

  class ConstantFolder;
  template<bool> class IRBuilderDefaultInserter;
  template<bool,typename,typename> class IRBuilder;

  namespace sys {
    /*
     * I have included this because the file which defines it in LLVM
     * also defines a type called \c alignof, which has become a keyword
     * in C++0x and therefore won't compile. The files involved are:
     *
     * llvm/System/Host.h
     * llvm/Supper/AlignOf.h
     */
    std::string getHostTriple();
  }
}

#endif
