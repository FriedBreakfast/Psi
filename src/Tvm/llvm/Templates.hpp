#ifndef HPP_PSI_TVM_LLVM_TEMPLATES
#define HPP_PSI_TVM_LLVM_TEMPLATES

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      template<typename T>
      struct PtrValidBase {
        T* invalid() const {return NULL;}
        bool valid(const T* t) const {return t;}
      };

      /**
       * Utility callback for building functional or type terms
       * lazily.
       */
      template<typename ValueMap, typename Callback>
      std::pair<typename ValueMap::value_type::second_type, bool>
      build_term(ValueMap& values, typename ValueMap::value_type::first_type term, const Callback& cb) {
        std::pair<typename ValueMap::iterator, bool> itp =
          values.insert(std::make_pair(term, cb.invalid()));
        if (!itp.second) {
          if (cb.valid(itp.first->second)) {
            return std::make_pair(itp.first->second, false);
          } else {
            throw BuildError("Cyclical term found");
          }
        }

        typename ValueMap::value_type::second_type r = cb.build(term);
        if (cb.valid(r)) {
          itp.first->second = r;
        } else {
          values.erase(itp.first);
          throw BuildError("LLVM term building failed");
        }

        return std::make_pair(r, true);
      }
    }
  }
}

#endif
