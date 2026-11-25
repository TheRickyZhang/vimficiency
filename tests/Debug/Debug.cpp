// tests/Debug/Debug.cpp
//
// Debug utilities and scratch tests for development.
// Enable a test by removing DISABLED_ prefix.
//
// Run: ./build/tests/vimfy_debug --gtest_filter="DebugTest.*"
//   - Or: ./vimfy_unit_tests --gtest_filter="NeovimOracleDebug.*"

#include <gtest/gtest.h>
#include <cassert>
#include <climits>
#include <cstdio>

import std.compat;
import vf.Interpreter.EditInterpreter;
import vf.Interpreter.MovementInterpreter;
import vf.Keyboard.Config;
import vf.Optimizer.TransformOptimizer.TransformOptimizer;
import vf.Optimizer.CompositionOptimizer.CompositionOptimizer;
import vf.Optimizer.CompositionOptimizer.CompositionSearchContext;
import vf.Optimizer.DiffPlanner.DiffState;
import vf.Optimizer.DiffPlanner.MyersDiff;
import vf.Optimizer.DiffPlanner.PlannerCosts;
import vf.Optimizer.DiffPlanner.VimDiff;
import vf.Optimizer.NavOptimizer.NavOptimizer;
import vf.Boundary.TransformBoundary;
import vf.Boundary.NavBoundary;
import vf.tests.Utils.NeovimOracle;
import vf.tests.Utils.OracleReplay;
import vf.Utils.PrettyText;
import vf.tests.Utils.RandomBufferHelpers;
import vf.tests.Utils.RandomGeneration;
import vf.Optimizer.TransformOptimizer.TransformState;
import vf.VimCore.VimEditUtils;
import vf.VimCore.VimEndpointUtils;

using namespace std;

namespace {

}

TEST(DebugTest, DISABLED_JoinPartition) {
  const Config config = Config::uniform();
  const Lines initial({"one", "two", "three", "four", "five"});
  const Lines goal({"one two three", "four five"});
  const CompositionOptimizerParams params = CompositionOptimizerParams{}.withMaxResults(5);
  auto newlineToSpace = [&](int line, int col) {
    return DiffState(CursorPos{line, col}, CursorPos{line + 1, 0}, "\n", " ",
                     TransformBoundary(initial, CursorPos{line, col}, CursorPos{line + 1, 0}));
  };
  const vector<DiffState> perNewline = {newlineToSpace(0, 3), newlineToSpace(1, 3),
                                        newlineToSpace(3, 4)};
  auto show = [&](const char* label, const vector<DiffState>* diffs) {
    CompositionOptimizer opt(config);
    auto res = opt.optimize(initial, {0, 0}, goal, {0, 0}, params, "", NavBoundary(), NavContext(),
                            SearchControl::NEVER_STOP, diffs);
    cout << "== " << label << "\n";
    for (const DiffState& d : res.getDiffs())
      cout << "   (" << d.beginPos.line << "," << d.beginPos.col << ") del=["
           << VF::prettify(d.deletedText) << "] ins=[" << VF::prettify(d.insertedText) << "]\n";
    vector<Result> results = res.getResults();
    sort(results.begin(), results.end(),
         [](const Result& a, const Result& b) { return a.getCost() < b.getCost(); });
    for (const Result& r : results)
      cout << "   " << r.getCost() << "  " << VF::prettify(r.getSequence().str()) << "\n";
  };
  show("planner", nullptr);
  show("one region per newline", &perNewline);
}

TEST(DebugTest, DISABLED_MoveFromLineEnd) {
  const Lines initial({". b .cfcda.;", "  e.daba bf  b;", "bb,df", "", ", a e,daeccfa, {", "}"});
  const VimDiff::FlatText flat(initial);
  VimDiff::TilingCost moveCost(flat, 1.0, CountPrefixLimits::DEFAULT_MAX_PREFIX_COUNT,
                               VimDiff::TilingCost::Kind::Move);
  auto at = [&](int line, int col) {
    return DiffText::positionToFlatIndex(CursorPos{line, col}, initial);
  };
  auto show = [&](const char* label, int begin, int end) {
    cout << label << " cost=" << moveCost.query(begin, end) << ":";
    for (const VimDiff::TilingChunk& c : moveCost.cover(begin, end))
      cout << "  kind=" << (int)c.kind << " k=" << c.count << " [" << c.begin << "," << c.end
           << ") cost=" << c.cost;
    cout << "\n";
  };
  show("line 0 newline -> (3,0)", at(1, 0) - 1, at(3, 0));
  show("(1,0) -> (3,0)", at(1, 0), at(3, 0));
  show("line 0 newline -> (2,0)", at(1, 0) - 1, at(2, 0));
  show("(1,0) -> (2,0)", at(1, 0), at(2, 0));
  show("line 0 newline -> (1,0)", at(1, 0) - 1, at(1, 0));
  show("line 0 newline -> (1,2)", at(1, 0) - 1, at(1, 2));
  show("(1,0) -> (1,2)", at(1, 0), at(1, 2));
}

