#ifndef HPP_PSI_TREE_PATTERN
#define HPP_PSI_TREE_PATTERN

#include <boost/typeof/typeof.hpp>

#include "Compiler.hpp"
#include "Utility.hpp"
#include "Tree.hpp"

namespace Psi {
  namespace Compiler {
    template<typename Label> class GetLabel {};

    template<typename Label, typename MatchLabel, typename Value, typename Previous>
    struct MatchValueGetType {
      typedef typename Previous::template GetType<Label>::Type Type;
    };

    template<typename Label, typename Value, typename Previous>
    struct MatchValueGetType<Label, Label, Value, Previous> {
      typedef Value Type;
    };

    /**
     * \brief Type which goes at the root of the chain of match objects.
     *
     * This terminates the chain of \c Previous match types and stores
     * whether the match was successful or not.
     */
    class MatchBase {
      bool m_success;

    public:
      MatchBase() : m_success(false) {}
      MatchBase(bool success) : m_success(success) {}

      operator bool () const {
        return m_success;
      }
    };

    /**
     * \brief Type usually used for returning values generated during pattern matching.
     */
    template<typename Label, typename Value, typename Previous>
    class MatchValue {
      Previous m_previous;
      Value m_value;

    public:
      MatchValue() {
      }

      MatchValue(const Previous& previous, const Value& value)
      : m_previous(previous), m_value(value) {
      }

      operator bool () const {
        return bool(m_previous);
      }

      template<typename T>
      struct GetType {
        typedef typename MatchValueGetType<T, Label, Value, Previous>::Type Type;
      };

      template<typename T>
      const typename Previous::template GetType<T>::Type& get(const GetLabel<T>& label) {
        return m_previous.get(label);
      }

      const Value& get(const GetLabel<Label>&) const {
        return m_value;
      }
    };

    /**
     * \brief Free function for interrogating match objects.
     */
    template<typename T, typename U>
    typename U::template GetType<T>::Type get(const U& match) {
      return match.get(GetLabel<T>());
    }

    /**
     * \brief A simple matcher which matches by pointer equality to an existing Tree.
     */
    class TreeMatch {
      GCPtr<Tree> m_tree;

    public:
      explicit TreeMatch(const GCPtr<Tree>& tree)
      : m_tree(tree) {
      }

      template<typename Previous> struct MatchType {
        typedef Previous Type;
      };

      template<typename Previous>
      Previous match(Tree* tree, const Previous& previous) const {
        return (tree == m_tree) ? previous : Previous();
      }

      const GCPtr<Tree>& build(CompileContext&) const {
        return m_tree;
      }
    };

    /**
     * \brief Get the matcher type for a parameter passed to a matcher factory.
     */
    template<typename T>
    struct MatcherType {
      typedef T Type;
    };

    template<>
    struct MatcherType<Tree*> {
      typedef TreeMatch Type;
    };

    /**
     * \brief Convert the argument passed to a matcher.
     *
     * This is the identity function. There is an overload for Tree* arguments which
     * returns a TreeMatch. Note that this returns the passed reference, so should
     * not be used in contexts where this will cause problems.
     */
    template<typename T>
    const T& as_matcher(const T& matcher) {
      return matcher;
    }

    TreeMatch as_matcher(const GCPtr<Tree>& tree) {
      return TreeMatch(tree);
    }

    /**
     * \brief Matcher which always successfully matches.
     */
    class AnyPattern {
    public:
      AnyPattern() {
      }

      template<typename Previous> struct MatchType {
        typedef Previous Type;
      };

      template<typename Previous>
      Previous match(Tree*, const Previous& previous) const {
        return previous;
      }
    };

    template<typename Label, typename Inner>
    class CapturePattern {
      Inner m_inner;

    public:
      CapturePattern(const Inner& inner)
      : m_inner(inner) {
      }

      template<typename Previous> struct MatchType {
        typedef typename Inner::template MatchType<MatchValue<Label, Tree*, Previous> >::Type Type;
      };

      template<typename Previous>
      typename MatchType<Previous>::Type match(Tree* tree, const Previous& previous) const {
        return m_inner.match(tree, typename MatchType<Previous>::Type(previous));
      }
    };

