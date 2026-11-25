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
import vf.Optimizer.SearchStats;
import vf.Types.CursorPos;
import vf.Types.Lines;
import vf.Types.NavContext;

using namespace std;

namespace {

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

}  // namespace

bool isScreenLineMotion(string_view keys) {
  const size_t digits = keys.find_first_not_of("0123456789");
  if (digits == string_view::npos) return false;
  const string_view motion = keys.substr(digits);
  return motion == "gj" || motion == "gk";
}

NavBoundary sliceBoundary(const Lines& lines, bool hasLinesAbove, bool hasLinesBelow) {
  const int lastLine = lines.lastLine();
  return NavBoundary(lines, CursorPos(0, 0),
                     CursorPos(lastLine, lines.getSize(lastLine)),
                     hasLinesAbove, hasLinesBelow);
}

SearchRegion computeSearchRegion(
    int startRow, int endRow, const Lines& startLines, const Lines& endLines,
    int padding, UntouchedRows untouched) {
  startRow = clamp(startRow, 0, startLines.lastLine());
  endRow = clamp(endRow, 0, endLines.lastLine());
  const int initialHeight = startLines.lastLine() + 1;
  const int goalHeight = endLines.lastLine() + 1;
  untouched.include(startRow, initialHeight - startRow - 1);
  untouched.include(endRow, goalHeight - endRow - 1);
  padding = max(0, padding);
  if (&startLines == &endLines) {
    const int end = initialHeight - untouched.below;
    return {untouched.above, end, end, min(untouched.above, padding), min(untouched.below, padding)};
  }

  int begin = 0;
  while (begin < untouched.above && startLines[begin] == endLines[begin]) {
    ++begin;
  }

  int initialEnd = initialHeight;
  int goalEnd = goalHeight;
  while (initialHeight - initialEnd < untouched.below &&
         startLines[initialEnd - 1] == endLines[goalEnd - 1]) {
    --initialEnd;
    --goalEnd;
  }

  return {begin, initialEnd, goalEnd, min(begin, padding), min(initialHeight - initialEnd, padding)};
}

SearchSlice sliceSearch(
    const Lines& initialLines, const Lines& goalLines, const SessionEnds& ends,
    string_view userSeq, string_view userTyped, const SearchRegion& region) {
  const int paddedBegin = region.begin - region.paddingAbove;
  const int paddedInitialEnd = region.initialEnd + region.paddingBelow;
  SearchSlice slice;
  slice.initialLines = initialLines.getLineRange(paddedBegin, paddedInitialEnd);
  slice.goalLines = goalLines.getLineRange(paddedBegin, region.goalEnd + region.paddingBelow);
  slice.ends = ends;
  slice.ends.startRow -= paddedBegin;
  slice.ends.endRow -= paddedBegin;
  slice.hasLinesAbove = paddedBegin > 0;
  slice.hasLinesBelow = paddedInitialEnd <= initialLines.lastLine();
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
  in.indent = slice.ends.indent;
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

    const LandingNavResult navigated =
        opt.optimize(in.initialLines, in.initialPos, in.goalPos, navParams,
                     in.userSeq, in.boundary, in.navContext, control);
    const vector<LandingResult>& navResults = navigated.getResults();
    out.stopReason = navigated.getStats().stopReason();

    // NavOptimizer returns LandingResult (with goalPos); analyze only surfaces
    // (sequence, cost), so down-convert to Result for shared output.
    out.results.reserve(navResults.size());
    for (const LandingResult& r : navResults) {
      out.results.push_back(r);
    }
  } else {
    CompositionOptimizer opt(config);

    CompositionOptimizerParams compParams = in.overrides.resolved(
        CompositionOptimizerParams{}.withMaxResults(resultsCalculated));

    compParams.transform.indent = in.indent;
    const CompositionResult composed = opt.optimize(
        in.initialLines, in.initialPos, in.goalLines, in.goalPos, compParams,
        in.userSeq, in.boundary, in.navContext, control);
    out.results = composed.getResults();
    out.diffs = composed.getDiffs();
    const CompositionSearchStats& stats = composed.getStats();
    out.stopReason = stats.stopReason();
    out.unbeaten = out.results.empty() &&
                   stats.stopReason() == SearchStopReason::FullyExplored &&
                   stats.statesOverEffortCap() > 0;
  }

  out.userCost = getEffort(in.userTyped, config);

  erase_if(out.results, [](const Result& r) { return r.getSequence().empty(); });
  stable_sort(out.results.begin(), out.results.end(), [](const Result& a, const Result& b) {
    return a.getCost() < b.getCost();
  });
  return out;
}