TEST(DebugTest, DISABLED_SessionAPartition) {
  const Config config = Config::uniform();
  const Lines initial({"", "#include <bits/stdc++.h>", "using namespace std;", "", "int main() {",
                       "  int m = 10;", "  for(int i = 0; i < n; i++) {", "    cout << i+1 << en;",
                       "  }", "}"});
  const Lines goal({"", "#include <bits/stdc++.h>", "using namespace std;", "", "int main() {",
                    "  int n = 1;", "  for(int i = 0; i < m; i++) {", "    cout << 2 * i << en;",
                    "  }", "}"});
  const string userSeq = "wrnf;i\x08\x1bjfnrmjBBBcW2 * i\x1b";
  const CompositionOptimizerParams params = CompositionOptimizerParams{}.withMaxResults(20);

  VimDiff::Plan plan = VimDiff::calculate(
      initial, goal, config,
      VimDiff::CostOptions{.moveDeleteScale = params.moveDeleteScale,
                           .maxPrefixCount = params.maxPrefixCount});

  vector<DiffState> merged(plan.diffs.begin(), plan.diffs.begin() + 3);
  merged.emplace_back(CursorPos{7, 12}, CursorPos{7, 15}, "i+1", "2 * i",
                      TransformBoundary(initial, CursorPos{7, 12}, CursorPos{7, 15}));

  auto show = [&](const char* label, const vector<DiffState>& diffs) {
    cout << "== " << label << "\n";
    for (const DiffState& d : diffs)
      cout << "   (" << d.beginPos.line << "," << d.beginPos.col << ") del=[" << d.deletedText
           << "] ins=[" << d.insertedText << "]\n";
    CompositionOptimizer opt(config);
    auto res = opt.optimize(initial, {5, 2}, goal, {7, 16}, params, userSeq, NavBoundary(),
                            NavContext(), SearchControl::NEVER_STOP, &diffs);
    vector<Result> results = res.getResults();
    sort(results.begin(), results.end(),
         [](const Result& a, const Result& b) { return a.getCost() < b.getCost(); });
    for (const Result& r : results)
      cout << "   " << r.getCost() << "  " << VF::prettify(r.getSequence().str()) << "\n";
  };
  cout << "planner est=" << plan.cost << " userEffort=" << getEffort(userSeq, config) << "\n";

  const VimDiff::CostOptions costOptions{.moveDeleteScale = params.moveDeleteScale,
                                         .maxPrefixCount = params.maxPrefixCount};
  for (const VimDiff::RegionBreakdown& r :
       VimDiff::calculateBreakdown(initial, goal, config, costOptions).regions)
    cout << "   region (" << r.diff.beginPos.line << "," << r.diff.beginPos.col << ") del=["
         << r.diff.deletedText << "] ins=[" << r.diff.insertedText << "]  move=" << r.move
         << " del=" << r.del << " ins=" << r.ins << "\n";

  const VimDiff::FlatText flatInitial(initial), flatGoal(goal);
  VimDiff::TilingCost delCost(flatInitial, costOptions.moveDeleteScale, costOptions.maxPrefixCount,
                              VimDiff::TilingCost::Kind::Delete);
  VimDiff::TilingCost moveCost(flatInitial, costOptions.moveDeleteScale, costOptions.maxPrefixCount,
                               VimDiff::TilingCost::Kind::Move);
  const VimDiff::Typing typing(flatGoal, config);
  auto at = [&](const Lines& lines, int line, int col) {
    return DiffText::positionToFlatIndex(CursorPos{line, col}, lines);
  };
  const int prevEnd = at(initial, 6, 22);
  cout << "merged alt: move=" << moveCost.query(prevEnd, at(initial, 7, 12))
       << " del(i+1)=" << delCost.query(at(initial, 7, 12), at(initial, 7, 15))
       << " ins(2 * i)+esc=" << typing.ins(at(goal, 7, 12), at(goal, 7, 17)) + typing.esc << "\n";
  for (int col : {11, 12}) {
    cout << "move tiling to (7," << col << "):";
    for (const VimDiff::TilingChunk& c : moveCost.cover(prevEnd, at(initial, 7, col)))
      cout << "  kind=" << (int)c.kind << " k=" << c.count << " [" << c.begin - prevEnd << ","
           << c.end - prevEnd << ") cost=" << c.cost;
    cout << "\n";
  }
  cout << "split pieces: move to (7,11)=" << moveCost.query(prevEnd, at(initial, 7, 11))
       << " move to (7,12)=" << moveCost.query(prevEnd, at(initial, 7, 12))
       << " move over ' i'=" << moveCost.query(at(initial, 7, 11), at(initial, 7, 13))
       << " move over 'i'=" << moveCost.query(at(initial, 7, 12), at(initial, 7, 13))
       << " del(+1)=" << delCost.query(at(initial, 7, 13), at(initial, 7, 15))
       << " del(i)=" << delCost.query(at(initial, 7, 12), at(initial, 7, 13)) << "\n";
  show("vimdiff plan", plan.diffs);
  show("merged last line", merged);
}
namespace {
void showComposition(const char* label, const Config& config, const Lines& initial,
                     CursorPos start, const Lines& goal, CursorPos end, const string& userSeq) {
  const CompositionOptimizerParams params = CompositionOptimizerParams{}.withMaxResults(8);
  const VimDiff::CostOptions costOptions{.moveDeleteScale = params.moveDeleteScale,
                                         .maxPrefixCount = params.maxPrefixCount};
  cout << "== " << label << "  user=" << VF::prettify(userSeq)
       << " userEffort=" << getEffort(userSeq, config) << "\n";
  for (const VimDiff::RegionBreakdown& r :
       VimDiff::calculateBreakdown(initial, goal, config, costOptions).regions)
    cout << "   region (" << r.diff.beginPos.line << "," << r.diff.beginPos.col << ") del=["
         << VF::prettify(r.diff.deletedText) << "] ins=[" << VF::prettify(r.diff.insertedText)
         << "]  move=" << r.move << " del=" << r.del << " ins=" << r.ins << "\n";
  CompositionOptimizer opt(config);
  auto res = opt.optimize(initial, start, goal, end, params, userSeq, NavBoundary(), NavContext(),
                          SearchControl::NEVER_STOP, nullptr);
  vector<Result> results = res.getResults();
  sort(results.begin(), results.end(),
       [](const Result& a, const Result& b) { return a.getCost() < b.getCost(); });
  for (const Result& r : results)
    cout << "   " << r.getCost() << "  " << VF::prettify(r.getSequence().str()) << "\n";
}
}  // namespace

