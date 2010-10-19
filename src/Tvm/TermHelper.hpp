#ifndef HPP_TVM_TERM_HELPER
#define HPP_TVM_TERM_HELPER

/**
 * \file
 *
 * Helper classes for building functional and instruction terms.
 */

#include "Core.hpp"

#include <stdexcept>

namespace Psi {
  namespace Tvm {
    template<typename Derived>
    class PrimitiveType {
    public:
      TermPtr<> type(Context& context, std::size_t n_parameters, Term *const*) const {
        if (n_parameters != 0)
          throw std::logic_error("primitive type created with parameters");
        return context.get_metatype();
      }

      LLVMFunctionBuilder::Result llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
        return llvm_value_constant(builder.constant_builder(), term);
      }

      LLVMConstantBuilder::Constant llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const {
        LLVMConstantBuilder::Type ty = static_cast<const Derived*>(this)->llvm_type(builder, term);
        return builder.metatype_value(ty.type());
      }
    };

    template<typename Derived>
    class PrimitiveValue {
    public:
      void check_primitive_parameters(std::size_t n, Term*const*) const {
        if (n)
          throw std::logic_error("primitive value created with parameters");
      }

      LLVMConstantBuilder::Type llvm_type(LLVMConstantBuilder&, FunctionalTerm&) const {
        throw std::logic_error("the type of a term cannot be a primitive value");
      }

      LLVMFunctionBuilder::Result llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
        return static_cast<const Derived*>(this)->llvm_value_constant(builder.constant_builder(), term);
      }
    };
  }
}

#endif