export module vf.Journal.Evaluate;

import std.compat;
import vf.Journal.Windows;
import vf.Keyboard.Config;
import vf.Optimizer.OptimizerParamOverrides;

export namespace Journal {

struct EvaluateParams {
  int slicePadding = 0;
  int maxSearchLines = 0;
  int resultsCalculated = 0;
  // 0 runs to the natural caps.
  int deadlineMs = 0;
  OptimizerParamOverrides overrides;
};

enum class Outcome : uint8_t {
  Scored,
  TooLarge,     // the changed region is taller than maxSearchLines
  NoResult,     // the search found no sequence
  Unsupported,  // a line holds a newline or NUL byte
};

// How one window's keys compare with the best sequence found for the same
// change. Costs are meaningful only when `outcome` is Scored.
struct Sample {
  Outcome outcome = Outcome::Scored;
  double userCost = 0;
  double bestCost = 0;
  // The text is the same at both ends.
  bool navOnly = false;
  // The text and the cursor are the same at both ends, so the best sequence is
  // none at all.
  bool netZero = false;
  bool deadlineHit = false;
  double analyzeMs = 0;
};

// Runs the same search a session over the window would have.
Sample evaluate(const Window& window, const EvaluateParams& params, const Config& config);

}  // namespace Journal
