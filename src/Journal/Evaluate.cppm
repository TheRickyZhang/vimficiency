export module vf.Journal.Evaluate;

import std.compat;
import vf.Journal.Windows;
import vf.Keyboard.Config;
import vf.Optimizer.Analyze;
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
  Unbeaten,     // no modeled sequence comes within exploreFactor × the user's effort
};

// How one window's keys compare with the best sequence found for the same
// change. Costs are meaningful only when `outcome` is Scored.
struct Sample {
  Outcome outcome = Outcome::Scored;
  double userCost = 0;
  double bestCost = 0;
  std::string bestSeq;
  // The text is the same at both ends.
  bool navOnly = false;
  // The text and the cursor are the same at both ends, so the best sequence is
  // none at all.
  bool netZero = false;
  bool deadlineHit = false;
  double analyzeMs = 0;
  int querySize = 0;
};

struct WindowSlice {
  // Scored, TooLarge or Unsupported; inputs and size are filled only when Scored.
  Outcome outcome = Outcome::Scored;
  SearchInputs inputs;
  int querySize = 0;
};

// The window cut down to the search a session over it would run. Only
// `slicePadding`, `maxSearchLines` and `overrides` of `params` are read.
WindowSlice sliceWindow(const Window& window, const EvaluateParams& params);

// Runs the same search a session over the window would have.
Sample evaluate(const Window& window, const EvaluateParams& params, const Config& config);

}  // namespace Journal
