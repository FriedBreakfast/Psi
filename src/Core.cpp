#include "Core.hpp"

#include <llvm/Target/TargetSelect.h>

/*
 * I declare this here because the llvm/System/Host.h, which I should
 * include, llvm/Support/AlignOf.hpp which defines a type named
 * alignof, violating C++0x.
 */
namespace llvm {
namespace sys {
  std::string getHostTriple();
}
}

namespace Psi {
  Context::Context() {
    init_llvm();
    init_types();
  }

  Context::~Context() {
    delete m_llvm_target_machine;
  }

  void Context::init_llvm() {
    llvm::InitializeNativeTarget();

    std::string host = llvm::sys::getHostTriple();

    std::string error_msg;
    const llvm::Target *target = llvm::TargetRegistry::lookupTarget(host, error_msg);
    if (!target)
      throw std::runtime_error("Could not get LLVM JIT target: " + error_msg);

    m_llvm_target_machine = target->createTargetMachine(host, "");
    if (!m_llvm_target_machine)
      throw std::runtime_error("Failed to create target machine");

    m_llvm_target_data = m_llvm_target_machine->getTargetData();
  }

  void Context::init_types() {
    m_type_void = NULL;
    m_type_size = NULL;
    m_type_char = NULL;
    m_type_int8 = NULL;
    m_type_uint8 = NULL;
    m_type_int16 = NULL;
    m_type_uint16 = NULL;
    m_type_int32 = NULL;
    m_type_uint32 = NULL;
    m_type_int64 = NULL;
    m_type_uint64 = NULL;
    m_type_real32 = NULL;
    m_type_real64 = NULL;
    m_type_real128 = NULL;
  }

  Term::Term() : m_llvm_value_built(false), m_llvm_type_built(false) {
  }
}
