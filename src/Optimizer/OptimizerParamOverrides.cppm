export module vf.Optimizer.OptimizerParamOverrides;

import std.compat;
export import vf.Optimizer.NavOptimizer.NavOptimizerParams;
export import vf.Optimizer.TransformOptimizer.TransformOptimizerParams;
export import vf.Optimizer.CompositionOptimizer.CompositionOptimizerParams;

// Sparse override set for one params type: `set` marks which `values` the
// user supplied. Values are parsed once, at parse time.
template <class Params>
struct ParamPatch {
  Params values;
  std::bitset<size_t(Params::Field::COUNT)> set;
};

export {

using OptimizerParamOverrideErrors = std::vector<std::string>;

// Lua-authored optimizer tweaks, `<scope>:<key>=<value>` per line. `shared:`
// is restricted to OptimizerParamsBase fields; concrete optimizer fields must
// use their concrete scope.
class OptimizerParamOverrides {
public:
  static std::expected<OptimizerParamOverrides, OptimizerParamOverrideErrors>
  parse(std::string_view encoded);

  // `base` carries the call site's defaults; `shared:` applies on top, then
  // the scope matching Params.
  template <class Params>
  Params resolved(Params base) const {
    applyTo(base);
    return base;
  }

  inline bool empty() const {
    return shared_.set.none() && nav_.set.none() &&
           transform_.set.none() && composition_.set.none();
  }

private:
  void applyTo(NavOptimizerParams& params) const;
  void applyTo(TransformOptimizerParams& params) const;
  void applyTo(CompositionOptimizerParams& params) const;

  ParamPatch<OptimizerParamsBase> shared_;
  ParamPatch<NavOptimizerParams> nav_;
  ParamPatch<TransformOptimizerParams> transform_;
  ParamPatch<CompositionOptimizerParams> composition_;
};

std::string formatOptimizerParamOverrideErrors(
    const OptimizerParamOverrideErrors& errors);

}  // export