TEST(DebugTest, DISABLED_CiNoResults) {
  showComposition("ci", Config::uniform(), Lines({"]", "n2|~~"}), {1, 4}, Lines({"]~"}), {0, 1},
                  "");
  const Lines initial({"]", "n2|~~"});
  const Config config = Config::uniform();
  TransformOptimizer topt(config);
  const TransformBoundary boundary(initial, CursorPos{0, 1}, CursorPos{1, 4});
  TransformResult tres = topt.optimizePureDeletion(Lines({"", "n2|~"}), boundary,
                                                   TransformOptimizerParams{}, 0, 1,
                                                   CursorPos{0, 1});
  cout << "starts=" << tres.startPositions().size() << "\n";
  const auto& buckets = tres.getResults();
  for (size_t i = 0; i < buckets.size(); i++)
    for (const Result& r : buckets[i])
      cout << "   start (" << tres.startPositions()[i].line << "," << tres.startPositions()[i].col
           << ")  " << r.getCost() << "  " << VF::prettify(r.getSequence().str()) << "\n";
}

TEST(DebugTest, DISABLED_JoinCounts) {
  for (const Config& config : {Config::uniform(), Config::qwerty()}) {
    showComposition("5->2 (4+1)", config, Lines({"one", "two", "three", "four", "five"}), {0, 0},
                    Lines({"one two three four", "five"}), {0, 0}, "");
    showComposition("5->1", config, Lines({"one", "two", "three", "four", "five"}), {0, 0},
                    Lines({"one two three four five"}), {0, 0}, "");
    showComposition("7->1", config, Lines({"a", "b", "c", "d", "e", "f", "g"}), {0, 0},
                    Lines({"a b c d e f g"}), {0, 0}, "");
    cout << "plain typing effort: JJJ=" << getEffort("JJJ", config) << " 4J=" << getEffort("4J", config)
         << " J..=" << getEffort("J..", config) << " 7J=" << getEffort("7J", config)
         << " JJJJJJ=" << getEffort("JJJJJJ", config) << "\n";
  }
}

