// tests/Unit/Explore/FixtureIntegrationTest.cpp
//
// Integration tests for Explore::View driven by canonical saved-session
// JSON fixtures. Each fixture mirrors the real on-disk schema written by
// the Lua layer, so these tests exercise the same typed inputs that a live
// plugin session hands to the optimizer — no hand-rolled Lines/cursors.

#include <gtest/gtest.h>

import std.compat;
import vf.Boundary.NavBoundary;
import vf.Keyboard.Config;
import vf.Explore.Explore;
import vf.LuaExports.Common;
import vf.Types.CursorPos;
import vf.Types.Lines;
import vf.Types.NavContext;
import vf.tests.Unit.Explore.FixtureLoader;

using namespace std;

namespace {

Explore::View viewFromFixture(const ExploreFixtures::Fixture& f,
                              NavContext navContext, Config config) {
  NavBoundary boundary = VF::LuaExports::helpers::sliceBoundary(
      f.lines, 0, f.lines.back().size(), f.hasLinesAbove, f.hasLinesBelow);
  return Explore::View(f.lines, f.startPos, f.goalLines, f.endPos,
                       std::move(boundary), navContext, config,
                       /*userSequence=*/"");
}

class ExploreFixtureTest : public ::testing::Test {
 protected:
  Config config = Config::uniform();
  NavContext navContext{24, 12};
};

// Baseline: canonical fixture loads, view enters Navigate/Transform, and the
// frontier produces at least one distinct recommendation.
TEST_F(ExploreFixtureTest, RenameFixtureStartsInActivePhase) {
  auto f = ExploreFixtures::loadFixture("rename_int_n_to_m");
  auto view = viewFromFixture(f, navContext, config);

  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()) ||
              std::holds_alternative<Explore::Transform>(view.phase()));
  EXPECT_GT(view.totalEdits(), 0);
  EXPECT_EQ(view.state().cursor, f.startPos);

  auto recs = view.recommendations(5);
  EXPECT_FALSE(recs.empty());

  set<string> texts;
  for (const auto& rec : recs) texts.insert(rec.token);
  EXPECT_EQ(texts.size(), recs.size()) << "recommendations should be distinct";
}

// Stepping test: after moving the cursor along the top motion rec, the
// frontier should continue to provide candidates until the plan completes.
TEST_F(ExploreFixtureTest, InsertFixtureDrivesForwardUntilCompletion) {
  auto f = ExploreFixtures::loadFixture("insert_plus_one");
  auto view = viewFromFixture(f, navContext, config);

  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()) ||
              std::holds_alternative<Explore::Transform>(view.phase()));

  int steps = 0;
  const int maxSteps = 60;  // per-token walk; bounded generously above
                            // the Manhattan distance between start and target
  while ((std::holds_alternative<Explore::Navigate>(view.phase()) ||
          std::holds_alternative<Explore::Transform>(view.phase())) &&
         steps < maxSteps) {
    auto recs = view.recommendations(5);
    if (recs.empty()) {
      ADD_FAILURE()
          << "empty frontier at step " << steps
          << " cursor=(" << view.state().cursor.line << ","
          << view.state().cursor.col << ")"
          << " editIndex="
          << Explore::phaseIndex(view.phase());
      break;
    }

    // Navigate phase emits motions plus single-action composition
    // structurals (`A`, `a`, ...); Transform phase emits edits. This test
    // only walks the motion frontier, so break out once we hit Transform
    // and step along the best motion.
    if (!std::holds_alternative<Explore::Navigate>(view.phase())) break;
    const bool moved = any_of(recs.begin(), recs.end(), [&](const Suggestion& s) {
      return view.applyMovement(s.token).has_value();
    });
    ASSERT_TRUE(moved) << "no motion among the top recommendations at step " << steps;
    steps++;
  }

  EXPECT_LT(steps, maxSteps);
}

}  // namespace
