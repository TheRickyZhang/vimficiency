module;
#include <gtest/gtest.h>
#include <climits>

export module vf.tests.Unit.TransformOptimizer.ManualTestHelpers;

import std.compat;
export import vf.Boundary.TransformBoundary;
export import vf.Interpreter.EditInterpreter;
export import vf.Keyboard.Config;
export import vf.Optimizer.TransformOptimizer.TransformOptimizer;
export import vf.Types.CursorPos;
export import vf.Types.Lines;
export import vf.Types.Mode;
export import vf.tests.Utils.NeovimOracle;
export import vf.tests.Utils.OracleReplay;

export {

class TransformOptimizer_ManualTest : public ::testing::Test {
protected:
  inline static std::unique_ptr<NeovimOracle> oracle;
  Config config = Config::uniform();
  TransformOptimizerParams params = TransformOptimizerParams{}.withMaxResults(INT_MAX);
  TransformOptimizer opt{config};

  static void SetUpTestSuite() { oracle = std::make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }

  TransformResult pureDeletionResult(const Lines& initialLines,
                                     TransformBoundary boundary) {
    return opt.optimizePureDeletion(initialLines, boundary, params);
  }

  // Replays the first `maxChecked` results of one start-position bucket from
  // `fullStart` in `fullBuffer` and expects each to reach `expectedFull`.
  void expectBucketReplays(std::span<const Result> bucket,
                           const Lines& fullBuffer, CursorPos fullStart,
                           const Lines& expectedFull, std::string_view context,
                           size_t maxChecked = SIZE_MAX,
                           std::optional<CursorPos> goalPos = std::nullopt) {
    ASSERT_FALSE(bucket.empty()) << context;
    size_t checked = std::min(maxChecked, bucket.size());
    for (size_t i = 0; i < checked; i++) {
      EXPECT_TRUE(OracleReplay::matches(
          *oracle, fullBuffer, fullStart, bucket[i].getSequence().str(),
          expectedFull, goalPos, Mode::Normal, context))
          << "sequence=" << bucket[i].getSequence().str();
    }
  }
};

template<typename Fn>
void forEachValidResult(const std::vector<std::vector<Result>>& results,
                        const Lines& lines, Fn fn) {
  int idx = 0;
  for (int r = 0; r < static_cast<int>(lines.size()); r++) {
    for (int c = 0; c < lines[r].effectiveSize(); c++) {
      const auto& bucket = results[idx++];
      if (!bucket.empty()) {
        fn(CursorPos(r, c), bucket[0].getSequence());
      }
    }
  }
}

}  // export
