#include "Tree.hpp"

#include <boost/checked_delete.hpp>

namespace Psi {
  namespace Compiler {
    Tree::Tree(CompileContext& compile_context, const SourceLocation& location, typename MoveRef<DependencyPtr>::type dependency)
    : m_compile_context(&compile_context),
    m_location(location),
    m_dependency(move_ref(dependency)) {
      m_completion_state = m_dependency ? completion_constructed : completion_finished;
    }

    Tree::Tree(const TreePtr<Type>& type, const SourceLocation& location, typename MoveRef<DependencyPtr>::type dependency)
    : m_compile_context(&type->compile_context()),
    m_location(location),
    m_type(type),
    m_dependency(move_ref(dependency)) {
      m_completion_state = m_dependency ? completion_constructed : completion_finished;
    }

    Tree::Tree(CompileContext& compile_context, const SourceLocation& location)
    : m_compile_context(&compile_context),
    m_location(location),
    m_completion_state(completion_finished) {
    }
    
    Tree::Tree(const TreePtr<Type>& type, const SourceLocation& location)
    : m_compile_context(&type->compile_context()),
    m_location(location),
    m_type(type),
    m_completion_state(completion_finished) {
    }

    Tree::~Tree() {
    }

    void Tree::gc_visit(GCVisitor& visitor) {
      visitor % m_type;
      if (m_dependency)
        m_dependency->vptr->gc_visit(m_dependency.get(), &visitor);
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
      DependencyPtr dependency;
      dependency.swap(m_dependency);
      PSI_ASSERT(dependency);
      
      try {
        m_completion_state = completion_running;
        dependency->vptr->run(dependency.get(), this);
        m_completion_state = completion_finished;
      } catch (...) {
        m_completion_state = completion_failed;
        throw CompileException();
      }
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

    Type::Type(CompileContext& compile_context, const SourceLocation& location, typename MoveRef<DependencyPtr>::type dependency)
    : Tree(compile_context, location, move_ref(dependency)) {
    }
    
    Type::Type(const TreePtr<Type>& type, const SourceLocation& location, typename MoveRef<DependencyPtr>::type dependency)
    : Tree(type, location, move_ref(dependency)) {
    }
    
    Type::Type(CompileContext& compile_context, const SourceLocation& location)
    : Tree(compile_context, location) {
    }
    
    Type::Type(const TreePtr<Type>& type, const SourceLocation& location)
    : Tree(type, location) {
    }

    Type::~Type() {
    }

    FunctionType::FunctionType(CompileContext& context, const SourceLocation& location, typename MoveRef<DependencyPtr>::type dependency)
    : Type(context, location, dependency) {
    }

    FunctionType::FunctionType(CompileContext& context, const SourceLocation& location)
    : Type(context, location) {
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
      
      for (ArrayList<TreePtr<FunctionTypeArgument> >::iterator ii = arguments.begin(), ie = arguments.end(); ii != ie; ++ii) {
        TreePtr<> rw_type = (*ii)->type()->rewrite(location, substitutions);
        if (rw_type != (*ii)->type())
          goto rewrite_required;
      }
      return TreePtr<>(this);

    rewrite_required:
      std::map<TreePtr<>, TreePtr<> > child_substitutions(substitutions);
      ArrayList<TreePtr<FunctionTypeArgument> > rw_arguments;
      for (ArrayList<TreePtr<FunctionTypeArgument> >::iterator ii = arguments.begin(), ie = arguments.end(); ii != ie; ++ii) {
        TreePtr<> rw_type = (*ii)->type()->rewrite(location, child_substitutions);
        TreePtr<Type> rw_cast_type = dynamic_pointer_cast<Type>(rw_type);
        if (!rw_cast_type)
          compile_context().error_throw(location, "Rewritten function argument type is not a type");

        TreePtr<FunctionTypeArgument> rw_arg(new FunctionTypeArgument(rw_cast_type, this->location()));
        rw_arguments.push_back(rw_arg);
        child_substitutions[*ii] = rw_arg;
      }

      TreePtr<> rw_result = result_type->type()->rewrite(location, child_substitutions);
      TreePtr<Type> cast_rw_result = dynamic_pointer_cast<Type>(rw_result);
      if (!cast_rw_result)
        compile_context().error_throw(location, "Rewritten function result type is not a type");

      TreePtr<FunctionType> rw_self(new FunctionType(compile_context(), this->location()));
      rw_self->arguments.swap(rw_arguments);

      return rw_self;
    }

    TreePtr<Type> FunctionType::argument_type_after(const SourceLocation& location, const ArrayList<TreePtr<> >& previous) {
      if (previous.size() >= arguments.size())
        compile_context().error_throw(location, "Too many arguments passed to function");
      
      std::map<TreePtr<>, TreePtr<> > substitutions;
      for (unsigned ii = 0, ie = previous.size(); ii != ie; ++ii)
        substitutions[arguments[ii]] = previous[ii];

      TreePtr<> type = arguments[previous.size()]->type()->rewrite(location, substitutions);
      TreePtr<Type> cast_type = dynamic_pointer_cast<Type>(type);
      if (!cast_type)
        compile_context().error_throw(location, "Rewritten function argument type is not a type");

      return cast_type;
    }
    
    TreePtr<Type> FunctionType::result_type_after(const SourceLocation& location, const ArrayList<TreePtr<> >& previous) {
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

    FunctionTypeArgument::FunctionTypeArgument(const TreePtr<Type>& type, const SourceLocation& location)
    : Tree(type, location) {
    }

    FunctionTypeArgument::~FunctionTypeArgument() {
    }

    void FunctionTypeArgument::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
    }

    Function::Function(const TreePtr<FunctionType>& type, const SourceLocation& location)
    : Tree(type, location) {
    }

    Function::Function(const TreePtr<FunctionType>& type, const SourceLocation& location, typename MoveRef<DependencyPtr>::type dependency)
    : Tree(type, location, move_ref(dependency)) {
    }

    Function::~Function() {
    }

    void Function::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor.visit_range(arguments);
      visitor % body;
    }

    FunctionArgument::FunctionArgument(const TreePtr<Type>& type, const SourceLocation& location)
    : Tree(type, location) {
    }

    FunctionArgument::~FunctionArgument() {
    }

    TryFinally::TryFinally(const TreePtr<Type>& type, const SourceLocation& location)
    : Tree(type, location) {
    }

    TryFinally::TryFinally(const TreePtr<Type>& type, const SourceLocation& location, typename MoveRef<DependencyPtr>::type dependency)
    : Tree(type, location, dependency) {
    }

    TryFinally::~TryFinally() {
    }

    void TryFinally::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % try_block % finally_block;
    }

    Block::Block(const TreePtr<Type>& type, const SourceLocation& location)
    : Tree(type, location) {
    }

    Block::Block(const TreePtr<Type>& type, const SourceLocation& location, typename MoveRef<DependencyPtr>::type dependency)
    : Tree(type, location, dependency) {
    }

    Block::~Block() {
    }

    void Block::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      for (ArrayList<TreePtr<Statement> >::iterator ii = statements.begin(), ie = statements.end(); ii != ie; ++ii)
        visitor % *ii;
    }

    Statement::Statement(const TreePtr<>& value_, const SourceLocation& location)
    : Tree(value_->type(), location), value(value_) {
    }

    Statement::~Statement() {
    }

    void Statement::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % value;
    }
  }
}