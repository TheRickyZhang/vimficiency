module vf.Journal.Evaluate;

import std.compat;
import vf.Effort.RunningEffort;
import vf.Journal.Windows;
import vf.Keyboard.Config;
import vf.Optimizer.Analyze;
import vf.Optimizer.OptimizerParamOverrides;
import vf.Optimizer.SearchControl;
import vf.Types.Lines;

using namespace std;

namespace Journal {

namespace {

bool supported(const Lines& lines) {
  return ranges::none_of(lines, [](const Line& line) {
    return line.contains('\n') || line.contains('\0');
  });
}

}  // namespace

Sample evaluate(const Window& window, const EvaluateParams& params, const Config& config) {
  const Lines& initialLines = *window.start.lines;
  const Lines& goalLines = *window.end.lines;
  Sample sample;

  const pair<int, int> region = computeSearchRegion(
      window.start.row, window.end.row, initialLines, goalLines, params.slicePadding);
  if (region.second - region.first > params.maxSearchLines) {
    sample.outcome = Outcome::TooLarge;
    return sample;
  }

  SearchSlice slice = sliceSearch(
      initialLines, goalLines,
      SessionEnds{
          .startRow = window.start.row,
          .startCol = window.start.col,
          .endRow = window.end.row,
          .endCol = window.end.col,
          .windowHeight = window.start.windowHeight,
          .scrollAmount = window.start.scrollAmount,
      },
      window.keys, window.typed, region);
  if (!supported(slice.initialLines) || !supported(slice.goalLines)) {
    sample.outcome = Outcome::Unsupported;
    return sample;
  }

  const SearchInputs in = searchInputs(std::move(slice), params.overrides);
  sample.userCost = getEffort(in.userTyped, config);
  sample.navOnly = in.initialLines == in.goalLines;
  sample.netZero = sample.navOnly && in.initialPos.line == in.goalPos.line &&
                   in.initialPos.col == in.goalPos.col;
  if (sample.netZero) return sample;

  using Clock = SearchControl::Clock;
  const Clock::time_point begun = Clock::now();
  const Clock::time_point deadline = params.deadlineMs > 0
      ? begun + chrono::milliseconds(params.deadlineMs)
      : Clock::time_point::max();
  const SearchControl control(deadline);
  const AnalyzeOutput analyzed = analyze(in, params.resultsCalculated, config, control);
  const Clock::time_point ended = Clock::now();
  sample.analyzeMs = chrono::duration<double, milli>(ended - begun).count();
  sample.deadlineHit = ended >= deadline;

  if (analyzed.results.empty()) {
    sample.outcome = Outcome::NoResult;
    return sample;
  }
  sample.bestCost = analyzed.results.front().getCost();
  return sample;
}

}  // namespace Journal
