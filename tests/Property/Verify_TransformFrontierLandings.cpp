#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

import std.compat;
import vf.Optimizer.TransformOptimizer.TransformFrontier;
import vf.tests.Property.PropertyDomains;
import vf.tests.Utils.OracleReplay;

using namespace std;

namespace {

class TransformFrontierLandingPropertyTest {
 public:
  void DeletionSuggestionsReplay(const string& prefix, const string& body, const string& suffix,
                                 const string& delimiters, bool around, int cursorCol,
                                 const string& previousLine) {
    const string object = delimiters.front() + body + delimiters.back();
    const Lines before = {previousLine, prefix + object + suffix, "below"};
    const int beginCol = prefix.size() + (around ? 0 : 1);
    const string& deleted = around ? object : body;
    const CursorPos begin(1, beginCol), end(1, beginCol + deleted.size());
    const DiffState diff(begin, end, deleted, "", TransformBoundary(before, begin, end));
    const CursorPos start(1, min(cursorCol, before[1].lastCol()));
    const auto suggestions = collectTransformEdits(
        TransformFrontierQuery{FrontierQuery{.lines = before, .cursor = start}, diff},
        config_, bank_);

    Lines expected = before;
    expected[1].erase(begin.col, deleted.size());
    for (const auto& suggestion : suggestions) {
      EXPECT_TRUE(OracleReplay::matches(oracle_, before, start, suggestion.token, expected,
                                        suggestion.landingPos, Mode::Normal, "Explore delete"));
    }
  }

 private:
  Config config_ = Config::uniform();
  EffortBank bank_{config_};
  NeovimOracle oracle_;
};

}  // namespace

FUZZ_TEST_F(TransformFrontierLandingPropertyTest, DeletionSuggestionsReplay)
    .WithDomains(PropertyDomains::LineTextDomain(0, 6), PropertyDomains::LineTextDomain(1, 8),
                 PropertyDomains::LineTextDomain(0, 6),
                 fuzztest::ElementOf<string>({"()", "[]", "{}", "\"\"", "''", "``"}),
                 fuzztest::Arbitrary<bool>(), fuzztest::InRange<int>(0, 21),
                 PropertyDomains::LineTextDomain(0, 12))
    .WithSeeds({{"abc", "one two", "", "()", true, 4, "above"},
                {"abc", "one two", "", "\"\"", true, 4, "above"},
                {"   ", "AAkW} ~|", "<~", "``", true, 0, "above"},
                {"", "=~", "", "{}", true, 0, "above"},
                {"\\", "O~~M~ ", "NN+", "()", false, 6, "above"},
                {"D\"Z", "]  o!\"", ";~~", "()", true, 6, "above"},
                {"abc ", "x", " z", "()", true, 1, "( outer"},
                {"5", "`\"\\", "! H@~", "''", false, 5, "above"}});
