#include <gtest/gtest.h>

import std.compat;
import vf.tests.Unit.CompositionOptimizer.ManualTestHelpers;

using namespace std;

namespace {

TEST_F(CompositionOptimizer_ManualTest, PureInsertion_NewLineBetween) {
  // Insert new line between existing lines: should use 'o' shortcut
  Lines initial = {"a", "c"};
  Lines goal = {"a", "b", "c"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "new line insertion");

  EXPECT_TRUE(hasSequenceContaining(results, "ob")) << "Expected 'o' shortcut for new line insertion";
}

TEST_F(CompositionOptimizer_ManualTest, PureInsertion_AppendToLine) {
  // Append to end of line: should use 'A' shortcut
  Lines initial = {"a", "c"};
  Lines goal = {"ab", "c"};
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "append to line");

  EXPECT_TRUE(hasSequenceContaining(results, "Ab")) << "Expected 'A' shortcut for append insertion";
}

}  // namespace
