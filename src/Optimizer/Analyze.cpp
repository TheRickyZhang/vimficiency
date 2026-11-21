module vf.Optimizer.Analyze;

import std.compat;
import vf.Boundary.NavBoundary;
import vf.Effort.RunningEffort;
import vf.Keyboard.Config;
import vf.Optimizer.CompositionOptimizer.CompositionOptimizer;
import vf.Optimizer.DiffPlanner.DiffState;
import vf.Optimizer.NavOptimizer.NavOptimizer;
import vf.Optimizer.OptimizerParamOverrides;
import vf.Optimizer.Result;
import vf.Optimizer.SearchControl;
import vf.Types.CursorPos;
import vf.Types.Lines;
import vf.Types.NavContext;

using namespace std;

namespace {

pair<int, int> findDiffRange(const Lines& a, const Lines& b) {
  int firstDiff = -1;
  int lastDiff = -1;
  const int maxLen = max(ssize(a), ssize(b));
  for (int i = 0; i < maxLen; ++i) {
    const string* lhs = i < ssize(a) ? &a[i] : nullptr;
    const string* rhs = i < ssize(b) ? &b[i] : nullptr;
    if (!lhs || !rhs || *lhs != *rhs) {
      if (firstDiff < 0) firstDiff = i;
      lastDiff = i;
    }
  }
  return {firstDiff, lastDiff};
}

void replaceAll(string& text, string_view from, string_view to) {
  for (size_t at = text.find(from); at != string::npos; at = text.find(from, at + to.size())) {
    text.replace(at, from.size(), to);
  }
}

// Screen-line motions are not simulated, so they are costed as the line motions
// they equal on unwrapped text.
string approximateScreenMotions(string_view userSeq) {
  string out(userSeq);
  replaceAll(out, "gj", "j");
  replaceAll(out, "gk", "k");
  return out;
}

Lines rows(const Lines& lines, pair<int, int> region) {
  return lines.getLineRange(region.first, min(region.second + 1, static_cast<int>(lines.size())));
}

}  // namespace

NavBoundary sliceBoundary(const Lines& lines, bool hasLinesAbove, bool hasLinesBelow) {
  const int lastLine = lines.lastLine();
  return NavBoundary(lines, CursorPos(0, 0),
                     CursorPos(lastLine, lines.getSize(lastLine)),
                     hasLinesAbove, hasLinesBelow);
}

pair<int, int> computeSearchRegion(
    int startRow, int endRow, const Lines& startLines, const Lines& endLines,
    int padding) {
  int minRow = min(startRow, endRow);
  int maxRow = max(startRow, endRow);
  const auto [firstDiff, lastDiff] = findDiffRange(startLines, endLines);
  if (firstDiff >= 0) {
    minRow = min(minRow, firstDiff);
    maxRow = max(maxRow, lastDiff);
  }

  const int bufferLen = max(ssize(startLines), ssize(endLines));
  const int regionStart = max(0, minRow - padding);
  const int regionEnd = min(bufferLen - 1, maxRow + padding);
  return {regionStart, regionEnd};
}

SearchSlice sliceSearch(
    const Lines& initialLines, const Lines& goalLines, const SessionEnds& ends,
    string_view userSeq, string_view userTyped, pair<int, int> region) {
  SearchSlice slice;
  slice.initialLines = rows(initialLines, region);
  slice.goalLines = rows(goalLines, region);
  slice.ends = ends;
  slice.ends.startRow -= region.first;
  slice.ends.endRow -= region.first;
  slice.hasLinesAbove = region.first > 0;
  slice.hasLinesBelow = region.second < max(ssize(initialLines), ssize(goalLines)) - 1;
  slice.userSeq = approximateScreenMotions(userSeq);
  slice.userTyped = userTyped;
  return slice;
}

SearchInputs searchInputs(SearchSlice slice, OptimizerParamOverrides overrides) {
  SearchInputs in;
  in.initialLines = std::move(slice.initialLines);
  in.goalLines = std::move(slice.goalLines);
  in.userSeq = std::move(slice.userSeq);
  in.userTyped = std::move(slice.userTyped);
  in.overrides = std::move(overrides);
  // Neovim can capture an insert-mode / 'virtualedit' cursor one past
  // end-of-line, or (for recall windows) a row outside the sliced buffer. The
  // optimizer requires normal-mode-valid endpoints, so clamp here rather than
  // aborting on the downstream CHECK.
  in.initialPos = in.initialLines.clamp(CursorPos(slice.ends.startRow, slice.ends.startCol));
  in.goalPos = in.goalLines.clamp(CursorPos(slice.ends.endRow, slice.ends.endCol));
  in.boundary = sliceBoundary(in.initialLines, slice.hasLinesAbove, slice.hasLinesBelow);
  in.navContext = NavContext(slice.ends.windowHeight, slice.ends.scrollAmount);
  return in;
}

AnalyzeOutput analyze(const SearchInputs& in, int resultsCalculated,
                      const Config& config, const SearchControl& control) {
  AnalyzeOutput out;

  if (in.initialLines == in.goalLines) {
    NavOptimizer opt(config);

    const NavOptimizerParams navParams =
        in.overrides.resolved(NavOptimizerParams{}
                                  .withMaxResults(resultsCalculated)
                                  .withMaxResultsPerEndPos(resultsCalculated));

    const vector<LandingResult> navResults =
        opt.optimize(in.initialLines, in.initialPos, in.goalPos, navParams,
                     in.userSeq, in.boundary, in.navContext, control)
            .getResults();

    // NavOptimizer returns LandingResult (with goalPos); analyze only surfaces
    // (sequence, cost), so down-convert to Result for shared output.
    out.results.reserve(navResults.size());
    for (const LandingResult& r : navResults) {
      out.results.emplace_back(r.getSequence(), r.getCost());
    }
  } else {
    CompositionOptimizer opt(config);

    const CompositionOptimizerParams compParams = in.overrides.resolved(
        CompositionOptimizerParams{}.withMaxResults(resultsCalculated));

    const CompositionResult composed = opt.optimize(
        in.initialLines, in.initialPos, in.goalLines, in.goalPos, compParams,
        in.userSeq, in.boundary, in.navContext, control);
    out.results = composed.getResults();
    out.diffs = composed.getDiffs();
  }

  out.userCost = getEffort(in.userTyped, config);

  erase_if(out.results, [](const Result& r) { return r.getSequence().empty(); });
  sort(out.results.begin(), out.results.end(), [](const Result& a, const Result& b) {
    return a.getCost() < b.getCost();
  });
  return out;
}