    template<typename Label, typename Inner>
    CapturePattern<Label, typename MatcherType<Inner>::Type> capture(const Inner& inner) {
      return CapturePattern<Label, typename MatcherType<Inner>::Type>(as_matcher(inner));
    }

    template<typename Label>
    CapturePattern<Label, AnyPattern> capture() {
      return CapturePattern<Label, AnyPattern>(AnyPattern());
    }

    template<typename NodeType, typename Child>
    class UnaryOperationPattern {
      Child m_child;

    public:
      UnaryOperationPattern(const Child& child)
      : m_child(child) {
      }

      template<typename Previous> struct MatchType {
        typedef typename Child::template MatchType<Previous>::Type Type;
      };

      template<typename Previous>
      typename MatchType<Previous>::Type match(Tree* tree, const Previous& previous) const {
        if (NodeType *node = dynamic_cast<NodeType*>(tree)) {
          return m_child.match(node->child, previous);
        } else {
          return typename MatchType<Previous>::Type();
        }
      }

      GCPtr<NodeType> build(CompileContext& context) const {
        GCPtr<NodeType> node(new NodeType(context));
        node->child = m_child.build(context);
        return node;
      }
    };

    template<typename NodeType, typename Left, typename Right>
    class BinaryOperationPattern {
      Left m_left;
      Right m_right;

    public:
      BinaryOperationPattern(const Left& left, const Right& right)
      : m_left(left), m_right(right) {
      }

      template<typename Previous> struct MatchType {
        typedef typename Right::template MatchType<typename Left::template MatchType<Previous>::Type>::Type Type;
      };

      template<typename Previous>
      typename MatchType<Previous>::Type match(Tree* tree, const Previous& previous) const {
        if (NodeType *node = dynamic_cast<NodeType*>(tree)) {
          return m_right.match(node->left, m_left.match(node->right, previous));
        } else {
          return typename MatchType<Previous>::Type();
        }
      }

      GCPtr<NodeType> build(CompileContext& context) const {
        GCPtr<NodeType> node(new NodeType(context));
        node->left = m_left.build(context);
        node->right = m_right.build(context);
        return node;
      }
    };

#define PSI_BINARY_OPERATION(name,op) \
    template<typename Left, typename Right> \
    BinaryOperationPattern<op, typename MatcherType<Left>::Type, typename MatcherType<Right>::Type> name(const Left& left, const Right& right) { \
      return BinaryOperationPattern<op, typename MatcherType<Left>::Type, typename MatcherType<Right>::Type>(as_matcher(left), as_matcher(right)); \
    }

    PSI_BINARY_OPERATION(add, AddOperation)
    PSI_BINARY_OPERATION(subtract, SubtractOperation)
    PSI_BINARY_OPERATION(multiply, MultiplyOperation)
    PSI_BINARY_OPERATION(divide, DivideOperation)
    PSI_BINARY_OPERATION(remainder, RemainderOperation)

#undef PSI_BINARY_OPERATION

    template<typename Pattern>
    typename Pattern::template MatchType<MatchBase>::Type match(Tree *tree, const Pattern& pattern) {
      return pattern.match(tree, MatchBase(true));
    }

    template<typename Pattern>
    Tree* tree(CompileContext& context, const Pattern& pattern) {
      return pattern.tree(context);
    }

/**
 * \brief Calls match() and assigns the result to the variable named \c name.
 *
 * \param name Name of resulting match variable.
 * \param tree Tree to match.
 * \param expr Pattern to match against \c tree.
 *
 * This is designed to be used in code such as:
 *
 * \code
 * if (PSI_MATCH(m1, tree, plus(capture<_1>(), capture<_2>()))) {
 *   ...
 * }
 * \endcode
 */
#define PSI_MATCH(name,tree,expr) BOOST_AUTO(name, ::Psi::Compiler::match(tree, expr))
  }
}

BOOST_TYPEOF_REGISTER_TYPE(Psi::Compiler::MatchBase)
BOOST_TYPEOF_REGISTER_TEMPLATE(Psi::Compiler::MatchValue, 3)

#endif
