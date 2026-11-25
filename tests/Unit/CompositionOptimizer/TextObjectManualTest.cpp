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

TEST_F(CompositionOptimizer_ManualTest, MultilineTextObjectChangeAccountsForAutoindent) {
  const vector<TextObjectCase> cases = {
      {{"  (old)"}, {"  (X", "Y)"}, "ci("},
      {{"  (old)  tail"}, {"  X", "Y  tail"}, "ca("},
  };
  const CursorPos start(0, 1), finish(1, 0);
  for (const TextObjectCase& test : cases) {
    const auto res = opt.optimize(test.initial, start, test.goal, finish, params);
    EXPECT_TRUE(ranges::any_of(res.getResults(), [&](const Result& r) {
      return r.getSequence().view().starts_with(test.expectedCommand);
    })) << test.expectedCommand;
    for (const Result& r : res.getResults()) {
      EXPECT_TRUE(OracleReplay::matches(*oracle, test.initial, start, r.getSequence().str(),
                                        test.goal, finish, Mode::Normal, test.expectedCommand));
    }
  }
}

}  // namespace
