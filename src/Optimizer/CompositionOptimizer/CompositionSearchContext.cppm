export module vf.Optimizer.CompositionOptimizer.CompositionSearchContext;

import std.compat;
export import vf.Optimizer.SearchControl;
export import vf.Optimizer.FrontierCommon;
export import vf.Keyboard.Config;
export import vf.Boundary.NavBoundary;
export import vf.Optimizer.CompositionOptimizer.CompositionOptimizerParams;
export import vf.Optimizer.CompositionOptimizer.JoinPlan;
export import vf.Optimizer.CompositionOptimizer.PlannedEditArtifacts;
export import vf.Optimizer.DiffPlanner.DiffState;
export import vf.Optimizer.NavOptimizer.NavHeuristic;
export import vf.Optimizer.TransformOptimizer.TransformOptimizer;
export import vf.Optimizer.SearchStats;
export import vf.Types.NavContext;
export import vf.Types.CursorPos;
export import vf.Optimizer.CompositionOptimizer.CompositionState;
export import vf.Types.Lines;

export {


// =============================================================================
// CompositionExploredState
// =============================================================================
// Extends ExploredState with composition-specific tracking.

struct CompositionExploredState : ExploredState {
  int editsCompleted;
};

// =============================================================================
// CompositionSearchContext
// =============================================================================
// Encapsulates shared state and logic for composition optimization search.
// Handles:
// - Pre-computed diff states and edit results
// - A* priority queue and cost map for state exploration
// - Heuristic computation with asymmetric overshoot penalty

struct CompositionSearchContext {
  // References to external data
  const Config& config;
  const CompositionOptimizerParams& params;
  const NavContext& navContext;
  const NavBoundary& boundary;

  // Cooperative stop signal (null when the search runs to its normal caps).
  // Set by the async FFI worker so the main A* loop can bail to best-so-far.
  const SearchControl* control = nullptr;

  // Set when `control` was cancelled mid-construction. The remaining setup
  // phases are skipped, so per-edit data is incomplete; callers must not
  // search on an aborted context.
  bool aborted = false;

  // Final cursor target. Search terminates when editsCompleted == totalEdits()
  // AND pos == goalPos. Used by heuristic() to score post-final-edit nav cost.
  CursorPos goalPos;

  // Per-edit bundled data (one entry per diff, indexed 0..totalEdits()-1)
  struct PerEditData {
    DiffState diffState;

    TransformResult transformResult;

    std::optional<JoinPlan> joinPlan;
    BracketQuoteContext bracketQuoteContext;

  };
  std::vector<PerEditData> edits;

  inline int totalEdits() const { return static_cast<int>(edits.size()); }

  int maxLineLength;

  // Search state.
  // pq lives locally in optimizeImpl() because its element type depends on
  // the trace policy (see QueueEntry<Trace> in CompositionOptimizer.cpp).
  // costMap stays here since its key (line, col, mode, editsCompleted) is
  // independent of trace.
  std::unordered_map<CompositionStateKey, double, CompositionStateKeyHash> costMap;

  // Search limits
  double maxEffort;  // userEffort * exploreFactor

  // Stats tracking
  int nodesProcessed = 0;
  int totalPops = 0;  // Hard budget: all queue pops, including stale.
  int statesSkipped = 0;

  // Sub-optimizer aggregate stats
  int navNodesExplored = 0;
  int editNodesExplored = 0;

  // Search-summary metrics deposited by `optimizeImpl` just before its local
  // priority queue goes out of scope; read by `buildCompositionResult`. Only
  // valid between those two points within a single optimize call.
  int lastPqRemaining = 0;
  bool lastFullyExplored = false;

  // Debug: optionally track explored states (composition-specific)
  std::vector<CompositionExploredState> exploredStates;

  // ==========================================================================
  // Construction
  // ==========================================================================

  // Constructor - performs all pre-computation. `forcedDiffs` is the
  // calibration/diagnostic seam: when non-null, the search runs over that exact
  // partition (ordered, non-overlapping, original-buffer spans — the planner's
  // output contract) instead of running the diff planner here.
  CompositionSearchContext(
      const Lines& initialLines,
      const CursorPos& initialPos,
      const Lines& goalLines,
      const CursorPos& goalPos,
      std::string_view userSequence,
      const NavContext& navContext,
      const NavBoundary& boundary,
      const CompositionOptimizerParams& params,
      const Config& config,
      const std::vector<DiffState>* forcedDiffs = nullptr,
      const SearchControl* control = nullptr);

  // ==========================================================================
  // Heuristic and distance computation
  // ==========================================================================

  // Concave per-axis distance, matching the NavOptimizer heuristic that
  // actually executes these inter-edit legs (linear Manhattan overestimates
  // nav effort because long-range motions cover many lines/cols per keystroke).
  inline double costToGoal(const CursorPos& curr, const CursorPos& goal) const {
    return NavHeuristic::fastSqrt(std::abs(goal.line - curr.line)) +
           NavHeuristic::fastSqrt(std::abs(goal.col - curr.col));
  }

  // h(n) for A*: estimates remaining cost
  // Uses suffix sum of median edit costs + asymmetric distance to next edit region
  double heuristic(const CompositionState& s) const;

  // Get buffer state for a given number of completed edits (0..totalEdits())
  inline const Lines& getLinesAfter(int editsCompleted) const {
    return linesAfterNEdits_[editsCompleted];
  }

  // Get the diff state for an edit index
  inline const DiffState& getDiffState(int editIndex) const {
    return edits[editIndex].diffState;
  }

  // ==========================================================================
  // Stats
  // ==========================================================================

  // Track an explored state when search-trace collection is compiled in.
  inline void trackState(const CompositionState& s) {
    if constexpr (SEARCH_TRACE_STATS_ENABLED) {
      if (std::ssize(exploredStates) >= MAX_TRACED_STATES) return;
      CursorPos pos = s.getPos();
      exploredStates.push_back({{pos.line, pos.col, s.getEffort(), s.getSequence().str()},
                                s.getEditsCompleted()});
    }
  }

  inline std::vector<CompositionExploredState> takeExploredStates() {
    return std::move(exploredStates);
  }

  // Build CompositionSearchStats from current context state.
  // `pqRemaining` is the size of the templated priority queue (owned by
  // optimizeImpl, not by the context); pass 0 if the search exited via
  // result/budget caps. `fullyExplored` records whether the queue drained
  // before any cap was hit.
  CompositionSearchStats getStats(int resultsFound,
                                  int pqRemaining,
                                  bool fullyExplored) const;

private:
  // Fencepost vectors (size = totalEdits() + 1): represent states *between* edits
  std::vector<Lines> linesAfterNEdits_;
  std::vector<double> suffixEditCosts_;

  // Helper: compute suffix sums of median edit costs
  std::vector<double> computeSuffixEditCosts() const;

  // Helper: solve each edit region independently, populates edits[i].transformResult
  void calculateTransformResults();

  // Helper: build intermediate buffer states after each diff.
  // Non-const: maps each planned diff into its pre-edit buffer context.
  std::vector<Lines> calculateLinesAfterDiffs(const Lines& initialLines);

  // Helper: compute text object contexts for each edit, populates edits[i].bracketQuoteContext
  void computeTextObjectContexts();

  // Helper: compute J (join lines) plans for diffs where source has more lines than target, populates edits[i].joinPlan
  void computeJoinPlans();

};

}  // export
