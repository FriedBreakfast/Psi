#include "CallingConventions.hpp"
#include "../FunctionalBuilder.hpp"

#include <boost/format.hpp>

namespace Psi {
namespace Tvm {
namespace LLVM {      
CallingConventionHandler::~CallingConventionHandler() {
}

class CallingConventionSimple : public CallingConventionHandler {
public:
  enum ParameterMode {
    mode_default,
    /**
     * Set LLVM byval flag, unless on a return type in which case sret flag.
     */
    mode_by_value,
    /**
     * Set LLVM inreg flag. This is not the same thing as passing
     * in a register, it's interpreted by the target calling
     * convention definition.
     */
    mode_in_register,
    mode_ignore,
    /**
     * Weird ARM argument passing mode where the start of an argument
     * is passed in registers and the remainder on the stack.
     * 
     * coerce_type is expected to hold a struct which has two members,
     * the first of which is the register part and the second the
     * "by value" part.
     */
    mode_arm_split
  };
  
  struct ParameterInfo {
    /// Whether to pass using LLVM by_value flag
    ParameterMode mode;
    /// Parameter alignment if not zero
    unsigned alignment;
    /// Type to coerce to.
    ValuePtr<> coerce_type;
    /// If the coerced type is a struct, whether to expand members to a sequence of arguments.
    bool coerce_expand;
  };
  
  struct FunctionTypeInfo {
    bool is_sret; ///< Whether this function returns by sret, either generated or set by the front-end
    bool left_to_right;
    ParameterInfo result;
    std::vector<ParameterInfo> parameters;
  };
  
  FunctionTypeInfo function_type_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionType>& function_type, const SourceLocation& location, bool left_to_right=false) {
    FunctionTypeInfo fti;
    fti.left_to_right = left_to_right;
    fti.result = return_info(rewriter, function_type, location);
    fti.is_sret = function_type->sret() || (fti.result.mode == mode_by_value);
    return fti;
  }
  
  static ParameterInfo parameter_by_value(unsigned alignment) {
    ParameterInfo pi;
    pi.mode = mode_by_value;
    pi.alignment = alignment;
    return pi;
  }
  
  static ParameterInfo parameter_ignore() {
    ParameterInfo pi;
    pi.mode = mode_ignore;
    pi.alignment = 0;
    return pi;
  }
  
  static ParameterInfo parameter_default(const ValuePtr<>& coerce_type=ValuePtr<>(), bool coerce_expand=false) {
    ParameterInfo pi;
    pi.mode = mode_default;
    pi.alignment = 0;
    pi.coerce_type = coerce_type;
    pi.coerce_expand = coerce_expand;
    return pi;
  }
  
  static ParameterInfo parameter_register(const ValuePtr<>& coerce_type=ValuePtr<>(), bool coerce_expand=false) {
    ParameterInfo pi;
    pi.mode = mode_in_register;
    pi.alignment = 0;
    pi.coerce_type = coerce_type;
    pi.coerce_expand = coerce_expand;
    return pi;
  }
  
  static ParameterInfo parameter_arm_split(unsigned alignment, const ValuePtr<>& reg_part, const ValuePtr<>& stack_part, const SourceLocation& location) {
    ParameterInfo pi;
    pi.mode = mode_arm_split;
    pi.alignment = alignment;
    pi.coerce_type = FunctionalBuilder::struct_type(reg_part->context(), vector_of(reg_part, stack_part), location);
    pi.coerce_expand = false;
    return pi;
  }

  static ParameterAttributes make_attributes(const ParameterInfo& info, const ParameterAttributes& attr, bool is_return) {
    ParameterAttributes pa = attr;
    switch (info.mode) {
    case mode_by_value: if (!is_return) pa.flags |= ParameterAttributes::llvm_byval; break;
    case mode_in_register: pa.flags |= ParameterAttributes::llvm_inreg; break;
    default: break;
    }
    pa.alignment = info.alignment;
    return pa;
  }
  
