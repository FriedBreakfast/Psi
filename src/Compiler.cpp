#include "Compiler.hpp"
#include "Tree.hpp"
#include "Platform.hpp"
#include "Parser.hpp"

#ifdef PSI_DEBUG
#include <iostream>
#endif

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/next_prior.hpp>

namespace Psi {
  namespace Compiler {
    bool LogicalSourceLocation::Key::operator < (const Key& other) const {
      if (index) {
	if (other.index)
	  return index < other.index;
	else
	  return false;
      } else {
	if (other.index)
	  return true;
	else
	  return name < other.name;
      }
    } 

    bool LogicalSourceLocation::Compare::operator () (const LogicalSourceLocation& lhs, const LogicalSourceLocation& rhs) const {
      return lhs.m_key < rhs.m_key;
    }

    struct LogicalSourceLocation::KeyCompare {
      bool operator () (const Key& key, const LogicalSourceLocation& node) const {
	return key < node.m_key;
      }

      bool operator () (const LogicalSourceLocation& node, const Key& key) const {
	return node.m_key < key;
      }
    };

    LogicalSourceLocation::LogicalSourceLocation(const Key& key, const LogicalSourceLocationPtr& parent)
      : m_reference_count(0), m_key(key), m_parent(parent) {
    }

    LogicalSourceLocation::~LogicalSourceLocation() {
      if (m_parent)
	m_parent->m_children.erase(m_parent->m_children.iterator_to(*this));
    }

    /**
     * \brief Create a location with no parent. This should only be used by CompileContext.
     */
    LogicalSourceLocationPtr LogicalSourceLocation::new_root_location() {
      Key key;
      key.index = 0;
      return LogicalSourceLocationPtr(new LogicalSourceLocation(key, LogicalSourceLocationPtr()));
    }

    /**
     * \brief Create a new named child of this location.
     */
    LogicalSourceLocationPtr LogicalSourceLocation::named_child(const String& name) {
      Key key;
      key.index = 0;
      key.name = name;
      ChildMapType::insert_commit_data commit_data;
      std::pair<ChildMapType::iterator, bool> result = m_children.insert_check(key, KeyCompare(), commit_data);

      if (!result.second)
	return LogicalSourceLocationPtr(&*result.first);

      LogicalSourceLocationPtr node(new LogicalSourceLocation(key, LogicalSourceLocationPtr(this)));
      m_children.insert_commit(*node, commit_data);
      return node;
    }

    LogicalSourceLocationPtr LogicalSourceLocation::new_anonymous_child() {
      unsigned index = 1;
      ChildMapType::iterator end = m_children.end();
      if (!m_children.empty()) {
	ChildMapType::iterator last = end;
	--last;
	if (last->anonymous())
	  index = last->index() + 1;
      }

      Key key;
      key.index = index;
      LogicalSourceLocationPtr node(new LogicalSourceLocation(key, LogicalSourceLocationPtr(this)));
      m_children.insert(end, *node);
      return node;
    }

    /**
     * \brief Count the number of parent nodes between this location and the root node.
     */
    unsigned LogicalSourceLocation::depth() {
      unsigned d = 0;
      for (LogicalSourceLocation *l = this->parent().get(); l; l = l->parent().get())
	++d;
      return d;
    } 

    /**
     * \brief Get the ancestor of this location which is a certain
     * number of parent nodes away.
     */
    LogicalSourceLocationPtr LogicalSourceLocation::ancestor(unsigned depth) {
      LogicalSourceLocation *ptr = this;
      for (unsigned i = 0; i != depth; ++i)
	ptr = ptr->parent().get();
      return LogicalSourceLocationPtr(ptr);
    }

