// Why does a composition search return nothing? Buckets every no-result case
// by cause, over random buffers and over journal windows.
//   ./build/tests/vimfy_debug --gtest_filter='NoResultCauses.*'
//   VIMFY_JOURNAL_DIR=<dir> ./build/tests/vimfy_debug --gtest_filter='NoResultCauses.Journal'
// The journal test prints counts and region shapes only, never buffer text.

#include <gtest/gtest.h>

import std.compat;
import vf.Journal.Evaluate;
import vf.Journal.Replay;
import vf.Journal.Windows;
import vf.Keyboard.Config;
import vf.Optimizer.Analyze;
import vf.Optimizer.CompositionOptimizer.CompositionOptimizer;
import vf.Optimizer.CompositionOptimizer.CompositionStrategies;
import vf.Optimizer.CompositionOptimizer.PlannedEditArtifacts;
import vf.Optimizer.DiffPlanner.DiffAlgorithm;
import vf.Optimizer.SearchControl;
import vf.Utils.PrettyText;
import vf.VimCore.VimEditUtils;
import vf.tests.Utils.NeovimOracle;
import vf.tests.Utils.OracleReplay;

using namespace std;
namespace fs = std::filesystem;

namespace {

struct Case {
  Lines initial;
  CursorPos initialPos;
  Lines goal;
  CursorPos goalPos;
  string userSeq;
  NavBoundary boundary;
  NavContext navContext;
};

int envInt(const char* name, int fallback) {
  const char* v = getenv(name);
  return v ? atoi(v) : fallback;
}

// Cursor positions inside [begin, end): a column past a line's end is not one.
int cursorCells(const DiffState& d, const Lines& before) {
  int cells = 0;
  for (int line = d.beginPos.line; line <= d.endPos.line && line <= before.lastLine(); line++) {
    const int size = static_cast<int>(before[line].size());
    const int from = line == d.beginPos.line ? d.beginPos.col : 0;
    if (line == d.endPos.line) {
      cells += max(0, min(d.endPos.col, size) - from);
    } else if (size == 0) {
      cells += from == 0 ? 1 : 0;
    } else {
      cells += max(0, size - from);
    }
  }
  return cells;
}

string shape(const DiffState& d) {
  auto side = [](const string& s) {
    const auto newlines = ranges::count(s, '\n');
    return to_string(s.size() - newlines) + " chars + " + to_string(newlines) + " newlines";
  };
  return "delete [" + side(d.deletedText) + "] insert [" + side(d.insertedText) + "]";
}

struct Study {
  Config config = Config::uniform();
  bool printCases = true;
  int cases = 0, noResult = 0, effortCap = 0;
  int plansWithDeadRegion = 0, deadRegionButSolved = 0;
  map<string, int> causes;
  map<string, int> myersSolves;
  map<string, int> deadShapes;
  map<string, int> printed;

  auto run(const Case& c, const string& userSeq, int algorithm) {
    CompositionOptimizerParams params;
    params.diffAlgorithm = algorithm;
    CompositionOptimizer opt(config);
    return opt.optimize(c.initial, c.initialPos, c.goal, c.goalPos, params, userSeq, c.boundary,
                        c.navContext, SearchControl::NEVER_STOP);
  }

