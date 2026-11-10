export module vf.Effort.RunningEffort;

import std.compat;
export import vf.Keyboard.Config;
export import vf.Keyboard.Key;
export import vf.Keyboard.PhysicalKeys;

export {

// -----------------------------------------------------------------------------
// Composable effort model (monoid over key sequences).
//
// Tracks typing cost that can be merged in O(1) via
// RunningEffort::merge(a, b, model), producing the same result as sequentially
// appending all keys of b after a.
//
// Costs are weighed as keys arrive, so every operation on one value must use
// the same Config.
// -----------------------------------------------------------------------------

class RunningEffort {
private:
  int    strokes       = 0;
  double typing_effort = 0.0; // weighted key + bigram cost
  double penalty_      = 0.0; // additive penalty (e.g., cognitive overhead)

  // Boundary keys for monoid composition
  Key    first_key     = Key::None;
  Key    last_key      = Key::None;

  void appendSingle(Key key, const Config &model);

public:
  RunningEffort() = default;
  explicit RunningEffort(const PhysicalKeys& keys, const Config& model);

  inline double getEffort() const { return typing_effort + penalty_; }

  inline void addPenalty(double p) { penalty_ += p; }

  double append(const PhysicalKeys& keys, const Config& model);

  // Merge two effort segments in O(1), pricing the bigram at the seam.
  static RunningEffort merge(const RunningEffort& a, const RunningEffort& b,
                             const Config& model);

  // In-place merge(*this, other). Returns getEffort().
  double appendFrom(const RunningEffort& other, const Config& model);

  inline int getStrokes() const { return strokes; }
  inline double getPenalty() const { return penalty_; }
};

double getEffort(std::string_view seq, const Config &cfg);

}  // export