    /**
     * \brief Get the full name of this location for use in an error message.
     *
     * \param relative_to Location at which the error occurred, so
     * that a common prefix may be skipped.
     *
     * \param ignore_anonymous_tail Do not include anonymous nodes at
     * the bottom of the tree.
     */
    String LogicalSourceLocation::error_name(const LogicalSourceLocationPtr& relative_to, bool ignore_anonymous_tail) {
      std::vector<LogicalSourceLocation*> nodes;
      bool last_anonymous = false;

      unsigned print_depth = depth();
      if (relative_to) {
	// Find the common ancestor of this and relative_to.
	unsigned this_depth = depth();
	unsigned relative_to_depth = relative_to->depth();
	unsigned min_depth = std::min(this_depth, relative_to_depth);
	LogicalSourceLocation *this_ancestor = ancestor(this_depth - min_depth).get();
	LogicalSourceLocation *relative_to_ancestor = relative_to->ancestor(relative_to_depth - min_depth).get();
	print_depth = this_depth - min_depth;

	while (this_ancestor != relative_to_ancestor) {
	  ++print_depth;
	  this_ancestor = this_ancestor->parent().get();
	  relative_to_ancestor = relative_to_ancestor->parent().get();
	}

	// Keep going until we get to a named ancestor
	while (this_ancestor->anonymous()) {
	  ++print_depth;
	  this_ancestor = this_ancestor->parent().get();
	}
      }

      print_depth = std::max(print_depth, 1u);

      for (LogicalSourceLocation *l = this; print_depth; l = l->parent().get(), --print_depth) {
        if (!l->anonymous()) {
	  nodes.push_back(l);
	  last_anonymous = false;
        } else {
	  if (!last_anonymous)
	    nodes.push_back(l);
	  last_anonymous = true;
	}
      }

      if (ignore_anonymous_tail && nodes.front()->anonymous())
	nodes.erase(nodes.begin());

      if (!nodes.front()->parent())
	return "(root namespace)";

      std::stringstream ss;
      for (std::vector<LogicalSourceLocation*>::reverse_iterator ib = nodes.rbegin(),
	     ii = nodes.rbegin(), ie = nodes.rend(); ii != ie; ++ii) {
	if (ii != ib)
	  ss << '.';

	if ((*ii)->anonymous())
	  ss << "(anonymous)";
	else
	  ss << (*ii)->name();
      }

      const std::string& sss = ss.str();
      return String(sss.c_str(), sss.length());
    }

#if defined(PSI_DEBUG) || defined(PSI_DOXYGEN)
    /**
     * \brief Dump the name of this location to stderr.
     *
     * Only available if \c PSI_DEBUG is defined.
     */
    void LogicalSourceLocation::dump_error_name() {
      std::cerr << error_name(LogicalSourceLocationPtr()) << std::endl;
    }
#endif

    bool si_is_a(SIBase *object, const SIVtable *cls) {
      for (const SIVtable *super = object->m_vptr; super; super = super->super) {
        if (super == cls)
          return true;
      }

      return false;
    }

    /**
     * Create a string containing a list of parameters passed to an interface.
     */
    std::string interface_parameters_message(const List<TreePtr<Term> >& parameters, const SourceLocation& location) {
      std::stringstream ss;

      bool first = true;
      for (LocalIterator<TreePtr<Term> > p(parameters); p.next();) {
	if (first)
	  first = false;
	else
	  ss << ", ";
        TreePtr<Term>& current = p.current();
	ss << '\'' << current->location().logical->error_name(location.logical) << '\'';
      }

      return ss.str();
    }

    /**
     * \brief Checks the result of an interface lookup is non-NULL and of the correct type.
     *
     * \param parameters Parameters the interfacce was searched on.
     */
    void interface_cast_check(const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters, const TreePtr<>& result, const SourceLocation& location, const TreeVtable* cast_type) {
      CompileContext& compile_context = interface->compile_context();

      if (!result)
        compile_context.error_throw(location,
				    boost::format("'%s' interface not available for %s")
				    % interface->location().logical->error_name(location.logical)
				    % interface_parameters_message(parameters, location));

      if (!si_is_a(result.get(), &cast_type->base))
        compile_context.error_throw(location,
				    boost::format("'%s' interface has the wrong type")
				    % interface->location().logical->error_name(location.logical)
				    % interface_parameters_message(parameters, location),
				    CompileError::error_internal);
    }

