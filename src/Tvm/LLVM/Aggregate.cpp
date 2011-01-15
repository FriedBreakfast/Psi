#include "Builder.hpp"

#include "../Aggregate.hpp"
#include "../TermOperationMap.hpp"

#include <boost/assign.hpp>
#include <boost/make_shared.hpp>

#include <llvm/Constant.h>

using namespace Psi;
using namespace Psi::Tvm;
using namespace Psi::Tvm::LLVM;

namespace {
  const llvm::Type* invalid_type_callback(ConstantBuilder&, Term*) {
    PSI_FAIL("term cannot be used as a type");
  }

  llvm::Value* array_value_insn(FunctionBuilder& builder, ArrayValue::Ptr term) {
    const llvm::Type *type = builder.build_type(term->type());
    llvm::Value *array = llvm::UndefValue::get(type);
    for (unsigned i = 0; i < term->length(); ++i) {
      llvm::Value *element = builder.build_value(term->value(i));
      array = builder.irbuilder().CreateInsertValue(array, element, i);
    }

    return array;
  }

  llvm::Constant* array_value_const(ModuleBuilder& builder, ArrayValue::Ptr term) {
    const llvm::Type *type = builder.build_type(term->type());
    llvm::SmallVector<llvm::Constant*, 4> elements(term->length());
    for (unsigned i = 0; i < term->length(); ++i)
      elements[i] = builder.build_constant(term->value(i));

    return llvm::ConstantArray::get(llvm::cast<llvm::ArrayType>(type), &elements[0], elements.size());
  }

  llvm::Value* struct_value_insn(FunctionBuilder& builder, StructValue::Ptr term) {
    const llvm::Type *type = builder.build_type(term->type());
    llvm::Value *result = llvm::UndefValue::get(type);
    for (std::size_t i = 0; i < term->n_members(); ++i) {
      llvm::Value *val = builder.build_value(term->member_value(i));
      result = builder.irbuilder().CreateInsertValue(result, val, i);
    }
    return result;
  }

  llvm::Constant* struct_value_const(ModuleBuilder& builder, StructValue::Ptr term) {
    llvm::SmallVector<llvm::Constant*, 4> members(term->n_members());
    for (unsigned i = 0; i < term->n_members(); ++i)
      members[i] = builder.build_constant(term->member_value(i));

    return llvm::ConstantStruct::get(builder.llvm_context(), &members[0], members.size(), false);
  }

  llvm::Value* function_specialize_insn(FunctionBuilder& builder, FunctionSpecialize::Ptr term) {
    return builder.build_value(term->function());
  }

  llvm::Constant* function_specialize_const(ModuleBuilder& builder, FunctionSpecialize::Ptr term) {
    return builder.build_constant(term->function());
  }

  struct CallbackMapValue {
    virtual llvm::Value* build_instruction(FunctionBuilder& builder, FunctionalTerm* term) const = 0;
    virtual llvm::Constant* build_constant(ModuleBuilder& builder, FunctionalTerm* term) const = 0;
    virtual const llvm::Type* build_type(ConstantBuilder& builder, FunctionalTerm* term) const = 0;
  };

  template<typename TermTagType, typename InsnCbType, typename ConstCbType, typename TypeCbType>
  class CallbackMapValueImpl : public CallbackMapValue {
    InsnCbType m_insn_cb;
    ConstCbType m_const_cb;
    TypeCbType m_type_cb;

  public:
    CallbackMapValueImpl(InsnCbType insn_cb, ConstCbType const_cb, TypeCbType type_cb)
      : m_insn_cb(insn_cb), m_const_cb(const_cb), m_type_cb(type_cb) {
    }

    virtual llvm::Value* build_instruction(FunctionBuilder& builder, FunctionalTerm* term) const {
      return m_insn_cb(builder, cast<TermTagType>(term));
    }

