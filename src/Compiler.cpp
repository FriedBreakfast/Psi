#include "Compiler.hpp"
#include "Tree.hpp"
#include "Platform.hpp"
#include "Parser.hpp"

#include "Class.hpp"
#include "Function.hpp"

#ifdef PSI_DEBUG
#include <iostream>
#endif

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/next_prior.hpp>

namespace Psi {
  namespace Compiler {
    void TreePtrBase::update_chain(const TreeBase *ptr) const {
      const TreePtrBase *hook = this;
      ObjectPtr<const TreeCallback> ptr_cb;
      while (hook->m_ptr.get() != ptr) {
        PSI_ASSERT(derived_vptr(hook->m_ptr.get())->is_callback);
        ObjectPtr<const TreeCallback> next_ptr_cb(static_cast<const TreeCallback*>(hook->m_ptr.get()), true);
        const TreePtrBase *next_hook = &next_ptr_cb->m_value;
        hook->m_ptr.reset(ptr);
        hook = next_hook;
        ptr_cb.swap(next_ptr_cb);
      }
    }
    
    /**
     * Evaluate a lazily evaluated Tree (recursively if necessary) and return the final result.
     */
    const Tree* TreePtrBase::get_helper() const {
      PSI_ASSERT(m_ptr);

      /*
       * Evaluate chain of hooks until either a NULL is found or a non-callback
       * value is reached.
       */
      const TreePtrBase *hook = this;
      while (true) {
        if (!hook->m_ptr)
          break;
        
        const TreeBaseVtable *vtable = derived_vptr(hook->m_ptr.get());
        if (!vtable->is_callback)
          break;

        TreeCallback *ptr_cb = static_cast<TreeCallback*>(const_cast<TreeBase*>(hook->m_ptr.get()));
        hook = &ptr_cb->m_value;

        switch (ptr_cb->m_state) {
        case TreeCallback::state_ready: {
          const TreeCallbackVtable *vtable_cb = reinterpret_cast<const TreeCallbackVtable*>(vtable);
          RunningTreeCallback running(ptr_cb);
          ptr_cb->m_state = TreeCallback::state_running;
          const TreeBase *eval_ptr;
          try {
            eval_ptr = vtable_cb->evaluate(ptr_cb);
          } catch (...) {
            ptr_cb->m_state = TreeCallback::state_failed;
            update_chain(ptr_cb);
            throw;
          }
          PSI_ASSERT(!hook->m_ptr);
          hook->m_ptr.reset(eval_ptr, false);
          ptr_cb->m_state = TreeCallback::state_finished;
          break;
        }

        case TreeCallback::state_running:
          update_chain(ptr_cb);
          RunningTreeCallback::throw_circular_dependency(ptr_cb);
          PSI_UNREACHABLE();

        case TreeCallback::state_finished:
          break;

        case TreeCallback::state_failed:
          update_chain(ptr_cb);
          throw CompileException();
        }
      }

      update_chain(hook->m_ptr.get());

      PSI_ASSERT(!m_ptr || !derived_vptr(m_ptr.get())->is_callback);
      return static_cast<const Tree*>(m_ptr.get());
    }
    
#ifdef PSI_DEBUG
    void TreePtrBase::debug_print() const {
      if (!m_ptr) {
        std::cerr << "(null)" << std::endl;
        return;
      }
      
      const SourceLocation& loc = location();
      std::cerr << loc.physical.file->url << ':' << loc.physical.first_line << ": "
      << loc.logical->error_name(LogicalSourceLocationPtr())
      << " : " << si_vptr(m_ptr.get())->classname << std::endl;
    }
#endif
    
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
      unsigned print_depth = depth();
      if (relative_to) {
	      // Find the common ancestor of this and relative_to.
	      unsigned this_depth = print_depth;
	      unsigned relative_to_depth = relative_to->depth();
	      unsigned min_depth = std::min(this_depth, relative_to_depth);
	      print_depth = this_depth - min_depth;
	      LogicalSourceLocation *this_ancestor = ancestor(print_depth).get();
	      LogicalSourceLocation *relative_to_ancestor = relative_to->ancestor(relative_to_depth - min_depth).get();

	      while (this_ancestor != relative_to_ancestor) {
	        ++print_depth;
	        this_ancestor = this_ancestor->parent().get();
	        relative_to_ancestor = relative_to_ancestor->parent().get();
	      }
      }

      print_depth = std::max(print_depth, 1u);

      std::vector<LogicalSourceLocation*> nodes;
      bool last_anonymous = false;
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

