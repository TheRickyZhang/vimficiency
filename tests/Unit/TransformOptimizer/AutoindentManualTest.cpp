#include <gtest/gtest.h>

import std.compat;
import vf.tests.Unit.TransformOptimizer.ManualTestHelpers;

using namespace std;

namespace {

struct AutoindentCase {
  const char* name;
  Lines fullBuffer;
  CursorPos begin;
  CursorPos end;
  Lines goal;          // edit-region goal
  Lines expectedFull;  // full buffer after the edit
};

// Linewise changes (cc/cj/ck/Ncc) keep the source line's autoindent, which
// the typed suffix must correct up or down; embedded cases also exercise the
// collapse-with-BS path into a prefix line.
TEST_F(TransformOptimizer_ManualTest, AutoindentLinewiseChangesReplayToGoal) {
  const vector<AutoindentCase> cases = {
      {"matching indent", {"    aaa", "    bbb"}, {0, 0}, {1, 7}, {"    xxx"}, {"    xxx"}},
      {"excess autoindent", {"        aaa", "    bbb"}, {0, 0}, {1, 7}, {"    xxx"}, {"    xxx"}},
      {"insufficient autoindent", {"  aaa", "    bbb"}, {0, 0}, {1, 7}, {"        xxx"}, {"        xxx"}},
      {"counted cc", {"    aaa", "    bbb", "    ccc"}, {0, 0}, {2, 7}, {"    xxx"}, {"    xxx"}},
      {"no indent", {"aaa", "bbb"}, {0, 0}, {1, 3}, {"    xxx"}, {"    xxx"}},
      {"boundary context", {"context_above", "    aaa", "    bbb", "context_below"},
       {1, 0}, {2, 7}, {"    xxx"}, {"context_above", "    xxx", "context_below"}},
      {"collapse with BS", {"prefix", "    aaa", "    bbb", "suffix"},
       {1, 0}, {2, 7}, {"    xxx"}, {"prefix", "    xxx", "suffix"}},
  };

  for (const AutoindentCase& test : cases) {
    Lines editRegion = test.fullBuffer.getSpan(test.begin, test.end);
    TransformBoundary boundary(test.fullBuffer, test.begin, test.end);

    TransformResult res = opt.optimizeTransform(editRegion, test.goal, boundary, params);
    ASSERT_FALSE(res.getResults()[0].empty()) << test.name;

    forEachValidResult(res.getResults(), editRegion, [&](CursorPos pos, const auto& seq) {
      CursorPos fullPos(pos.line + test.begin.line,
                        pos.line == 0 ? pos.col + test.begin.col : pos.col);
      EXPECT_TRUE(OracleReplay::matches(
          *oracle, test.fullBuffer, fullPos, seq.str(), test.expectedFull,
          nullopt, Mode::Normal, test.name));
    });
  }
}

}  // namespace
