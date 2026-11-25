// tests/Unit/CompositionOptimizer/BracketQuoteContextTest.cpp
//
// Validates TextObjectContext masks: for each column on an edit's line,
// the mask should agree with Neovim on whether ci"/ca"/ci(/ca( etc.
// from that column produces the exact edit region.
//
// Run: ./build/tests/vimfy_unit_tests --gtest_filter="TextObjectContextTest.*"

#include <gtest/gtest.h>

import std.compat;
import vf.Boundary.NavBoundary;
import vf.Types.NavContext;
import vf.Optimizer.CompositionOptimizer.CompositionSearchContext;
import vf.Keyboard.Config;
import vf.Types.Lines;
import vf.tests.Utils.NeovimOracle;

using namespace std;

namespace {
bool isQuoteChar(char c) { return c == '"' || c == '\'' || c == '`'; }
} // namespace

class TextObjectContextTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  Config config = Config::uniform();
  CompositionOptimizerParams params{};

  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }

  CompositionSearchContext makeContext(const Lines& initial, const Lines& goal) {
    return CompositionSearchContext(
        initial, CursorPos(0, 0), goal, CursorPos(0, 0), "",
        NavContext(), NavBoundary(), params, config);
  }

  // Validate mask for any delimiter type (quote or bracket) against oracle.
  void validateMask(
      const Lines& initial, const Lines& goal,
      const BracketQuoteContext& toCtx, const DiffState& diff, char delim) {
    if (toCtx.line < 0) return;

    bool quote = isQuoteChar(delim);
    bool around = quote ? toCtx.useAroundQuote.seen(delim)
                        : toCtx.useAroundBracket.seen(delim);
    string cmd = "c"s + (around ? "a" : "i") + delim + diff.insertedText + "<Esc>";

    int lineLen = static_cast<int>(initial[toCtx.line].size());
    for (int col = 0; col < lineLen; col++) {
      bool maskValid = quote
          ? (col < static_cast<int>(toCtx.validQuoteMask.size())
             && toCtx.validQuoteMask[col].seen(delim))
          : (col < static_cast<int>(toCtx.validBracketMask.size())
             && toCtx.validBracketMask[col].seen(delim));

      auto result = oracle->simulate(initial, toCtx.line, col, cmd);
      EXPECT_EQ(maskValid, result.lines == goal)
          << "'" << delim << "' mismatch at col " << col
          << " on '" << initial[toCtx.line] << "'\n"
          << "  Mask: " << (maskValid ? "valid" : "invalid")
          << "  Cmd: " << cmd << "\n"
          << "  Oracle: " << result.lines << "\n"
          << "  Goal:   " << goal;
    }
  }

  // Convenience: make context and validate a single-edit case
  void validateSingleEdit(const Lines& initial, const Lines& goal, char delim) {
    auto ctx = makeContext(initial, goal);
    ASSERT_EQ(ctx.totalEdits(), 1);
    validateMask(initial, goal, ctx.edits[0].bracketQuoteContext, ctx.edits[0].diffState, delim);
  }
};

unique_ptr<NeovimOracle> TextObjectContextTest::oracle;

// =============================================================================
// Manual Edge Case Tests
// =============================================================================

// Inner quote: basic single pair
TEST_F(TextObjectContextTest, InnerQuote_SinglePair) {
  validateSingleEdit({"foo \"hello\" bar"}, {"foo \"X\" bar"}, '"');
}

// Second quote pair: positions before first pair must NOT be valid
TEST_F(TextObjectContextTest, InnerQuote_SecondPair) {
  validateSingleEdit(
      {"aaa \"first\" bbb \"second\" ccc"},
      {"aaa \"first\" bbb \"X\" ccc"}, '"');
}

TEST_F(TextObjectContextTest, AroundQuoteConsumesTrailingWhitespaceOrFallsBackToLeading) {
  for (char quote : QuoteFlags::ALL_QUOTES) {
    const string object = quote + "value"s + quote;
    for (auto [line, goal] : {pair{"a  " + object + " \tb", "a  Xb"s},
                              pair{"a \t" + object + "b", "aXb"s}}) {
      const Lines initial{line}, expected{goal};
      auto ctx = makeContext(initial, expected);
      ASSERT_EQ(ctx.totalEdits(), 1);
      const auto& edit = ctx.edits[0];
      ASSERT_TRUE(edit.bracketQuoteContext.useAroundQuote.seen(quote));
      validateMask(initial, expected, edit.bracketQuoteContext, edit.diffState, quote);
    }
  }
}