      if (ignore_anonymous_tail) {
	      if (nodes.front()->anonymous())
	        nodes.erase(nodes.begin());
	      if (nodes.empty())
	        return "(anonymous)";
      }

      if (!nodes.back()->parent()) {
	      nodes.pop_back();
	      if (nodes.empty())
	        return "(root namespace)";
      }

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

    bool si_derived(const SIVtable *base, const SIVtable *derived) {
      for (const SIVtable *super = derived; super; super = super->super) {
        if (super == base)
          return true;
      }
      
      return false;
    }

    bool si_is_a(const SIBase *object, const SIVtable *cls) {
      return si_derived(cls, object->m_vptr);
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
	      ss << '\'' << current.location().logical->error_name(location.logical) << '\'';
      }

      return ss.str();
    }

    /**
     * \brief Checks the result of an interface lookup is non-NULL and of the correct type.
     *
     * \param parameters Parameters the interfacce was searched on.
     */
    void interface_cast_check(const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters, const TreePtr<>& result, const SourceLocation& location, const TreeVtable* cast_type) {
      CompileContext& compile_context = interface.compile_context();

      if (!result)
        compile_context.error_throw(location,
                                    boost::format("'%s' interface not available for %s")
                                    % interface.location().logical->error_name(location.logical)
                                    % interface_parameters_message(parameters, location));

      if (!si_is_a(result.get(), &cast_type->base.base.base))
        compile_context.error_throw(location,
                                    boost::format("'%s' interface value has the wrong type (%s) for %s")
                                    % interface.location().logical->error_name(location.logical)
                                    % si_vptr(result.get())->classname
                                    % interface_parameters_message(parameters, location),
                                    CompileError::error_internal);
    }

