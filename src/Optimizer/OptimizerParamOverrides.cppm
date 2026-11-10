export module vf.Optimizer.OptimizerParamOverrides;

import std.compat;
export import vf.Optimizer.NavOptimizer.NavOptimizerParams;
export import vf.Optimizer.TransformOptimizer.TransformOptimizerParams;
export import vf.Optimizer.CompositionOptimizer.CompositionOptimizerParams;

export {

class OptimizerParamOverridesParser;

// Parses and applies optimizer parameter overrides from Lua.
class OptimizerParamOverrides {
public:
  OptimizerParamOverrides() = default;

  void applyTo(NavOptimizerParams& params) const;
  void applyTo(TransformOptimizerParams& params) const;
  void applyTo(CompositionOptimizerParams& params) const;

  inline bool empty() const {
    return shared_.empty() && nav_.empty() && transform_.empty() && composition_.empty();
  }

  // TODO review if this is the best design pattern
  template <class Params>
  static Params resolved(const OptimizerParamOverrides* overrides) {
    Params params;
    if (overrides) overrides->applyTo(params);
    return params;
  }

private:
  friend class OptimizerParamOverridesParser;

  using ScopeMap = std::map<std::string, std::string, std::less<>>;
  ScopeMap shared_;
  ScopeMap nav_;
  ScopeMap transform_;
  ScopeMap composition_;
};

using OptimizerParamOverrideErrors = std::vector<std::string>;
using OptimizerParamOverridesParseResult =
    std::expected<OptimizerParamOverrides, OptimizerParamOverrideErrors>;

// `shared:` is restricted to OptimizerParamsBase fields; concrete optimizer
// fields must use their concrete scope.
OptimizerParamOverridesParseResult parseOptimizerParamOverrides(
    std::string_view encoded);
std::string formatOptimizerParamOverrideErrors(
    const OptimizerParamOverrideErrors& errors);

}  // export
