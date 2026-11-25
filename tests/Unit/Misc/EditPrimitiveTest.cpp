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
import vf.tests.Utils.InterpreterModelReplay;
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

TEST_F(EditPrimitiveTest, BracketObjectsUseQuoteContextWhenFindingTheCloser) {
  for (const auto& [lines, start] : vector<pair<Lines, CursorPos>>{
           {{R"src("(x"))src"}, {0, 2}},
           {{R"src("(x)")src"}, {0, 2}},
           {{R"src(a(x")z)src"}, {0, 2}},
           {{R"src(a(x "\"" ')' y)z)src"}, {0, 2}},
           {{R"src(f(x "hello\)src", R"src(still ) quoted" y)z)src"}, {0, 2}},
           {{R"src("previous\)src", R"src(f(x")z)src"}, {1, 2}},
       }) {
    OracleReplay::expectModelMatchesOracle(*oracle, lines, start, "da(");
    OracleReplay::expectModelMatchesOracle(*oracle, lines, start, "ca(<Esc>");
  }
}

TEST_F(EditPrimitiveTest, QuoteObjectsRespectEscapedQuotes) {
  for (const string& line : {R"(5'b\'! tail)"s, R"(a 'b\'c' z)"s, R"(a 'b\\' z)"s,
                             R"(a 'p' \'bc' z)"s}) {
    for (int col = 0; col < ssize(line); ++col) {
      OracleReplay::expectModelMatchesOracle(*oracle, {line}, {0, col}, "di'");
      OracleReplay::expectModelMatchesOracle(*oracle, {line}, {0, col}, "ci'<Esc>");
    }
  }
}

TEST_F(EditPrimitiveTest, OpenLineAutoindentsFromCurrentLine) {
  OracleReplay::expectModelMatchesOracle(*oracle, {"  a", "c"}, {0, 0}, "ob<Esc>");
  OracleReplay::expectModelMatchesOracle(*oracle, {"a", "  c"}, {1, 0}, "Ob<Esc>");
  OracleReplay::expectModelMatchesOracle(*oracle, {"  a", "    c"}, {1, 2}, "Ob<Esc>");
  OracleReplay::expectModelMatchesOracle(*oracle, {"  a"}, {0, 0}, "Ox<CR>y<Esc>");
}

TEST_F(EditPrimitiveTest, OpenLineIndentationAtTabStopMatchesNeovim) {
  const Lines lines{"  \t     source"};
  for (const bool ai : {true, false}) {
    for (const bool et : {false, true}) {
      for (const bool ci : {false, true}) {
        const IndentOptions options{ai, et, ci, 4, 2};
        const string settings = format("{}ai {}et {}ci ts=4 sw=2", ai ? "" : "no",
                                       et ? "" : "no", ci ? "" : "no");
        for (const string cmd : {"oX<Esc>", "OX<Esc>", "AX<CR>Y<Esc>", "ccX<Esc>", "o<BS>X<Esc>"}) {
          SCOPED_TRACE(settings + " " + cmd);
          const auto actual = oracle->simulate(lines, 0, 8, cmd, settings);
          const auto model = applyUserSequence(lines, {0, 8}, cmd, options);
          EXPECT_EQ(model.lines, actual.lines);
          EXPECT_EQ(model.cursor, CursorPos(actual.row, actual.col));
          EXPECT_EQ(model.mode, actual.mode);
        }
      }
    }
  }
}

TEST_F(EditPrimitiveTest, OpenLineEscStripsUnusedAutoindent) {
  OracleReplay::expectModelMatchesOracle(*oracle, {"  a"}, {0, 0}, "O<Esc>");
  OracleReplay::expectModelMatchesOracle(*oracle, {"  a"}, {0, 0}, "o<Esc>");
}

TEST_F(EditPrimitiveTest, BuildTypedCommands_MatchesOracle_CC) {
  Lines initialLines = {"        deep indent"};
  Lines goalLines = {"    less"};

  // `cc` keeps the line's autoindent; the typed sequence must correct it.
  KeyedSequence typed = buildTypedCommands(goalLines, IndentOptions::leadingIndent(initialLines[0]));
  auto result = oracle->simulate(initialLines, 0, 0, "cc" + typed.seq.str());

  EXPECT_EQ(result.lines, goalLines) << "typed sequence: " << typed.seq;
}

TEST_F(EditPrimitiveTest, BuildTypedCommands_MultiLine) {
  Lines initialLines = {"    original"};
  Lines goalLines = {"    first", "    second", "third"};

  KeyedSequence typed = buildTypedCommands(goalLines, IndentOptions::leadingIndent(initialLines[0]));
  auto result = oracle->simulate(initialLines, 0, 0, "cc" + typed.seq.str());

  EXPECT_EQ(result.lines, goalLines) << "typed sequence: " << typed.seq;
}

TEST_F(EditPrimitiveTest, BuildTypedCommands_EmitsCanonicalKeyNotation) {
  Lines goalLines = {"a <b\tc"};
  KeyedSequence typed = buildTypedCommands(goalLines);

  EXPECT_EQ(typed.seq, "a<Space><lt>b<C-v><Tab>c<Esc>");

  auto result = oracle->simulate({""}, 0, 0, "i" + typed.seq.str());
  EXPECT_EQ(result.lines, goalLines);
}
