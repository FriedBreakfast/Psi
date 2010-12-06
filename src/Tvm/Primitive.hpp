#ifndef HPP_PSI_TVM_PRIMITIVE
#define HPP_PSI_TVM_PRIMITIVE

/**
 * \file
 *
 * Helper classes for building functional and instruction terms.
 */

#include "Core.hpp"
#include "Functional.hpp"
#include "LLVMBuilder.hpp"

#include <stdexcept>

namespace Psi {
  namespace Tvm {
    template<typename T>
    class TrivialAccess {
    public:
      TrivialAccess(const FunctionalTerm*, const T*) {}
    };

    class StatelessOperand {
    public:
      bool operator == (const StatelessOperand&) const;
      friend std::size_t hash_value(const StatelessOperand&);
    };

    template<typename Derived>
    class PrimitiveType {
    public:
      FunctionalTypeResult type(Context& context, ArrayPtr<Term*const> parameters) const;

      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
        return llvm_value_constant(builder, term);
      }

      LLVMValue llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const;
    };

    template<typename Derived>
    class PrimitiveValue {
    public:
      void check_primitive_parameters(ArrayPtr<Term*const> parameters) const {
        if (parameters.size() != 0)
          throw TvmUserError("primitive value created with parameters");
      }

      LLVMType llvm_type(LLVMValueBuilder&, FunctionalTerm&) const {
        PSI_FAIL("the type of a term cannot be a primitive value");
      }

      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
        return static_cast<const Derived*>(this)->llvm_value_constant(builder, term);
      }
    };

    class Metatype {
    public:
      FunctionalTypeResult type(Context& context, ArrayPtr<Term*const> parameters) const;
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

      typedef TrivialAccess<Metatype> Access;
    };

    template<typename Derived>
    FunctionalTypeResult PrimitiveType<Derived>::type(Context& context, ArrayPtr<Term*const> parameters) const {
      if (parameters.size() != 0)
        throw TvmUserError("primitive type created with parameters");
      return FunctionalTypeResult(context.get_metatype().get(), false);
    }

    template<typename Derived>
    LLVMValue PrimitiveType<Derived>::llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const {
      LLVMType ty = static_cast<const Derived*>(this)->llvm_type(builder, term);
      PSI_ASSERT(ty.is_known());
      return Metatype::llvm_from_type(builder, ty.type());
    }

    class EmptyType : public PrimitiveType<EmptyType> {
    public:
      LLVMType llvm_type(LLVMValueBuilder&, Term&) const;
      bool operator == (const EmptyType&) const;
      friend std::size_t hash_value(const EmptyType&);

      typedef TrivialAccess<EmptyType> Access;

      static LLVMType llvm_type(LLVMValueBuilder&);
      static LLVMValue llvm_value(LLVMValueBuilder&);
    };

    class BlockType : public PrimitiveType<BlockType> {
    public:
      LLVMType llvm_type(LLVMValueBuilder&, Term&) const;
      bool operator == (const BlockType&) const;
      friend std::size_t hash_value(const BlockType&);

      typedef TrivialAccess<BlockType> Access;
    };
  }
}

#endif
