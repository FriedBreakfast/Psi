#ifndef HPP_PSI_AGGREGATE
#define HPP_PSI_AGGREGATE

#include "Tree.hpp"

namespace Psi {
namespace Compiler {
/**
 * \brief Parameters to lifecycle functions.
 */
struct AggregateLifecycleParameters {
  /// \brief Generic specialization parameters
  PSI_STD::vector<TreePtr<Term> > parameters;
  
  /// \brief Destination variable
  TreePtr<Term> dest;
  /// \brief Source variable, if a two parameter function
  TreePtr<Term> src;
};

struct AggregateMovableParameter {
  /// \brief Containing generic type
  TreePtr<GenericType> generic;
  /// \brief Initialization parameters
  AggregateLifecycleParameters lc_init;
  /// \brief Move parameters
  AggregateLifecycleParameters lc_move;
  /// \brief Finalization parameters
  AggregateLifecycleParameters lc_fini;
};

struct AggregateMovableResult {
  /// \brief Initialization code
  TreePtr<Term> lc_init;
  /// \brief Move code
  TreePtr<Term> lc_move;
  /// \brief Finalization code
  TreePtr<Term> lc_fini;
  
  template<typename V>
  static void visit(V& v) {
    v("lc_init", &AggregateMovableResult::lc_init)
    ("lc_move", &AggregateMovableResult::lc_move)
    ("lc_fini", &AggregateMovableResult::lc_fini);
  }
};

struct AggregateCopyableParameter {
  /// \brief Containing generic type
  TreePtr<GenericType> generic;
  /// \brief Copy parameters
  AggregateLifecycleParameters lc_copy;
};

struct AggregateCopyableResult {
  /// \brief Copy code
  TreePtr<Term> lc_copy;
  
  template<typename V>
  static void visit(V& v) {
    v("lc_copy", &AggregateCopyableResult::lc_copy);
  }
};

struct AggregateMemberArgument;

/**
 * \brief Result returned from aggregate member macros.
 */
struct AggregateMemberResult {
  AggregateMemberResult() : no_move(false), no_copy(false) {}
  
  /// \brief Member type, or no data if NULL.
  TreePtr<Term> member_type;
  
  /// \brief Do not generate movable interface
  PsiBool no_move;
  /// \brief Do not generate copyable interface
  PsiBool no_copy;
  
  /// \brief Callback to generate movable interface functions
  SharedDelayedValue<AggregateMovableResult, AggregateMovableParameter> movable_callback;
  /// \brief Callback to generate copyable interface functions
  SharedDelayedValue<AggregateCopyableResult, AggregateCopyableParameter> copyable_callback;
  /// \brief Callback to generate interface overloads
  SharedDelayedValue<PSI_STD::vector<TreePtr<OverloadValue> >, AggregateMemberArgument> overloads_callback;
};

/**
 * \brief Argument passed to aggregate member evaluations.
 */
struct AggregateMemberArgument {
  typedef AggregateMemberResult EvaluateResultType;
  
  /// \brief Generic type whose member is being built
  TreePtr<GenericType> generic;
  /// \brief Parameters used to set-up generic type.
  PSI_STD::vector<TreePtr<Anonymous> > parameters;
  /// \brief Generic type \c generic specialized with \c parameters
  TreePtr<Term> instance;
};
}
}

#endif