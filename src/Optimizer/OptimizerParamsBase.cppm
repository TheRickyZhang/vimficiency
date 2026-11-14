module;
#include "Optimizer/OptimizerParamFields.h"
#include <cassert>

export module vf.Optimizer.OptimizerParamsBase;

import std.compat;
export import vf.Types.CountPrefixLimits;

export {

struct OptimizerParamsBase {
  OPTIMIZER_BASE_FIELDS(VF_PARAMS_DECLARE_OWN_FIELD)
  enum class Field { OPTIMIZER_BASE_FIELDS(VF_PARAMS_FIELD_ENUM) COUNT };

  // Called only from OptimizerParamOverrides::resolved() — the trust boundary
  // for external Lua-config input. Direct in-tree construction uses literals
  // and is trusted, so optimizer entry points don't validate.
  inline void validate() const {
    assert(minPrefixCount >= CountPrefixLimits::MIN_PREFIX_COUNT);
    assert(maxPrefixCount >= 0 && maxPrefixCount <= CountPrefixLimits::MAX_PREFIX_COUNT);
  }

  inline bool countPrefixesEnabled() const { return minPrefixCount <= maxPrefixCount; }
};

}  // export
