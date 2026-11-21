export module vf.Optimizer.Analyze;

import std.compat;
import vf.Boundary.NavBoundary;
import vf.Keyboard.Config;
import vf.Optimizer.DiffPlanner.DiffState;
import vf.Optimizer.OptimizerParamOverrides;
import vf.Optimizer.Result;
import vf.Optimizer.SearchControl;
import vf.Types.CursorPos;
import vf.Types.Lines;
import vf.Types.NavContext;

export {

// Where a captured session began and ended in its whole buffers, and the
// window it ran in. Rows and columns are 0-indexed.
struct SessionEnds {
  int startRow = 0;
  int startCol = 0;
  int endRow = 0;
  int endCol = 0;
  int windowHeight = 0;
  int scrollAmount = 0;
};

// A session cut down to the rows a search needs. Rows are relative to the slice.
struct SearchSlice {
  Lines initialLines;
  Lines goalLines;
  SessionEnds ends;
  bool hasLinesAbove = false;
  bool hasLinesBelow = false;
  // Resolved keys: what ran, after mappings. Bounds the search.
  std::string userSeq;
  // Physical keys: what was pressed. Costs the user.
  std::string userTyped;
};

// `overrides` stays unresolved because analyze applies it to Nav or Composition
// params depending on whether initial == goal.
struct SearchInputs {
  Lines initialLines;
  Lines goalLines;
  CursorPos initialPos{0, 0};
  CursorPos goalPos{0, 0};
  NavBoundary boundary;
  NavContext navContext;
  std::string userSeq;
  std::string userTyped;
  OptimizerParamOverrides overrides;
};

struct AnalyzeOutput {
  // Cheapest first, with the empty "no action" sequence removed.
  std::vector<Result> results;
  // The planner's regions the search ran against; empty for pure navigation.
  std::vector<DiffState> diffs;
  double userCost = 0;
};

// Boundary for a sliced buffer. The slice is the whole edit region, so there is
// no horizontal prefix or suffix; only the lines cut off above and below vary.
NavBoundary sliceBoundary(const Lines& lines, bool hasLinesAbove, bool hasLinesBelow);

// Inclusive rows covering both cursors and every changed line, plus `padding`.
std::pair<int, int> computeSearchRegion(
    int startRow, int endRow, const Lines& startLines, const Lines& endLines,
    int padding);

// Rows `region` (from computeSearchRegion) of a session, as one search problem.
SearchSlice sliceSearch(
    const Lines& initialLines, const Lines& goalLines, const SessionEnds& ends,
    std::string_view userSeq, std::string_view userTyped, std::pair<int, int> region);

SearchInputs searchInputs(SearchSlice slice, OptimizerParamOverrides overrides);

AnalyzeOutput analyze(const SearchInputs& in, int resultsCalculated,
                      const Config& config, const SearchControl& control);

}  // export