TEST(DebugTest, DISABLED_EpsilonOmega) {
  const Lines text({"alpha beta gamma", "delta epsilon", "zeta eta theta", "iota kappa"});
  Lines goal = text;
  goal[1] = "delta omega";
  for (const Config& config : {Config::uniform(), Config::qwerty()}) {
    showComposition("mid buffer", config, text, {1, 6}, goal, {1, 10}, "Comega\x1b");
    showComposition("single line", config, Lines({"alpha beta gamma delta epsilon", "second line"}),
                    {0, 23}, Lines({"alpha beta gamma delta omega", "second line"}), {0, 27},
                    "Comega\x1b");
  }
}

TEST(DebugTest, DISABLED_EpsilonOmegaTransform) {
  const Lines text({"alpha beta gamma", "delta epsilon", "zeta eta theta", "iota kappa"});
  const TransformBoundary boundary(text, CursorPos{1, 6}, CursorPos{1, 13});
  for (const Config& config : {Config::uniform(), Config::qwerty()}) {
    for (int perStart : {1, 4}) {
      for (double distanceWeight : {-1.0, 0.0}) {
        TransformOptimizerParams params =
            TransformOptimizerParams{}.withMaxResultsPerStartPos(perStart);
        if (distanceWeight == 0.0) params.withDistanceWeight(0.0);
        TransformOptimizer opt(config);
        TransformResult res = opt.optimizeTransform(Lines({"epsilon"}), Lines({"omega"}), boundary,
                                                    params, 1, 6, CursorPos{1, 10});
        cout << "== perStart=" << perStart << (distanceWeight == 0.0 ? " dijkstra" : " default")
             << " nodes=" << res.getStats().nodesExplored() << "\n";
        const auto& buckets = res.getResults();
        for (size_t i = 0; i < buckets.size(); i++)
          for (const Result& r : buckets[i])
            cout << "   start " << i << "  " << r.getCost() << "  "
                 << VF::prettify(r.getSequence().str()) << "\n";
      }
    }
  }
}

TEST(DebugTest, DISABLED_CountPenaltyThroughComposition) {
  const Lines initial({"one", "two", "three", "four", "five", "six"});
  const Lines goal({"five", "six"});
  for (const Config& config : {Config::uniform(), Config::qwerty()}) {
    TransformOptimizer topt(config);
    const TransformBoundary boundary(initial, CursorPos{0, 0}, CursorPos{4, 0});
    TransformResult tres = topt.optimizePureDeletion(
        Lines({"one", "two", "three", "four", ""}), boundary, TransformOptimizerParams{}, 0, 0,
        CursorPos{0, 0});
    if (const Result* r = tres.resultAt(0, 0))
      cout << "transform alone: " << r->getCost() << "  " << VF::prettify(r->getSequence().str())
           << "  (plain typing effort " << getEffort(r->getSequence().str(), config) << ")\n";
    showComposition("4dd", config, initial, {0, 0}, goal, {0, 0}, "");
  }
}

TEST(DebugTest, DISABLED_SuffixCacheDotContext) {
  const Lines fullBuffer = {"ce.d.d ", "aa.eb", "f. e", ".e ae"};
  const Lines editRegion = {".d.d ", "aa.eb", "f. e", ".e a"};
  const Lines goalLines = {"fbbccbae", "c.dead", "aaadd   ", "e.b,"};
  TransformBoundary boundary(fullBuffer, CursorPos(0, 2), CursorPos(3, 4));
  TransformOptimizer opt(Config::uniform());
  TransformResult res = opt.optimizeTransform(
      editRegion, goalLines, boundary,
      TransformOptimizerParams{}.withMaxResults(200).withMaxResultsPerStartPos(3));
  cout << res.getStats() << "\n";
  const auto& buckets = res.getResults();
  for (size_t i = 0; i < buckets.size(); i++) {
    cout << "start " << i << ": " << buckets[i].size();
    if (!buckets[i].empty())
      cout << "  " << buckets[i][0].getCost() << " " << VF::prettify(buckets[i][0].getSequence().str());
    cout << "\n";
  }
}

