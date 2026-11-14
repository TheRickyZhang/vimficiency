// Counted-motion discovery and the minCountRepeat gate. Assertions are
// membership checks (`contains` a count-prefixed sequence), never exact-menu
// snapshots: equal-cost alternatives order differently across stdlibs.

#include <gtest/gtest.h>

import std.compat;
import vf.tests.Unit.NavOptimizer.ManualTestHelpers;

using namespace std;

namespace {

// "one two three four five six seven eight"
//  0   4   8     14   19   24  28    34      word begins
//  2   6   12    17   22   26  32    38      word ends
const Lines WORD_LINE = {"one two three four five six seven eight"};

TEST_F(NavOptimizer_ManualTest, CountedWordMotionsAreDiscovered) {
  EXPECT_TRUE(contains_all(runOptimizer(WORD_LINE, {0, 0}, {0, 19}, "wwww"), {"4w"}));
  EXPECT_TRUE(contains_all(runOptimizer(WORD_LINE, {0, 34}, {0, 14}, "bbbb"), {"4b"}));
  EXPECT_TRUE(contains_all(runOptimizer(WORD_LINE, {0, 0}, {0, 17}, "eeee"), {"4e"}));
}

TEST_F(NavOptimizer_ManualTest, CountedGeIsDiscoveredBelowCheaperFinds) {
  // 4ge ranks below find-based alternatives, so a larger result cap is needed
  // to assert it is still produced (discovery, not top-rank).
  NavOptimizer opt(Config::uniform());
  NavBoundary boundary;
  auto results = opt.optimize(WORD_LINE, {0, 38}, {0, 17},
                              NavOptimizerParams{}
                                  .withMaxResults(150)
                                  .withMaxNodesPopped(20000)
                                  .withMaxResultsPerEndPos(150),
                              "gegegege", boundary, navContext).getResults();

  EXPECT_TRUE(contains_all(results, {"4ge"}));
}

TEST_F(NavOptimizer_ManualTest, CountOfOneIsNotEmitted) {
  auto results = runOptimizer(WORD_LINE, {0, 0}, {0, 4}, "w");
  EXPECT_FALSE(hasSequence(results, "1w")) << "Should not emit 1w, just w";
}

TEST_F(NavOptimizer_ManualTest, MinCountRepeat_BlocksSmallCounts) {
  // "one two three four five six" — "four" (col 14) is reachable by 3w
  Lines lines = {"one two three four five six"};
  auto results = runOptimizer(lines, {0, 0}, {0, 14}, "");

  // Default minCountRepeat=4 blocks count 3.
  EXPECT_FALSE(hasSequence(results, "3w")) << "3w should be blocked by minCountRepeat=4";
  EXPECT_FALSE(hasSequence(results, "3W")) << "3W should be blocked by minCountRepeat=4";
}

TEST_F(NavOptimizer_ManualTest, MinCountRepeat_LowThresholdAllowsSmallCounts) {
  Lines lines = {"one two three four five six"};
  NavOptimizer opt(Config::uniform());
  NavBoundary boundary;
  auto results = opt.optimize(lines, {0, 0}, {0, 14},
      NavOptimizerParams{}.withMaxResults(30).withMaxNodesPopped(20000)
          .withMinCountRepeat(2)
          .withMaxResultsPerEndPos(30),
      "", boundary, navContext).getResults();

  EXPECT_TRUE(hasSequence(results, "3w")) << "3w should be allowed with minCountRepeat=2";
}

}  // namespace
