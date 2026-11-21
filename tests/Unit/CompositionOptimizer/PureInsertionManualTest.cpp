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

TEST_F(CompositionOptimizer_ManualTest, PureInsertion_OpenAboveFirstLine) {
  Lines initial = {"first", "second"};
  Lines goal = {"new", "first", "second"};
  CursorPos initialPos(0, 3);
  CursorPos goalPos(0, 2);

  auto compResult = opt.optimize(initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "open above line 0");
  EXPECT_EQ(results.front().getSequence().str(), "Onew<Esc>");
}

TEST_F(CompositionOptimizer_ManualTest, PureInsertion_OpenAboveFromCursorLine) {
  // `O` from the line the cursor is on saves the `k` that `o` needs.
  Lines initial = {"a", "c"};
  Lines goal = {"a", "b", "c"};
  CursorPos initialPos(1, 0);
  CursorPos goalPos(1, 0);

  auto compResult = opt.optimize(initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "O from cursor line");
  EXPECT_EQ(results.front().getSequence().str(), "Ob<Esc>");
}

TEST_F(CompositionOptimizer_ManualTest, PureInsertion_OpenBelowTakesIndentFromAbove) {
  Lines initial = {"    a", "c"};
  Lines goal = {"    a", "    b", "c"};
  CursorPos initialPos(0, 4);
  CursorPos goalPos(1, 4);

  auto compResult = opt.optimize(initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "o copies indent");
  EXPECT_EQ(results.front().getSequence().str(), "ob<Esc>");
}

TEST_F(CompositionOptimizer_ManualTest, PureInsertion_AppendPrefersLowercaseOnTie) {
  // Under the uniform keyboard `a` (cursor already on the last char) and `A`
  // cost the same; the lowercase entry wins the tie.
  Lines initial = {"keep them"};
  Lines goal = {"keep them, later"};
  CursorPos initialPos(0, 8);
  CursorPos goalPos(0, 15);

  auto compResult = opt.optimize(initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  expectHasValidResults(results, initial, initialPos, goal, "a/A tie");
  EXPECT_EQ(results.front().getSequence().str(), "a,<Space>later<Esc>");
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