TEST(DebugTest, DISABLED_EmbeddedChangeCounterexample) {
  const Lines fullBuffer = {"\"S ~E", "  T .", "E u~ "};
  const Lines editRegion = {" ~E", "  T .", "E u"};
  TransformBoundary boundary(fullBuffer, CursorPos(0, 2), CursorPos(2, 3));
  TransformOptimizer opt(Config::uniform());
  TransformResult res =
      opt.optimizeTransform(editRegion, Lines({"", "", ""}), boundary, TransformOptimizerParams{});
  cout << res.getStats() << "\n";
  const auto& buckets = res.getResults();
  for (size_t i = 0; i < buckets.size(); i++)
    for (const Result& r : buckets[i])
      cout << "start " << i << "  " << r.getCost() << "  " << VF::prettify(r.getSequence().str())
           << "\n";
}

TEST(DebugTest, DISABLED_EmbeddedChangeOracle) {
  NeovimOracle oracle;
  const Lines fullBuffer = {"\"S ~E", "  T .", "E u~ "};
  const Lines expected = {"\"S", "", "~ "};
  for (const char* seq : {"d)", "d)dB", "d)dB.", "d)dB.ce\r\r\x1b", "d)dB.Jcaw\r\r\x1b"}) {
    auto result = OracleReplay::matches(oracle, fullBuffer, CursorPos(1, 2), seq, expected,
                                        std::nullopt, Mode::Normal, "probe");
    cout << "== " << VF::prettify(seq) << (result ? "  MATCH" : "") << "\n";
    if (!result) cout << result.message() << "\n";
  }
}

TEST(DebugTest, DISABLED_CompositionNoResultCounterexample) {
  showComposition("fuzz no-result", Config::uniform(), Lines({"    `", "1", "~"}), {0, 3},
                  Lines({"    `", "1~N", "~~"}), {2, 0}, "");
  showComposition("fuzz no-result, goal (2,1)", Config::uniform(), Lines({"    `", "1", "~"}), {0, 3},
                  Lines({"    `", "1~N", "~~"}), {2, 1}, "");
}

TEST(DebugTest, DISABLED_NewlineReplacementTransform) {
  const Lines initial({"    `", "1", "~"});
  const TransformBoundary boundary(initial, CursorPos{1, 1}, CursorPos{2, 0});
  TransformOptimizer opt(Config::uniform());
  TransformResult res = opt.optimizeTransform(Lines({"", ""}), Lines({"~N", "~"}), boundary,
                                              TransformOptimizerParams{}, 1, 1, CursorPos{2, 0});
  cout << res.getStats() << "\n";
  const auto& buckets = res.getResults();
  for (size_t i = 0; i < buckets.size(); i++)
    cout << "start " << i << ": " << buckets[i].size() << " results\n";
}

namespace {
const char* entryCurswantName(EntryCurswant e) {
  switch (e) {
    case EntryCurswant::Ignored: return "Ignored";
    case EntryCurswant::Followed: return "Followed";
    case EntryCurswant::Required: return "Required";
  }
  return "?";
}

void showStickyLanding(const char* label, const Lines& initial, CursorPos start, const Lines& goal,
                       CursorPos end, int transformPerStart) {
  const Config config = Config::uniform();
  CompositionOptimizerParams params = CompositionOptimizerParams{}.withMaxResults(8);
  params.transformMaxResultsPerStartPos = transformPerStart;
  CompositionOptimizer opt(config);
  auto res = opt.optimize(initial, start, goal, end, params, "", NavBoundary(), NavContext(),
                          SearchControl::NEVER_STOP, nullptr);
  cout << "== " << label << " perStart=" << transformPerStart << " results "
       << res.getResults().size() << " pops " << res.getStats().totalPops() << "\n";
  for (int e = 0; e < res.totalEdits(); e++) {
    const PlannedEditView view = res.plannedEditAt(e);
    cout << "   diff " << view.diff.beginPos << "-" << view.diff.endPos << " del=["
         << VF::prettify(view.diff.deletedText) << "] ins=["
         << VF::prettify(view.diff.insertedText) << "]\n";
    for (const CursorPos& sp : view.transformResult.startPositions())
      for (const Result& r : view.transformResult.resultsAt(sp.line, sp.col))
        cout << "      start " << sp << " " << entryCurswantName(r.getEntryCurswant()) << " "
             << r.getCost() << "  " << VF::prettify(r.getSequence().str()) << "\n";
  }
  vector<Result> results = res.getResults();
  sort(results.begin(), results.end(),
       [](const Result& a, const Result& b) { return a.getCost() < b.getCost(); });
  for (const Result& r : results)
    cout << "   " << r.getCost() << "  " << VF::prettify(r.getSequence().str()) << "\n";
}
}  // namespace