  void lower_function_call(AggregateLoweringPass::FunctionRunner& runner, const ValuePtr<Call>& term) {
    ValuePtr<FunctionType> ftype = term->target_function_type();
    
    FunctionTypeInfo info = parameter_info(runner, ftype, ftype->location());
    PSI_ASSERT(info.parameters.size() == ftype->parameter_types().size() - ftype->n_phantom());
    PSI_ASSERT(!ftype->sret() || (info.result.mode != mode_by_value));

    std::vector<ValuePtr<> > alloca_stack;
    std::vector<ValuePtr<> > parameters;
    std::vector<ParameterType> parameter_types;
    for (std::size_t ii = 0, ie = info.parameters.size(); ii != ie; ++ii) {
      // Prepare parameters left-to-right or right-to-left depending on convention
      std::size_t info_idx = info.left_to_right ? ii : ie - ii - 1;
      std::size_t type_idx = ftype->n_phantom() + info_idx;
      
      const ParameterInfo& pi = info.parameters[info_idx];
      const ParameterType& pt = ftype->parameter_types()[type_idx];
      LoweredValue value = runner.rewrite_value(term->parameters[type_idx]);
      
      if (pi.mode == mode_ignore) {
        // Do nothing
      } else if (pi.mode == mode_by_value) {
        ValuePtr<> ptr = runner.alloca_(value.type(), term->location());
        alloca_stack.push_back(ptr);
        runner.store_value(value, ptr, term->location());
        ValuePtr<> cast_ptr = FunctionalBuilder::pointer_cast(ptr, value.type().register_type(), term->location());
        parameters.push_back(cast_ptr);
        parameter_types.push_back(ParameterType(cast_ptr->type(), make_attributes(pi, pt.attributes, false)));
      } else if (pi.coerce_type) {
        // Coerce to type
        ValuePtr<> ptr = runner.builder().alloca_(pi.coerce_type, term->location());
        runner.store_value(value, ptr, term->location());
        if (pi.coerce_expand) {
          ValuePtr<StructType> sty = value_cast<StructType>(pi.coerce_type);
          for (unsigned ji = 0, je = sty->n_members(); ji != je; ++ji) {
            ValuePtr<> member_ptr = Tvm::FunctionalBuilder::element_ptr(ptr, ji, term->location());
            parameters.push_back(runner.builder().load(member_ptr, term->location()));
            parameter_types.push_back(ParameterType(sty->member_type(ji), make_attributes(pi, pt.attributes, false)));
          }
        } else if (pi.mode == mode_arm_split) {
          ValuePtr<StructType> sty = value_cast<StructType>(pi.coerce_type);
          ValuePtr<> reg_part = runner.builder().load(FunctionalBuilder::element_ptr(ptr, 0, term->location()), term->location());
          parameters.push_back(reg_part);
          parameter_types.push_back(ParameterType(reg_part->type(), make_attributes(parameter_register(), pt.attributes, false)));
          ValuePtr<> stack_part = FunctionalBuilder::element_ptr(ptr, 1, term->location());
          parameters.push_back(stack_part);
          parameter_types.push_back(ParameterType(stack_part->type(), make_attributes(parameter_by_value(pi.alignment), pt.attributes, false)));
        } else {
          parameters.push_back(runner.builder().load(ptr, term->location()));
          parameter_types.push_back(ParameterType(pi.coerce_type, make_attributes(pi, pt.attributes, false)));
        }
        runner.builder().freea(ptr, term->location());
      } else {
        PSI_ASSERT(pi.mode != mode_arm_split);
        parameters.push_back(value.register_value());
        parameter_types.push_back(ParameterType(value.register_value()->type(), make_attributes(pi, pt.attributes, false)));
      }
      
      PSI_ASSERT(parameters.size() == parameter_types.size());
    }
    
    if (!info.left_to_right) {
      std::reverse(parameters.begin(), parameters.end());
      std::reverse(parameter_types.begin(), parameter_types.end());
    }
    
    LoweredType lowered_result_type = runner.rewrite_type(ftype->result_type().value);
    ValuePtr<> sret_ptr;
    ParameterType result_type;
    bool sret = ftype->sret();
    if (info.result.mode == mode_by_value) {
      PSI_ASSERT(!ftype->sret());
      sret = true;
      sret_ptr = runner.alloca_(lowered_result_type, term->location());
      parameters.push_back(sret_ptr);
      parameter_types.push_back(ParameterType(sret_ptr->type(), make_attributes(info.result, ftype->result_type().attributes, true)));
      result_type = ParameterType(FunctionalBuilder::empty_type(runner.context(), ftype->location()));
    } else if (info.result.coerce_type) {
      result_type = ParameterType(info.result.coerce_type, make_attributes(info.result, ftype->result_type().attributes, true));
    } else {
      result_type = ParameterType(lowered_result_type.register_type(), make_attributes(info.result, ftype->result_type().attributes, true));
    }
    
    ValuePtr<> lowered_type = FunctionalBuilder::function_type(ftype->calling_convention(), result_type, parameter_types, 0, sret, ftype->location());
    
    ValuePtr<> lowered_target = runner.rewrite_value_register(term->target).value;
    ValuePtr<> cast_target = FunctionalBuilder::pointer_cast(lowered_target, lowered_type, term->location());
    ValuePtr<> call_insn = runner.builder().call(cast_target, parameters, term->location());
    
    LoweredValue result_value;
    if (sret_ptr) {
      result_value = runner.load_value(lowered_result_type, sret_ptr, term->location());
      runner.builder().freea_cast(sret_ptr, term->location());
    } else if (info.result.coerce_type) {
      ValuePtr<> coerce_ptr = runner.builder().alloca_(info.result.coerce_type, term->location());
      runner.builder().store(call_insn, coerce_ptr, term->location());
      result_value = runner.load_value(lowered_result_type, coerce_ptr, term->location());
      runner.builder().freea(coerce_ptr, term->location());
    } else {
      result_value = LoweredValue::register_(lowered_result_type, false, call_insn);
    }
    
    if (!alloca_stack.empty())
      runner.alloca_free(alloca_stack.front(), term->location());

    runner.add_mapping(term, result_value);
  }
  
