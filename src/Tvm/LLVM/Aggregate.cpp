#include "Builder.hpp"

#include "../Aggregate.hpp"
#include "../TermOperationMap.hpp"

#include <boost/assign.hpp>
#include <boost/make_shared.hpp>

#include <llvm/Constant.h>

namespace Psi {
  namespace Tvm {
    namespace LLVM {
#if 0
      /**
       * Create a PHI node for a given value type, by traversing the
       * type and handling each component in a default way
       * (i.e. unions are treated as opaque byte arrays).
       *
       * \param insert_point Instruction to insert conversion
       * instructions after.
       */
      BuiltValue* FunctionBuilder::build_phi_node(Term *type, llvm::Instruction *insert_point) {
	if (const llvm::Type *simple_type = build_type(type)) {
	  llvm::PHINode *phi = llvm::PHINode::Create(simple_type);
	  irbuilder().GetInsertBlock()->getInstList().push_front(phi);
	  return new_function_value_simple(type, phi, insert_point);
	} else if (StructType::Ptr struct_ty = dyn_cast<StructType>(type)) {
	  llvm::SmallVector<BuiltValue*,4> elements;
	  for (unsigned i = 0, e = struct_ty->n_members(); i != e; ++i)
	    elements.push_back(build_phi_node(struct_ty->member_type(i), insert_point));
	  return new_function_value_aggregate(type, elements);
	} else if (ArrayType::Ptr array_ty = dyn_cast<ArrayType>(type)) {
	  if (array_ty->length()->global()) {
	    unsigned length = build_constant_integer(array_ty->length()).getZExtValue();
	    Term *element_type = array_ty->element_type();
	    llvm::SmallVector<BuiltValue*,4> elements;
	    for (unsigned i = 0; i != length; ++i)
	      elements.push_back(build_phi_node(element_type, insert_point));
	    return new_function_value_aggregate(type, elements);
	  }
	}

	// Type is neither a known simple type nor an aggregate I
	// can handle, so create it as an unknown type.
	llvm::PHINode *phi = llvm::PHINode::Create(get_pointer_type());
	irbuilder().GetInsertBlock()->getInstList().push_front(phi);

	llvm::Value *type_size = build_value_simple(MetatypeSize::get(type));
	llvm::AllocaInst *copy_dest = irbuilder().CreateAlloca(get_byte_type(), type_size);
	copy_dest->setAlignment(unknown_alloca_align());
	llvm::Instruction *memcpy_insn = create_memcpy(copy_dest, phi, type_size);

	return new_function_value_raw(type, memcpy_insn, memcpy_insn);
      }

      /**
       * Assign a PHI node a given value on an incoming edge from a
       * block.
       */
      void FunctionBuilder::populate_phi_node(BuiltValue *phi_node, llvm::BasicBlock *incoming_block, BuiltValue *value) {
	PSI_ASSERT(phi_node);
	FunctionValue *phi_node_cast = checked_cast<FunctionValue*>(phi_node);

	if (phi_node->simple_type()) {
	  llvm::PHINode *llvm_phi = llvm::cast<llvm::PHINode>(phi_node_cast->m_simple_value);
	  llvm_phi->addIncoming(value->simple_value(), incoming_block);
	  return;
	} else if (StructType::Ptr struct_ty = dyn_cast<StructType>(phi_node->type())) {
	  for (unsigned i = 0, e = struct_ty->n_members(); i != e; ++i)
	    populate_phi_node(phi_node_cast->m_elements[i], incoming_block, value->struct_element_value(i));
	  return;
	} else if (ArrayType::Ptr array_ty = dyn_cast<ArrayType>(phi_node->type())) {
	  if (array_ty->length()->global()) {
	    unsigned length = build_constant_integer(array_ty->length()).getZExtValue();
	    for (unsigned i = 0; i != length; ++i)
	      populate_phi_node(phi_node_cast->m_elements[i], incoming_block, value->array_element_value(i));
	    return;
	  }
	}

	llvm::PHINode *llvm_phi = llvm::cast<llvm::PHINode>(phi_node_cast->m_raw_value);
	llvm_phi->addIncoming(value->raw_value(), incoming_block);
      }

      /**
       * Internal function to do the actual work of building a
       * type. This function handles aggregate types, primitive types
       * are forwarded to build_type_internal_simple.
       */
      const llvm::Type* ConstantBuilder::build_type_internal(FunctionalTerm *term) {
	if (const CallbackMapValue *cb = get_callback(term->operation())) {
	  return cb->build_type(*this, term);
        } else {
          return build_type_internal_simple(term);
        }
      }
#endif
    }
  }
}
