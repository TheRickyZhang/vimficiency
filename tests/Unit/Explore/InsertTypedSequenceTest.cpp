#include <gtest/gtest.h>

import std.compat;
import vf.tests.Unit.Explore.TestHelpers;
import vf.Optimizer.CompositionOptimizer.CompositionStrategies;
import vf.Optimizer.DiffPlanner.DiffState;

using namespace std;

namespace {

// Explore's Insert-phase recommendation must be one of the typed payloads
// CompositionStrategies enumerates for the same planned insertion, with the
// same exit cursor.
struct Case {
  const char* name;
  Lines initial;
  Lines goal;
  CursorPos start;
};

class InsertTypedSequenceTest : public ExploreViewTest {};

TEST_F(InsertTypedSequenceTest, MatchesCompositionStrategies) {
  const vector<Case> cases = {
      {"empty line", {"a", "", "c"}, {"a", "xy", "c"}, {1, 0}},
      {"new line below indented", {"  a", "c"}, {"  a", "  b", "c"}, {0, 0}},
      {"new line below unindented", {"a", "c"}, {"a", "b", "c"}, {0, 0}},
      {"new line below multi", {"  a", "c"}, {"  a", "  b", "    d", "c"}, {0, 0}},
      {"end of buffer append", {"ab"}, {"ab", "cd"}, {0, 1}},
      {"end of line", {"ab"}, {"abX"}, {0, 1}},
      {"mid line", {"ab cd"}, {"ab XY cd"}, {0, 3}},
      {"mid line multi indented suffix", {"ab   cd"}, {"ab X", "Y   cd"}, {0, 3}},
      {"first non blank at fnb", {"  ab"}, {"  Xab"}, {0, 2}},
      {"first non blank from col 0", {"  ab"}, {"  Xab"}, {0, 0}},
      {"blank line at fnb", {"  ", "z"}, {"  X", "z"}, {0, 2}},
  };

  for (const Case& c : cases) {
    SCOPED_TRACE(c.name);
    auto view = makeView(c.initial, c.start, c.goal, c.goal.lastPos());
    ASSERT_EQ(view.totalEdits(), 1);
    ASSERT_TRUE(view.beginInsert().has_value());
    auto recs = view.recommendations(5);
    ASSERT_EQ(recs.size(), 1u);
    const Suggestion& rec = recs[0];

    CharRange range = view.currentTargetRange();
    ASSERT_EQ(range.begin, range.end);
    const string initialFlat = c.initial.flatten();
    const string goalFlat = c.goal.flatten();
    const int flat = DiffText::positionToFlatIndex(range.begin, c.initial);
    DiffState diff(range.begin, range.begin, "",
                   goalFlat.substr(flat, goalFlat.size() - initialFlat.size()),
                   TransformBoundary(c.initial, range.begin, range.begin));

    vector<pair<string, CursorPos>> enumerated;
    CompositionStrategies::enumerateInsertions(
        diff, c.initial, [&](const CompositionStrategies::Insertion& s) {
          string typed = s.insertCmd.substr(s.structural.size());
          ASSERT_TRUE(typed.ends_with("<Esc>"));
          typed.resize(typed.size() - 5);
          enumerated.emplace_back(std::move(typed), s.goalPos);
        });
    ASSERT_FALSE(enumerated.empty());

    const string token(string_view(rec.token));
    bool matched = false;
    for (const auto& [typed, exit] : enumerated) {
      if (typed != token) continue;
      matched = true;
      EXPECT_EQ(rec.landingPos, exit) << "typed " << typed;
    }
    EXPECT_TRUE(matched) << "Explore typed '" << token
                         << "' not among composition strategies";
  }
}

}  // namespace
