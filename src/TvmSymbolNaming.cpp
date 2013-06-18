#include "TvmLowering.hpp"

namespace Psi {
namespace Compiler {
/**
 * \brief Encode a number into an ASCII string.
 * 
 * The result is emitted in base-31. The 10 ASCII decimal digits plus
 * 26 alphabetic characters (lower and upper case) encode the numbers 0-61. For each digit \f$ n \f$,
 * if \f$ n >= 31 \f$ the digit is \f$ n-31 \f$ and there is another digit after.
 * If \f$ n<31 \f$, then the encoded digit is \f$ n \f$ and it is the last digit.
 */
void symbol_encode_number(std::ostream& os, unsigned n) {
  const char *low_digits =  "0123456789ABCDEFGHIJKLMNOPQRSTU";
  const char *high_digits = "VWXYZabcdefghijklmnopqrstuvwxyz";
  
  if (n == 0) {
    os << '0';
    return;
  }
  
  unsigned char digits[32];
  unsigned n_digits = 0;
  for (unsigned m = n; m > 0; m /= 31)
    digits[n_digits++] = m%31;
  PSI_ASSERT(n_digits > 0);
  while (--n_digits)
    os << high_digits[digits[n_digits]];
  os << low_digits[digits[0]];
}

std::string SymbolNameSet::unique_name(const std::string& base) {
  unsigned index = m_unique_names[base]++;
  std::stringstream ss;
  ss << base;
  symbol_encode_number(ss, index);
  return ss.str();
}

const std::string& SymbolNameSet::symbol_name(const TreePtr<ModuleGlobal>& global) {
  std::string& name = m_symbol_names[global];
  if (!name.empty())
    return name;
  
  if (!global->symbol_name.empty()) {
    PSI_ASSERT(global->linkage != link_local);
    name = global->symbol_name;
  } else {
    SymbolNameBuilder symbol_name_builder;
    symbol_name_builder.emit(global->location().logical);
    name = symbol_name_builder.name();
    if (global->linkage == link_local)
      name = unique_name(name);
  }
  return name;
}

SymbolNameBuilder::SymbolNameBuilder()
: m_buckets(initial_buckets),
m_nodes(NodeSet::bucket_traits(m_buckets.get(), initial_buckets)) {
  m_node_index = 0;
  m_current = &m_root;
  m_root.parent = NULL;
}

SymbolNameBuilder::~SymbolNameBuilder() {
  m_nodes.clear();
}

SymbolNameBuilder::Node::Node() {
  index_first = false;
}

struct SymbolNameBuilder::NodeDisposer {
  void operator () (Node *ptr) {
    delete ptr;
  }
};

SymbolNameBuilder::Node::~Node() {
  clear();
}

void SymbolNameBuilder::Node::clear() {
  children.clear_and_dispose(NodeDisposer());
  std::string().swap(name);
}

bool SymbolNameBuilder::Node::operator == (const Node& other) const {
  return equals(*this, other);
}

std::size_t SymbolNameBuilder::Node::hash() const {
  std::size_t h = 0;
  boost::hash_combine(h, name);
  for (NodeList::const_iterator ii = children.begin(), ie = children.end(); ii != ie; ++ii)
    boost::hash_combine(h, ii->index);
  return h;
}

bool SymbolNameBuilder::equals(const Node& lhs, const Node& rhs) {
  if (lhs.name != rhs.name)
    return false;
  if (lhs.children.size() != rhs.children.size())
    return false;
  
  for (NodeList::const_iterator ii = lhs.children.begin(), ji = rhs.children.begin(), ie = lhs.children.end(); ii != ie; ++ii, ++ji) {
    if (ii->index != ji->index)
      return false;
  }
  
  return true;
}

void SymbolNameBuilder::enter() {
  Node *child = new Node;
  child->parent = m_current;
  m_current->children.push_back(*child);
  m_current = child;
}

void SymbolNameBuilder::exit() {
  PSI_ASSERT(m_current->parent);
  std::pair<NodeSet::iterator, bool> ins = m_nodes.insert(*m_current);
  if (ins.second) {
    m_current->index_first = false;
    m_current->index = ins.first->index;
  } else {
    m_current->index_first = true;
    m_current->index = m_node_index++;
    m_current->clear();
  }
  m_current = m_current->parent;
  
  if (m_nodes.size() > m_buckets.size()) {
    UniqueArray<NodeSet::bucket_type> new_buckets(m_buckets.size() * 2);
    m_nodes.rehash(NodeSet::bucket_traits(new_buckets.get(), new_buckets.size()));
    m_buckets.swap(new_buckets);
  }
}

void SymbolNameBuilder::emit(const std::string& name) {
  enter();
  m_current->name = name;
  exit();
}

void SymbolNameBuilder::emit(const LogicalSourceLocationPtr& location) {
  std::vector<const LogicalSourceLocation*> ancestors;
  for (const LogicalSourceLocation *ptr = location.get(); ptr->parent(); ptr = ptr->parent().get())
    ancestors.push_back(ptr);
  enter();
  for (std::vector<const LogicalSourceLocation*>::const_reverse_iterator ii = ancestors.rbegin(), ie = ancestors.rend(); ii != ie; ++ii)
    emit((*ii)->name());
  exit();
}

std::string SymbolNameBuilder::name() {
  PSI_ASSERT(m_current == &m_root);
  std::ostringstream ss;
  ss << "_Y";
  Node *p = &m_root;
  while (true) {
    PSI_ASSERT(p->name.empty() || p->children.empty());
    if (!p->children.empty()) {
      symbol_encode_number(ss, p->children.size()*2 + 1);
      p = &p->children.front();
    } else {
      symbol_encode_number(ss, p->name.size()*2);
      ss << p->name;
      
      while (true) {
        if (!p->parent)
          return ss.str();
        
        NodeList::iterator next = p->parent->children.iterator_to(*p);
        ++next;
        if (next == p->parent->children.end()) {
          p = p->parent;
        } else {
          p = &*next;
          break;
        }
      }
    }
  }
}
}
}
