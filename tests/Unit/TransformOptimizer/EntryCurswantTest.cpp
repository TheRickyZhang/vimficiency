// tests/Unit/TransformOptimizer/EntryCurswantTest.cpp
//
// TransformResult::forEachEditFrom: which results apply from a cursor whose
// curswant differs from its column, and where they land.

#include <gtest/gtest.h>

import std.compat;
import vf.Optimizer.TransformOptimizer.TransformResult;
import vf.Types.CursorPos;
import vf.Types.Lines;

using namespace std;

namespace {

struct Visited {
  string sequence;
  CursorPos goal;
};

// One start position (0, 0) holding a result of each class.
TransformResult resultWithEveryClass() {
  Lines initial = {"a"};
  vector<vector<Result>> buckets = {{
      Result(Sequence("x"), 1.0, EntryCurswant::Ignored),
      Result(Sequence("dd"), 2.0, EntryCurswant::Followed),
      Result(Sequence("ddx"), 3.0, EntryCurswant::Required),
  }};
  vector<vector<CursorPos>> goals = {{CursorPos(0, 0), CursorPos(0, 0), CursorPos(0, 0)}};
  return TransformResult(std::move(buckets), TransformSearchStats{}, initial, 0, 0,
                         CursorPos(0, 0), std::move(goals));
}

vector<Visited> visit(const TransformResult& result, CursorPos cursor, const Lines& linesAfter,
                      bool& anyApplied) {
  vector<Visited> visited;
  anyApplied = result.forEachEditFrom(cursor, linesAfter, [&](const Result& r, CursorPos goal) {
    visited.push_back({r.getSequence().str(), goal});
  });
  return visited;
}

}  // namespace

TEST(TransformResultEntryCurswantTest, MatchingCurswantAppliesEveryResult) {
  TransformResult result = resultWithEveryClass();
  bool anyApplied = false;
  auto visited = visit(result, CursorPos(0, 0), Lines{"0123456789"}, anyApplied);

  EXPECT_TRUE(anyApplied);
  ASSERT_EQ(visited.size(), 3u);
  for (const Visited& v : visited) EXPECT_EQ(v.goal, CursorPos(0, 0)) << v.sequence;
}

TEST(TransformResultEntryCurswantTest, ClampedCursorSkipsRequiredAndFollowsCurswant) {
  TransformResult result = resultWithEveryClass();
  bool anyApplied = false;
  auto visited = visit(result, CursorPos(0, 0, 6), Lines{"0123456789"}, anyApplied);

  EXPECT_TRUE(anyApplied);
  ASSERT_EQ(visited.size(), 2u);
  EXPECT_EQ(visited[0].sequence, "x");
  EXPECT_EQ(visited[0].goal, CursorPos(0, 0));
  EXPECT_EQ(visited[1].sequence, "dd");
  EXPECT_EQ(visited[1].goal, CursorPos(0, 6));
}

TEST(TransformResultEntryCurswantTest, FollowedLandingResetsCurswantToClampedColumn) {
  TransformResult result = resultWithEveryClass();
  bool anyApplied = false;
  auto visited = visit(result, CursorPos(0, 0, 6), Lines{"0123"}, anyApplied);

  ASSERT_EQ(visited.size(), 2u);
  EXPECT_EQ(visited[1].goal.col, 3);
  EXPECT_EQ(visited[1].goal.targetCol, 3);
}

TEST(TransformResultEntryCurswantTest, OnlyRequiredResultsMeansNothingApplies) {
  Lines initial = {"a"};
  vector<vector<Result>> buckets = {{Result(Sequence("ddx"), 3.0, EntryCurswant::Required)}};
  TransformResult result(std::move(buckets), TransformSearchStats{}, initial);

  bool anyApplied = true;
  auto visited = visit(result, CursorPos(0, 0, 6), Lines{"0123"}, anyApplied);

  EXPECT_FALSE(anyApplied);
  EXPECT_TRUE(visited.empty());
}