    /**
     * \brief Walk a tree looking for an interface implementation.
     */
    TreePtr<> interface_lookup_search(const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters, const TreePtr<Term>& term) {
      if (TreePtr<ImplementationTerm> templ = dyn_treeptr_cast<ImplementationTerm>(term)) {
	for (PSI_STD::vector<TreePtr<Implementation> >::iterator ii = templ->implementations.begin(), ie = templ->implementations.end(); ii != ie; ++ii) {
	  if (interface == (*ii)->interface) {
	    PSI_ASSERT((*ii)->interface_parameters.size() == parameters.size());
	    PSI_STD::vector<TreePtr<Term> > wildcards((*ii)->wildcard_types.size());
	    for (std::size_t ji = 0, je = parameters.size(); ji != je; ++ji) {
	      if (!(*ii)->interface_parameters[ji]->match(parameters[ji], list_from_stl(wildcards)))
		goto match_failed;
	    }

	    return (*ii)->value;
	  }

	match_failed:;
	}
      }

      for (LocalIterator<TreePtr<Term> > p(*term); p.next();) {
        TreePtr<Term>& current = p.current();
        if (TreePtr<> result = interface_lookup_search(interface, parameters, current))
          return result;        
      }

      return TreePtr<>();
    }

    /**
     * \brief Locate an interface implementation for a given set of parameters.
     *
     * \param interface Interface to look up implementation for.
     * \param parameters Parameters to the interface.
     */    
    TreePtr<> interface_lookup(const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters, const SourceLocation&) {
      // Walk the various parameters and look for matching interface implementations
      for (LocalIterator<TreePtr<Term> > p(parameters); p.next();) {
        TreePtr<> result = interface_lookup_search(interface, parameters, p.current());
        if (result)
          return result;
      }

      return TreePtr<>();
    }

    CompileException::CompileException() {
    }

    CompileException::~CompileException() throw() {
    }

    const char *CompileException::what() const throw() {
      return "Psi compile exception";
    }

    CompileError::CompileError(CompileContext& compile_context, const SourceLocation& location, unsigned flags)
      : m_compile_context(&compile_context), m_location(location), m_flags(flags) {
      bool error_occurred = false;
      switch (flags) {
      case error_warning: m_type = "warning"; break;
      case error_internal: m_type = "internal error"; error_occurred = true; break;
      default: m_type = "error"; error_occurred = true; break;
      }

      if (error_occurred)
	m_compile_context->set_error_occurred();

      m_compile_context->error_stream() << boost::format("%s:%s: in '%s'\n") % location.physical.file->url
	% location.physical.first_line % location.logical->error_name(LogicalSourceLocationPtr(), true);
    }

    void CompileError::info(const std::string& message) {
      info(m_location, message);
    }

    void CompileError::info(const SourceLocation& location, const std::string& message) {
      m_compile_context->error_stream() << boost::format("%s:%s:%s: %s\n")
	% location.physical.file->url % location.physical.first_line % m_type % message;
    }

    void CompileError::end() {
    }

    CompileContext::CompileContext(std::ostream *error_stream)
      : m_error_stream(error_stream), m_error_occurred(false), m_running_completion_stack(NULL),
	m_root_location(PhysicalSourceLocation(), LogicalSourceLocation::new_root_location()) {
      PhysicalSourceLocation core_physical_location;
      m_root_location.physical.file.reset(new SourceFile());
      m_root_location.physical.first_line = m_root_location.physical.first_column = 0;
      m_root_location.physical.last_line = m_root_location.physical.last_column = 0;

      SourceLocation psi_location = m_root_location.named_child("psi");
      SourceLocation psi_compiler_location = psi_location.named_child("compiler");

      m_metatype.reset(new Metatype(*this, psi_location));
      m_empty_type.reset(new EmptyType(*this, psi_location));
      m_macro_interface.reset(new Interface(*this, psi_compiler_location.named_child("Macro")));
      m_argument_passing_interface.reset(new Interface(*this, psi_compiler_location.named_child("ArgumentPasser")));
    }

    struct CompileContext::TreeDisposer {
      void operator () (Tree *t) {
	if (!--t->m_reference_count)
	  t->destroy();
	else
	  PSI_WARNING_FAIL("Dangling pointers to Tree during context destruction");
      }
    };