  ValuePtr<Function> lower_function(AggregateLoweringPass& pass, const ValuePtr<Function>& function) {
    ValuePtr<FunctionType> ftype = function->function_type();
    FunctionTypeInfo info = parameter_info(pass.global_rewriter(), ftype, ftype->location());
    PSI_ASSERT(info.parameters.size() == ftype->parameter_types().size() - ftype->n_phantom());
    PSI_ASSERT(!ftype->sret() || (info.result.mode != mode_by_value));

    std::vector<ParameterType> parameter_types;
    for (std::size_t ii = 0, ie = info.parameters.size(); ii != ie; ++ii) {
      const ParameterInfo& pi = info.parameters[ii];
      const ParameterType& pt = ftype->parameter_types()[ii + ftype->n_phantom()];

      if (pi.mode == mode_ignore) {
        // Do nothing
      } else if (pi.coerce_type) {
        // Coerce to type
        if (pi.coerce_expand) {
          ValuePtr<StructType> sty = value_cast<StructType>(pi.coerce_type);
          for (unsigned ji = 0, je = sty->n_members(); ji != je; ++ji)
            parameter_types.push_back(ParameterType(sty->member_type(ji), make_attributes(pi, pt.attributes, false)));
        } else if (pi.mode == mode_arm_split) {
          ValuePtr<StructType> sty = value_cast<StructType>(pi.coerce_type);
          parameter_types.push_back(ParameterType(sty->member_type(0), make_attributes(parameter_register(), pt.attributes, false)));
          parameter_types.push_back(ParameterType(FunctionalBuilder::pointer_type(sty->member_type(1), ftype->location()),
                                                  make_attributes(parameter_by_value(pi.alignment), pt.attributes, false)));
        } else {
          parameter_types.push_back(ParameterType(pi.coerce_type, make_attributes(pi, pt.attributes, false)));
        }
      } else {
        LoweredType type = pass.global_rewriter().rewrite_type(pt.value);
        if (pi.mode == mode_by_value)
          parameter_types.push_back(ParameterType(FunctionalBuilder::pointer_type(type.register_type(), ftype->location()), make_attributes(pi, pt.attributes, false)));
        else
          parameter_types.push_back(ParameterType(type.register_type(), make_attributes(pi, pt.attributes, false)));
      }
    }
    
    LoweredType lowered_result_type = pass.global_rewriter().rewrite_type(ftype->result_type().value);
    ParameterType result_type;
    bool sret = ftype->sret();
    if (info.result.mode == mode_by_value) {
      PSI_ASSERT(!ftype->sret());
      sret = true;
      parameter_types.push_back(ParameterType(FunctionalBuilder::pointer_type(lowered_result_type.register_type(), ftype->location()),
                                              make_attributes(info.result, ftype->result_type().attributes, true)));
      result_type = ParameterType(FunctionalBuilder::empty_type(pass.context(), ftype->location()));
    } else if (info.result.mode == mode_ignore) {
      // Return void
      result_type = ParameterType(FunctionalBuilder::empty_type(pass.context(), ftype->location()));
    } else if (info.result.coerce_type) {
      result_type = ParameterType(info.result.coerce_type, make_attributes(info.result, ftype->result_type().attributes, true));
    } else {
      result_type = ParameterType(lowered_result_type.register_type(), make_attributes(info.result, ftype->result_type().attributes, true));
    }
    
    ValuePtr<FunctionType> lowered_type = FunctionalBuilder::function_type(ftype->calling_convention(), result_type, parameter_types, 0, sret, ftype->location());
    
    return pass.target_module()->new_function(function->name(), lowered_type, function->location());
  }
  
