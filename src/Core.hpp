#ifndef HPP_PSI_CORE
#define HPP_PSI_CORE

#include "User.hpp"

#include <llvm/LLVMContext.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetRegistry.h>
#include <llvm/Value.h>

namespace Psi {
  class Type;
  class TermType;

  class Context {
  public:
    Context();
    ~Context();

#define PSI_CONTEXT_TYPE(name)			\
    Type* type_##name() {return m_type_##name;}	\
  private: Type* m_type_##name; public:

    /**
     * \name Types
     *
     * Predefined types in this context.
     */
    //@{
    PSI_CONTEXT_TYPE(void)
    PSI_CONTEXT_TYPE(size)
    PSI_CONTEXT_TYPE(char)
    PSI_CONTEXT_TYPE(int8)
    PSI_CONTEXT_TYPE(uint8)
    PSI_CONTEXT_TYPE(int16)
    PSI_CONTEXT_TYPE(uint16)
    PSI_CONTEXT_TYPE(int32)
    PSI_CONTEXT_TYPE(uint32)
    PSI_CONTEXT_TYPE(int64)
    PSI_CONTEXT_TYPE(uint64)
    PSI_CONTEXT_TYPE(real32)
    PSI_CONTEXT_TYPE(real64)
    PSI_CONTEXT_TYPE(real128)
    //@}

#undef PSI_CONTEXT_TYPE

    /**
     * \brief Get the LLVM context associated with this context.
     *
     * This should not normally be used outside of the implementation
     * of #Term, since Term::build_llvm_value() and
     * Term::build_llvm_type() are passed the context as a parameter.
     */
    llvm::LLVMContext& llvm_context() {return m_llvm_context;}

  private:
    void init_llvm();
    void init_types();

    llvm::LLVMContext m_llvm_context;
    llvm::TargetMachine *m_llvm_target_machine;
    const llvm::TargetData *m_llvm_target_data;
  };

  /**
   * \brief The root type of all terms in Psi.
   *
   * This allows for dependent types since both types and values
   * derive from #Term and so can be used as parameters to other
   * types.
   */
  class Term : public Used, public User {
  protected:
    enum Slots {
      slot_type = 0,
      slot_max
    };

  public:
    Term();

    /**
     * \brief Get the type of this term.
     *
     * The type of all terms derives from #Type.
     */
    TermType* type();

    /**
     * \brief Get the LLVM value of this term.
     *
     * For most types, the meaning of this is fairly obvious. #Type
     * objects also have a value, which has an LLVM type of <tt>{ i32,
     * i32 }</tt> giving the size and alignment of the type.
     */
    const llvm::Value* llvm_value() {
      if (!m_llvm_value_built) {
	m_llvm_value = build_llvm_value(m_context->llvm_context());
	m_llvm_value_built = true;
      }
      return m_llvm_value;
    }

    /**
     * \brief Get the LLVM type of this term.
     *
     * Note that this is <em>not</em> the type of the value returned
     * by llvm_value(); rather
     * <tt>llvm_value()->getType() == type()->llvm_type()</tt>, so that
     * llvm_type() returns the LLVM type of terms whose type is this
     * term.
     */
    const llvm::Type* llvm_type() {
      if (!m_llvm_type_built) {
	m_llvm_type = build_llvm_type(m_context->llvm_context());
	m_llvm_type_built = true;
      }
      return m_llvm_type;
    }

  private:
    Context *m_context;
    bool m_llvm_value_built;
    bool m_llvm_type_built;
    llvm::Value *m_llvm_value;
    llvm::Type *m_llvm_type;

    virtual llvm::Value* build_llvm_value(llvm::LLVMContext& context) = 0;
    virtual llvm::Type* build_llvm_type(llvm::LLVMContext& context) = 0;
  };

  /**
   * \brief The type of a term.
   *
   * This is distinct from #Type because #Type is the type of a
   * #Value, whereas #TermType may be the type of a #Value or a #Type.
   */
  class TermType : public Term {
  };

  inline TermType* Term::type() {
    return use_get<TermType>(0);
  }

  /**
   * \brief The type of #Type terms.
   *
   * There is one global #Metatype object (per context), and all types
   * are of type #Metatype. #Metatype does not have a type (it is
   * impossible to quantify over #Metatype so this does not matter).
   */
  class Metatype : public TermType {
  };

  /**
   * \brief The type of a #Value term.
   */
  class Type : public TermType {
  public:
    Metatype* type() {return use_get<Metatype>(slot_type);}
  };

  class AppliedType;

  /**
   * \brief The type of values in Psi.
   */
  class Value : public Term {
  protected:
    enum Slots {
      slot_type=Term::slot_max,
      slot_max
    };

  public:
    Type* type() {return use_get<Type>(slot_type);}

  protected:
    AppliedType* applied_type();
  };
}

#endif