    CompileContext::~CompileContext() {
      m_metatype.reset();
      m_empty_type.reset();
      m_macro_interface.reset();
      m_argument_passing_interface.reset();

      // Add extra reference to each Tree
      BOOST_FOREACH(Tree& t, m_gc_list)
	++t.m_reference_count;

      // Clear cross references in each Tree
      BOOST_FOREACH(Tree& t, m_gc_list)
	t.gc_clear();

      m_gc_list.clear_and_dispose(TreeDisposer());
    }

    void CompileContext::error(const SourceLocation& loc, const std::string& message, unsigned flags) {
      CompileError error(*this, loc, flags);
      error.info(message);
      error.end();
    }

    void CompileContext::error_throw(const SourceLocation& loc, const std::string& message, unsigned flags) {
      error(loc, message, flags);
      throw CompileException();
    }

    /**
     * \brief JIT compile a global symbol.
     */
    void* CompileContext::jit_compile(const TreePtr<Global>& global) {
      if (!global->m_jit_ptr) {
        PSI_FAIL("not implemented");
      }

      return global->m_jit_ptr;
    }

    /**
     * \brief Create a tree for a global from the address of that global.
     */
    TreePtr<Global> CompileContext::tree_from_address(const SourceLocation& location, const TreePtr<Type>& type, void *ptr) {
      void *base;
      String name;
      try {
        name = Platform::address_to_symbol(ptr, &base);
      } catch (Platform::PlatformError& e) {
        error_throw(location, boost::format("Internal error: failed to get symbol name from addres: %s") % e.what());
      }
      if (base != ptr)
        error_throw(location, "Internal error: address used to retrieve symbol did not match symbol base");

      TreePtr<ExternalGlobal> result(new ExternalGlobal(type, location));
      result->symbol = name;
      result->m_jit_ptr = base;
      return result;
    }

    RunningCompletionState::RunningCompletionState(CompileContext& compile_context, CompletionState *state, const SourceLocation& location)
      : m_compile_context(&compile_context), m_state(state), m_location(location) {
      m_parent = m_compile_context->m_running_completion_stack;
      m_compile_context->m_running_completion_stack = this;
    }

    RunningCompletionState::~RunningCompletionState() {
      m_compile_context->m_running_completion_stack = m_parent;
    }

    void RunningCompletionState::throw_circular_dependency() {
      CompileError error(*m_compile_context, m_location);
      error.info("Circular dependency found");
      boost::format fmt("via: '%s'");
      for (RunningCompletionState *ancestor = m_parent; ancestor && (ancestor->m_state != m_state); ancestor = ancestor->m_parent)
	error.info(ancestor->m_location, fmt % ancestor->m_location.logical->error_name(m_location.logical));
      error.end();
      throw CompileException();
    }
    
    class EvaluateContextDictionary : public EvaluateContext {
      typedef PSI_STD::map<String, TreePtr<Term> > NameMapType;
      NameMapType m_entries;
      TreePtr<EvaluateContext> m_next;

    public:
      static const EvaluateContextVtable vtable;

      EvaluateContextDictionary(CompileContext& compile_context,
                                const SourceLocation& location,
                                const std::map<String, TreePtr<Term> >& entries,
                                const TreePtr<EvaluateContext>& next)
      : EvaluateContext(compile_context, location), m_entries(entries), m_next(next) {
        PSI_COMPILER_TREE_INIT();
      }

      template<typename Visitor>
      static void visit_impl(EvaluateContextDictionary& self, Visitor& visitor) {
        visitor
        ("entries", self.m_entries)
        ("next", self.m_next);
      }

      static LookupResult<TreePtr<Term> > lookup_impl(EvaluateContextDictionary& self, const String& name) {
        NameMapType::const_iterator it = self.m_entries.find(name);
        if (it != self.m_entries.end()) {
          return lookup_result_match(it->second);
        } else if (self.m_next) {
          return self.m_next->lookup(name);
        } else {
          return lookup_result_none;
        }
      }
    };

