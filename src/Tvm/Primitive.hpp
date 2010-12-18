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

    /**
     * This can be inherited by functional terms which have no state,
     * so that hashing and equality comparison can be implemented
     * trivially.
     */
    class StatelessTerm {
    public:
      bool operator == (const StatelessTerm&) const;
      friend std::size_t hash_value(const StatelessTerm&);
    };

    /**
     * Any functional term which takes no parameters can inherit from
     * this. llvm_value_instruction will abort since it should never
     * be called as functional terms with no parameters are
     * automatically global.
     */
    class PrimitiveTerm {
    public:
      void check_primitive_parameters(ArrayPtr<Term*const> parameters) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const;
    };

    /**
     * Types which represent types can subclass this. It implements a
     * default \c type member, which checks that no parameters have
     * been given to this term and returns the type of this term as
     * Metatype.
     */
    class PrimitiveType : public PrimitiveTerm {
    public:
      FunctionalTypeResult type(Context& context, ArrayPtr<Term*const> parameters) const;
      llvm::Constant* llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const;
      const llvm::Type* llvm_type(LLVMConstantBuilder&, FunctionalTerm&) const;
      virtual const llvm::Type* llvm_primitive_type(LLVMConstantBuilder&) const = 0;
    };

    /**
     * This type can be inherited by terms which define a value, and
     * therefore calling llvm_type on them is invalid. llvm_type will
     * therefore abort. Therefore, any term using this type should be
     * of category_value.
     */
    class ValueTerm {
    public:
      llvm::Type* llvm_type(LLVMConstantBuilder&, FunctionalTerm&) const;
    };

    /**
     * Types which are values and take no parameters subclass this. In
     * addition to llvm_value_instruction aborting (inherited from
     * PrimitiveTerm), this will also cause llvm_type to abort,
     * since any term using this operand should be of category_value.
     */
    class PrimitiveValue : public ValueTerm, public PrimitiveTerm {
    public:
      llvm::Constant* llvm_value_constant(LLVMConstantBuilder&, FunctionalTerm&) const;
      virtual llvm::Constant* llvm_primitive_value(LLVMConstantBuilder&) const = 0;
    };

    class Metatype : public PrimitiveTerm, public StatelessTerm {
    public:
      FunctionalTypeResult type(Context& context, ArrayPtr<Term*const> parameters) const;
      llvm::Constant* llvm_value_constant(LLVMConstantBuilder& builder, Term&) const;
      const llvm::Type* llvm_type(LLVMConstantBuilder&, Term&) const;

      typedef TrivialAccess<Metatype> Access;
    };

    class EmptyType : public PrimitiveType, public StatelessTerm {
    public:
      static llvm::Constant* llvm_empty_value(LLVMConstantBuilder&);
      virtual const llvm::Type* llvm_primitive_type(LLVMConstantBuilder&) const;
      typedef TrivialAccess<EmptyType> Access;
    };

    class BlockType : public PrimitiveType, public StatelessTerm {
    public:
      virtual const llvm::Type* llvm_primitive_type(LLVMConstantBuilder&) const;
      typedef TrivialAccess<BlockType> Access;
    };
  }
}

#endif
