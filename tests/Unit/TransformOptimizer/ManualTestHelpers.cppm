module;
#include <gtest/gtest.h>
#include <cassert>
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
export import vf.tests.Utils.InterpreterModelReplay;
export import vf.tests.Utils.NeovimOracle;

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
};

struct ApplyResult {
  Lines lines;
  CursorPos pos;
  Mode mode;

  ApplyResult(Lines lines, CursorPos pos, Mode mode = Mode::Normal)
      : lines(std::move(lines)), pos(pos), mode(mode) {}
};

inline ApplyResult applySequence(const Lines& source, CursorPos initialPos,
                                 const std::string& sequence) {
  ApplyResult result(source, initialPos);
  DotRepeat lastEdit;
  auto parsed = Edit::parseEdits(sequence);
  assert(parsed);
  for (const auto& op : *parsed) {
    Edit::applyEdit(result.lines, result.pos, result.mode, op, &lastEdit);
  }
  return result;
}

inline bool cursorStateMatches(const ApplyResult& ours,
                               const SimulationResult& nvim) {
  return ours.pos.line == nvim.row && ours.pos.col == nvim.col &&
         ours.mode == nvim.mode;
}

inline SimulationResult verifySequenceWithOracle(
    NeovimOracle* oracle,
    const Lines& source,
    CursorPos initialPos,
    const std::string& sequence) {
  SimulationResult nvim =
      oracle->simulate(source, initialPos.line, initialPos.col, sequence);
  ApplyResult ours = applySequence(source, initialPos, sequence);

  EXPECT_EQ(ours.lines, nvim.lines)
      << "Lines mismatch for seq='" << sequence << "' from " << initialPos << "\n"
      << "  Source: " << source << "\n"
      << "  Ours:   " << ours.lines << "\n"
      << "  Neovim: " << nvim.lines;

  EXPECT_TRUE(cursorStateMatches(ours, nvim))
      << "Cursor mismatch for seq='" << sequence << "'\n"
      << "  Ours:   " << ours.pos << " mode=" << static_cast<int>(ours.mode) << "\n"
      << "  Neovim: (" << nvim.row << "," << nvim.col
      << ") mode=" << static_cast<int>(nvim.mode);

  return nvim;
}

inline SimulationResult verifyUserSequenceWithOracle(
    NeovimOracle* oracle,
    const Lines& source,
    CursorPos initialPos,
    const std::string& sequence) {
  SimulationResult nvim =
      oracle->simulate(source, initialPos.line, initialPos.col, sequence);
  InterpreterReplayResult ours =
      applyUserSequence(source, initialPos, sequence);

  EXPECT_EQ(ours.lines, nvim.lines)
      << "Lines mismatch for seq='" << sequence << "' from " << initialPos << "\n"
      << "  Source: " << source << "\n"
      << "  Ours:   " << ours.lines << "\n"
      << "  Neovim: " << nvim.lines;

  EXPECT_EQ(ours.cursor.line, nvim.row);
  EXPECT_EQ(ours.cursor.col, nvim.col);
  EXPECT_EQ(ours.mode, nvim.mode);

  return nvim;
}

inline bool allPositionsValid(const std::vector<std::vector<Result>>& results,
                              const Lines& source) {
  int idx = 0;
  for (int r = 0; r < static_cast<int>(source.size()); r++) {
    for (int c = 0; c < source[r].effectiveSize(); c++) {
      if (results[idx++].empty()) {
        return false;
      }
    }
  }
  return true;
}

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