    const EvaluateContextVtable EvaluateContextDictionary::vtable =
    PSI_COMPILER_EVALUATE_CONTEXT(EvaluateContextDictionary, "psi.compiler.EvaluateContextDictionary", EvaluateContext);

    /**
     * \brief Create an evaluation context based on a dictionary.
     */
    TreePtr<EvaluateContext> evaluate_context_dictionary(CompileContext& compile_context, const SourceLocation& location, const std::map<String, TreePtr<Term> >& entries, const TreePtr<EvaluateContext>& next) {
      return TreePtr<EvaluateContext>(new EvaluateContextDictionary(compile_context, location, entries, next));
    }

    /**
     * \brief Create an evaluation context based on a dictionary.
     */
    TreePtr<EvaluateContext> evaluate_context_dictionary(CompileContext& compile_context, const SourceLocation& location, const std::map<String, TreePtr<Term> >& entries) {
      return evaluate_context_dictionary(compile_context, location, entries, TreePtr<EvaluateContext>());
    }

    /**
     * \brief OutputStreamable class which can be used to turn an expression into a simple string.
     */
    class ExpressionString {
      const char *m_begin;
      std::size_t m_count;

    public:
      ExpressionString(SharedPtr<Parser::Expression>& expr) {
        m_begin = expr->location.begin;
        m_count = expr->location.end - expr->location.begin;
      }
      
      friend std::ostream& operator << (std::ostream& os, const ExpressionString& self) {
        os.write(self.m_begin, self.m_count);
        return os;
      }
    };

    TreePtr<Macro> expression_macro(const TreePtr<Term>& expr, const SourceLocation& location) {
      CompileContext& compile_context = expr->compile_context();

      if (!expr->type())
        compile_context.error_throw(location, "Expression does not have a type", CompileError::error_internal);

      return interface_lookup_as<Macro>(compile_context.macro_interface(), expr->type(), location);
    }
    
    /**
     * \brief Compile an expression.
     *
     * \param expression Expression, usually as produced by the parser.
     * \param evaluate_context Context in which to lookup names.
     * \param source Logical (i.e. namespace etc.) location of the expression, for symbol naming and debugging.
     */
    TreePtr<Term> compile_expression(const SharedPtr<Parser::Expression>& expression,
                                     const TreePtr<EvaluateContext>& evaluate_context,
                                     const LogicalSourceLocationPtr& source) {

      CompileContext& compile_context = evaluate_context->compile_context();
      SourceLocation location(expression->location.location, source);

      switch (expression->expression_type) {
      case Parser::expression_macro: {
        const Parser::MacroExpression& macro_expression = checked_cast<Parser::MacroExpression&>(*expression);

        TreePtr<Term> first = compile_expression(macro_expression.elements.front(), evaluate_context, source->new_anonymous_child());
        PSI_STD::vector<SharedPtr<Parser::Expression> > rest(boost::next(macro_expression.elements.begin()), macro_expression.elements.end());

        return expression_macro(first, location)->evaluate(first, list_from_stl(rest), evaluate_context, location);
      }

      case Parser::expression_token: {
        const Parser::TokenExpression& token_expression = checked_cast<Parser::TokenExpression&>(*expression);

        switch (token_expression.token_type) {
        case Parser::TokenExpression::bracket:
        case Parser::TokenExpression::brace:
        case Parser::TokenExpression::square_bracket: {
          const char *bracket_operation, *bracket_str;
          switch (token_expression.token_type) {
          case Parser::TokenExpression::bracket: bracket_operation = "__bracket__"; bracket_str = "(...)"; break;
          case Parser::TokenExpression::brace: bracket_operation = "__brace__"; bracket_str = "{...}"; break;
          case Parser::TokenExpression::square_bracket: bracket_operation = "__squareBracket__"; bracket_str = "[...]"; break;
          default: PSI_FAIL("unreachable");
          }

          LookupResult<TreePtr<Term> > first = evaluate_context->lookup(bracket_operation);
          switch (first.type()) {
          case lookup_result_type_none:
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: '%s' operator missing") % bracket_str % bracket_operation);
          case lookup_result_type_conflict:
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: '%s' operator lookup ambiguous") % bracket_str % bracket_operation);
          default: break;
          }

          if (!first.value())
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: successful lookup of '%s' returned NULL value") % bracket_str % bracket_operation, CompileError::error_internal);

          boost::array<SharedPtr<Parser::Expression>, 1> expression_list;
          expression_list[0] = expression;
          return expression_macro(first.value(), location)->evaluate(first.value(), list_from_stl(expression_list), evaluate_context, location);
        }

        case Parser::TokenExpression::identifier: {
          String name(token_expression.text.begin, token_expression.text.end);
          LookupResult<TreePtr<Term> > result = evaluate_context->lookup(name);

          switch (result.type()) {
          case lookup_result_type_none: compile_context.error_throw(location, boost::format("Name not found: %s") % name);
          case lookup_result_type_conflict: compile_context.error_throw(location, boost::format("Conflict on lookup of: %s") % name);
          default: break;
          }

          if (!result.value())
            compile_context.error_throw(location, boost::format("Successful lookup of '%s' returned NULL value") % name, CompileError::error_internal);

          return result.value();
        }

        default:
          PSI_FAIL("Unknown token type");
        }
      }

      case Parser::expression_dot: {
        const Parser::DotExpression& dot_expression = checked_cast<Parser::DotExpression&>(*expression);
        TreePtr<Term> left = compile_expression(dot_expression.left, evaluate_context, source);
        return expression_macro(left, location)->dot(left, dot_expression.right, evaluate_context, location);
      }

      default:
        PSI_FAIL("unknown expression type");
      }
    }

