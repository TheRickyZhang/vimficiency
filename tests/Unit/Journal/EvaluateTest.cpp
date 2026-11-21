#include <gtest/gtest.h>

import std.compat;
import vf.Effort.RunningEffort;
import vf.Journal.Evaluate;
import vf.Journal.Windows;
import vf.Keyboard.Config;
import vf.Optimizer.Analyze;
import vf.Optimizer.SearchControl;
import vf.Types.Lines;

using namespace std;
using Journal::Outcome;
using Journal::Sample;
using Journal::Window;
using Journal::WindowState;

namespace {

const Journal::EvaluateParams PARAMS{
    .slicePadding = 2,
    .maxSearchLines = 50,
    .resultsCalculated = 3,
};

const Lines TEXT = {"alpha beta gamma", "delta epsilon", "zeta eta theta", "iota kappa"};

WindowState state(Lines lines, int row, int col) {
  return WindowState{
      .lines = make_shared<const Lines>(std::move(lines)),
      .row = row,
      .col = col,
      .windowHeight = 40,
      .scrollAmount = 20,
  };
}

Window window(WindowState start, WindowState end, string keys) {
  return Window{.start = std::move(start), .end = std::move(end), .keys = keys, .typed = keys};
}

Sample evaluate(const Window& w, const Journal::EvaluateParams& params = PARAMS) {
  return Journal::evaluate(w, params, Config::qwerty());
}

}  // namespace

TEST(JournalEvaluateTest, ScoresAMovementAgainstTheBestOne) {
  const Sample sample = evaluate(window(state(TEXT, 0, 0), state(TEXT, 3, 0), "jjj"));

  EXPECT_EQ(sample.outcome, Outcome::Scored);
  EXPECT_TRUE(sample.navOnly);
  EXPECT_FALSE(sample.netZero);
  EXPECT_GT(sample.userCost, 0);
  EXPECT_GT(sample.bestCost, 0);
  EXPECT_LE(sample.bestCost, sample.userCost);
}

TEST(JournalEvaluateTest, AWindowThatEndsWhereItBeganCostsNothingAtBest) {
  const Sample sample = evaluate(window(state(TEXT, 1, 2), state(TEXT, 1, 2), "jk"));

  EXPECT_EQ(sample.outcome, Outcome::Scored);
  EXPECT_TRUE(sample.navOnly);
  EXPECT_TRUE(sample.netZero);
  EXPECT_GT(sample.userCost, 0);
  EXPECT_EQ(sample.bestCost, 0);
}

TEST(JournalEvaluateTest, ScoresAnEditLikeTheSessionPathDoes) {
  Lines goal = TEXT;
  goal[1] = "epsilon";
  const Window w = window(state(TEXT, 1, 0), state(goal, 1, 0), "xxxxxx");

  const Sample sample = evaluate(w);
  EXPECT_EQ(sample.outcome, Outcome::Scored);
  EXPECT_FALSE(sample.navOnly);

  const pair<int, int> region = computeSearchRegion(1, 1, TEXT, goal, PARAMS.slicePadding);
  const SearchInputs inputs = searchInputs(
      sliceSearch(TEXT, goal,
                  SessionEnds{.startRow = 1, .endRow = 1, .windowHeight = 40, .scrollAmount = 20},
                  "xxxxxx", "xxxxxx", region),
      {});
  const AnalyzeOutput analyzed =
      analyze(inputs, PARAMS.resultsCalculated, Config::qwerty(), SearchControl::NEVER_STOP);
  ASSERT_FALSE(analyzed.results.empty());
  EXPECT_EQ(sample.bestCost, analyzed.results.front().getCost());
  EXPECT_EQ(sample.userCost, analyzed.userCost);
  EXPECT_LT(sample.bestCost, sample.userCost);
}

TEST(JournalEvaluateTest, TheUserIsCostedByTheKeysPressedNotTheKeysThatRan) {
  Window mapped = window(state(TEXT, 0, 0), state(TEXT, 3, 0), "jjj");
  mapped.typed = "t";
  const Sample pressed = evaluate(mapped);
  const Sample ran = evaluate(window(state(TEXT, 0, 0), state(TEXT, 3, 0), "jjj"));

  EXPECT_EQ(pressed.userCost, getEffort("t", Config::qwerty()));
  EXPECT_LT(pressed.userCost, ran.userCost);
  EXPECT_EQ(pressed.bestCost, ran.bestCost);
}

TEST(JournalEvaluateTest, ARegionTallerThanTheCapIsNotSearched) {
  Journal::EvaluateParams params = PARAMS;
  params.maxSearchLines = 1;

  const Sample sample = evaluate(window(state(TEXT, 0, 0), state(TEXT, 3, 0), "jjj"), params);
  EXPECT_EQ(sample.outcome, Outcome::TooLarge);
}

TEST(JournalEvaluateTest, ALineHoldingANewlineIsNotSearched) {
  Lines odd = TEXT;
  odd[1] = string("nul\nbyte");

  const Sample sample = evaluate(window(state(odd, 0, 0), state(odd, 3, 0), "jjj"));
  EXPECT_EQ(sample.outcome, Outcome::Unsupported);
}

TEST(JournalEvaluateTest, ACursorPastTheEndOfItsLineIsClamped) {
  const Sample sample = evaluate(window(state(TEXT, 0, 99), state(TEXT, 0, 15), "$"));

  EXPECT_EQ(sample.outcome, Outcome::Scored);
  EXPECT_TRUE(sample.netZero);
}
