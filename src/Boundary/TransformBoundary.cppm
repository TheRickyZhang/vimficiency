export module vf.Boundary.TransformBoundary;

import std.compat;
export import vf.Types.CursorPos;
export import vf.Types.Lines;

export {

// Pre-computed boundary info for constrained transform operations: the
// same-line content immediately outside the transform region plus
// hasLinesAbove/Below for line-level operations (gg, G, k, j escaping).
struct TransformBoundary {
  TransformBoundary() = default;
  // Narrow factory: a boundary whose only meaningful fields are
  // hasLinesAbove/hasLinesBelow. Empty prefix/suffix are correct only for
  // synthesized single-line residual buffers (today: the join-residual
  // transform in CompositionOptimizer/PlannedEditArtifacts). Do not extend
  // to other use sites without revisiting those defaults.
  static TransformBoundary joinResidualBoundary(bool hasLinesAbove, bool hasLinesBelow);

  // endPos is exclusive: one past the last valid cursor position on the end line
  TransformBoundary(const Lines& lines, CursorPos beginPos, CursorPos endPos);

  inline const std::string& prefix() const { return prefix_; }
  inline const std::string& suffix() const { return suffix_; }
  inline bool hasLinesAbove() const { return hasLinesAbove_; }
  inline bool hasLinesBelow() const { return hasLinesBelow_; }

  inline int leftColOffset() const { return static_cast<int>(prefix_.size()); }
  inline int rightColOffset() const { return static_cast<int>(suffix_.size()); }

  // Wrap `editRegionLines` with this boundary's prefix/suffix into the
  // "effective lines" view consumed by TransformExplorer / TransformOptimizer.
  // Both the optimizer's main loop setup and the depth-1 frontier
  // (TransformFrontier::enumerateDepth1DeletionStructurals) call this so the
  // construction stays in lockstep — direct mutation in one consumer was
  // a frequent source of mismatched cursor / boundary semantics.
  inline Lines withBoundary(Lines editRegionLines) const {
    if (!prefix_.empty()) editRegionLines.front().insert(0, prefix_);
    if (!suffix_.empty()) editRegionLines.back() += suffix_;
    return editRegionLines;
  }

  inline bool hasPrefix() const { return !prefix_.empty(); }
  inline bool hasSuffix() const { return !suffix_.empty(); }

private:
  std::string prefix_;
  std::string suffix_;
  bool hasLinesAbove_ = false;
  bool hasLinesBelow_ = false;
};

}  // export