  void add(const Case& c) {
    cases++;
    auto res = run(c, c.userSeq, DiffAlgorithm::VimDiff);
    bool capped = false;
    if (res.getResults().empty() && !c.userSeq.empty()) {
      res = run(c, "", DiffAlgorithm::VimDiff);
      capped = !res.getResults().empty();
    }

    string deadCause, deadShape;
    for (int e = 0; e < res.totalEdits(); e++) {
      const PlannedEditView view = res.plannedEditAt(e);
      if (view.diff.isPureInsertion() || view.diff.isLineBreakReplacement()) continue;
      if (!view.transformResult.startPositions().empty()) continue;
      if (computeJoinPlanForDiff(view.diff, view.preFencepost, CompositionOptimizerParams{}, config)) {
        continue;
      }
      const bool noCell = cursorCells(view.diff, view.preFencepost) == 0;
      deadCause = noCell ? "region with no cursor cell and no join plan"
                         : "region with cursor cells that Transform did not solve (" +
                               to_string(view.transformResult.getStats().stopReason()) + ")";
      deadShape = (noCell ? "no cell: " : "unsolved: ") + shape(view.diff);
      break;
    }
    if (!deadCause.empty()) {
      plansWithDeadRegion++;
      if (!res.getResults().empty()) deadRegionButSolved++;
    }

    if (capped) {
      effortCap++;
      return;
    }
    if (!res.getResults().empty()) return;
    noResult++;

    string cause = deadCause;
    if (cause.empty()) {
      cause = res.totalEdits() == 0 ? "no planned edits"
          : res.totalEdits() > 16 ? "more than 16 planned edits, search declined"
          : "every region has an executor, search found nothing (" +
                to_string(res.getStats().stopReason()) + ")";
    } else {
      deadShapes[deadShape]++;
    }
    causes[cause]++;
    if (!run(c, "", DiffAlgorithm::MyersDiff).getResults().empty()) myersSolves[cause]++;

    if (printCases && printed[cause]++ < 4) {
      cout << "  [" << cause << "]\n     initial=" << c.initial << " at " << c.initialPos
           << "\n     goal=" << c.goal << " at " << c.goalPos << " pops "
           << res.getStats().totalPops() << "\n";
      for (int e = 0; e < res.totalEdits(); e++) {
        const PlannedEditView view = res.plannedEditAt(e);
        cout << "     region " << e << " " << view.diff.beginPos << "-" << view.diff.endPos
             << " " << view.diff << " starts " << view.transformResult.startPositions().size()
             << "\n";
      }
    }
  }