  void lower_function_entry(AggregateLoweringPass::FunctionRunner& runner, const ValuePtr<Function>& source_function, const ValuePtr<Function>& target_function) {
    ValuePtr<FunctionType> ftype = source_function->function_type();
    
    FunctionTypeInfo info = parameter_info(runner, ftype, ftype->location());
    PSI_ASSERT(info.parameters.size() == ftype->parameter_types().size() - ftype->n_phantom());
    PSI_ASSERT(!ftype->sret() || (info.result.mode != mode_by_value));

    std::vector<ValuePtr<> > parameters;
    
    Function::ParameterList::const_iterator src_param_it = source_function->parameters().begin();
    std::advance(src_param_it, ftype->n_phantom());
    Function::ParameterList::const_iterator target_param_it = target_function->parameters().begin();
    
    std::vector<ParameterType> parameter_types;
    for (std::size_t ii = 0, ie = info.parameters.size(); ii != ie; ++ii, ++src_param_it) {
      const ParameterInfo& pi = info.parameters[ii];
      const ValuePtr<>& src_value = *src_param_it;
      LoweredType type = runner.rewrite_type(src_value->type());
      
      if (pi.mode == mode_ignore) {
        // Take no parameters
        ValuePtr<> dest_value = FunctionalBuilder::undef(type.register_type(), src_value->location());
        runner.add_mapping(src_value, LoweredValue::register_(type, false, dest_value));
      } else if (pi.mode == mode_by_value) {
        LoweredValue dest_value = runner.load_value(type, *target_param_it++, src_value->location());
        runner.add_mapping(src_value, dest_value);
      } else if (pi.coerce_type) {
        // Coerce to type
        ValuePtr<> ptr = runner.builder().alloca_(pi.coerce_type, src_value->location());
        if (pi.coerce_expand) {
          ValuePtr<StructType> sty = value_cast<StructType>(pi.coerce_type);
          for (unsigned ji = 0, je = sty->n_members(); ji != je; ++ji) {
            ValuePtr<> member_ptr = Tvm::FunctionalBuilder::element_ptr(ptr, ji, src_value->location());
            runner.builder().store(*target_param_it++, member_ptr, src_value->location());
          }
        } else if (pi.mode == mode_arm_split) {
          ValuePtr<StructType> sty = value_cast<StructType>(pi.coerce_type);
          // Register part
          runner.builder().store(*target_param_it++, FunctionalBuilder::element_ptr(ptr, 0, src_value->location()), src_value->location());
          // Stack part
          ValuePtr<> stack_part = runner.builder().load(*target_param_it++, src_value->location());
          runner.builder().store(stack_part, FunctionalBuilder::element_ptr(ptr, 1, src_value->location()), src_value->location());
        } else {
          runner.builder().store(*target_param_it++, ptr, src_value->location());
        }
        
        LoweredValue dest_value = runner.load_value(type, ptr, src_value->location());
        runner.add_mapping(src_value, dest_value);
      } else {
        runner.add_mapping(src_value, LoweredValue::register_(type, false, *target_param_it++));
      }
    }
  }

  ValuePtr<Instruction> lower_return(AggregateLoweringPass::FunctionRunner& runner, const ValuePtr<>& value, const SourceLocation& location) {
    ValuePtr<FunctionType> ftype = runner.old_function()->function_type();
    if (ftype->sret())
      return runner.builder().return_void(location);
    
    LoweredValue lv = runner.rewrite_value(value);
    
    ParameterInfo ret_info = return_info(runner, ftype, ftype->location());
    PSI_ASSERT(ret_info.mode != mode_arm_split);
    if (ret_info.mode == mode_by_value) {
      runner.store_value(lv, runner.new_function()->parameters().back(), location);
      return runner.builder().return_void(location);
    } else if (ret_info.mode == mode_ignore) {
      return runner.builder().return_void(location);
    } else if (ret_info.coerce_type) {
      ValuePtr<> coerce_ptr = runner.builder().alloca_(ret_info.coerce_type, location);
      runner.store_value(lv, coerce_ptr, location);
      ValuePtr<> ret_val = runner.builder().load(coerce_ptr, location);
      runner.builder().freea(coerce_ptr, location);
      return runner.builder().return_(ret_val, location);
    } else {
      return runner.builder().return_(lv.register_value(), location);
    }
  }
  
  // Get the index of the last non-sret argument, plus one
  static std::size_t argument_count(const ValuePtr<FunctionType>& ftype) {
    std::size_t n = ftype->parameter_types().size();
    if (ftype->sret())
      --n;
    return n;
  }
  
private:
  virtual ParameterInfo return_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionType>& function_type, const SourceLocation& location) = 0;
  virtual FunctionTypeInfo parameter_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionType>& function_type, const SourceLocation& location) = 0;
};

/**
 * SystemV calling convention for x86-64
 */
class CallingConventionHandler_x86_64_SystemV : public CallingConventionSimple {
  /**
   * Used to classify how each parameter should be passed (or
   * returned).
   */
  enum AMD64_Class {
    amd64_integer,
    amd64_sse,
    amd64_sse_up,
    amd64_x87,
    amd64_x87_up,
    amd64_complex_x87, //< Note that the COMPLEX_X87 class is not currently supported
    amd64_no_class,
    amd64_memory
  };

  /**
   * Get the parameter class resulting from two separate
   * classes. Described on page 19 of the ABI.
   */
  static AMD64_Class merge_amd64_class(AMD64_Class left, AMD64_Class right) {
    if (left == right) {
      return left;
    } else if (left == amd64_no_class) {
      return right;
    } else if (right == amd64_no_class) {
      return left;
    } else if ((left == amd64_memory) || (right == amd64_memory)) {
      return amd64_memory;
    } else if ((left == amd64_integer) || (right == amd64_integer)) {
      return amd64_integer;
    } else if ((left == amd64_x87) || (right == amd64_x87) ||
      (left == amd64_x87_up) || (right == amd64_x87_up) ||
      (left == amd64_complex_x87) || (right == amd64_complex_x87)) {
      return amd64_memory;
    } else {
      return amd64_sse;
    }
  }
  
