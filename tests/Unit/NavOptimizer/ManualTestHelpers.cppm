module;
#include <gtest/gtest.h>

export module vf.tests.Unit.NavOptimizer.ManualTestHelpers;

import std.compat;
export import vf.Boundary.NavBoundary;
export import vf.Keyboard.Config;
export import vf.Optimizer.NavOptimizer.BufferIndex;
export import vf.Optimizer.NavOptimizer.NavExplorer;
export import vf.Optimizer.NavOptimizer.NavOptimizer;
export import vf.Session.Snapshot;
export import vf.Types.Lines;
export import vf.Types.NavContext;
export import vf.tests.Utils.TestUtils;

export {

class NavOptimizer_ManualTest : public ::testing::Test {
protected:
  inline static Lines a1_long_line;
  inline static Lines a2_block_lines;
  inline static Lines a3_spaced_lines;
  inline static Lines m1_main_basic;
  inline static NavContext navContext;

  static void SetUpTestSuite() {
    a1_long_line = TestFiles::load("a1_long_line.txt");
    a2_block_lines = TestFiles::load("a2_block_lines.txt");
    a3_spaced_lines = TestFiles::load("a3_spaced_lines.txt");
    m1_main_basic = TestFiles::load("m1_main_basic.txt");
    navContext = NavContext();
  }

  static std::vector<LandingResult>
  runOptimizer(const Lines& lines, CursorPos start,
               CursorPos end, const std::string& userSeq,
               std::vector<KeyAdjustment> adjustments = {},
               Config config = Config::uniform()) {
    for (KeyAdjustment ka : adjustments) {
      config.keyInfo[static_cast<size_t>(ka.k)].base_cost = ka.cost;
    }
    return runNavOptimizer(lines, start, end, userSeq, config, NavBoundary{}, navContext);
  }
};

class NavBoundaryTest : public ::testing::Test {
protected:
  struct CountedCandidate {
    std::string sequence;
    CursorPos endpoint;
  };

  struct StaticCandidate {
    std::string sequence;
    CursorPos endpoint;
  };

  inline static NavContext navContext;

  static void SetUpTestSuite() {
    navContext = NavContext();
  }

  static std::vector<LandingResult>
  runWithBoundary(const Lines& lines, CursorPos start, CursorPos end,
                  const std::string& userSeq, const NavBoundary& boundary,
                  Config config = Config::uniform()) {
    return runNavOptimizer(lines, start, end, userSeq, config, boundary, navContext);
  }

  static std::vector<CountedCandidate> collectCountedCandidates(
      const Lines& lines,
      CursorPos start,
      CursorPos goal,
      const NavBoundary& boundary,
      NavOptimizerParams params = NavOptimizerParams{}
          .withMinCountRepeat(2)
          .withMaxCountRepeat(8)) {
    Config config = Config::uniform();
    BufferIndex index(lines);
    CharInterval goalRange(goal, goal);
    NavExplorer explorer(lines, navContext, boundary, params, goalRange, index, 0);

    auto score = [](CursorPos, double effort) { return effort; };
    NavStateFactory states(config, score);
    NavState base = states.initial(start);

    std::vector<CountedCandidate> candidates;
    auto onCounted = [&](KSId, const KeyedSequence& ks, int count,
                         CursorPos endpoint, double) {
      KeyedSequence counted(count, ks);
      candidates.push_back({counted.seq.str(), endpoint});
    };
    auto onFMotion = [](const KeyedSequence&, int) {};
    explorer.exploreCountedMotions(base, onCounted, onFMotion);
    return candidates;
  }

  static std::vector<StaticCandidate> collectStaticCandidates(
      const Lines& lines,
      CursorPos start,
      CursorPos goal,
      const NavBoundary& boundary,
      NavOptimizerParams params = NavOptimizerParams{}) {
    Config config = Config::uniform();
    BufferIndex index(lines);
    CharInterval goalRange(goal, goal);
    NavExplorer explorer(lines, navContext, boundary, params, goalRange, index, 0);

    auto score = [](CursorPos, double effort) { return effort; };
    NavStateFactory states(config, score);
    NavState base = states.initial(start);

    std::vector<StaticCandidate> candidates;
    auto onStatic = [&](KSId, const KeyedSequence& ks, CursorPos endpoint) {
      candidates.push_back({ks.seq.str(), endpoint});
    };
    explorer.exploreAllStandardMotions(base, onStatic);
    return candidates;
  }

  static bool hasCountedCandidate(
      const std::vector<CountedCandidate>& candidates,
      const std::string& sequence,
      CursorPos endpoint) {
    return std::any_of(candidates.begin(), candidates.end(),
        [&](const CountedCandidate& candidate) {
          return candidate.sequence == sequence && candidate.endpoint == endpoint;
        });
  }

  static bool hasStaticCandidate(
      const std::vector<StaticCandidate>& candidates,
      const std::string& sequence) {
    return std::any_of(candidates.begin(), candidates.end(),
        [&](const StaticCandidate& candidate) {
          return candidate.sequence == sequence;
        });
  }
};

}  // export
