// tests/Unit/Commands/EditInterpreterNavMotionTest.cpp
//
// Bare nav motions (`j`, `w`, `$`, ...) can reach `Edit::applyEdit` as tokens
// of a TransformOptimizer result sequence or inside `v{motion}d`. They must
// behave exactly like the same motion through MovementInterpreter and Neovim.
// Pinned drift: `$` not setting curswant to EOL (`$j`, `2$`), `w`/`e` landing
// past the last char at buffer end, counted `h`/`j`/`l` asserting instead of
// clamping.

#include <gtest/gtest.h>

import std.compat;
import vf.Interpreter.EditInterpreter;
import vf.Interpreter.MovementInterpreter;
import vf.tests.Utils.NeovimOracle;

using namespace std;

class EditInterpreterNavMotionTest : public ::testing::Test {
protected:
  inline static unique_ptr<NeovimOracle> oracle;
  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }

  // Replays `seq` through applyEdit and checks it against both the oracle and
  // MovementInterpreter; `expected` documents the Vim result.
  void verify(const Lines& lines, CursorPos start, const string& seq,
              CursorPos expected) {
    SCOPED_TRACE(::testing::Message() << "seq='" << seq << "' from " << start
                                      << " on " << lines);
    auto nvim = oracle->simulate(lines, start.line, start.col, seq);
    ASSERT_EQ(nvim.lines, lines);
    EXPECT_EQ(CursorPos(nvim.row, nvim.col), expected);

    Lines editLines = lines;
    CursorPos editPos = start;
    Mode mode = Mode::Normal;
    auto parsed = Edit::parseEdits(seq);
    ASSERT_TRUE(parsed.has_value());
    for (const auto& e : *parsed) Edit::applyEdit(editLines, editPos, mode, e);
    EXPECT_EQ(editLines, lines);
    EXPECT_EQ(mode, Mode::Normal);
    EXPECT_EQ(editPos, expected);

    CursorPos movePos = simulateMovements(start, seq, lines);
    EXPECT_EQ(movePos, expected);
    EXPECT_EQ(editPos.targetCol, movePos.targetCol);
  }
};

TEST_F(EditInterpreterNavMotionTest, DollarSetsCurswantToEndOfLine) {
  verify({"ab", "abcdef"}, CursorPos(0, 0), "$j", CursorPos(1, 5));
  verify({"abcdef", "ab"}, CursorPos(1, 0), "$k", CursorPos(0, 5));
}

TEST_F(EditInterpreterNavMotionTest, CountedDollarMovesDown) {
  verify({"ab", "abcdef"}, CursorPos(0, 0), "2$", CursorPos(1, 5));
  verify({"ab", "abcdef"}, CursorPos(0, 0), "5$", CursorPos(1, 5));
}

TEST_F(EditInterpreterNavMotionTest, WordMotionsClampAtBufferEnd) {
  verify({"abc def"}, CursorPos(0, 4), "w", CursorPos(0, 6));
  verify({"abc def"}, CursorPos(0, 0), "5w", CursorPos(0, 6));
  verify({"a-b c-d"}, CursorPos(0, 4), "W", CursorPos(0, 6));
  verify({"abc  "}, CursorPos(0, 1), "w", CursorPos(0, 4));
  verify({"abc  "}, CursorPos(0, 1), "e", CursorPos(0, 2));
  verify({"abc def"}, CursorPos(0, 6), "e", CursorPos(0, 6));
  verify({"abc def"}, CursorPos(0, 0), "5e", CursorPos(0, 6));
}

TEST_F(EditInterpreterNavMotionTest, CountedHjklClamp) {
  verify({"abc"}, CursorPos(0, 1), "5l", CursorPos(0, 2));
  verify({"abc"}, CursorPos(0, 1), "5h", CursorPos(0, 0));
  verify({""}, CursorPos(0, 0), "l", CursorPos(0, 0));
  verify({"a", "b"}, CursorPos(0, 0), "5j", CursorPos(1, 0));
  verify({"a", "b"}, CursorPos(1, 0), "j", CursorPos(1, 0));
  verify({"a", "b"}, CursorPos(1, 0), "5k", CursorPos(0, 0));
}
