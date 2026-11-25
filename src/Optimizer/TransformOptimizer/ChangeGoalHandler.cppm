export module vf.Optimizer.TransformOptimizer.ChangeGoalHandler;

import std.compat;
export import vf.Optimizer.TransformOptimizer.TransformOptimizerParams;
export import vf.Optimizer.TransformOptimizer.TransformResult;
export import vf.Optimizer.SearchStats;
export import vf.Boundary.TransformBoundary;
export import vf.Effort.RunningEffort;
export import vf.Keyboard.Config;
export import vf.Keyboard.KeyedSequence;
export import vf.Optimizer.Result;
export import vf.Optimizer.SequenceBinding;
export import vf.Optimizer.TransformOptimizer.TransformState;
export import vf.Optimizer.TransformOptimizer.GoalHandlerTypes;
export import vf.Optimizer.TransformOptimizer.SuffixCache;
export import vf.Types.CharLineRange;
export import vf.Types.CharRange;
export import vf.Types.CursorPos;
export import vf.Types.LineCharRange;
export import vf.Types.LineRange;
export import vf.Types.Lines;

export {


struct ChangeGoalHandler {
  const Lines& effectiveLines;
  int leftColOffset;
  double effortWeight;
  const Config& config;
  const Lines& initialLines;
  const Lines& goalLines;
  const std::string& pre;
  const std::string& suf;
  const std::string& preSuf;

  KeyedSequence typed;
  RunningEffort typedEffort;
  SuffixCacheMap suffixCache;
  int cacheHits = 0;
  int cachePopulations = 0;

  IndentOptions indent;
  std::string_view goalFirstIndent;
  KeyedSequence typedAfterIndent;
  RunningEffort afterIndentEffort;

  ChangeGoalHandler(const Lines& effectiveLines, int leftColOffset, int rightColOffset,
                    double effortWeight, const Config& config,
                    const TransformBoundary& transformBoundary, const Lines& initialLines,
                    const Lines& goalLines, const std::string& pre,
                    const std::string& suf, const std::string& preSuf, const IndentOptions& indent);

  bool isGoalReached(const Lines& lines) const;
  bool isGoalReached(const TransformEditorState& state) const;

  SuffixCacheHit findCachedSuffix(const TransformState& s);

  // Call when `goal` pops. A goal's path is only worth remembering once the
  // search has ranked it first; see dev/optimizer/transform-optimizer.md.
  void cacheSuffixesOfGoal(const TransformState& goal);

  TransformResult finalize(std::vector<std::vector<Result>>&& resultsByStart,
                      const Lines& initialLines, const Lines& goalLines,
                      const TransformOptimizerParams& params,
                      int bufferBeginLine, int bufferBeginCol, CursorPos goalPos,
                      TransformSearchStats stats,
                      std::vector<std::vector<CursorPos>>&& resultGoalPositions);

  Result resultFromClearedGoal(const TransformState& base) const;

  // Goal emission methods — return states for the dispatcher to emit.
  // Only called in change mode (not pure deletion).
  GoalStates onDeletionGoal(const TransformEditorState& afterDel, const TransformState& base,
                            const SequenceBinding& sourceCmd, const TransformEditorState* repeatLanding);

  GoalStates onLinewiseGoal(const TransformEditorState& afterDel, const TransformState& base,
                            LineRange range, const SequenceBinding& sourceCmd,
                            const TransformEditorState* repeatLanding);

  GoalStates onJoinGoal(const TransformEditorState& afterJn, const TransformState& base,
                        const SequenceBinding& sourceCmd);

  // Stateless command-form helpers — exposed so depth-1 frontiers
  // (TransformFrontier::enumerateDepth1DeletionStructurals) can convert
  // a deletion enumerated by TransformExplorer into its change-mode
  // analog without re-running the goal handler. `deleteToChangeChar`
  // covers `D`→`C`, `x`→`s`, `X`→`hs`, `dw`→`dwi`, generic `dX`→`cX`.
  // `deleteToChangeLine` covers `dd`→`cc` (or `0C` under autoindent
  // with leading whitespace) and counted variants.
  static KeyedSequence deleteToChangeChar(const SequenceBinding& sourceCmd);
  static KeyedSequence deleteToChangeLine(const SequenceBinding& sourceCmd,
                                          std::string_view lineContent, bool autoindent);

  static void enterAndCollapseClearedShell(KeyedSequence& keys, const Lines& lines, CursorPos pos);

  struct SuffixCommandInfo {
    bool isDotRepeat = false;
    bool updatesDotRepeat = false;
  };

private:
  struct KeyedSegment {
    const KeyedSequence& sequence;
    const RunningEffort& effort;
  };

  struct TypedGoalVariants {
    KeyedSegment normalTyped;
    KeyedSegment dotTyped;
  };

  bool isDotRepeat(const TransformState& base, const SequenceBinding& sourceCmd) const;

  // Static helpers
  static void appendOptionalCount(KeyedSequence& out, int count, const KeyedSequence& base);
  static KeyedSequence withOptionalCount(int count, const KeyedSequence& base);
  static RunningEffort mergeGoalSuffixEffort(const KeyedSequence& prefix,
                                             const RunningEffort& typedSuffixEffort,
                                             double completionPenalty,
                                             const Config& config);

  static KeyedSequence buildChangePrefix(const SequenceBinding& sourceCmd,
                                         const Lines& postDelLines, const CursorPos& postDelPos);

  // Suffix cache helpers
  std::vector<RunningEffort> buildRawSuffixEfforts(const SuffixProgram& program,
                                                   const RunningEffort& terminalSuffixEffort,
                                                   int extraPenaltyIndex,
                                                   double extraPenalty) const;
  SuffixValue buildSuffixValueForNextIndex(
      const std::shared_ptr<const SuffixProgram>& suffixProgram,
      const std::vector<SuffixCommandInfo>& commandInfos,
      const std::vector<RunningEffort>& rawSuffixEfforts,
      int nextIndex,
      const DotRepeat& lastEdit) const;

  // Core goal emission — returns states instead of calling back.
  GoalStates emitEditGoal(const TransformEditorState& postCompletionState,
                          const TransformState& base,
                          const SequenceBinding& sourceCmd,
                          const KeyedSequence& goalCompletionCmd,
                          const TypedGoalVariants& typedVariants,
                          const TransformEditorState* repeatLanding);

  static CursorPos seedPositionForStart(int startIndex, const Lines& initialLines, int leftColOffset);
};

}  // export