    class StatementListEntry : public Tree {
      TreePtr<Statement> m_value;
      SharedPtr<Parser::Expression> m_expression;
      SourceLocation m_location;
      TreePtr<EvaluateContext> m_evaluate_context;
      CompletionState m_type_completion_state;

    public:
      static const TreeVtable vtable;
      
      StatementListEntry(CompileContext& compile_context, const SourceLocation& location,
        const SharedPtr<Parser::Expression>& expression,
        const TreePtr<EvaluateContext>& evaluate_context)
      : Tree(compile_context, location),
      m_expression(expression),
      m_location(location),
      m_evaluate_context(evaluate_context) {
        PSI_COMPILER_TREE_INIT();
      }

      template<typename Visitor>
      static void visit_impl(StatementListEntry& self, Visitor& visitor) {
        visitor("evaluate_context", self.m_evaluate_context);
        visitor("value", self.m_value);
      }

      static void complete_callback_impl(StatementListEntry& self) {
        self.complete_statement();
        self.m_value->complete(true);
      }

      const TreePtr<Statement>& value() const {
        return m_value;
      }

      void complete_statement_callback() {
	TreePtr<Term> expr = compile_expression(m_expression, m_evaluate_context, m_location.logical);
	m_value.reset(new Statement(expr, m_location));
      }

      void complete_statement_cleanup() {
        m_expression.reset();
        m_location.logical.reset();
        m_evaluate_context.reset();
      }

      void complete_statement() {
	m_type_completion_state.complete(compile_context(), m_location, false,
					 boost::bind(&StatementListEntry::complete_statement_callback, this),
					 boost::bind(&StatementListEntry::complete_statement_cleanup, this));
      }
    };

    const TreeVtable StatementListEntry::vtable = PSI_COMPILER_TREE(StatementListEntry, "psi.compiler.StatementListEntry", Tree);

    class StatementListCompiler : public Dependency {
      std::vector<TreePtr<StatementListEntry> > m_statements;

    public:
      static const DependencyVtable vtable;
      
      StatementListCompiler(const std::vector<TreePtr<StatementListEntry> >& statements)
      : m_statements(statements) {
        PSI_COMPILER_DEPENDENCY_INIT();
      }

      template<typename Visitor>
      static void visit_impl(StatementListCompiler& self, Visitor& visitor) {
        for (std::vector<TreePtr<StatementListEntry> >::iterator ii = self.m_statements.begin(), ie = self.m_statements.end(); ii != ie; ++ii)
          visitor("", *ii);
      }

