// tests/Unit/Misc/EditPrimitiveTest.cpp
//
// VimCore edit primitives whose inputs the generated oracle properties
// deliberately exclude: hidden-context line deletes, comment-leader joins,
// closer-after-terminator sentences, and typed-insert key notation.

#include <gtest/gtest.h>

import std.compat;
import vf.Optimizer.BuildTypedCommands;
import vf.Optimizer.Indentation;
import vf.Types.Lines;
import vf.tests.Utils.NeovimOracle;
import vf.tests.Utils.OracleReplay;
import vf.VimCore.VimEditUtils;

using namespace std;

class EditPrimitiveTest : public ::testing::Test {
protected:
  inline static unique_ptr<NeovimOracle> oracle;
  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }
};

TEST_F(EditPrimitiveTest, LinewiseDeleteAllVisibleLinesWithHiddenAboveEscapesAbove) {
  Lines lines = {"first", "second"};
  CursorPos pos(0, 1);

  VimCore::deleteLineRangeAndUpdatePos(
      lines, LineRange(0, 2), pos,
      VimCore::LineDeleteContext{.hasLinesAbove = true});

  EXPECT_EQ(lines, Lines{""});
  EXPECT_EQ(pos.line, -1);
}

TEST_F(EditPrimitiveTest, LinewiseDeleteAllVisibleLinesPrefersHiddenBelow) {
  Lines lines = {"first", "second"};
  CursorPos pos(0, 1);

  VimCore::deleteLineRangeAndUpdatePos(
      lines, LineRange(0, 2), pos,
      VimCore::LineDeleteContext{
          .hasLinesAbove = true,
          .hasLinesBelow = true,
      });

  EXPECT_EQ(lines, Lines{""});
  EXPECT_EQ(pos.line, static_cast<int>(lines.size()));
}

TEST_F(EditPrimitiveTest, J_DoesNotStripCommentLeaders) {
  // We deliberately do not model Vim's `'comments'` option (NeovimOracle
  // clears it), so `#`/`-` stay out of the join property's alphabet.
  OracleReplay::expectModelMatchesOracle(*oracle, {"x- ", "#  1"}, {0, 0}, "J");
}

TEST_F(EditPrimitiveTest, ForwardSentenceDeleteIncludesLineEndCloser) {
  OracleReplay::expectModelMatchesOracle(
      *oracle, {"MM ~ ~", "o 0", "S!)", "~   -]"}, {0, 2}, "d)");
}

TEST_F(EditPrimitiveTest, BuildTypedCommands_MatchesOracle_CC) {
  Lines initialLines = {"        deep indent"};
  Lines goalLines = {"    less"};

  // `cc` keeps the line's autoindent; the typed sequence must correct it.
  KeyedSequence typed = buildTypedCommands(goalLines, leadingWhitespace(initialLines[0]));
  auto result = oracle->simulate(initialLines, 0, 0, "cc" + typed.seq.str());

  EXPECT_EQ(result.lines, goalLines) << "typed sequence: " << typed.seq;
}

TEST_F(EditPrimitiveTest, BuildTypedCommands_MultiLine) {
  Lines initialLines = {"    original"};
  Lines goalLines = {"    first", "    second", "third"};

  KeyedSequence typed = buildTypedCommands(goalLines, leadingWhitespace(initialLines[0]));
  auto result = oracle->simulate(initialLines, 0, 0, "cc" + typed.seq.str());

  EXPECT_EQ(result.lines, goalLines) << "typed sequence: " << typed.seq;
}

TEST_F(EditPrimitiveTest, BuildTypedCommands_EmitsCanonicalKeyNotation) {
  Lines goalLines = {"a <b\tc"};
  KeyedSequence typed = buildTypedCommands(goalLines);

  EXPECT_EQ(typed.seq, "a<Space><lt>b<Tab>c<Esc>");

  auto result = oracle->simulate({""}, 0, 0, "i" + typed.seq.str());
  EXPECT_EQ(result.lines, goalLines);
}
