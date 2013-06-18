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
void symbol_encode_number(std::ostream& os, uint64_t n) {
  const char *low_digits =  "0123456789ABCDEFGHIJKLMNOPQRSTU";
  const char *high_digits = "VWXYZabcdefghijklmnopqrstuvwxyz";
  
  if (n == 0) {
    os << '0';
    return;
  }
  
  unsigned char digits[32];
  unsigned n_digits = 0;
  for (uint64_t m = n; m > 0; m /= 31)
    digits[n_digits++] = m%31;
  PSI_ASSERT(n_digits > 0);
  while (--n_digits)
    os << high_digits[digits[n_digits]];
  os << low_digits[digits[0]];
}

/**
 * \brief Encode a signed number into an ASCII string.
 * 
 * Zero is encoded as zero. Positive numbers are encoded as
 * 1, 2, 3 -> 2, 4, 5... and negative numbers are encoded as
 * -1, -2, -3 -> 1, 3, 5 and then symbol_encode_number is used.
 */
void symbol_encode_signed_number(std::ostream& os, int64_t n) {
  if (n > 0) {
    symbol_encode_number(os, n*2);
  } else if (n < 0) {
    symbol_encode_number(os, -2*n-1);
  } else {
    os << '\0';
  }
}

std::string SymbolNameSet::unique_name(const std::string& base) {
  unsigned index = m_unique_names[base]++;
  std::stringstream ss;
  ss << base;
  symbol_encode_number(ss, index);
  return ss.str();
}

class SymbolLocationWriter {
  struct Node {
    int key;
    std::map<std::string, Node> children;
    Node() : key(-1) {}
  };
  std::ostream *m_output;
  int m_index;
  Node m_root;
  
public:
  SymbolLocationWriter(std::ostream& os) : m_output(&os), m_index(1) {m_root.key = 0;}
  std::ostream& output() {return *m_output;}
  
  void write(const LogicalSourceLocationPtr& loc, bool full, char prefix_full, char prefix_part) {
    std::vector<const LogicalSourceLocation*> ancestors;
    for (const LogicalSourceLocation *ptr = loc.get(); ptr->parent(); ptr = ptr->parent().get())
      ancestors.push_back(ptr);
    
    if (full)
      symbol_encode_number(output(), ancestors.size());
    
    Node *ptr = &m_root;
    while (!ancestors.empty()) {
      const std::string& s = ancestors.back()->name();
      Node *next = &ptr->children[s];
      
      if (next->key < 0)
        break;
      
      if (full) {
        symbol_encode_number(output(), s.size());
        output() << s;
      }
      
      ptr = next;
      ancestors.pop_back();
    }
    
    if (!full) {
      if (ptr == &m_root) {
        output() << prefix_full;
      } else {
        output() << prefix_part;
        symbol_encode_number(output(), ptr->key);
      }
      symbol_encode_number(output(), ancestors.size());
    }
    
    while (!ancestors.empty()) {
      const std::string& s = ancestors.back()->name();
      symbol_encode_number(output(), s.size());
      output() << s;
      ptr = &ptr->children[s];
      ptr->key = m_index++;
      ancestors.pop_back();
    }
  }
};

const std::string& SymbolNameSet::symbol_name(const TreePtr<ModuleGlobal>& global) {
  std::string& name = m_symbol_names[global];
  if (!name.empty())
    return name;
  
  if (!global->symbol_name.empty()) {
    PSI_ASSERT(global->linkage != link_local);
    name = global->symbol_name;
  } else {
    std::ostringstream ss;
    ss << "_Y0";
    SymbolLocationWriter lw(ss);
    lw.write(global->location().logical, true, '\0', '\0');
    name = ss.str();
    if (global->linkage == link_local)
      name = unique_name(name);
  }
  return name;
}


/**
 * \brief Generate a name of a type for use in a symbol.
 */
void symbol_type_name(SymbolLocationWriter& lw, const TreePtr<Term>& term) {
  if (TreePtr<TypeInstance> inst = term_unwrap_dyn_cast<TypeInstance>(term)) {
    if (inst->parameters.empty()) {
      lw.write(inst->generic->location().logical, false, 'A', 'B');
    } else {
      lw.write(inst->generic->location().logical, false, 'C', 'D');
      symbol_encode_number(lw.output(), inst->parameters.size());
      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = inst->parameters.begin(), ie = inst->parameters.end(); ii != ie; ++ii)
        symbol_type_name(lw, *ii);
    }
  } else if (TreePtr<NumberType> type = term_unwrap_dyn_cast<NumberType>(term)) {
    PSI_ASSERT(type->vector_size == 0);
    const char *type_keys = "GHIJKLMNOPQ";
    lw.output() << type_keys[type->scalar_type];
  } else if (TreePtr<IntegerConstant> value = term_unwrap_dyn_cast<IntegerConstant>(term)) {
    const char *type_keys = "ghijklmnopq";
    lw.output() << type_keys[value->number_type];
    if (NumberType::is_signed(value->number_type))
      symbol_encode_signed_number(lw.output(), value->value);
    else
      symbol_encode_number(lw.output(), value->value);
  } else {
    lw.write(inst->location().logical, false, 'E', 'F');
  }
}

std::string symbol_implementation_name(const TreePtr<Interface>& interface, const PSI_STD::vector<TreePtr<Term> >& parameters) {
  std::ostringstream ss;
  ss << "_Y1";
  SymbolLocationWriter lw(ss);
  lw.write(interface->location().logical, true, '\0', '\0');
  for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = parameters.begin(), ie = parameters.end(); ii != ie; ++ii)
    symbol_type_name(lw, *ii);
  return ss.str();
}
}
}
