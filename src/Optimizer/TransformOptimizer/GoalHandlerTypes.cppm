export module vf.Optimizer.TransformOptimizer.GoalHandlerTypes;

import std.compat;
export import vf.Boundary.TransformBoundary;
export import vf.Optimizer.TransformOptimizer.TransformState;
export import vf.VimCore.VimEditUtils;

export {

inline VimCore::LineDeleteContext lineDeleteContext(const TransformBoundary& boundary) {
  return {
      .hasLinesAbove = boundary.hasLinesAbove(),
      .hasLinesBelow = boundary.hasLinesBelow(),
  };
}

// Protocol types for GoalHandler — shared between the dispatcher and both
// goal handler implementations (DeletionGoalHandler, ChangeGoalHandler).

// on*Goal returns 1-2 EditStates for the dispatcher to emit.
struct GoalStates {
  TransformState primary;
  std::optional<TransformState> dotVariant;

  // Out of line: Clang 23 crashes emitting the implicit destructor in importers.
  // The declaration would otherwise suppress the implicit moves.
  GoalStates() = default;
  inline GoalStates(TransformState primary, std::optional<TransformState> dotVariant)
      : primary(std::move(primary)), dotVariant(std::move(dotVariant)) {}
  GoalStates(GoalStates&&) = default;
  GoalStates& operator=(GoalStates&&) = default;
  ~GoalStates();
};

struct ResolvedEditAction {
  VimCore::ResolvedDeleteRange deleteEffect;
  std::optional<VimCore::ResolvedDeleteRange> changeEffect;
  bool deleteEffectValid = true;
  bool changeEffectValid = true;

  inline ResolvedEditAction(VimCore::ResolvedDeleteRange deleteEffect,
                     bool deleteEffectValid)
      : deleteEffect(deleteEffect),
        changeEffect(std::nullopt),
        deleteEffectValid(deleteEffectValid),
        changeEffectValid(deleteEffectValid) {}

  inline ResolvedEditAction(VimCore::ResolvedDeleteRange deleteEffect,
                     VimCore::ResolvedDeleteRange changeEffect,
                     bool deleteEffectValid,
                     bool changeEffectValid)
      : deleteEffect(deleteEffect),
        changeEffect(changeEffect),
        deleteEffectValid(deleteEffectValid),
        changeEffectValid(changeEffectValid) {}

  inline const VimCore::ResolvedDeleteRange& effectForChange() const {
    return changeEffect ? *changeEffect : deleteEffect;
  }
};

// tryUseSuffixCache returns hit status and whether the start was capped.
struct SuffixCacheResult {
  bool hit = false;
  bool startCapped = false;
};

}  // export