  struct ParameterInfo_AMD64 {
    ParameterInfo_AMD64() : low_eightbyte(amd64_no_class), high_eightbyte(amd64_no_class), size(0), align(1) {}
    ParameterInfo_AMD64(AMD64_Class low_eightbyte_, AMD64_Class high_eightbyte_, unsigned size_, unsigned align_)
    : low_eightbyte(low_eightbyte_), high_eightbyte(high_eightbyte_), size(size_), align(align_) {
    }
    
    AMD64_Class low_eightbyte, high_eightbyte;
    unsigned size, align;
  };

  static ParameterInfo_AMD64 arg_info_combine(ParameterInfo_AMD64 a, ParameterInfo_AMD64 b) {
    ParameterInfo_AMD64 r;
    r.low_eightbyte = merge_amd64_class(a.low_eightbyte, b.low_eightbyte);
    r.high_eightbyte = merge_amd64_class(a.high_eightbyte, b.high_eightbyte);
    r.align = std::max(a.align, b.align);
    r.size = align_to(std::max(a.size, b.size), r.align);
    return r;
  }
  
  static ParameterInfo_AMD64 arg_info_primitive(AMD64_Class cls, unsigned offset, unsigned size, unsigned alignment=0) {
    if (!alignment)
      alignment = size;
    
    ParameterInfo_AMD64 r;
    r.align = alignment;
    r.size = align_to(offset + size, alignment);
    if (offset & (alignment-1)) {
      // force memory class for unaligned fields
      r.low_eightbyte = amd64_memory;
      r.high_eightbyte = amd64_memory;
    } else {
      if (offset < 8)
        r.low_eightbyte = cls;
      if (offset + size > 8)
        r.high_eightbyte = cls;
    }
    return r;
  }
  