      static void run_impl(StatementListCompiler& self, const TreePtr<Block>& block) {
        for (std::vector<TreePtr<StatementListEntry> >::iterator ii = self.m_statements.begin(), ie = self.m_statements.end(); ii != ie; ++ii) {
          (*ii)->complete(true);
          block->statements.push_back((*ii)->value());
        }
      }
    };

    const DependencyVtable StatementListCompiler::vtable = PSI_COMPILER_DEPENDENCY(StatementListCompiler, "psi.compiler.StatementListCompiler", Block);
    
    class StatementListContext : public EvaluateContext {
    public:
      typedef std::map<String, TreePtr<StatementListEntry> > NameMapType;
      NameMapType entries;

    private:
      TreePtr<EvaluateContext> m_next;

    public:
      static const EvaluateContextVtable vtable;
      
      StatementListContext(CompileContext& compile_context,
                           const SourceLocation& location,
                           const TreePtr<EvaluateContext>& next)
      : EvaluateContext(compile_context, location), m_next(next) {
        PSI_COMPILER_TREE_INIT();
      }

      template<typename Visitor>
      static void visit_impl(StatementListContext& self, Visitor& visitor) {
        EvaluateContext::visit_impl(self, visitor);
        visitor("next", self.m_next);
	visitor("entries", self.entries);
      }

      static LookupResult<TreePtr<Term> > lookup_impl(const StatementListContext& self, const String& name) {
        StatementListContext::NameMapType::const_iterator it = self.entries.find(name);
        if (it != self.entries.end()) {
          it->second->complete_statement();
          return lookup_result_match(it->second->value());
        } else if (self.m_next) {
          return self.m_next->lookup(name);
        } else {
          return lookup_result_none;
        }
      }
    };

    const EvaluateContextVtable StatementListContext::vtable = PSI_COMPILER_EVALUATE_CONTEXT(StatementListContext, "psi.compiler.StatementListContext", EvaluateContext);

    TreePtr<Block> compile_statement_list(const List<SharedPtr<Parser::NamedExpression> >& statements,
                                          const TreePtr<EvaluateContext>& evaluate_context,
                                          const SourceLocation& location) {
      CompileContext& compile_context = evaluate_context->compile_context();
      TreePtr<StatementListContext> context_tree(new StatementListContext(compile_context, location, evaluate_context));
      TreePtr<StatementListEntry> last_statement;
      PSI_STD::vector<TreePtr<StatementListEntry> > entries;

      for (LocalIterator<SharedPtr<Parser::NamedExpression> > ii(statements); ii.next();) {
        const Parser::NamedExpression& named_expr = *ii.current();
        if (named_expr.expression.get()) {
          String expr_name;
	  LogicalSourceLocationPtr logical_location;
	  if (named_expr.name) {
	    expr_name = String(named_expr.name->begin, named_expr.name->end);
	    logical_location = location.logical->named_child(expr_name);
	  } else {
	    logical_location = location.logical->new_anonymous_child();
	  }
          SourceLocation statement_location(named_expr.location.location, logical_location);
          last_statement.reset(new StatementListEntry(compile_context, statement_location, named_expr.expression, context_tree));
          entries.push_back(last_statement);
          
          if (named_expr.name)
            context_tree->entries[expr_name] = last_statement;
        } else {
          last_statement.reset();
        }
      }

      TreePtr<Term> block_value;
      if (last_statement) {
        last_statement->complete_statement();
        block_value = last_statement->value();
      } else {
        LookupResult<TreePtr<Term> > none = evaluate_context->lookup("__none__");
        switch (none.type()) {
        case lookup_result_type_none:
          compile_context.error_throw(location, "'__none__' missing");
        case lookup_result_type_conflict:
          compile_context.error_throw(location, "'__none__' has multiple definitions");
        default: break;
        }

        if (!none.value())
          compile_context.error_throw(location, "'__none__' returned a NULL tree", CompileError::error_internal);

        block_value = none.value();
      }

      TreePtr<Block> block(new Block(block_value->type(), location));
      block->result = block_value;
      block->dependency.reset(new StatementListCompiler(entries));

      return block;
    }
  }
}