// Inner bracket: basic single pair
TEST_F(TextObjectContextTest, InnerBracket_SinglePair) {
  validateSingleEdit({"foo (hello) bar"}, {"foo (X) bar"}, '(');
}

// Nested brackets: edit targets innermost pair
TEST_F(TextObjectContextTest, InnerBracket_Nested) {
  validateSingleEdit({"a ((hello)) c"}, {"a ((X)) c"}, '(');
}

TEST_F(TextObjectContextTest, EscapedAndNestedBracketsAgreeWithNeovimAtEveryColumn) {
  validateSingleEdit({R"(\(hello) tail)"}, {R"(\(X) tail)"}, '(');
  validateSingleEdit({R"(\\(hello) tail)"}, {R"(\\(X) tail)"}, '(');
  validateSingleEdit({R"((a\)b) tail)"}, {"(X) tail"}, '(');
  validateSingleEdit({"(a(b)c) tail"}, {"(X) tail"}, '(');
}

TEST_F(TextObjectContextTest, BracketOpenerIgnoresQuotesAndCloserRespectsThem) {
  validateSingleEdit({R"src("(x)")src"}, {R"src("(Y)")src"}, '(');
  validateSingleEdit({R"src(f(a ")" b)z)src"}, {"f(Y)z"}, '(');
  validateSingleEdit({R"src(f(a "(" b)z)src"}, {"f(Y)z"}, '(');
  validateSingleEdit({R"src(f(a ')' b)z)src"}, {"f(Y)z"}, '(');
}

TEST_F(TextObjectContextTest, ForwardBracketEntryRespectsAnEarlierEnclosingBlock) {
  for (const string& first : {"before ( outer"s, "before (balanced)"s}) {
    validateSingleEdit({first, "abc (x) z", ")"}, {first, "abc Y z", ")"}, '(');
  }
  validateSingleEdit({"outside {", "  {x} tail", "}"}, {"outside {", "  Y tail", "}"}, '{');
  validateSingleEdit({R"(\((x) tail)"}, {R"(\(Y tail)"}, '(');
}

TEST_F(TextObjectContextTest, QuoteObjectsRespectEscapesFromEveryColumn) {
  validateSingleEdit({R"(5'b\'! tail)"}, {"5'X'! tail"}, '\'');
  for (char q : QuoteFlags::ALL_QUOTES) {
    const string plain = "a "s + q;
    const string escaped = "a \\"s + q;
    const string tail = q + " z"s;
    validateSingleEdit({plain + "b\\" + q + "c" + tail}, {plain + "X" + tail}, q);
    validateSingleEdit({escaped + "bc" + tail}, {escaped + "X" + tail}, q);
    validateSingleEdit({"a"s + q + "p" + q + escaped + "bc" + tail},
                       {"a"s + q + "p" + q + escaped + "X" + tail}, q);
    validateSingleEdit({plain + "bc\\\\" + tail}, {plain + "X" + tail}, q);
  }
}

TEST_F(TextObjectContextTest, HiddenEarlierLinesOnlyExcludeForwardBracketStarts) {
  const Lines slice{"abc (x) z"};
  const CursorPos begin{0, 4}, end{0, 7};
  const DiffState diff(begin, end, "(x)", "", TransformBoundary(slice, begin, end));
  const auto ctx = computeTextObjectContextForDiff(diff, slice, true);
  ASSERT_GT(ctx.validBracketMask.size(), 6u);
  EXPECT_FALSE(ctx.validBracketMask[1].seen('('));
  for (int col = 4; col <= 6; ++col) EXPECT_TRUE(ctx.validBracketMask[col].seen('('));
}

// No delimiters present: nothing should be valid
TEST_F(TextObjectContextTest, NoDelimiters) {
  auto ctx = makeContext({"hello world"}, {"hello there"});
  ASSERT_EQ(ctx.totalEdits(), 1);
  EXPECT_FALSE(ctx.edits[0].bracketQuoteContext.hasAnyValid());
}

// Pure insertion: context should be skipped (line == -1)
TEST_F(TextObjectContextTest, PureInsertion_SkipsContext) {
  auto ctx = makeContext({"hello"}, {"hello world"});
  ASSERT_EQ(ctx.totalEdits(), 1);
  EXPECT_EQ(ctx.edits[0].bracketQuoteContext.line, -1);
}
