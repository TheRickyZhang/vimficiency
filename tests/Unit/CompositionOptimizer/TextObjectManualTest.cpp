#include <gtest/gtest.h>

import std.compat;
import vf.tests.Unit.CompositionOptimizer.ManualTestHelpers;

using namespace std;

namespace {

struct TextObjectCase {
  Lines initial;
  Lines goal;
  string expectedCommand;
};

// From before (or on) the opening delimiter, the optimizer must offer the
// inner text object and it must replay to the goal.
TEST_F(CompositionOptimizer_ManualTest, InnerTextObjectsReplaceDelimitedContent) {
  const vector<TextObjectCase> cases = {
      {{"foo \"hello\" bar"}, {"foo \"goodbye\" bar"}, "ci\""},
      {{"\"hello\""}, {"\"goodbye\""}, "ci\""},
      {{"foo (hello) bar"}, {"foo (goodbye) bar"}, "ci("},
      {{"foo {hello} bar"}, {"foo {goodbye} bar"}, "ci{"},
      {{"foo [hello] bar"}, {"foo [goodbye] bar"}, "ci["},
      {{"foo 'hello' bar"}, {"foo 'goodbye' bar"}, "ci'"},
  };

  for (const TextObjectCase& test : cases) {
    CursorPos pos(0, 0);
    auto results = opt.optimize(test.initial, pos, test.goal, pos, params).getResults();
    ASSERT_FALSE(results.empty()) << test.expectedCommand;
    EXPECT_TRUE(hasReplayingResultContaining(
        results, test.expectedCommand, test.initial, pos, test.goal))
        << "Expected a valid " << test.expectedCommand << " result on " << test.initial;
  }
}

}  // namespace
