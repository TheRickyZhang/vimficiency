module;
#include <cassert>

export module vf.Optimizer.CompositionOptimizer.CompositionPlan;

import std.compat;
export import vf.Optimizer.DiffPlanner.DiffState;
export import vf.Types.CharRange;
export import vf.Types.CursorPos;
export import vf.Types.Lines;

export {

// Plan layout: a session has E edits and E+1 navigation segments alternating
// between them — nav_0, edit_0, nav_1, edit_1, ..., edit_{E-1}, nav_E.
// `navTarget(i)` for i in [0, E) is the half-open range of edit i; `navTarget(E)`
// is the trivial point range at the user's `finalGoalPos`. Pure-motion sessions
// have E == 0 and one nav target (== finalGoalPos).
struct CompositionPlan {
  CursorPos finalGoalPos{0, 0};
  std::vector<DiffState> diffs;
  std::vector<Lines> fenceposts;

  inline int totalEdits() const { return static_cast<int>(diffs.size()); }

  // E + 1 nav segments per session.
  inline int totalNavigations() const { return totalEdits() + 1; }

  inline bool empty() const { return diffs.empty(); }

  inline const DiffState& diffAt(int editIndex) const {
    return diffs[static_cast<size_t>(editIndex)];
  }

  inline const Lines& fencepostAt(int editIndex) const {
    return fenceposts[static_cast<size_t>(editIndex)];
  }

  // Target range for the i-th navigation segment. For i < totalEdits(), this is
  // the diff range of edit i (the cursor must land somewhere inside it to apply
  // the edit). For i == totalEdits(), this is the trivial range at finalGoalPos.
  inline CharRange navTarget(int navIndex) const {
    assert(navIndex >= 0 && navIndex <= totalEdits() &&
           "navTarget index out of range");
    if (navIndex < totalEdits()) {
      const DiffState& d = diffs[static_cast<size_t>(navIndex)];
      return CharRange(d.beginPos, d.endPos);
    }
    return CharRange(finalGoalPos, finalGoalPos);
  }
};

}  // export