    /**
     * \brief Locate an interface implementation for a given set of parameters.
     *
     * \param interface Interface to look up implementation for.
     * \param parameters Parameters to the interface.
     */    
    TreePtr<> interface_lookup(const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters, const SourceLocation&) {
      PSI_ASSERT(interface->compile_time_type);

      // Walk the various parameters and look for matching interface implementations
      for (LocalIterator<TreePtr<Term> > p(parameters); p.next();) {
        TreePtr<> result = p.current()->interface_search(interface, parameters);
        if (result) {
          // Check result has the correct type
          if (!si_is_a(result.get(), interface->compile_time_type)) {
            CompileError error(interface.compile_context(), result.location());
            error.info(boost::format("Implementation of '%s' has the wrong tree type") % interface.location().logical->error_name(result.location().logical));
            error.info(boost::format("Tree type should be '%s' but is '%s'") % interface->compile_time_type->classname % si_vptr(result.get())->classname);
            error.info(interface.location(), "Interface defined here");
            error.end();
            throw CompileException();
          }
          
          if (interface->run_time_type) {
            PSI_ASSERT(si_derived(reinterpret_cast<const SIVtable*>(&Term::vtable), interface->compile_time_type));
            TreePtr<Term> term = treeptr_cast<Term>(result);
            if (!interface->run_time_type->match(term)) {
              CompileError error(interface.compile_context(), result.location());
              error.info(boost::format("Implementation of '%s' has the wrong type") % interface.location().logical->error_name(result.location().logical));
              error.info(boost::format("Type should be '%s' but is '%s'") % interface->run_time_type.location().logical->error_name(result.location().logical) % term->type.location().logical->error_name(result.location().logical));
              error.info(interface.location(), "Interface defined here");
              error.end();
              throw CompileException();
            }
          }
          
          return result;
        }
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
    
    BuiltinTypes::BuiltinTypes() {
    }

    void BuiltinTypes::initialize(CompileContext& compile_context) {
      SourceLocation psi_location = compile_context.root_location().named_child("psi");
      SourceLocation psi_compiler_location = psi_location.named_child("compiler");

      metatype.reset(new Metatype(compile_context, psi_location.named_child("Type")));
      empty_type.reset(new EmptyType(compile_context, psi_location.named_child("Empty")));
      
      macro_interface.reset(new Interface(compile_context, 1, &Macro::vtable, TreePtr<Term>(), psi_compiler_location.named_child("Macro")));
      argument_passing_info_interface.reset(new Interface(compile_context, 1, &ArgumentPassingInfoCallback::vtable, TreePtr<Term>(), psi_compiler_location.named_child("ArgumentPasser")));
      class_member_info_interface.reset(new Interface(compile_context, 1, &ClassMemberInfoCallback::vtable, TreePtr<Term>(), psi_compiler_location.named_child("ClassMemberInfo")));
    }

    CompileContext::CompileContext(std::ostream *error_stream)
    : m_error_stream(error_stream), m_error_occurred(false), m_running_completion_stack(NULL),
    m_root_location(PhysicalSourceLocation(), LogicalSourceLocation::new_root_location()) {
      PhysicalSourceLocation core_physical_location;
      m_root_location.physical.file.reset(new SourceFile());
      m_root_location.physical.first_line = m_root_location.physical.first_column = 0;
      m_root_location.physical.last_line = m_root_location.physical.last_column = 0;
      m_builtins.initialize(*this);
    }
    
#ifdef PSI_DEBUG
#define PSI_COMPILE_CONTEXT_REFERENCE_GUARD 20
#else
#define PSI_COMPILE_CONTEXT_REFERENCE_GUARD 1
#endif

    struct CompileContext::ObjectDisposer {
      void operator () (Object *t) {
#ifdef PSI_DEBUG
        if (t->m_reference_count == PSI_COMPILE_CONTEXT_REFERENCE_GUARD) {
          t->m_reference_count = 0;
          derived_vptr(t)->destroy(t);
        } else if (t->m_reference_count < PSI_COMPILE_CONTEXT_REFERENCE_GUARD) {
          PSI_WARNING_FAIL("Reference counting error: guard references have been used up");
          PSI_WARNING_FAIL(t->m_vptr->classname);
        } else {
          PSI_WARNING_FAIL("Reference counting error: dangling references to object");
          PSI_WARNING_FAIL(t->m_vptr->classname);
        }
#else
        derived_vptr(t)->destroy(t);
#endif
      }
    };

    CompileContext::~CompileContext() {
      m_builtins = BuiltinTypes();

      // Add extra reference to each Tree
      BOOST_FOREACH(Object& t, m_gc_list)
        t.m_reference_count += PSI_COMPILE_CONTEXT_REFERENCE_GUARD;

      // Clear cross references in each Tree
      BOOST_FOREACH(Object& t, m_gc_list)
        derived_vptr(&t)->gc_clear(&t);
        
#ifdef PSI_DEBUG
      bool failed = false;
      for (GCListType::iterator ii = m_gc_list.begin(), ie = m_gc_list.end(); ii != ie; ++ii) {
        if (ii->m_reference_count != PSI_COMPILE_CONTEXT_REFERENCE_GUARD)
          failed = true;
      }
      
      if (failed) {
        PSI_WARNING_FAIL("Incorrect reference count during context destruction: either dangling reference or multiple release");
        BOOST_FOREACH(Object& t, m_gc_list)
          PSI_WARNING_FAIL(t.m_vptr->classname);
      }
#endif

      m_gc_list.clear_and_dispose(ObjectDisposer());
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
      PSI_NOT_IMPLEMENTED();
    }

    RunningTreeCallback::RunningTreeCallback(TreeCallback *callback)
      : m_callback(callback) {
      m_parent = callback->compile_context().m_running_completion_stack;
      callback->compile_context().m_running_completion_stack = this;
    }

    RunningTreeCallback::~RunningTreeCallback() {
      m_callback->compile_context().m_running_completion_stack = m_parent;
    }

    /**
     * \brief Throw a circular dependency error caused by something depending on
     * its own value for evaluation.
     * 
     * \param callback Callback being recursively evaluated.
     */
    void RunningTreeCallback::throw_circular_dependency(TreeCallback *callback) {
      PSI_ASSERT(callback->m_state == TreeCallback::state_running);
      CompileError error(callback->compile_context(), callback->m_location);
      error.info("Circular dependency found");
      boost::format fmt("via: '%s'");
      for (RunningTreeCallback *ancestor = callback->compile_context().m_running_completion_stack;
           ancestor && (ancestor->m_callback != callback); ancestor = ancestor->m_parent)
        error.info(ancestor->m_callback->m_location, fmt % ancestor->m_callback->m_location.logical->error_name(callback->m_location.logical));
      error.end();
      throw CompileException();
    }
    
    class EvaluateContextDictionary : public EvaluateContext {
    public:
      static const EvaluateContextVtable vtable;

      typedef PSI_STD::map<String, TreePtr<Term> > NameMapType;

      EvaluateContextDictionary(CompileContext& compile_context,
                                const SourceLocation& location,
                                const NameMapType& entries_,
                                const TreePtr<EvaluateContext>& next_)
      : EvaluateContext(&vtable, compile_context, location), entries(entries_), next(next_) {
      }

      NameMapType entries;
      TreePtr<EvaluateContext> next;

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<EvaluateContext>(v);
        v("entries", &EvaluateContextDictionary::entries)
        ("next", &EvaluateContextDictionary::next);
      }

      static LookupResult<TreePtr<Term> > lookup_impl(const EvaluateContextDictionary& self, const String& name, const SourceLocation& location, const TreePtr<EvaluateContext>& evaluate_context) {
        NameMapType::const_iterator it = self.entries.find(name);
        if (it != self.entries.end()) {
          return lookup_result_match(it->second);
        } else if (self.next) {
          return self.next->lookup(name, location, evaluate_context);
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
      return interface_lookup_as<Macro>(expr.compile_context().builtins().macro_interface, expr, location);
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

      CompileContext& compile_context = evaluate_context.compile_context();
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

          LookupResult<TreePtr<Term> > first = evaluate_context->lookup(bracket_operation, location);
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
          LookupResult<TreePtr<Term> > result = evaluate_context->lookup(name, location);

          switch (result.type()) {
          case lookup_result_type_none: compile_context.error_throw(location, boost::format("Name not found: %s") % name);
          case lookup_result_type_conflict: compile_context.error_throw(location, boost::format("Conflict on lookup of: %s") % name);
          default: break;
          }

          if (!result.value())
            compile_context.error_throw(location, boost::format("Successful lookup of '%s' returned NULL value") % name, CompileError::error_internal);

          return result.value();
        }
        
        case Parser::TokenExpression::number: {
          LookupResult<TreePtr<Term> > first = evaluate_context->lookup("__number__", location);
          switch (first.type()) {
          case lookup_result_type_none:
            compile_context.error_throw(location, "Cannot evaluate number: '__number__' operator missing");
          case lookup_result_type_conflict:
            compile_context.error_throw(location, "Cannot evaluate number: '__number__' operator lookup ambiguous");
          default: break;
          }
          
          if (!first.value())
            compile_context.error_throw(location, "Cannot evaluate number: successful lookup of '__number__' returned NULL value", CompileError::error_internal);

          boost::array<SharedPtr<Parser::Expression>, 1> expression_list;
          expression_list[0] = expression;
          return expression_macro(first.value(), location)->evaluate(first.value(), list_from_stl(expression_list), evaluate_context, location);
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

    class StatementListEntry {
      SharedPtr<Parser::Expression> m_expression;
      TreePtr<EvaluateContext> m_evaluate_context;

    public:
      StatementListEntry(const SharedPtr<Parser::Expression>& expression,
                         const TreePtr<EvaluateContext>& evaluate_context)
        : m_expression(expression),
        m_evaluate_context(evaluate_context) {
      }
      
      template<typename Visitor>
      static void visit(Visitor& v) {
        v("expression", &StatementListEntry::m_expression)
        ("evaluate_context", &StatementListEntry::m_evaluate_context);
      }

      TreePtr<Statement> evaluate(const TreePtr<Statement>& self) {
        return TreePtr<Statement>(new Statement(compile_expression(m_expression, m_evaluate_context, self.location().logical), self.location()));
      }
    };
    
    class StatementListTree : public Tree {
    public:
      static const TreeVtable vtable;

      typedef std::map<String, TreePtr<Term> > NameMapType;

      StatementListTree(const PSI_STD::vector<TreePtr<Statement> >& entries_, TreePtr<Term>& block_value_, const NameMapType& named_entries_, CompileContext& compile_context, const SourceLocation& location)
      : Tree(&vtable, compile_context, location),
      entries(entries_),
      block_value(block_value_),
      named_entries(named_entries_) {
      }

      PSI_STD::vector<TreePtr<Statement> > entries;
      TreePtr<Term> block_value;
      NameMapType named_entries;

      template<typename Visitor>
      static void visit(Visitor& v) {
        v("entries", &StatementListTree::entries)
        ("block_value", &StatementListTree::block_value)
        ("named_entries", &StatementListTree::named_entries);
      }
    };
    
    const TreeVtable StatementListTree::vtable = PSI_COMPILER_TREE(StatementListTree, "psi.compiler.StatementListTree", Tree);

    class StatementListContext : public EvaluateContext {
    public:
      static const EvaluateContextVtable vtable;

      typedef StatementListTree::NameMapType NameMapType;
      
      StatementListContext(const TreePtr<StatementListTree>& statement_list_,
                           const TreePtr<EvaluateContext>& next_)
      : EvaluateContext(&vtable, statement_list_.compile_context(), statement_list_.location()),
      statement_list(statement_list_),
      next(next_) {
      }

      TreePtr<StatementListTree> statement_list;
      TreePtr<EvaluateContext> next;

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<EvaluateContext>(v);
        v("statement_list", &StatementListContext::statement_list)
        ("next", &StatementListContext::next);
      }

      static LookupResult<TreePtr<Term> > lookup_impl(const StatementListContext& self, const String& name, const SourceLocation& location, const TreePtr<EvaluateContext>& evaluate_context) {
        StatementListContext::NameMapType::const_iterator it = self.statement_list->named_entries.find(name);
        if (it != self.statement_list->named_entries.end()) {
          return lookup_result_match(it->second);
        } else if (self.next) {
          return self.next->lookup(name, location, evaluate_context);
        } else {
          return lookup_result_none;
        }
      }
    };

    const EvaluateContextVtable StatementListContext::vtable = PSI_COMPILER_EVALUATE_CONTEXT(StatementListContext, "psi.compiler.StatementListContext", EvaluateContext);

    class StatementListCompiler {
      PSI_STD::vector<SharedPtr<Parser::NamedExpression> > m_statements;
      TreePtr<EvaluateContext> m_evaluate_context;
      bool m_with_value;

    public:
      StatementListCompiler(const PSI_STD::vector<SharedPtr<Parser::NamedExpression> >& statements,
                            const TreePtr<EvaluateContext>& evaluate_context,
                            bool with_value)
      : m_statements(statements), m_evaluate_context(evaluate_context), m_with_value(with_value) {
      }
      
      template<typename Visitor>
      static void visit(Visitor& v) {
        v("statements", &StatementListCompiler::m_statements)
        ("evaluate_context", &StatementListCompiler::m_evaluate_context)
        ("with_value", &StatementListCompiler::m_with_value);
      }

      TreePtr<StatementListTree> evaluate(const TreePtr<StatementListTree>& self) {
        CompileContext& compile_context = self.compile_context();
        TreePtr<StatementListContext> context_tree(new StatementListContext(self, m_evaluate_context));
        TreePtr<Statement> last_statement;
        bool last_statement_set = false;
        PSI_STD::vector<TreePtr<Statement> > entries;
        StatementListTree::NameMapType named_entries;

        for (PSI_STD::vector<SharedPtr<Parser::NamedExpression> >::iterator ii = m_statements.begin(), ie = m_statements.end(); ii != ie; ++ii) {
          const Parser::NamedExpression& named_expr = **ii;
          if ((last_statement_set = named_expr.expression.get())) {
            String expr_name;
            LogicalSourceLocationPtr logical_location;
            if (named_expr.name) {
              expr_name = String(named_expr.name->begin, named_expr.name->end);
              logical_location = self.location().logical->named_child(expr_name);
            } else {
              logical_location = self.location().logical->new_anonymous_child();
            }
            SourceLocation statement_location(named_expr.location.location, logical_location);
            last_statement = tree_callback<Statement>(compile_context, statement_location, StatementListEntry(named_expr.expression, context_tree));
            entries.push_back(last_statement);

            if (named_expr.name)
              named_entries[expr_name] = last_statement;
          }
        }

        TreePtr<Term> block_value;
        if (m_with_value) {
          if (last_statement_set) {
            block_value = last_statement;
          } else {
            LookupResult<TreePtr<Term> > none = m_evaluate_context->lookup("__none__", self.location());
            switch (none.type()) {
            case lookup_result_type_none:
              compile_context.error_throw(self.location(), "'__none__' missing");
            case lookup_result_type_conflict:
              compile_context.error_throw(self.location(), "'__none__' has multiple definitions");
            default: break;
            }

            if (!none.value())
              compile_context.error_throw(self.location(), "'__none__' returned a NULL tree", CompileError::error_internal);

            block_value = none.value();
          }
        }
        
        return TreePtr<StatementListTree>(new StatementListTree(entries, block_value, named_entries, self.compile_context(), self.location()));
      }
    };

    TreePtr<Block> compile_statement_list(const PSI_STD::vector<SharedPtr<Parser::NamedExpression> >& statements,
                                          const TreePtr<EvaluateContext>& evaluate_context,
                                          const SourceLocation& location) {
      TreePtr<StatementListTree> t = tree_callback<StatementListTree>(evaluate_context.compile_context(), location, StatementListCompiler(statements, evaluate_context, true));
      return TreePtr<Block>(new Block(t->entries, t->block_value, location));
    }
    
    TreePtr<Namespace> compile_namespace(const PSI_STD::vector<SharedPtr<Parser::NamedExpression> >& statements,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
      TreePtr<StatementListTree> t = tree_callback<StatementListTree>(evaluate_context.compile_context(), location, StatementListCompiler(statements, evaluate_context, false));
      return TreePtr<Namespace>(new Namespace(t->entries, evaluate_context.compile_context(), location));
    }
  }
}
