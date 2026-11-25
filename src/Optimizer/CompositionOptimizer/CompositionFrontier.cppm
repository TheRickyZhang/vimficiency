export module vf.Optimizer.CompositionOptimizer.CompositionFrontier;

import std.compat;
export import vf.Boundary.NavBoundary;
export import vf.Keyboard.Config;
export import vf.Optimizer.CompositionOptimizer.CompositionOptimizerParams;
export import vf.Optimizer.DiffPlanner.DiffState;
export import vf.Optimizer.FrontierCommon;
export import vf.Types.NavContext;

export {

// =============================================================================
// Composition frontier — single direct edit-bearing tokens
// =============================================================================
// Parallel to rankNavFrontier. Where NavFrontier surfaces motions toward
// edit sites, CompositionFrontier surfaces single Vim primitives whose
// activation regions go beyond ordinary TransformFrontier start columns:
//
//   - Pure-insertion shortcuts: `i / a / I / A / o`. Activation:
//     cursor inside the strategy's insertion column range.
//   - Bracket/quote text objects: `di" / da{ / ci( / ca[ / ...`. Activation:
//     cursor at the enumerated `(line, col)` of a visible bracket/quote.
//   - Join shortcut: bare `J` from the join plan's entry line when it
//     makes progress on the planned diff, including completion.
//
// Invariant: every emitted Suggestion's `token` is a single immediately
// executable Vim action. No motion prefix. No typed insert payload. No
// `<Esc>`. Movement to a composition activation region is NavFrontier's
// job. Typed payload + `<Esc>` after Insert-entering actions is owned by
// Explore's Insert phase, derived from observed state.
//
// Tokens across all returned suggestions are pairwise distinct
// (release-active CHECK).



struct CompositionFrontierQuery : FrontierQuery {
  const DiffState& diff;
  const NavBoundary& boundary;
  const NavContext& navContext;
};

// Composition actions available from the current cursor before ranking.
std::vector<Suggestion> collectCompositionEdits(
    const CompositionFrontierQuery& query,
    const CompositionOptimizerParams& params, const Config& config);

}  // export
