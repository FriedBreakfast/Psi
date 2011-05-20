#include "Tree.hpp"

#include <boost/checked_delete.hpp>

namespace Psi {
  namespace Compiler {
    Tree::Tree(CompileContext& compile_context, const SourceLocation& location, const TreePtr<Type>& type, const DependencyPtr& dependency)
    : m_compile_context(&compile_context),
    m_location(location),
    m_type(type),
    m_completion_state(completion_constructed),
    m_dependency(0) {
      compile_context.m_gc_pool.add(this);
      m_dependency = dependency.release();
      if (!m_dependency)
        m_completion_state = completion_finished;
    }

    Tree::~Tree() {
      if (m_dependency)
        m_dependency->vptr->destroy(m_dependency);
    }

    void Tree::gc_visit(GCVisitor& visitor) {
      visitor % m_type;
      if (m_dependency)
        m_dependency->vptr->gc_visit(m_dependency, &visitor);
    }

    void Tree::gc_destroy() {
      delete this;
    }

    TreePtr<> Tree::rewrite_hook(const SourceLocation&, const std::map<TreePtr<>, TreePtr<> >&) {
      return TreePtr<>(this);
    }
    
    void Tree::complete() {
      switch (m_completion_state) {
      case completion_constructed: complete_main(); break;
      case completion_running: compile_context().error_throw(m_location, "Circular dependency during code evaluation");
      case completion_finished: break;
      case completion_failed: throw CompileException();
      default: PSI_FAIL("unknown future state");
      }
    }

    void Tree::dependency_complete() {
      switch (m_completion_state) {
      case completion_constructed: complete_main(); break;
      case completion_running:
      case completion_finished: break;
      case completion_failed: throw CompileException();
      default: PSI_FAIL("unknown future state");
      }
    }
    
    void Tree::complete_main() {
      TreePtr<CompileImplementation> dependency;
      dependency.swap(m_dependency);
      PSI_ASSERT(dependency);
      
      DependencyVtable *vptr = static_cast<DependencyVtable*>(compile_context().jit_compile(dependency->vtable));
      try {
        m_completion_state = completion_running;
        vptr->run(vptr, dependency.get(), this);
        m_completion_state = completion_finished;
      } catch (...) {
        m_completion_state = completion_failed;
        throw CompileException();
      }
    }

    void Tree::throw_circular_exception() {
    }

    /**
      * \brief Rewrite a term, substituting new trees for existing ones.
      *
      * \param location Location to use for error reporting.
      * \param substitutions Substitutions to make.
      */
    TreePtr<> Tree::rewrite(const SourceLocation& location, const std::map<TreePtr<>, TreePtr<> >& substitutions) {
      if (!this)
        return TreePtr<>();

      TreePtr<> this_gc(this);
      std::map<TreePtr<>, TreePtr<> >::const_iterator i = substitutions.find(this_gc);
      if (i != substitutions.end())
        return i->second;
      else
        return rewrite_hook(location, substitutions);
    }

    Type::Type(CompileContext& context) : Tree(context) {
    }

    Type::~Type() {
    }

    void Type::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % type;
    }

    EmptyType::EmptyType(CompileContext& compile_context) : Type(compile_context) {
    }

    EmptyType::~EmptyType() {
    }

    EmptyValue::EmptyValue(const TreePtr<EmptyType>& type) : Tree(type->compile_context()) {
      this->type = type;
    }

    EmptyValue::~EmptyValue() {
    }

    UnaryOperation::UnaryOperation(CompileContext& context) : Tree(context) {
    }

    UnaryOperation::UnaryOperation(const UnaryOperation& src)
    : Tree(src.compile_context()), child(src.child) {
      type = src.type;
    }

    UnaryOperation::~UnaryOperation() {
    }

