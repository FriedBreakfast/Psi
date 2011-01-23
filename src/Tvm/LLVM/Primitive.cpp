#include "Builder.hpp"

#include <llvm/Module.h>
#include <../lib/llvm-2.8/include/llvm/Target/TargetData.h>

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      /**
       * \brief Get the LLVM type for Metatype values.
       */
      const llvm::Type *metatype_type(llvm::LLVMContext& context, const llvm::TargetData *target_data) {
        const llvm::Type* int_ty = target_data->getIntPtrType(context);
        return llvm::StructType::get(context, int_ty, int_ty, NULL);
      }

      /**
       * \brief Get a metatype value for size and alignment
       * specified in BigInteger.
       */
      llvm::Constant* metatype_from_constant(ConstantBuilder& c, const llvm::APInt& size, const llvm::APInt& align) {
        PSI_ASSERT(align != 0);
        PSI_ASSERT(!size.urem(align));
        PSI_ASSERT(!(align & (align - 1)));

        const llvm::IntegerType *int_ty = c.llvm_target_machine()->getTargetData()->getIntPtrType(c.llvm_context());
        llvm::Constant* values[2] = {
          llvm::ConstantInt::get(int_ty, size),
          llvm::ConstantInt::get(int_ty, align)
        };
        return llvm::ConstantStruct::get(c.llvm_context(), values, 2, false);
      }

      /**
       * \brief Get a metatype value for size and alignment
       * specified in BigInteger.
       */
      llvm::Constant* metatype_from_constant(ConstantBuilder& c, uint64_t size, uint64_t align) {
        PSI_ASSERT(align != 0);
        PSI_ASSERT(size % align == 0);
        PSI_ASSERT((align & (align - 1)) == 0);

        const llvm::IntegerType *int_ty = c.llvm_target_machine()->getTargetData()->getIntPtrType(c.llvm_context());
        llvm::Constant* values[2] = {
          llvm::ConstantInt::get(int_ty, size),
          llvm::ConstantInt::get(int_ty, align)
        };
        return llvm::ConstantStruct::get(c.llvm_context(), values, 2, false);
      }

      /**
       * \brief Get an LLVM value for Metatype for the given LLVM type.
       */
      llvm::Constant* metatype_from_type(ConstantBuilder& c, const llvm::Type *ty) {
        return metatype_from_constant(c, c.type_size(ty), c.type_alignment(ty));
      }

      /**
       * \brief Get an LLVM value for a specified size and alignment.
       *
       * The result of this call will be a global constant.
       */
      llvm::Value* metatype_from_value(FunctionBuilder& builder, llvm::Value *size, llvm::Value *align) {
        const llvm::IntegerType *int_ty = builder.llvm_target_machine()->getTargetData()->getIntPtrType(builder.llvm_context());
        if ((size->getType() != int_ty) || (align->getType() != int_ty))
          throw BuildError("values supplied for metatype have the wrong type");
        llvm::Value *undef = llvm::UndefValue::get(metatype_type(builder.llvm_context(), builder.llvm_target_machine()->getTargetData()));
        llvm::Value *stage1 = builder.irbuilder().CreateInsertValue(undef, size, 0);
        llvm::Value *stage2 = builder.irbuilder().CreateInsertValue(stage1, align, 1);
        return stage2;
      }

      /// \brief Utility function used by intrinsic_memcpy_64 and
      /// intrinsic_memcpy_32.
      llvm::Function* intrinsic_memcpy_n(llvm::Module& m, const llvm::IntegerType *size_type, const char *name) {
        llvm::Function *f = m.getFunction(name);
        if (f)
          return f;

        llvm::LLVMContext& c = m.getContext();
        std::vector<const llvm::Type*> args;
        args.push_back(llvm::Type::getInt8PtrTy(c));
        args.push_back(llvm::Type::getInt8PtrTy(c));
        args.push_back(size_type);
        args.push_back(llvm::Type::getInt32Ty(c));
        args.push_back(llvm::Type::getInt1Ty(c));
        llvm::FunctionType *ft = llvm::FunctionType::get(llvm::Type::getVoidTy(c), args, false);
        f = llvm::Function::Create(ft, llvm::GlobalValue::ExternalLinkage, name, &m);

        return f;
      }

      /// \brief Gets the LLVM intrinsic <tt>llvm.memcpy.p0i8.p0i8.i64</tt>
      llvm::Function* intrinsic_memcpy_64(llvm::Module& m) {
        return intrinsic_memcpy_n(m, llvm::Type::getInt64Ty(m.getContext()),
                                  "llvm.memcpy.p0i8.p0i8.i64");
      }

      /// \brief Gets the LLVM intrinsic <tt>llvm.memcpy.p0i8.p0i8.i32</tt>
      llvm::Function* intrinsic_memcpy_32(llvm::Module& m) {
        return intrinsic_memcpy_n(m, llvm::Type::getInt32Ty(m.getContext()),
                                  "llvm.memcpy.p0i8.p0i8.i32");
      }

      /// \brief Gets the LLVM intrnisic <tt>llvm.stacksave</tt>
      llvm::Function* intrinsic_stacksave(llvm::Module& m) {
        const char *name = "llvm.stacksave";
        llvm::Function *f = m.getFunction(name);
        if (f)
          return f;

        llvm::LLVMContext& c = m.getContext();
        std::vector<const llvm::Type*> args;
        llvm::FunctionType *ft = llvm::FunctionType::get(llvm::Type::getInt8PtrTy(c), args, false);
        f = llvm::Function::Create(ft, llvm::GlobalValue::ExternalLinkage, name, &m);

        return f;
      }

      /// \brief Gets the LLVM intrinsic <tt>llvm.stackrestore</tt>
      llvm::Function* intrinsic_stackrestore(llvm::Module& m) {
        const char *name = "llvm.stackrestore";
        llvm::Function *f = m.getFunction(name);
        if (f)
          return f;

        llvm::LLVMContext& c = m.getContext();
        std::vector<const llvm::Type*> args;
        args.push_back(llvm::Type::getInt8PtrTy(c));
        llvm::FunctionType *ft = llvm::FunctionType::get(llvm::Type::getVoidTy(c), args, false);
        f = llvm::Function::Create(ft, llvm::GlobalValue::ExternalLinkage, name, &m);

        return f;
      }
    }
  }
}
