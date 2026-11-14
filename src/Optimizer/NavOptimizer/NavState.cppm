export module vf.Optimizer.NavOptimizer.NavState;

import std.compat;
export import vf.Effort.RunningEffort;
export import vf.Keyboard.Config;
export import vf.Keyboard.KeyedSequence;
export import vf.Types.CursorPos;
export import vf.Types.Mode;
export import vf.Types.Sequence;

export {

// Ranked navigation search state. NavStateSpace is the only construction and
// transition path, so effort and cost are updated together.
class NavState {
  template<class CostFn>
  friend class NavStateSpace;

  // Visible, core editor state
  CursorPos pos;

  // Progress so far
  Sequence movementSequence;

  // Necessary for ranking states
  double effort;
  double cost;

  // Internal mechanism
  RunningEffort runningEffort;

  inline NavState(CursorPos pos, RunningEffort runningEffort, double effort, double cost)
    : pos(pos), effort(effort), cost(cost),
      runningEffort(std::move(runningEffort)) {
  }

public:
  inline bool operator<(const NavState& other) const {
    return cost < other.cost;
  }
  inline bool operator>(const NavState& other) const {
    return cost > other.cost;
  }

  inline Pos getKey() const {
    return Pos(pos.line, pos.col);
  }
  inline CursorPos getPos()                       const { return pos; }
  inline const Sequence& getSequence()            const { return movementSequence; }
  inline double getEffort()                      const { return effort; }
  inline double getCost()                        const { return cost; }
  inline const RunningEffort& getRunningEffort() const { return runningEffort; }

private:
  // Internal: apply motion to this state (mutates)
  void applyMotionImpl(const KeyedSequence& ks, const RunningEffort& precomputed,
                       CursorPos endpoint, const Config& config);
  void applyCountedMotionImpl(const KeyedSequence& baseMotion, int cnt,
                              CursorPos endpoint, const Config& config,
                              double extraPenalty);
  void applyFMotionImpl(const KeyedSequence& fMotion, int newCol, const Config& config);
};

template<class CostFn>
class NavStateSpace {
  const Config& config;
  CostFn computeCost;

  inline double costFor(const NavState& state) const {
    return computeCost(state.getPos(), state.getEffort());
  }

public:
  inline NavStateSpace(const Config& config, CostFn computeCost)
      : config(config), computeCost(std::move(computeCost)) {}

  [[nodiscard]] inline NavState initial(CursorPos pos) const {
    return initial(pos, RunningEffort());
  }

  [[nodiscard]] inline NavState initial(CursorPos pos, RunningEffort runningEffort) const {
    double effort = runningEffort.getEffort();
    return NavState(pos, std::move(runningEffort), effort, computeCost(pos, effort));
  }

  [[nodiscard]] inline NavState afterMotion(
      const NavState& state, const KeyedSequence& ks,
      const RunningEffort& precomputed, CursorPos endpoint) const {
    NavState newState = state;
    newState.applyMotionImpl(ks, precomputed, endpoint, config);
    newState.cost = costFor(newState);
    return newState;
  }

  [[nodiscard]] inline NavState afterCountedMotion(
      const NavState& state, const KeyedSequence& baseMotion, int cnt,
      CursorPos endpoint, double extraPenalty) const {
    NavState newState = state;
    newState.applyCountedMotionImpl(baseMotion, cnt, endpoint, config, extraPenalty);
    newState.cost = costFor(newState);
    return newState;
  }

  [[nodiscard]] inline NavState afterFMotion(
      const NavState& state, const KeyedSequence& fMotion, int newCol) const {
    NavState newState = state;
    newState.applyFMotionImpl(fMotion, newCol, config);
    newState.cost = costFor(newState);
    return newState;
  }
};

template<class CostFn>
NavStateSpace(const Config&, CostFn) -> NavStateSpace<CostFn>;

}  // export
