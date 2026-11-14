export module vf.Optimizer.NavOptimizer.NavOptimizer;

import std.compat;
export import vf.Optimizer.NavOptimizer.BufferIndex;
export import vf.Optimizer.LandingResult;
export import vf.Boundary.NavBoundary;
export import vf.Keyboard.Config;
export import vf.Effort.EffortBank;
export import vf.Optimizer.NavOptimizer.NavOptimizerParams;
export import vf.Optimizer.OptimizerResult;
export import vf.Optimizer.SearchControl;
export import vf.Optimizer.SearchStats;
export import vf.Types.Lines;
export import vf.Types.NavContext;
export import vf.Types.CursorPos;
export import vf.Types.Lines;

export {

// All NavOptimizer searches return `LandingNavResult` — each result carries
// its landing position via `LandingResult::getGoalPos()`
struct LandingNavResult : BaseOptimizerResult<LandingResult> {
  inline const NavSearchStats& getStats() const { return stats_; }

private:
  NavSearchStats stats_;
  friend struct NavOptimizer;
  inline LandingNavResult(std::vector<LandingResult> results, NavSearchStats stats)
    : BaseOptimizerResult(std::move(results)), stats_(std::move(stats)) {}
};

struct NavOptimizer {
  Config config;

  EffortBank bank;

  inline NavOptimizer(const Config& config) : config(config), bank(config) {}

  // Find sequences from initialPos to some positions in goalInterval.
  // params.maxResultsPerEndPos caps the cheapest sequences kept per landing.
  // `control` is polled each pop; the search returns best-so-far
  // when it fires.
  LandingNavResult optimize(
    const Lines& lines, const CursorPos& initialPos, const CharInterval& goalInterval,
    NavOptimizerParams params = {}, std::string_view userSequence = "",
    const NavBoundary& boundary = NavBoundary(),
    const NavContext& navigationContext = NavContext(),
    const SearchControl& control = SearchControl::NEVER_STOP
  );

  // Same overload but takes a pre-built BufferIndex
  LandingNavResult optimize(
    const Lines& lines, const CursorPos& initialPos, const CharInterval& goalInterval,
    NavOptimizerParams params, std::string_view userSequence,
    const NavBoundary& boundary,
    const NavContext& navigationContext,
    const BufferIndex& bufferIndex, int lineOffset,
    const SearchControl& control = SearchControl::NEVER_STOP
  );

  // single-cursor goal overload, underlying implementation is the same.
  // To get several sequences, raise params.maxResultsPerEndPos to maxResults.
  LandingNavResult optimize(
    const Lines& lines, const CursorPos& initialPos, const CursorPos& goalPos,
    NavOptimizerParams params = {}, std::string_view userSequence = "",
    const NavBoundary& boundary = NavBoundary(),
    const NavContext& navigationContext = NavContext(),
    const SearchControl& control = SearchControl::NEVER_STOP
  );
};

}  // export
