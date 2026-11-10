export module vf.Optimizer.CompositionOptimizer.CompositionState;

import std.compat;
export import vf.Keyboard.Config;
export import vf.Keyboard.PhysicalKeys;
export import vf.Effort.RunningEffort;
export import vf.Types.Mode;
export import vf.Types.CursorPos;
export import vf.Types.Sequence;

export {

// =============================================================================
// CompositionStateKey: Unique identifier for deduplication in A* search
// =============================================================================
//
// CompositionState derives lines from editsCompleted, but Vim's sticky column
// can change later linewise edits even when visible line/col are identical.

struct CompositionStateKey {
  int line;
  int col;
  int targetCol;
  Mode mode;
  int editsCompleted;

  inline CompositionStateKey(
      int line, int col, int targetCol, Mode mode, int editsCompleted)
      : line(line), col(col), targetCol(targetCol), mode(mode),
        editsCompleted(editsCompleted) {}

  inline bool operator==(const CompositionStateKey& other) const {
    return line == other.line && col == other.col && targetCol == other.targetCol &&
           mode == other.mode && editsCompleted == other.editsCompleted;
  }
};

struct CompositionStateKeyHash {
  inline size_t operator()(const CompositionStateKey& key) const {
    size_t h = 0;
    combine(h, key.line);
    combine(h, key.col);
    combine(h, key.targetCol);
    combine(h, static_cast<int>(key.mode));
    combine(h, key.editsCompleted);
    return h;
  }

private:
  template <typename T>
  static void combine(size_t& h, const T& v) {
    h ^= std::hash<T>{}(v) + 0x9e3779b9 + (h << 6) + (h >> 2);
  }
};

// Ranked composition search state. CompositionStateFactory is the public
// creation and transition boundary, so cost stays tied to semantic state.
class CompositionState {
  template<class CostFn>
  friend class CompositionStateFactory;

  CursorPos pos;
  Mode mode;

  // Index into edit sequence / line state stored externally, 0 = no edits done
  int editsCompleted;

  // Concatenated command sequence
  Sequence sequence;

  double effort;
  double cost;

  RunningEffort runningEffort;

  // Helper to append to sequence and update effort
  void appendSequence(std::string_view s, const PhysicalKeys& keys, const Config& config);

  inline CompositionState(CursorPos pos, Mode mode, int editsCompleted,
                   RunningEffort runningEffort = RunningEffort(),
                   double effort = 0.0, double cost = 0.0)
      : pos(pos), mode(mode), editsCompleted(editsCompleted),
        runningEffort(std::move(runningEffort)), effort(effort), cost(cost) {}

public:
  // Comparison for priority queue (min-heap by cost)
  inline bool operator<(const CompositionState& other) const { return cost < other.cost; }
  inline bool operator>(const CompositionState& other) const { return cost > other.cost; }

  // Key for deduplication
  inline CompositionStateKey getKey() const {
    return CompositionStateKey(
        pos.line, pos.col, pos.targetCol, mode, editsCompleted);
  }

  // Accessors
  inline CursorPos getPos() const { return pos; }
  inline Mode getMode() const { return mode; }
  inline int getEditsCompleted() const { return editsCompleted; }

  // Get sequence
  inline const Sequence& getSequence() const { return sequence; }

  inline double getEffort() const { return effort; }
  inline double getCost() const { return cost; }
  inline const RunningEffort& getRunningEffort() const { return runningEffort; }

private:
  // Internal: apply edit transition to this state (mutates)
  void applyEditTransitionImpl(const Sequence& editSequence,
                               const CursorPos& newPos, Mode newMode,
                               const Config& config);

  // Internal: apply motion result to this state (mutates)
  void applyNavResultImpl(const Sequence& moveSequence,
                             const CursorPos& newPos, const Config& config);
};

template<class CostFn>
class CompositionStateFactory {
  const Config& config;
  CostFn computeCost;

  inline double costFor(const CompositionState& state) const {
    return computeCost(state);
  }

public:
  inline CompositionStateFactory(const Config& config, CostFn computeCost)
      : config(config), computeCost(std::move(computeCost)) {}

  [[nodiscard]] inline CompositionState initial(
      CursorPos pos, Mode mode = Mode::Normal,
      int editsCompleted = 0,
      RunningEffort runningEffort = RunningEffort()) const {
    double effort = runningEffort.getEffort();
    CompositionState state(pos, mode, editsCompleted, std::move(runningEffort), effort, 0.0);
    state.cost = costFor(state);
    return state;
  }

  [[nodiscard]] inline CompositionState afterEditTransition(
      const CompositionState& state,
      const Sequence& editSequence,
      const CursorPos& newPos, Mode newMode) const {
    CompositionState newState = state;
    newState.applyEditTransitionImpl(editSequence, newPos, newMode, config);
    newState.cost = costFor(newState);
    return newState;
  }

  [[nodiscard]] inline CompositionState afterNavResult(
      const CompositionState& state,
      const Sequence& moveSequence,
      const CursorPos& newPos) const {
    CompositionState newState = state;
    newState.applyNavResultImpl(moveSequence, newPos, config);
    newState.cost = costFor(newState);
    return newState;
  }
};

template<class CostFn>
CompositionStateFactory(const Config&, CostFn) -> CompositionStateFactory<CostFn>;

}  // export
