module;
#include <gtest/gtest.h>

export module vf.tests.Unit.CompositionOptimizer.ManualTestHelpers;

import std.compat;
export import vf.Boundary.NavBoundary;
export import vf.Keyboard.Config;
export import vf.Optimizer.CompositionOptimizer.CompositionOptimizer;
export import vf.Types.CursorPos;
export import vf.Types.Lines;
export import vf.Types.Mode;
export import vf.tests.Utils.NeovimOracle;
export import vf.tests.Utils.OracleReplay;

export {

class CompositionOptimizer_ManualTest : public ::testing::Test {
protected:
  inline static std::unique_ptr<NeovimOracle> oracle;
  Config config = Config::uniform();
  CompositionOptimizer opt{config};
  CompositionOptimizerParams params{};

  static void SetUpTestSuite() { oracle = std::make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }

  void verifySingleResult(
      const Result& result,
      const Lines& initial, CursorPos initialPos,
      const Lines& goal,
      const std::string& context = "") {
    EXPECT_TRUE(OracleReplay::matches(
        *oracle, initial, initialPos, result.getSequence().str(),
        goal, std::nullopt, Mode::Normal, context));
  }

  void expectHasValidResults(
      const std::vector<Result>& results,
      const Lines& initial,
      CursorPos initialPos,
      const Lines& goal,
      const std::string& testContext = "") {

    ASSERT_FALSE(results.empty())
        << "No results returned" << (testContext.empty() ? "" : " (" + testContext + ")");

    for (size_t i = 0; i < results.size(); i++) {
      const auto& seq = results[i].getSequence();
      EXPECT_TRUE(OracleReplay::matches(
          *oracle, initial, initialPos, seq.str(),
          goal, std::nullopt, Mode::Normal,
          testContext.empty() ? "result " + std::to_string(i)
                              : testContext + " result " + std::to_string(i)));
    }
  }
};

}  // export