  ParameterInfo_AMD64 arg_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<>& type, const SourceLocation& location) {
    AggregateLayout layout = rewriter.aggregate_layout(type, location);
    if (layout.size > 16) {
    pass_in_memory:
      return ParameterInfo_AMD64(amd64_memory, layout.size<=8?amd64_no_class:amd64_memory, layout.size, layout.alignment);
    }
    
    ParameterInfo_AMD64 info;
    for (std::vector<AggregateLayout::Member>::const_iterator ii = layout.members.begin(), ie = layout.members.end(); ii != ie; ++ii) {
      if (ii->offset & (ii->alignment-1))
        goto pass_in_memory;
      
      ParameterInfo_AMD64 element;
      if (isa<PointerType>(ii->type) || isa<BooleanType>(ii->type) || isa<IntegerType>(ii->type)) {
        element = arg_info_primitive(amd64_integer, ii->offset, ii->size);
      } else if (ValuePtr<FloatType> float_ty = dyn_cast<FloatType>(ii->type)) {
        switch (float_ty->width()) {
        case FloatType::fp_x86_80:
          if (ii->offset == 0)
            return ParameterInfo_AMD64(amd64_x87, amd64_x87_up, 0, 16);
          else
            goto pass_in_memory;
          break;
        
        case FloatType::fp128:
          if (ii->offset == 0)
            return ParameterInfo_AMD64(amd64_sse, amd64_sse_up, 0, 16);
          else
            goto pass_in_memory;
          
        default:
          element = arg_info_primitive(amd64_sse, ii->offset, ii->size);
        }
      } else {
        PSI_ASSERT_MSG(!dyn_cast<ParameterPlaceholder>(ii->type) && !dyn_cast<FunctionParameter>(ii->type),
                        "low-level parameter type should not depend on function type parameters");
        PSI_FAIL("unknown type");
      }
      
      info = arg_info_combine(info, element);
    }
    
    if ((info.low_eightbyte == amd64_memory) || (info.high_eightbyte == amd64_memory)) {
      info.low_eightbyte = amd64_memory;
      info.high_eightbyte = info.size > 8 ? amd64_memory : amd64_no_class;
    } else if ((info.high_eightbyte == amd64_sse_up) && (info.low_eightbyte != amd64_sse)) {
      /*
       * This rule seems a little crazy since SSEUP would usually be preceeded by SSE,
       * however it does indeed appear to imply that
       * union {__float128 a; long b;}
       * is passed as
       * struct {long a; double b;}
       */
      info.high_eightbyte = amd64_sse;
    }
    
    return info;
  }

  /**
   * Get the type used to pass a parameter of a given class with a
   * given size in bytes.
   */
  ValuePtr<> amd64_coercion_type(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, AMD64_Class amd64_class, unsigned size, const ValuePtr<>& orig_type, const SourceLocation& location) {
    switch (amd64_class) {
    case amd64_sse: {
      FloatType::Width width;
      switch (size) {
      case 4:  width = FloatType::fp32; break;
      case 8:  width = FloatType::fp64; break;
      case 16: width = FloatType::fp128; break;
      default: PSI_FAIL("unknown SSE floating point type width");
      }
      if (ValuePtr<FloatType> float_ty = dyn_cast<FloatType>(orig_type)) {
        if (width == float_ty->width())
          return ValuePtr<>();
      }
      return FunctionalBuilder::float_type(rewriter.context(), width, location);
    }

    case amd64_x87:
      PSI_ASSERT(size == 16);
      if (ValuePtr<FloatType> float_ty = dyn_cast<FloatType>(orig_type)) {
        if (float_ty->width() == FloatType::fp_x86_80)
          return ValuePtr<>();
      }
      return FunctionalBuilder::float_type(rewriter.context(), FloatType::fp_x86_80, location);

    case amd64_integer: {
      // Pointers must be kept as pointers
      if (isa<IntegerType>(orig_type) || isa<PointerType>(orig_type))
        return ValuePtr<>();
      
      IntegerType::Width width;
      switch (size) {
      case 1:  width = IntegerType::i8; break;
      case 2:  width = IntegerType::i16; break;
      case 4:  width = IntegerType::i32; break;
      case 8:  width = IntegerType::i64; break;
      case 16: width = IntegerType::i128; break;
      default: PSI_FAIL("unknown integer width in AMD64 parameter passing");
      }
      // This is handled separately because integers can be signed or unsigned,
      // but LLVM doesn't distinguish the two
      if (ValuePtr<IntegerType> int_ty = dyn_cast<IntegerType>(orig_type)) {
        if (width == int_ty->width())
          return ValuePtr<>();
      }
      return FunctionalBuilder::int_type(rewriter.context(), width, false, location);
    }

    default:
      PSI_FAIL("unexpected amd64 parameter class here");
    }
  }
  
  ParameterInfo amd64_handle_parameter(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ParameterType& type, unsigned& n_regs, unsigned& n_sse_regs, const SourceLocation& location) {
    ParameterInfo_AMD64 et = arg_info(rewriter, type.value, location);
    if (et.low_eightbyte == amd64_memory) {
      PSI_ASSERT((et.high_eightbyte == amd64_memory) || (et.high_eightbyte == amd64_no_class));
      return parameter_by_value(std::max(et.align, 8u));
    } else if (et.low_eightbyte == amd64_no_class) {
      if (et.high_eightbyte != amd64_no_class)
        rewriter.error_context().error_throw(location, "Struct layout with no data in low eightbyte not covered by x86-64 System V ABI");
      return parameter_ignore();
    }
    
    if ((et.low_eightbyte == amd64_sse) && (et.high_eightbyte == amd64_sse_up)) {
      // Always coerce: LLVM doesn't actually support 128-bit floats right now
      // so coercion will certainly be required if I ever add them
      ValuePtr<> double_ty = FunctionalBuilder::float_type(rewriter.context(), FloatType::fp64, location);
      ValuePtr<> coerce_type = FunctionalBuilder::struct_type(rewriter.context(), std::vector<ValuePtr<> >(2, double_ty), location);
      return parameter_default(coerce_type, true);
    } else if (et.low_eightbyte == amd64_x87) {
      PSI_ASSERT(et.high_eightbyte == amd64_x87_up);
      // These are passed on the stack but LLVM understands that
      ValuePtr<> coerce_type = FunctionalBuilder::float_type(rewriter.context(), FloatType::fp_x86_80, location);
      // It could be in a single element struct
      return parameter_default(coerce_type != type.value ? coerce_type : ValuePtr<>());
    }
    
    if (et.size <= 8) {
      if (et.low_eightbyte == amd64_integer) {
        if (n_regs) {
          --n_regs;
          // May need to coerce
          return parameter_default(amd64_coercion_type(rewriter, et.low_eightbyte, et.size, type.value, location));
        }
      } else {
        PSI_ASSERT(et.low_eightbyte == amd64_sse);
        if (n_sse_regs) {
          --n_sse_regs;
          return parameter_default(amd64_coercion_type(rewriter, et.low_eightbyte, et.size, type.value, location));
        }
      }
      return parameter_by_value(std::max(et.align, 8u));
    } else {
      // Definitely requires coercion if enough registers available
      unsigned req_regs = 0, req_sse_regs = 0;
      if (et.low_eightbyte == amd64_integer)
        ++req_regs;
      else
        ++req_sse_regs;
      
      if (et.high_eightbyte == amd64_integer)
        ++req_regs;
      else
        ++req_sse_regs;
      
      if ((n_regs >= req_regs) && (n_sse_regs >= req_sse_regs)) {
        n_regs -= req_regs;
        n_sse_regs -= req_sse_regs;
        ValuePtr<> coerce_low = amd64_coercion_type(rewriter, et.low_eightbyte, 8, ValuePtr<>(), location);
        ValuePtr<> coerce_high = amd64_coercion_type(rewriter, et.high_eightbyte, et.size-8, ValuePtr<>(), location);
        ValuePtr<> coerce_type = FunctionalBuilder::struct_type(rewriter.context(), vector_of(coerce_low, coerce_high), location);
        return parameter_default(coerce_type);
      } else {
        return parameter_by_value(std::max(et.align, 8u));
      }
    }
  }
  
  virtual FunctionTypeInfo parameter_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionType>& function_type, const SourceLocation& location) {
    unsigned n_regs = 6, n_sse_regs = 8;
    
    FunctionTypeInfo fti = function_type_info(rewriter, function_type, location);
    if (fti.is_sret)
      --n_regs;
    
    for (std::size_t ii = function_type->n_phantom(), ie = argument_count(function_type); ii != ie; ++ii)
      fti.parameters.push_back(amd64_handle_parameter(rewriter, function_type->parameter_types()[ii], n_regs, n_sse_regs, location));
    
    if (function_type->sret())
      fti.parameters.push_back(parameter_default());
    
    return fti;
  }
  
  virtual ParameterInfo return_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionType>& function_type, const SourceLocation& location) {
    if (function_type->sret())
      return parameter_ignore();
    
    // Number of registers which can be used for returning
    unsigned n_regs = 2, n_sse_regs = 2;
    return amd64_handle_parameter(rewriter, function_type->result_type(), n_regs, n_sse_regs, location);
  }
};

