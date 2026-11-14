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