    virtual llvm::Constant* build_constant(ModuleBuilder& builder, FunctionalTerm* term) const {
      return m_const_cb(builder, cast<TermTagType>(term));
    }

    virtual const llvm::Type* build_type(ConstantBuilder& builder, FunctionalTerm* term) const {
      return m_type_cb(builder, cast<TermTagType>(term));
    }
  };

  template<typename TermTagType, typename InsnCbType, typename ConstCbType, typename TypeCbType>
  boost::shared_ptr<CallbackMapValue> make_callback_map_value(InsnCbType insn_cb, ConstCbType const_cb, TypeCbType type_cb) {
    return boost::make_shared<CallbackMapValueImpl<TermTagType, InsnCbType, ConstCbType, TypeCbType> >(insn_cb, const_cb, type_cb);
  }

  template<typename TermTagType, typename TypeCbType>
  class TypeWrapper {
    TypeCbType m_type_cb;

  public:
    TypeWrapper(TypeCbType type_cb) : m_type_cb(type_cb) {}

    llvm::Constant* operator () (ConstantBuilder& builder, typename TermTagType::Ptr term) const {
      return metatype_from_type(builder, m_type_cb(builder, term));
    }
  };

  template<typename TermTagType, typename TypeCbType>
  TypeWrapper<TermTagType, TypeCbType> make_type_wrapper(TypeCbType insn_cb) {
    return TypeWrapper<TermTagType, TypeCbType>(insn_cb);
  }

#define CALLBACK(ty,cb_insn,cb_const,cb_type) (ty::operation, make_callback_map_value<ty>(cb_insn, cb_const, cb_type))
#define OP_CALLBACK(ty,cb_insn,cb_const) CALLBACK(ty,cb_insn,cb_const,invalid_type_callback)
#define TYPE_CALLBACK(ty,cb_type) CALLBACK(ty,make_type_wrapper<ty>(cb_type),make_type_wrapper<ty>(cb_type),cb_type)

  typedef std::tr1::unordered_map<const char*, boost::shared_ptr<CallbackMapValue> > CallbackMapType;

  const CallbackMapType callbacks =
    boost::assign::map_list_of<const char*, CallbackMapType::mapped_type>
    OP_CALLBACK(ArrayValue, array_value_insn, array_value_const)
    OP_CALLBACK(StructValue, struct_value_insn, struct_value_const)
    OP_CALLBACK(FunctionSpecialize, function_specialize_insn, function_specialize_const);

  const CallbackMapValue *get_callback(const char *s) {
    CallbackMapType::const_iterator it = callbacks.find(s);
    if (it != callbacks.end())
      return it->second.get();
    else
      return 0;
  }
}

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
#endif

      /**
       * Build a value for an instruction operation.
       *
       * This handles complex operations on aggregate types; numeric
       * operations are forwarded to build_value_instruction_simple.
       */
      llvm::Instruction* FunctionBuilder::build_value_instruction(InstructionTerm *term) {
	if (false) {
	} else {
	  return build_value_instruction_simple(term);
	}
      }

      /**
       * Build a value for a functional operation.
       *
       * This handles aggregate types. Primitive types are forwarded
       * to build_value_functional_simple.
       */
      llvm::Value* FunctionBuilder::build_value_functional(FunctionalTerm *term) {
	if (const CallbackMapValue *cb = get_callback(term->operation())) {
	  return cb->build_instruction(*this, term);
        } else {
	  return build_value_functional_simple(term);
        }
      }

      /**
       * Build an LLVM constant. The second component of the return value is
       * the required alignment of the return value.
       *
       * This handles aggregate types. Primitive types are forwarded
       * to build_constant_internal_simple.
       */
      llvm::Constant* ModuleBuilder::build_constant_internal(FunctionalTerm *term) {
	if (const CallbackMapValue *cb = get_callback(term->operation())) {
	  return cb->build_constant(*this, term);
        } else {
	  return build_constant_internal_simple(term);
        }
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
    }
  }
}