class CallingConventionHandler_x86_cdecl : public CallingConventionSimple {
  bool register_return;
  
  ValuePtr<> coercion_type(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, unsigned size, const ValuePtr<>& orig_type, const SourceLocation& location) {
    if (isa<IntegerType>(orig_type) || isa<PointerType>(orig_type))
      return ValuePtr<>();
    
    PSI_ASSERT((1 <= size) && (size <= 8));
    unsigned short_size = 1 + (size - 1) % 4;
    PSI_ASSERT((1 <= short_size) && (short_size <= 4));
    
    IntegerType::Width w;
    if (short_size == 1) w = IntegerType::i8;
    else if (short_size == 2) w = IntegerType::i16;
    else w = IntegerType::i32;
    
    if (size <= 4) {
      return FunctionalBuilder::int_type(rewriter.context(), w, false, location);
    } else {
      std::vector<ValuePtr<> > entries;
      entries.push_back(FunctionalBuilder::int_type(rewriter.context(), IntegerType::i32, false, location));
      entries.push_back(FunctionalBuilder::int_type(rewriter.context(), w, false, location));
      return FunctionalBuilder::struct_type(rewriter.context(), entries, location);
    }
  }
  
  virtual FunctionTypeInfo parameter_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionType>& function_type, const SourceLocation& location) {
    FunctionTypeInfo fti = function_type_info(rewriter, function_type, location);
    
    for (std::size_t ii = function_type->n_phantom(), ie = argument_count(function_type); ii != ie; ++ii) {
      ValuePtr<> simple_type = rewriter.simplify_argument_type(function_type->parameter_types()[ii].value);
      if (isa<StructType>(simple_type) || isa<UnionType>(simple_type) || isa<ArrayType>(simple_type)) {
        AggregateLayout layout = rewriter.aggregate_layout(simple_type, location, false);
        if (layout.size > 0)
          fti.parameters.push_back(parameter_by_value(std::max(unsigned(layout.alignment), 4u)));
        else
          fti.parameters.push_back(parameter_ignore());
      } else {
        fti.parameters.push_back(parameter_default());
      }
    }
    
    if (function_type->sret())
      fti.parameters.push_back(parameter_default());

    return fti;
  }

  virtual ParameterInfo return_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionType>& function_type, const SourceLocation& location) {
    if (function_type->sret())
      return parameter_ignore();
    
    ValuePtr<> simple_type = rewriter.simplify_argument_type(function_type->result_type().value);
    if (isa<StructType>(simple_type) || isa<UnionType>(simple_type) || isa<ArrayType>(simple_type)) {
      AggregateLayout layout = rewriter.aggregate_layout(simple_type, location, false);
      if (layout.size == 0)
        return parameter_ignore();
      else if (!register_return || (layout.size > 8))
        return parameter_by_value(std::max(unsigned(layout.alignment), 4u));
      else
        return parameter_default(coercion_type(rewriter, layout.size, simple_type, location));
    } else {
      return parameter_default();
    }
  }
  
public:
  CallingConventionHandler_x86_cdecl(bool register_return_) : register_return(register_return_) {}
};

class CallingConventionHandler_arm_eabi : public CallingConventionSimple {
  bool hard_float;
  
