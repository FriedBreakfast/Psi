#ifndef HPP_PSI_TVM_PRIMITIVE
#define HPP_PSI_TVM_PRIMITIVE

/**
 * \file
 *
 * Helper classes for building functional and instruction terms.
 */

#include "Core.hpp"
#include "LLVMBuilder.hpp"

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

      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
        return llvm_value_constant(builder, term);
      }

      LLVMValue llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const;
    };

    template<typename Derived>
    class PrimitiveValue {
    public:
      void check_primitive_parameters(std::size_t n, Term*const*) const {
        if (n)
          throw std::logic_error("primitive value created with parameters");
      }

      LLVMType llvm_type(LLVMValueBuilder&, FunctionalTerm&) const {
        throw std::logic_error("the type of a term cannot be a primitive value");
      }

      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
        return static_cast<const Derived*>(this)->llvm_value_constant(builder, term);
      }
    };

    /**
     * \brief Value type of MetatypeTerm.
     *
     * This is here for easy interfacing with C++ and must be kept in
     * sync with LLVMConstantBuilder::metatype_type.
     */
    struct MetatypeValue {
      std::tr1::uint64_t size;
      std::tr1::uint64_t align;
    };

    class Metatype {
    public:
      TermPtr<> type(Context& context, std::size_t n_parameters, Term *const*) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const;

      LLVMType llvm_type(LLVMValueBuilder&, Term&) const;
      bool operator == (const Metatype&) const;
      friend std::size_t hash_value(const Metatype&);

      static LLVMType llvm_type(LLVMValueBuilder&);
      static LLVMValue llvm_value(LLVMValueBuilder&, std::size_t size, std::size_t align);
      static LLVMValue llvm_empty(LLVMValueBuilder&);
      static LLVMValue llvm_from_type(LLVMValueBuilder&, const llvm::Type* ty);
      static LLVMValue llvm_from_constant(LLVMValueBuilder&, llvm::Constant *size, llvm::Constant *align);
      static LLVMValue llvm_runtime(LLVMFunctionBuilder&, llvm::Value *size, llvm::Value *align);
    };

    template<typename Derived>
    LLVMValue PrimitiveType<Derived>::llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const {
      LLVMType ty = static_cast<const Derived*>(this)->llvm_type(builder, term);
      return Metatype::llvm_from_type(builder, ty.type());
    }

    class BlockType : public PrimitiveType<BlockType> {
    public:
      LLVMType llvm_type(LLVMValueBuilder&, Term&) const;
      bool operator == (const BlockType&) const;
      friend std::size_t hash_value(const BlockType&);
    };
  }
}

#endif
