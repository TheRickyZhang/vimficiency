export module vf.Optimizer.TransformOptimizer.TransformPostExplorerEmissions;

import std.compat;
export import vf.Keyboard.Config;
export import vf.Optimizer.Result;
export import vf.Optimizer.TransformOptimizer.TransformOptimizerParams;
export import vf.Types.CursorPos;
export import vf.Types.Lines;

export {

// =============================================================================
// Post-explorer emissions — single source of truth
// =============================================================================
// Structurals that supplement the per-step enumeration in TransformExplorer.
// Both the full optimizer's finalize paths and the depth-1 frontier call
// these helpers directly, so eligibility and sequence construction stay
// aligned.



namespace TransformPostExplorer {

struct VisualDeleteResult {
  Result result;
  CursorPos goalPos;
};

// Emits `r{char}` / `Nr{char}` for single-line same-length replacement diffs.
std::optional<Result> tryReplacement(
    std::string_view deleted,
    std::string_view inserted,
    const Config& config,
    double maxEffort);

// Builds a `v{motion}d` candidate that selects the entire deletion region
// and deletes it. Returns nullopt when there's no spannable content (single
// position or boundary collapse). Used only for pure-deletion diffs — the
// optimizer never emits a visual variant for replacement diffs.
//
// `effectiveLines` is the prefix+content+suffix view (matches what the
// optimizer's main loop sees and what `enumerateDepth1DeletionStructurals`
// constructs).
std::optional<VisualDeleteResult> tryVisualDelete(
    const Lines& effectiveLines,
    int leftColOffset,
    int rightColOffset,
    const TransformOptimizerParams& params,
    const Config& config);

}  // namespace TransformPostExplorer

}  // export
