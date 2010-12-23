#include "Utility.hpp"
#include "Function.hpp"

namespace Psi {
  namespace Tvm {
    /**
     * Find the common dominator of the two blocks. If no such block
     * exists, throw an exception.
     */
    Term* common_source(Term *t1, Term *t2) {
      if (t1 && t2) {
        switch (t1->term_type()) {
        case term_function:
          switch (t2->term_type()) {
          case term_function:
            if (t1 == t2)
              return t1;
            else
              goto common_source_fail;

          case term_block:
            if (t1 == cast<BlockTerm>(t2)->function())
              return t2;
            else
              goto common_source_fail;

          default:
            PSI_FAIL("unexpected term type");
          }

        case term_block:
          switch (t2->term_type()) {
          case term_function:
            if (t2 == cast<BlockTerm>(t1)->function())
              return t1;
            else
              goto common_source_fail;

          case term_block: {
            BlockTerm *bt1 = cast<BlockTerm>(t1);
            BlockTerm *bt2 = cast<BlockTerm>(t2);
            if (bt1->dominated_by(bt2))
              return t1;
            else if (bt2->dominated_by(bt1))
              return t2;
            else
              goto common_source_fail;
          }

          default:
            PSI_FAIL("unexpected term type");
          }

        default:
          PSI_FAIL("unexpected term type");
        }

      common_source_fail:
        throw TvmUserError("cannot find common term source block");
      } else {
        return t1 ? t1 : t2;
      }
    }

    /**
     * Check whether a source term is dominated by another.
     */
    bool source_dominated(Term *dominator, Term *dominated) {
      if (dominator && dominated) {
        switch (dominator->term_type()) {
        case term_function:
          switch (dominated->term_type()) {
          case term_function: return dominator == dominated;
          case term_block: return dominator == cast<BlockTerm>(dominated)->function();
          default: PSI_FAIL("unexpected term type");
          }

        case term_block:
          switch (dominated->term_type()) {
          case term_function: return false;
          case term_block: return cast<BlockTerm>(dominated)->dominated_by(cast<BlockTerm>(dominator));
          default: PSI_FAIL("unexpected term type");
          }

        default:
          PSI_FAIL("unexpected term type");
        }
      } else {
        return dominated || !dominator;
      }
    }
  }
}


