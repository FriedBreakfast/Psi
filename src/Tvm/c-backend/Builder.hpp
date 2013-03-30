#ifndef HPP_PSI_TVM_CBACKEND_BUILDER
#define HPP_PSI_TVM_CBACKEND_BUILDER

#include <deque>
#include <string>
#include <boost/optional.hpp>

#include "../Core.hpp"

namespace Psi {
namespace Tvm {
/**
 * \brief TVM backend which writes C99 code.
 * 
 * Note that the resulting code will not be valid C89 or C++. It also uses
 * features which are "optional" in C11, but mandatory in C99.
 * 
 * <ul>
 * <li>Forward declaration of static variables is not allowed in C++ but works in C.</li>
 * <li>Variably sized arrays are a mandatory C99 feature but an optional C11 one.</li>
 * </ul>
 */
namespace CBackend {
class CModuleBuilder {
  std::string m_type_prefix, m_parameter_prefix, m_variable_prefix;
  std::deque<std::string> m_type_declarations, m_global_declarations, m_global_definitions;
  typedef std::map<ValuePtr<Function>, unsigned> ConstructorPriorityMap;
  ConstructorPriorityMap m_constructor_functions, m_destructor_functions;
  static boost::optional<unsigned> get_priority(const ConstructorPriorityMap& priority_map, const ValuePtr<Function>& function);
  
  void declare_global(std::ostream& os, const ValuePtr<GlobalVariable>& global);
  void define_global(std::ostream& os, const ValuePtr<GlobalVariable>& global);
  void declare_function(std::ostream& os, const ValuePtr<Function>& function);
  void define_function(std::ostream& os, const ValuePtr<Function>& function);
  
public:
  void run(Module& module);
  void build_function(const ValuePtr<Function>& function);
  void build_global_variable(const ValuePtr<GlobalVariable>& gvar);

  const std::string& build_type(const ValuePtr<>& type);
};
}
}
}

#endif