  ValuePtr<> coercion_type(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, unsigned size, unsigned alignment, const ValuePtr<>& orig_type, const SourceLocation& location) {
    if (size <= 4) {
      if (isa<IntegerType>(orig_type) || isa<PointerType>(orig_type))
        return ValuePtr<>();
      
      IntegerType::Width w;
      if (size == 1) w = IntegerType::i8;
      else if (size == 2) w = IntegerType::i16;
      else w = IntegerType::i32;
      
      return FunctionalBuilder::int_type(rewriter.context(), w, false, location);
    } else {
      ValuePtr<> word_type;
      unsigned n_words;
      if (alignment > 4) {
        n_words = (size + 7) / 8;
        word_type = FunctionalBuilder::int_type(rewriter.context(), IntegerType::i64, false, location);
      } else {
        n_words = (size + 3) / 4;
        word_type = FunctionalBuilder::int_type(rewriter.context(), IntegerType::i32, false, location);
      }
      return FunctionalBuilder::array_type(word_type, n_words, location);
    }
  }
  
  virtual FunctionTypeInfo parameter_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionType>& function_type, const SourceLocation& location) {
    std::size_t n_core_regs = 4;
    bool stack_used = false;
    
    FunctionTypeInfo fti = function_type_info(rewriter, function_type, location);
    if (fti.is_sret)
      --n_core_regs;
    
    for (std::size_t ii = function_type->n_phantom(), ie = argument_count(function_type); ii != ie; ++ii) {
      ValuePtr<> simple_type = rewriter.simplify_argument_type(function_type->parameter_types()[ii].value);
      AggregateLayout layout = rewriter.aggregate_layout(simple_type, location, false);
      if (layout.alignment == 8)
        n_core_regs &= ~1; // Round-to-even
      
      if (layout.size <= n_core_regs*4) {
        fti.parameters.push_back(parameter_default(coercion_type(rewriter, layout.size, layout.alignment, simple_type, location)));
        n_core_regs -= (layout.size + 3) / 4;
      } else if (!stack_used && (n_core_regs > 0)) {
        ValuePtr<> reg_part = coercion_type(rewriter, n_core_regs * 4, layout.alignment, default_, location);
        ValuePtr<> stack_part = coercion_type(rewriter, align_to(layout.size - n_core_regs*4, layout.alignment), layout.alignment, default_, location);
        fti.parameters.push_back(parameter_arm_split(layout.alignment, default_, default_, location));
        n_core_regs = 0;
      } else {
        PSI_ASSERT(n_core_regs == 0);
        fti.parameters.push_back(parameter_by_value(layout.alignment));
      }
    }
    
    if (function_type->sret())
      fti.parameters.push_back(parameter_default());
    
    return fti;
  }

  virtual ParameterInfo return_info(AggregateLoweringPass::AggregateLoweringRewriter& rewriter, const ValuePtr<FunctionType>& function_type, const SourceLocation& location) {
    if (function_type->sret())
      return parameter_ignore();
    
    ValuePtr<> simple_type = rewriter.simplify_argument_type(function_type->result_type().value);
    AggregateLayout layout = rewriter.aggregate_layout(simple_type, location, false);
    if (isa<StructType>(simple_type) || isa<UnionType>(simple_type) || isa<ArrayType>(simple_type)) {
      if (layout.size == 0)
        return parameter_ignore();
      else if (layout.size > 4)
        return parameter_by_value(layout.alignment);
      else
        return parameter_default(coercion_type(rewriter, layout.size, layout.alignment, simple_type, location));
    } else {
      return parameter_default();
    }
  }
  
public:
  CallingConventionHandler_arm_eabi(bool hard_float_) : hard_float(hard_float_) {}
};

void calling_convention_handler(const CompileErrorPair& error_loc, llvm::Triple triple, CallingConvention cc, UniquePtr<CallingConventionHandler>& result) {
  switch (cc) {
  case cconv_c:
    switch (triple.getArch()) {
    case llvm::Triple::x86_64:
      switch (triple.getOS()) {
      case llvm::Triple::FreeBSD:
      case llvm::Triple::Linux: result.reset(new CallingConventionHandler_x86_64_SystemV()); return;
      default: break;
      }
      break;
      
    case llvm::Triple::x86:
      switch (triple.getOS()) {
      case llvm::Triple::Linux: result.reset(new CallingConventionHandler_x86_cdecl(false)); return;
      case llvm::Triple::FreeBSD:
      case llvm::Triple::MinGW32:
      case llvm::Triple::Win32: result.reset(new CallingConventionHandler_x86_cdecl(true)); return;
      default: break;
      }
      break;
      
    case llvm::Triple::arm:
      switch (triple.getOS()) {
      case llvm::Triple::Linux:
        switch (triple.getEnvironment()) {
        case llvm::Triple::GNUEABIHF: result.reset(new CallingConventionHandler_arm_eabi(true)); return;
        case llvm::Triple::GNUEABI:
        case llvm::Triple::Android: result.reset(new CallingConventionHandler_arm_eabi(false)); return;
        default: break;
        }
      default: break;
      }
      break;

    default:
      break;
    }

  case cconv_x86_stdcall:
    break;
    
  case cconv_x86_thiscall:
    break;
    
  case cconv_x86_fastcall:
    break;
  }
  
  error_loc.error_throw(boost::format("Calling convention %s not supported on target %s") % cconv_name(cc) % triple.str());
}
}
}
}