// A clamped nav landing (`k` onto a shorter line keeps curswant) meets a start
// whose only stored edit is EntryCurswant::Required.
TEST(DebugTest, DISABLED_StickyLandingNoResult) {
  for (int perStart : {1, 2}) {
    showStickyLanding("a/b/~~", Lines({"a", "b", "~~"}), {2, 1}, Lines({"a", "x", "y", "z~"}),
                      {3, 1}, perStart);
    showStickyLanding("b/~~", Lines({"b", "~~"}), {1, 1}, Lines({"x", "y", "z~"}), {2, 1},
                      perStart);
    showStickyLanding("a//~~", Lines({"a", "", "~~"}), {2, 1}, Lines({"a", "x", "y", "z~"}),
                      {3, 1}, perStart);
    showStickyLanding("k onto empty line", Lines({"D", "HHHHH", "", "         "}), {3, 8},
                      Lines({"D", "HHHHH", " "}), {2, 0}, perStart);
  }
}

// VimDiff merges an end-of-line insert and a next-line insert into one
// replacement of the newline, which Transform cannot solve from any start.
TEST(DebugTest, DISABLED_NewlineReplacementPlan) {
  const Config config = Config::uniform();
  const Lines initial({"", "~", ""});
  const Lines goal({"", "~~", " "});
  showStickyLanding("same family", Lines({"~", ""}), {1, 0}, Lines({"~~  "}), {0, 3}, 1);
  showStickyLanding("same family", Lines({" ", ""}), {1, 0}, Lines({"     "}), {0, 4}, 1);
  showComposition("planner", config, initial, {2, 0}, goal, {1, 0}, "");

  const vector<DiffState> twoInserts = {
      DiffState(CursorPos{1, 1}, CursorPos{1, 1}, "", "~",
                TransformBoundary(initial, CursorPos{1, 1}, CursorPos{1, 1})),
      DiffState(CursorPos{2, 0}, CursorPos{2, 0}, "", " ",
                TransformBoundary(Lines({"", "~~", ""}), CursorPos{2, 0}, CursorPos{2, 0})),
  };
  CompositionOptimizer opt(config);
  auto res = opt.optimize(initial, {2, 0}, goal, {1, 0},
                          CompositionOptimizerParams{}.withMaxResults(8), "", NavBoundary(),
                          NavContext(), SearchControl::NEVER_STOP, &twoInserts);
  cout << "== two insertions forced: " << res.getResults().size() << " results\n";
  for (const Result& r : res.getResults())
    cout << "   " << r.getCost() << "  " << VF::prettify(r.getSequence().str()) << "\n";

  const VimDiff::CostOptions costOptions{};
  const VimDiff::FlatText flatInitial(initial), flatGoal(goal);
  VimDiff::TilingCost delCost(flatInitial, costOptions.moveDeleteScale, costOptions.maxPrefixCount,
                              VimDiff::TilingCost::Kind::Delete);
  VimDiff::TilingCost moveCost(flatInitial, costOptions.moveDeleteScale, costOptions.maxPrefixCount,
                               VimDiff::TilingCost::Kind::Move);
  const VimDiff::Typing typing(flatGoal, config);
  auto at = [&](const Lines& lines, int line, int col) {
    return DiffText::positionToFlatIndex(CursorPos{line, col}, lines);
  };
  const int newline = at(initial, 2, 0) - 1;
  cout << "pieces: del(newline)=" << delCost.query(newline, newline + 1)
       << " move(newline)=" << moveCost.query(newline, newline + 1)
       << " ins(~)=" << typing.ins(at(goal, 1, 1), at(goal, 1, 2))
       << " ins(~ nl sp)=" << typing.ins(at(goal, 1, 1), at(goal, 2, 1))
       << " ins(sp)=" << typing.ins(at(goal, 2, 0), at(goal, 2, 1))
       << " esc=" << typing.esc << "\n";
}