    void UnaryOperation::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % child;
    }

    TreePtr<> UnaryOperation::rewrite_hook(const SourceLocation& location, const std::map<TreePtr<>, TreePtr<> >& substitutions) {
      TreePtr<> rw_child = child->rewrite(location, substitutions);
      if (rw_child == child)
        return this;

      TreePtr<UnaryOperation> rw_self = rewrite_duplicate_hook();
      rw_self->child = rw_child;
      return rw_self;
    }

    BinaryOperation::BinaryOperation(CompileContext& context) : Tree(context) {
    }

    BinaryOperation::BinaryOperation(const BinaryOperation& src)
    : Tree(src.compile_context()), left(src.left), right(src.right) {
      type = src.type;
    }

    BinaryOperation::~BinaryOperation() {
    }

    void BinaryOperation::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % left % right;
    }

    TreePtr<> BinaryOperation::rewrite_hook(const SourceLocation& location, const std::map<TreePtr<>, TreePtr<> >& substitutions) {
      TreePtr<> rw_left = left->rewrite(location, substitutions);
      TreePtr<> rw_right = right->rewrite(location, substitutions);
      if ((left != rw_left) || (right != rw_right))
        return this;

      TreePtr<BinaryOperation> rw_self = rewrite_duplicate_hook();
      rw_self->left = rw_left;
      rw_self->right = rw_right;
      return rw_self;
    }

    FunctionType::FunctionType(CompileContext& context) : Type(context) {
    }

    FunctionType::~FunctionType() {
    }

    void FunctionType::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % result_type;
      visitor.visit_range(arguments);
    }

    TreePtr<> FunctionType::rewrite_hook(const SourceLocation& location, const std::map<TreePtr<>, TreePtr<> >& substitutions) {
      PSI_FAIL("need to sort out function type equivalence checking");
      
      for (std::vector<TreePtr<FunctionTypeArgument> >::iterator ii = arguments.begin(), ie = arguments.end(); ii != ie; ++ii) {
        TreePtr<> rw_type = (*ii)->type->rewrite(location, substitutions);
        if (rw_type != (*ii)->type)
          goto rewrite_required;
      }
      return TreePtr<>(this);

    rewrite_required:
      TreePtr<FunctionType> rw_self(new FunctionType(compile_context()));
      rw_self->arguments.reserve(arguments.size());

      std::map<TreePtr<>, TreePtr<> > child_substitutions(substitutions);
      std::vector<TreePtr<FunctionTypeArgument> > rw_arguments;
      for (std::vector<TreePtr<FunctionTypeArgument> >::iterator ii = arguments.begin(), ie = arguments.end(); ii != ie; ++ii) {
        TreePtr<> rw_type = (*ii)->type->rewrite(location, child_substitutions);
        TreePtr<Type> rw_cast_type = dynamic_pointer_cast<Type>(rw_type);
        if (!rw_cast_type)
          compile_context().error_throw(location, "Rewritten function argument type is not a type");

        TreePtr<FunctionTypeArgument> rw_arg(new FunctionTypeArgument(compile_context()));
        rw_arg->type = rw_cast_type;
        rw_self->arguments.push_back(rw_arg);
        child_substitutions[*ii] = rw_arg;
      }

      TreePtr<> rw_result = result_type->type->rewrite(location, child_substitutions);
      rw_self->result_type = dynamic_pointer_cast<Type>(rw_result);
      if (!rw_self->result_type)
        compile_context().error_throw(location, "Rewritten function result type is not a type");

      return rw_self;
    }

    TreePtr<Type> FunctionType::argument_type_after(const SourceLocation& location, const std::vector<TreePtr<> >& previous) {
      if (previous.size() >= arguments.size())
        compile_context().error_throw(location, "Too many arguments passed to function");
      
      std::map<TreePtr<>, TreePtr<> > substitutions;
      for (unsigned ii = 0, ie = previous.size(); ii != ie; ++ii)
        substitutions[arguments[ii]] = previous[ii];

      TreePtr<> type = arguments[previous.size()]->type->rewrite(location, substitutions);
      TreePtr<Type> cast_type = dynamic_pointer_cast<Type>(type);
      if (!cast_type)
        compile_context().error_throw(location, "Rewritten function argument type is not a type");

      return cast_type;
    }
    
    TreePtr<Type> FunctionType::result_type_after(const SourceLocation& location, const std::vector<TreePtr<> >& previous) {
      if (previous.size() != arguments.size())
        compile_context().error_throw(location, "Incorrect number of arguments passed to function");

      std::map<TreePtr<>, TreePtr<> > substitutions;
      for (unsigned ii = 0, ie = previous.size(); ii != ie; ++ii)
        substitutions[arguments[ii]] = previous[ii];

      TreePtr<> type = result_type->rewrite(location, substitutions);
      TreePtr<Type> cast_type = dynamic_pointer_cast<Type>(type);
      if (!cast_type)
        compile_context().error_throw(location, "Rewritten function result type is not a type");

      return cast_type;
    }

    FunctionTypeArgument::FunctionTypeArgument(CompileContext& context) : Tree(context) {
    }

    FunctionTypeArgument::~FunctionTypeArgument() {
    }

    void FunctionTypeArgument::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
    }

    Function::Function(CompileContext& context) : Tree(context) {
    }

    Function::~Function() {
    }

    void Function::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor.visit_range(arguments);
      visitor % body;
    }

    FunctionArgument::FunctionArgument(CompileContext& context) : Tree(context) {
    }

    FunctionArgument::~FunctionArgument() {
    }

    void FunctionArgument::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % type;
    }

    TryFinally::TryFinally(CompileContext& context) : Tree(context) {
    }

    TryFinally::~TryFinally() {
    }

    void TryFinally::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % try_block % finally_block;
    }

    Block::Block(CompileContext& context) : Tree(context) {
    }

    Block::~Block() {
    }

    void Block::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % statements;
    }

    Statement::Statement(CompileContext& context) : Tree(context) {
    }

    Statement::~Statement() {
    }

    void Statement::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % next % value;
    }
  }
}