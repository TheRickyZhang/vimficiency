#include "LuaExports/Api.h"
import std.compat;
import vf.LuaExports.Common;
import vf.LuaExports.Payload;
import vf.Optimizer.DiffPlanner.VimDiff;
import vf.Optimizer.DiffPlanner.DiffAlgorithm;
import vf.Optimizer.DiffPlanner.DiffState;
import vf.Optimizer.DiffPlanner.MyersDiff;
import vf.Types.Lines;

using namespace std;
using namespace VF::LuaExports;

namespace {

// Standalone planner run for results that lack stored diffs (files saved
// before the analyze payload carried them). Live sessions take their regions
// from the analyze payload — the partition the search actually planned
// against; this run uses default cost options, so it can differ from a tuned
// optimizer config.
VF::LuaExports::Expected<string> computeDiffsImpl(
    const char *encoded_initial_lines,
    size_t encoded_initial_lines_len,
    const char *encoded_goal_lines,
    size_t encoded_goal_lines_len,
    int diff_algorithm) {
  auto initialText = requiredBytes(
      encoded_initial_lines, encoded_initial_lines_len, "encoded_initial_lines");
  if (!initialText) return unexpected(initialText.error());
  auto goalText = requiredBytes(
      encoded_goal_lines, encoded_goal_lines_len, "encoded_goal_lines");
  if (!goalText) return unexpected(goalText.error());

  auto initialLinesResult = payload::decodeLineArray(*initialText);
  if (!initialLinesResult) return unexpected(initialLinesResult.error());
  auto goalLinesResult = payload::decodeLineArray(*goalText);
  if (!goalLinesResult) return unexpected(goalLinesResult.error());
  const Lines initialLines = std::move(*initialLinesResult);
  const Lines goalLines = std::move(*goalLinesResult);

  // Match the algorithm the CompositionOptimizer used so the view's highlight
  // is the same breakdown (see CompositionSearchContext.cpp).
  vector<DiffState> diffs;
  if (diff_algorithm == DiffAlgorithm::VimDiff) {
    diffs = VimDiff::calculate(initialLines, goalLines, g_config_internal).diffs;
  } else if (diff_algorithm == DiffAlgorithm::MyersDiff) {
    diffs = MyersDiff::calculate(initialLines, goalLines);
  }
  return payload::encodeDiffRegions(diffs);
}

}  // namespace

extern "C" {

VFByteSlice vf_compute_diffs(
    const char *encoded_initial_lines,
    size_t encoded_initial_lines_len,
    const char *encoded_goal_lines,
    size_t encoded_goal_lines_len,
    int diff_algorithm) {
  static string result_storage;
  return storeBytes(
      result_storage,
      computeDiffsImpl(
          encoded_initial_lines,
          encoded_initial_lines_len,
          encoded_goal_lines,
          encoded_goal_lines_len,
          diff_algorithm));
}

}
