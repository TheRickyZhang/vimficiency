export module vf.Optimizer.DiffPlanner.VimDiff;

import std.compat;
export import vf.Optimizer.DiffPlanner.DiffState;
export import vf.Optimizer.DiffPlanner.SealMatchedRuns;
export import vf.Keyboard.Config;
export import vf.Types.CountPrefixLimits;
export import vf.Types.Lines;

export {

namespace VimDiff {

// DP budget summed over blocks in `(n+1)·(m+1)` cells (16 B each, two tables),
// so this bounds memory as well as time. Over it `calculate` skips the DP and
// returns the sealed partition instead.
inline constexpr long long MAX_PLANNER_CELLS = 100'000'000;

struct CostOptions {
  double moveDeleteScale = 1.0;   // scales keystroke move/delete vs insert effort
  int maxPrefixCount = CountPrefixLimits::DEFAULT_MAX_PREFIX_COUNT;  // same knob as the searches
  long long maxPlannerCells = MAX_PLANNER_CELLS;
};

// The cheapest partition and its planner cost. `diffs` is empty when initial
// already equals goal. Over `options.maxPlannerCells`, or when a block spans
// more than 65535 chars on either side, the plan is the sealed partition (one
// region per unmatched block), correct but not weighed against alternatives.
struct Plan {
  std::vector<DiffState> diffs;
  double cost = 0.0;
};

Plan calculate(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options = {});

// Per-region cost the planner weighed. `ins` is the insert phase: typed effort
// plus the ending <Esc>, plus the entry key only for a pure insertion — a
// deletion's change form absorbs it (0 when nothing is typed). `move` is the
// counted-motion traversal from the previous region's initial end to this
// region's initial begin (0 for the first). `total` adds REGION_PENALTY per
// region and equals the DP optimum exactly. Diagnostic surface —
// `calculate` does not use it.
struct RegionBreakdown {
  DiffState diff;
  double del = 0.0;
  double ins = 0.0;
  double move = 0.0;
};

struct CostBreakdown {
  std::vector<RegionBreakdown> regions;
  double total = 0.0;
};

CostBreakdown calculateBreakdown(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options = {});

// Read-only view of one planner run: the sealed blocks, every DP cell, and the
// cells on the optimal path. Diagnostic surface for the docs-site widgets and
// tests/Debug/VimDiffTraceExport; `calculate` does not use it.

// Three step families: move (MOVE within a block, CROSS over a seal), delete
// (DELETE, or CHANGE when the `c` form lands straight in insert mode and so
// skips the entry key) and type (ENTER from normal mode, TYPE within insert
// mode). EXIT is the free edge back to normal mode; LEADING is the root.
enum Step : int8_t { LEADING, MOVE, CROSS, DELETE, CHANGE, ENTER, TYPE, EXIT };

struct TraceCell {
  double cost = INF;  // INF: unreached
  Step step = LEADING;
  int pi = 0, pj = 0;  // predecessor; in the previous block for CROSS, in `in` for TYPE/EXIT
};

// out[i, j]: normal mode, initial [0,i) consumed, goal [0,j) produced;
// in[i, j]: insert mode. Cell (i, j) sits at index i * (m + 1) + j.
struct TraceBlock {
  Block block;
  int lead = 0, trail = 0;  // matched diagonal length at the block's start / end
  std::vector<TraceCell> out, in;

  inline const TraceCell& outAt(int i, int j) const { return out[i * (block.m() + 1) + j]; }
  inline const TraceCell& inAt(int i, int j) const { return in[i * (block.m() + 1) + j]; }
};

struct TracePathCell {
  int block, i, j;
  bool inTable;
};

struct Trace {
  std::vector<TraceBlock> blocks;
  std::vector<TracePathCell> path;  // root first, optimum last; empty for the sealed fallback
  long long dpCells = 0, rawCells = 0;  // Σ(n+1)(m+1) over blocks vs (N+1)(M+1)
  bool sealedFallback = false;
  CostBreakdown breakdown;
};

Trace trace(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options = {});

}  // namespace VimDiff

}  // export
