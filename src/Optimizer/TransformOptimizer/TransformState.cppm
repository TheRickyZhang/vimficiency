export module vf.Optimizer.TransformOptimizer.TransformState;

import std.compat;
import vf.Utils.HashCombine;
export import vf.Keyboard.Config;
export import vf.Effort.RunningEffort;
export import vf.Interpreter.EditInterpreter;
export import vf.Optimizer.SequenceBinding;
export import vf.Types.CharLineRange;
export import vf.Types.LineCharRange;
export import vf.Types.LineRange;
export import vf.Types.Mode;
export import vf.Types.CursorPos;
export import vf.Types.CharRange;
export import vf.Types.Lines;
export import vf.VimCore.VimCore;
export import vf.VimCore.VimEditUtils;

export {

// =============================================================================
// TransformStateKey - for visited state tracking in A* search
// =============================================================================
//
// `targetCol` (Vim's curswant — the column the user wanted before vertical
// motion) is *deliberately omitted* from this key. See CompositionStateKey
// and SuffixKey for parallel omissions; the invariant below is shared.
//
// The invariant. No two reachable states share the same
// (linesHash, lineCount, line, col, mode, startIndex) tuple while differing
// in `targetCol`. This holds today because every search arm either:
//   (a) re-anchors the cursor through an operator immediately after vertical
//       motion (which sets targetCol == col), or
//   (b) never lets the cursor sit at a clamped column (col < targetCol).
// The omission keeps the key compact and the visited set well-behaved.
//
// Trip wires — adding any of these makes targetCol load-bearing, and the
// key (and its hash) must be widened to include it:
//
//   1. A search arm that emits curswant-preserving motion (j/k/gg/G/<C-d>/
//      <C-u>/<C-f>/<C-b>) and then yields exploration at a state immediately
//      after the motion — exposing a column where col < targetCol — *without*
//      re-anchoring through an operator. Today the prefix- and suffix-
//      boundary-escape paths in TransformOptimizer.cpp emit j/k with
//      `pos.targetCol` preserved, but they always continue into an operator-
//      anchored state, so the invariant holds. Candidates that would break
//      it: a new "vertical scan to find a paragraph boundary" arm, or any
//      planned-edit kind that walks vertically between fenceposts.
//
//   2. SuffixCache replaying a stored suffix that starts (or contains) a
//      curswant-preserving motion. Today suffixes are dominated by <Esc> +
//      insert-text patterns where the column is fully determined by the
//      suffix itself, so caller `targetCol` is irrelevant. If a stored
//      suffix ever begins with j/k/gg/G/<C-d>/<C-u>/<C-f>/<C-b> — or
//      includes one mid-way — the cache key omits a value the replay
//      depends on. Symptom: replay column diverges from cache-promised
//      column on a hit.
//
//   3. CompositionOptimizer planned edits that walk vertically across
//      width-varying lines. Today no planned-edit kind does this; if one
//      is added, CompositionStateKey needs targetCol too.
//
//   4. Allowing the cursor to sit at a clamped column (col < targetCol)
//      mid-search without an operator re-anchor. This is the general form
//      of #1-#3; any change of that shape invalidates the invariant.
//
// Where the bug would surface first. The property suite in tests/Property/
// (especially Verify_NavOptimizerResults and Verify_TransformOptimizerResults)
// generates prose-like buffers with varying line widths — the soil this
// kind of divergence grows in. Tabular fixtures rarely expose it because
// all lines are similar width and clamping rarely happens. If you add a
// trip wire above and the affected fuzz property starts failing with two
// paths producing different columns from "identical" states, this is why.

struct TransformStateKey {
  uint64_t linesHash;
  int lineCount;
  int line;
  int col;
  Mode mode;
  int startIndex;  // Include startIndex so each starting position has independent search

  inline TransformStateKey(uint64_t lh, int lc, CursorPos p, Mode m, int idx)
      : linesHash(lh), lineCount(lc), line(p.line), col(p.col), mode(m),
        startIndex(idx) {}

  inline bool operator==(const TransformStateKey& other) const {
    return linesHash == other.linesHash && lineCount == other.lineCount
        && line == other.line && col == other.col
        && mode == other.mode && startIndex == other.startIndex;
  }
};

struct TransformStateKeyHash {
  inline size_t operator()(const TransformStateKey& k) const {
    size_t h = k.linesHash;
    hashCombine(h, k.lineCount);
    hashCombine(h, k.line);
    hashCombine(h, k.col);
    hashCombine(h, static_cast<int>(k.mode));
    hashCombine(h, k.startIndex);
    return h;
  }
};

// Editor-only snapshot: Vim buffer, cursor, mode, and cached content hash.
// Search path, effort, and queue cost are deliberately kept out.
class TransformEditorState {
  friend class TransformSimulator;

  Lines lines_;
  CursorPos pos_;
  Mode mode_ = Mode::Normal;
  uint64_t linesHash_ = 0;

  inline void refreshHash() { linesHash_ = hashLines(lines_); }

public:
  inline TransformEditorState(Lines lines, CursorPos pos, Mode mode = Mode::Normal)
      : lines_(std::move(lines)), pos_(pos), mode_(mode), linesHash_(hashLines(lines_)) {}

  inline const Lines& getLines() const { return lines_; }
  inline CursorPos getPos() const { return pos_; }
  inline Mode getMode() const { return mode_; }
  inline uint64_t getLinesHash() const { return linesHash_; }
};

struct TransformPathStep {
  std::shared_ptr<const TransformPathStep> prev;
  uint64_t linesHashAfter = 0;
  int lineCountAfter = 0;
  CursorPos posAfter;
  Mode modeAfter = Mode::Normal;
  DotRepeat lastEditAfter;
  std::string command;
  bool isDotRepeat = false;
  bool updatesDotRepeat = false;

  inline TransformPathStep(std::shared_ptr<const TransformPathStep> prev,
                    const TransformEditorState& editorAfter,
                    DotRepeat lastEditAfter,
                    std::string command,
                    bool isDotRepeat,
                    bool updatesDotRepeat)
      : prev(std::move(prev)),
        linesHashAfter(editorAfter.getLinesHash()),
        lineCountAfter(static_cast<int>(editorAfter.getLines().size())),
        posAfter(editorAfter.getPos()),
        modeAfter(editorAfter.getMode()),
        lastEditAfter(std::move(lastEditAfter)),
        command(std::move(command)),
        isDotRepeat(isDotRepeat),
        updatesDotRepeat(updatesDotRepeat) {}
};

// Pure editor mutations used by optimizer exploration and replay verification.
class TransformSimulator {
public:
  [[nodiscard]] inline static TransformEditorState withPos(
      const TransformEditorState& state, CursorPos newPos) {
    TransformEditorState newState = state;
    newState.pos_ = newPos;
    return newState;
  }

  [[nodiscard]] inline static TransformEditorState afterDeletion(
      const TransformEditorState& state, const CharRange& range,
      VimCore::LineDeleteContext context = {}) {
    TransformEditorState newState = state;
    VimCore::deleteRangeAndUpdatePos(
        newState.lines_, range, newState.pos_, Mode::Normal, context);
    newState.refreshHash();
    return newState;
  }

  [[nodiscard]] inline static TransformEditorState afterLinewiseDeletion(
    const TransformEditorState& state, int line,
    VimCore::LineDeleteContext context = {}) {
    TransformEditorState newState = state;
    VimCore::deleteLineRangeAndUpdatePos(
        newState.lines_, LineRange(line, line + 1), newState.pos_, context);
    newState.refreshHash();
    return newState;
  }

  [[nodiscard]] inline static TransformEditorState afterMultiLinewiseDeletion(
      const TransformEditorState& state, LineRange range,
      VimCore::LineDeleteContext context = {}) {
    TransformEditorState newState = state;
    VimCore::deleteLineRangeAndUpdatePos(newState.lines_, range, newState.pos_, context);
    newState.refreshHash();
    return newState;
  }

  [[nodiscard]] inline static TransformEditorState afterResolvedDeletion(
      const TransformEditorState& state,
      const VimCore::ResolvedDeleteRange& resolved,
      VimCore::LineDeleteContext context = {}) {
    TransformEditorState newState = state;
    VimCore::deleteResolvedRangeAndUpdatePos(
        newState.lines_, resolved, newState.pos_, Mode::Normal, context);
    newState.refreshHash();
    return newState;
  }

  [[nodiscard]] inline static TransformEditorState afterResolvedChangeDeletion(
      const TransformEditorState& state,
      const VimCore::ResolvedDeleteRange& resolved,
      VimCore::LineDeleteContext context = {}) {
    TransformEditorState newState = state;
    VimCore::deleteResolvedRangeAndUpdatePos(
        newState.lines_, resolved, newState.pos_, Mode::Insert, context);
    newState.refreshHash();
    return newState;
  }

  [[nodiscard]] inline static TransformEditorState afterJoin(
      const TransformEditorState& state, bool addSpace) {
    TransformEditorState newState = state;
    VimCore::joinLineRange(newState.lines_, newState.pos_, 2, addSpace);
    newState.refreshHash();
    return newState;
  }

  [[nodiscard]] inline static TransformEditorState afterMultiJoin(
      const TransformEditorState& state, int count, bool addSpace) {
    TransformEditorState newState = state;
    int lineCount = std::max(count, 2);
    if (lineCount == 2) {
      VimCore::joinLineRange(newState.lines_, newState.pos_, 2, addSpace);
    } else {
      VimCore::joinLineRange(
          newState.lines_, newState.pos_, lineCount, addSpace);
    }
    newState.refreshHash();
    return newState;
  }
};

// =============================================================================
// TransformState - A* search state for transform optimization
// =============================================================================

// Queue-ready search state. It wraps a TransformEditorState with sequence,
// effort, cost, and dot-repeat metadata; callers advance it through
// TransformStateFactory so those fields stay synchronized.
class TransformState {
  friend class TransformStateFactory;

  TransformEditorState editor;
  int startIndex;                 // Which starting position this search is for

  std::string seq_{};             // Sequence of operations taken
  DotRepeat lastEdit_;            // Last edit for dot repeat (not in TransformStateKey)
  std::shared_ptr<const TransformPathStep> pathTail_;
  RunningEffort runningEffort{};  // Typing effort tracker (internal)
  double effort_ = 0.0;           // Cached effort value
  double cost_ = 0.0;             // Priority = effort + heuristic

  inline TransformState(TransformEditorState editor, int startIndex, double initialCost)
      : editor(std::move(editor)), startIndex(startIndex), cost_(initialCost) {}

public:
  // For priority queue ordering (min-heap) - default uses cost (A*)
  inline bool operator>(const TransformState& other) const { return cost_ > other.cost_; }
  inline bool operator<(const TransformState& other) const { return cost_ < other.cost_; }

  // -----------------------------------------------------------------------------
  // Getters
  // -----------------------------------------------------------------------------
  inline const TransformEditorState& getEditorState() const { return editor; }
  inline const Lines& getLines() const { return editor.getLines(); }
  inline CursorPos getPos() const { return editor.getPos(); }
  inline Mode getMode() const { return editor.getMode(); }
  inline int getStartIndex() const { return startIndex; }
  inline size_t getLinesHash() const { return editor.getLinesHash(); }

  inline TransformStateKey getKey() const {
    return TransformStateKey(
        editor.getLinesHash(), static_cast<int>(editor.getLines().size()),
        editor.getPos(), editor.getMode(), startIndex);
  }
  inline double getEffort() const { return effort_; }
  inline double getCost() const { return cost_; }
  inline const std::string& getSeq() const { return seq_; }
  inline const DotRepeat& getLastEdit() const { return lastEdit_; }
  inline bool hasLastEdit() const { return !lastEdit_.empty(); }
  inline const RunningEffort& getRunningEffort() const { return runningEffort; }
  inline std::shared_ptr<const TransformPathStep> getPathTail() const { return pathTail_; }

  inline friend std::ostream& operator<<(std::ostream& os, const TransformState& state) {
    os << state.seq_ << " (cost=" << state.cost_ << ")";
    return os;
  }
};

// Couples an editor snapshot with command effort and heuristic cost to create
// queue-ready TransformState values.
class TransformStateFactory {
  const Config& config;
  double effortWeight;

  inline static void recordPathStep(TransformState& state,
                             const TransformState& base,
                             std::string command,
                             bool updatesDotRepeat) {
    bool isDotRepeat = command == ".";
    state.pathTail_ = std::make_shared<TransformPathStep>(
        base.pathTail_, state.editor, state.lastEdit_,
        std::move(command), isDotRepeat, updatesDotRepeat);
  }

  inline void appendCommand(TransformState& state, std::string_view cmd,
                     const RunningEffort& precomputed,
                     double heuristicCost) const {
    state.seq_ += cmd;
    state.effort_ = state.runningEffort.appendFrom(precomputed, config);
    state.cost_ = effortWeight * state.effort_ + heuristicCost;
  }

  inline void appendCountedCommand(TransformState& state, int count,
                            std::string_view baseCmd,
                            const RunningEffort& precomputed,
                            double heuristicCost) const {
    if (count > 0) state.seq_ += std::to_string(count);
    state.seq_ += baseCmd;
    state.effort_ = state.runningEffort.appendFrom(precomputed, config);
    state.cost_ = effortWeight * state.effort_ + heuristicCost;
  }

public:
  inline TransformStateFactory(const Config& config, double effortWeight)
      : config(config), effortWeight(effortWeight) {}

  [[nodiscard]] inline TransformState initial(
      Lines lines, CursorPos pos, int startIndex, double initialCost) const {
    return initial(TransformEditorState(std::move(lines), pos), startIndex, initialCost);
  }

  [[nodiscard]] inline TransformState initial(
      TransformEditorState editor, int startIndex, double initialCost) const {
    return TransformState(std::move(editor), startIndex, initialCost);
  }

  [[nodiscard]] inline TransformState afterCommand(
      const TransformState& base,
      TransformEditorState editor,
      std::string_view cmd,
      const RunningEffort& precomputed,
      double heuristicCost) const {
    TransformState state = base;
    state.editor = std::move(editor);
    appendCommand(state, cmd, precomputed, heuristicCost);
    recordPathStep(state, base, std::string(cmd), false);
    return state;
  }

  [[nodiscard]] inline TransformState afterEditCommand(
      const TransformState& base,
      TransformEditorState editor,
      int count,
      std::string_view baseCmd,
      const RunningEffort& precomputed,
      double heuristicCost) const {
    TransformState state = base;
    state.editor = std::move(editor);
    appendCountedCommand(state, count, baseCmd, precomputed, heuristicCost);
    state.lastEdit_ = DotRepeat{std::string(baseCmd), count};
    recordPathStep(state, base, countedCommandString(count, baseCmd), true);
    return state;
  }

  [[nodiscard]] inline TransformState afterCommandWithLastEdit(
      const TransformState& base,
      TransformEditorState editor,
      std::string_view cmd,
      const RunningEffort& precomputed,
      double heuristicCost,
      DotRepeat lastEdit) const {
    TransformState state = base;
    state.editor = std::move(editor);
    appendCommand(state, cmd, precomputed, heuristicCost);
    bool hasEdit = !lastEdit.empty();
    state.lastEdit_ = std::move(lastEdit);
    state.pathTail_ = std::make_shared<TransformPathStep>(
        base.pathTail_, state.editor, state.lastEdit_,
        std::string(cmd), cmd == ".", hasEdit);
    return state;
  }
};

}  // export