  void report(const char* label) const {
    cout << "== " << label << ": " << cases << " cases, no result " << noResult;
    if (effortCap) cout << " (plus " << effortCap << " cut only by the user-effort cap)";
    cout << "\n   plans holding a region with no executor: " << plansWithDeadRegion
         << " (still solved: " << deadRegionButSolved << ")\n";
    for (const auto& [cause, n] : causes) {
      const auto it = myersSolves.find(cause);
      cout << "   " << setw(5) << n << "  " << cause << "; Myers plan solves "
           << (it == myersSolves.end() ? 0 : it->second) << "\n";
    }
    for (const auto& [s, n] : deadShapes) cout << "      " << setw(5) << n << "  " << s << "\n";
  }
};

// Same shape as CompositionOptimizerGeneratedPropertyTest's domain.
void mutate(Lines& lines, mt19937& rng, int maxCol, const function<string(int)>& text) {
  auto pick = [&](int lo, int hi) { return uniform_int_distribution<int>(lo, hi)(rng); };
  const int kind = pick(0, 4);
  const int size = static_cast<int>(lines.size());
  const int line = pick(0, size - 1);
  if ((kind == 2 || kind == 4) && size < 2) return mutate(lines, rng, maxCol, text);
  switch (kind) {
    case 0: {
      string& s = lines[line];
      const int begin = clamp(pick(0, maxCol), 0, static_cast<int>(s.size()));
      const int end = clamp(pick(0, maxCol), begin, static_cast<int>(s.size()));
      s.replace(begin, end - begin, text(6));
      break;
    }
    case 1: {
      string& s = lines[line];
      const int col = clamp(pick(0, maxCol), 0, static_cast<int>(s.size()));
      string suffix = s.substr(col);
      s.erase(col);
      lines.insert(lines.begin() + line + 1, suffix);
      break;
    }
    case 2: {
      const int at = min(line, size - 2);
      lines[at] += lines[at + 1];
      lines.erase(lines.begin() + at + 1);
      break;
    }
    case 3:
      lines.insert(lines.begin() + pick(0, size), text(6));
      break;
    default:
      lines.erase(lines.begin() + line);
      break;
  }
}

Case finishCase(vector<string> initial, mt19937& rng, int maxCol, const function<string(int)>& text) {
  auto pick = [&](int lo, int hi) { return uniform_int_distribution<int>(lo, hi)(rng); };
  Case c;
  c.initial = Lines(initial);
  c.goal = c.initial;
  for (int i = pick(1, 3); i > 0; i--) mutate(c.goal, rng, maxCol, text);
  c.initialPos = c.initial.cursorFromFlatIndexClamped(pick(0, c.initial.totalPositions() - 1));
  c.goalPos = c.goal.cursorFromFlatIndexClamped(pick(0, c.goal.totalPositions() - 1));
  return c;
}

Case asciiCase(mt19937& rng) {
  auto pick = [&](int lo, int hi) { return uniform_int_distribution<int>(lo, hi)(rng); };
  const function<string(int)> text = [&](int maxLen) {
    string s(pick(0, maxLen), ' ');
    for (char& ch : s) ch = static_cast<char>(pick(32, 126));
    return s;
  };
  vector<string> initial(pick(1, 4));
  for (string& line : initial) line = text(9);
  return finishCase(std::move(initial), rng, 12, text);
}

string words(mt19937& rng, int maxLen) {
  auto pick = [&](int lo, int hi) { return uniform_int_distribution<int>(lo, hi)(rng); };
  string s;
  const int target = pick(0, maxLen);
  while (static_cast<int>(s.size()) < target) {
    if (!s.empty()) s += pick(0, 5) == 0 ? ", " : " ";
    for (int i = pick(1, 6); i > 0; i--) s += static_cast<char>('a' + pick(0, 25));
    if (pick(0, 7) == 0) s += pick(0, 1) ? "." : "();";
  }
  return s;
}

Case proseCase(mt19937& rng) {
  auto pick = [&](int lo, int hi) { return uniform_int_distribution<int>(lo, hi)(rng); };
  const function<string(int)> text = [&](int maxLen) { return words(rng, maxLen); };
  vector<string> initial(pick(3, 8));
  for (string& line : initial) {
    if (pick(0, 5) == 0) continue;
    line = string(2 * pick(0, 2), ' ') + text(30);
  }
  return finishCase(std::move(initial), rng, 30, text);
}

// Indented blocks separated by blank lines, edited with indented text.
Case codeCase(mt19937& rng) {
  auto pick = [&](int lo, int hi) { return uniform_int_distribution<int>(lo, hi)(rng); };
  const function<string(int)> text = [&](int maxLen) {
    if (pick(0, 4) == 0) return string();
    return string(4 * pick(0, 2), ' ') + words(rng, maxLen);
  };
  vector<string> initial(pick(4, 10));
  for (string& line : initial) line = text(24);
  return finishCase(std::move(initial), rng, 24, text);
}

void runRandom(const char* label, Case (*make)(mt19937&), int count) {
  Study study;
  mt19937 rng(20261125);
  for (int i = 0; i < count; i++) {
    Case c = make(rng);
    if (c.initial == c.goal) continue;
    study.add(c);
  }
  study.report(label);
}

TransformResult unrestrictedTransform(const DiffState& diff, const Config& config) {
  const CompositionOptimizerParams params;
  TransformOptimizer optimizer(config);
  const TransformOptimizerParams editParams =
      TransformOptimizerParams(params.transform)
          .withMinCountRepeat(params.minPrefixCount)
          .withMaxCountRepeat(params.maxPrefixCount)
          .withMaxResultsPerStartPos(params.transformMaxResultsPerStartPos);
  if (diff.isPureDeletion()) {
    return optimizer.optimizePureDeletion(diff.deletedLines(), diff.boundary, editParams,
                                          diff.beginPos.line, diff.beginPos.col, diff.beginPos);
  }
  return optimizer.optimizeTransform(diff.deletedLines(), diff.insertedLines(), diff.boundary,
                                     editParams, diff.beginPos.line, diff.beginPos.col,
                                     diff.beginPos);
}

// Replays computeJoinPlanForDiff's single-group path and reports each gate.
void traceJoinPlan(const DiffState& diff, const Lines& before, const Config& config) {
  const Lines deleted = diff.deletedLines();
  const Lines target = diff.insertedLines();
  const int sourceCount = static_cast<int>(deleted.size());
  cout << "       join plan: " << sourceCount << " source lines -> " << target.size()
       << " target lines";
  if (diff.isPureInsertion() || sourceCount <= static_cast<int>(target.size())) {
    cout << "  => none: line count does not drop\n";
    return;
  }
  if (target.size() != 1) {
    cout << "  => multi-group, not traced\n";
    return;
  }
  const string& prefix = diff.boundary.prefix();
  const string& suffix = diff.boundary.suffix();
  const string fullTarget = prefix + target[0] + suffix;
  const bool joinRequired = !prefix.empty() && !suffix.empty();
  cout << (joinRequired ? "  (similarity gate skipped)\n" : "  (similarity gate applies)\n");
  for (const bool addSpace : {true, false}) {
    Lines work = before.getLineRange(diff.beginPos.line, diff.beginPos.line + sourceCount);
    CursorPos pos(0, 0);
    for (int i = 1; i < sourceCount; i++) VimCore::joinLineRange(work, pos, 2, addSpace);
    const string joined = work[0];
    cout << "         " << (addSpace ? "J " : "gJ") << " lands col " << pos.col << " on '"
         << VF::prettify(joined) << "' target '" << VF::prettify(fullTarget) << "'";
    if (joined == fullTarget) {
      cout << "  => exact\n";
      continue;
    }
    TransformOptimizer optimizer(config);
    const CompositionOptimizerParams params;
    const TransformOptimizerParams residualParams =
        TransformOptimizerParams(params.transform)
            .withMinCountRepeat(params.minPrefixCount)
            .withMaxCountRepeat(params.maxPrefixCount);
    const TransformBoundary boundary = TransformBoundary::joinResidualBoundary(true, true);
    const CursorPos goalPos(0, max(0, static_cast<int>(fullTarget.size()) - 1));
    const TransformResult residual = fullTarget.empty()
        ? optimizer.optimizePureDeletion(Lines({joined}), boundary, residualParams, 0, 0, goalPos)
        : optimizer.optimizeTransform(Lines({joined}), Lines({fullTarget}), boundary,
                                      residualParams, 0, 0, goalPos);
    const Result* res = residual.resultAt(0, pos.col);
    cout << "  => residual from col " << pos.col << ": "
         << (res ? VF::prettify(res->getSequence().str()) : string("UNSOLVED")) << " ("
         << residual.startPositions().size() << " starts solved, "
         << to_string(residual.getStats().stopReason()) << ")\n";
  }
}

void probe(const char* label, const Lines& initial, CursorPos initialPos, const Lines& goal,
           CursorPos goalPos) {
  const Config config = Config::uniform();
  CompositionOptimizer opt(config);
  auto res = opt.optimize(initial, initialPos, goal, goalPos, CompositionOptimizerParams{});
  SCOPED_TRACE(label);
  cout << "== " << label << ": " << res.getResults().size() << " results\n";
  NeovimOracle oracle;
  EXPECT_FALSE(res.getResults().empty());
  for (const Result& result : res.getResults()) {
    EXPECT_TRUE(OracleReplay::matches(oracle, initial, initialPos, result.getSequence().str(),
                                      goal, goalPos, ::Mode::Normal, label));
  }
  for (int e = 0; e < res.totalEdits(); e++) {
    const PlannedEditView view = res.plannedEditAt(e);
    const DiffState& d = view.diff;
    cout << "   region " << e << " " << d.beginPos << "-" << d.endPos << " " << d << " prefix '"
         << VF::prettify(d.boundary.prefix()) << "' suffix '"
         << VF::prettify(d.boundary.suffix()) << "' starts "
         << view.transformResult.startPositions().size() << "\n";
    for (CursorPos start : view.transformResult.startPositions()) {
      view.transformResult.forEachEditFrom(start, view.postFencepost,
          [&](const Result& result, CursorPos landing) {
            EXPECT_TRUE(OracleReplay::matches(
                oracle, view.preFencepost, start, result.getSequence().str(),
                view.postFencepost, landing, ::Mode::Normal, label));
          });
    }
    if (d.isPureInsertion() || !view.transformResult.startPositions().empty()) continue;
    const TransformResult open = unrestrictedTransform(d, config);
    cout << "       without the [begin, end) restriction: " << open.startPositions().size()
         << " starts (" << to_string(open.getStats().stopReason()) << ", "
         << open.getStats().nodesExplored() << " nodes)\n";
    for (const CursorPos& sp : open.startPositions()) {
      cout << "         from " << sp << ": "
           << VF::prettify(open.resultAt(sp.line, sp.col)->getSequence().str()) << "\n";
    }
    traceJoinPlan(d, view.preFencepost, config);
  }
}

TEST(NoResultCauses, Probe) {
  probe("newline replaced, same line count", Lines({"", "~", ""}), {2, 0},
        Lines({"", "~~", " "}), {1, 0});
  probe("newline replaced, same line count, text after", Lines({"(", "unU>"}), {1, 3},
        Lines({"(APD", "]}?unU>"}), {1, 0});
  probe("newline replaced by text",
        Lines({"kddsou kiywcr wf(); ndrom jsexk", "    z trohz wuqxv",
               "  b wclryh, ul onibhn();", "  ldwiqe"}), {0, 30},
        Lines({"kddsou kiywcr wf(); ndrom jsexk", "    z trohz wuqxv",
               "  b wclryh, ul onibhn();kpnfos  ldwiqe"}), {0, 26});
  probe("newline replaced by text, short", Lines({"hwn", "", "        mvdi zdjbe yng. pxd", ""}),
        {2, 24}, Lines({"hwn    xqko", "        mvdi zdjbe yng. pxd", ""}), {1, 26});
  probe("line dropped and neighbours joined",
        Lines({"iesamo", "    zvmtj fkvte labzs f yz yp", "    si. erz", "  sls v, jizob"}),
        {1, 5}, Lines({"iesamo  sls v, jizob"}), {0, 2});
  probe("line dropped and neighbours joined, blank tail",
        Lines({"  vlgw nryx, nhsecw", "    wbqhwb snfm", "    ", "", "tail"}), {4, 0},
        Lines({"  vlgw nryx, nhsecw    ", "", "tail"}), {0, 13});
  probe("line dropped and neighbours joined, short", Lines({"ab", "cd", "ef"}), {0, 0},
        Lines({"abef"}), {0, 0});
}

TEST(NoResultCauses, GeneratedCodeIndentationReplay) {
  NeovimOracle oracle;
  const Config config = Config::uniform();
  mt19937 rng(20261125);
  for (int i = 0; i < 120; ++i) {
    const Case c = codeCase(rng);
    if (c.initial == c.goal) continue;
    for (const auto& [options, settings] : array{
             pair{IndentOptions{}, ""},
             pair{IndentOptions{.expandtab = true}, "et"},
             pair{IndentOptions{.copyindent = true}, "ci"}}) {
      SCOPED_TRACE(format("case {} '{}'", i, settings));
      CompositionOptimizer opt(config);
      CompositionOptimizerParams params;
      params.transform.indent = options;
      const auto result = opt.optimize(c.initial, c.initialPos, c.goal, c.goalPos, params);
      const auto& solutions = result.getResults();
      ASSERT_FALSE(solutions.empty());
      const auto best = ranges::min_element(solutions, {}, &Result::getCost);
      const auto actual = oracle.simulate(c.initial, c.initialPos.line, c.initialPos.col,
                                          best->getSequence().str(), settings);
      EXPECT_EQ(actual.lines, c.goal) << best->getSequence();
      EXPECT_EQ(CursorPos(actual.row, actual.col), c.goalPos);
      EXPECT_EQ(actual.mode, Mode::Normal);
    }
  }
}

TEST(NoResultCauses, LineBreakEntriesReplay) {
  NeovimOracle oracle;
  for (const string prefix : {"", "    ", "  abc"}) {
    for (const string suffix : {"", "  def"}) {
      const Lines before = {"above", prefix, "", suffix, "below"};
      const CursorPos begin(1, before.getSize(1)), end(3, 0);
      const DiffState diff(begin, end, "\n\n", "X\n y",
                           TransformBoundary(before, begin, end));
      const Lines after = {"above", prefix + "X", " y" + suffix, "below"};
      int entries = 0;
      CompositionStrategies::enumerateLineBreakReplacements(diff, before, {}, [&](const auto& s) {
        ++entries;
        for (int col = s.beginCol; col < s.endCol; ++col) {
          EXPECT_TRUE(OracleReplay::matches(oracle, before, CursorPos(s.targetLine, col),
              s.insertCmd, after, s.goalPos, ::Mode::Normal, "line-break replacement"));
        }
      });
      EXPECT_EQ(entries, 2);
    }
  }
}

TEST(NoResultCauses, Ascii) { runRandom("ascii", asciiCase, envInt("VFX_CASES", 20000)); }
TEST(NoResultCauses, Prose) { runRandom("prose", proseCase, envInt("VFX_CASES", 2000)); }
TEST(NoResultCauses, Code) { runRandom("code", codeCase, envInt("VFX_CASES", 2000)); }

TEST(NoResultCauses, Journal) {
  const char* dir = getenv("VIMFY_JOURNAL_DIR");
  if (!dir) GTEST_SKIP() << "set VIMFY_JOURNAL_DIR";
  Study study;
  study.printCases = false;
  const Journal::EvaluateParams params{.slicePadding = 5, .maxSearchLines = 500,
                                       .resultsCalculated = 20, .deadlineMs = 2000};
  const array<int, 4> sizes{2, 4, 8, 16};

  vector<fs::path> files;
  for (const auto& e : fs::directory_iterator(dir)) {
    if (e.path().extension() == ".vfj") files.push_back(e.path());
  }
  ranges::sort(files);
  for (const fs::path& file : files) {
    ifstream stream(file, ios::binary);
    const string bytes(istreambuf_iterator<char>(stream), {});
    Journal::Replay replay(bytes);
    Journal::WindowCutter cutter(sizes, 30000);
    Journal::Event event;
    vector<Journal::Window> windows;
    while (replay.next(event)) {
      windows.clear();
      cutter.push(event, windows);
      for (const Journal::Window& w : windows) {
        const Lines& initial = *w.start.lines;
        const Lines& goal = *w.end.lines;
        if (initial == goal) continue;
        const SearchRegion region = computeSearchRegion(
            w.start.row, w.end.row, initial, goal, params.slicePadding);
        if (region.paddedHeight() - 1 > params.maxSearchLines) continue;
        SearchSlice slice = sliceSearch(
            initial, goal,
            SessionEnds{.startRow = w.start.row, .startCol = w.start.col,
                        .endRow = w.end.row, .endCol = w.end.col,
                        .windowHeight = w.start.windowHeight,
                        .scrollAmount = w.start.scrollAmount},
            w.keys, w.typed, region);
        SearchInputs in = searchInputs(std::move(slice), params.overrides);
        study.add(Case{in.initialLines, in.initialPos, in.goalLines, in.goalPos,
                       in.userSeq, in.boundary, in.navContext});
      }
    }
  }
  study.report("journal windows of 2/4/8/16 commands");
}

}  // namespace
