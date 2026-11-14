// tests/Unit/TransformOptimizer/RegressionTests.cpp
//
// Shrunk counterexamples for TransformOptimizer edge cases, mostly embedded
// regions whose protected prefix/suffix context must survive the edit.

#include <gtest/gtest.h>
#include <cassert>

import std.compat;
import vf.Effort.EffortBank;
import vf.Optimizer.TransformOptimizer.TransformExplorer;
import vf.Optimizer.TransformOptimizer.TransformPostExplorerEmissions;
import vf.tests.Unit.TransformOptimizer.ManualTestHelpers;
import vf.VimCore.VimEndpointUtils;

using namespace std;

namespace {

TransformOptimizerParams regressionParams(int maxResults, int perStart = 3) {
  return TransformOptimizerParams{}
      .withMaxResults(maxResults)
      .withMaxResultsPerStartPos(perStart);
}

CursorPos toFullBufferPos(CursorPos localPos, CursorPos regionBegin) {
  localPos.line += regionBegin.line;
  if (localPos.line == regionBegin.line) {
    localPos.col += regionBegin.col;
  }
  return localPos;
}

}  // namespace

TEST_F(TransformOptimizer_ManualTest, BoundaryAwareReplayPrefixKeepsXApplicable) {
  // Hardcoded snapshot of a `RandomGen::seed(465950)` fixture; seed-driven
  // fixtures diverge between libstdc++ and libc++.
  Lines fullBuffer = {
      "ee,.  c.",
      "d.eeb,b.,ac., a",
      "c.debfefb bcc,.",
      "f aa,, ad.a.e",
  };
  CursorPos firstPos(0, 4);
  CursorPos endPos(3, 9);
  TransformBoundary boundary(fullBuffer, firstPos, endPos);

  // For this benchmark-derived shape, effective lines equal fullBuffer.
  Lines replayLines = fullBuffer.getSpan(firstPos, endPos);
  replayLines[0] = boundary.prefix() + replayLines[0];
  replayLines[replayLines.lastLine()] += boundary.suffix();
  ASSERT_EQ(replayLines, fullBuffer);

  int leftOffset = boundary.leftColOffset();
  ASSERT_GT(static_cast<int>(replayLines[0].size()), leftOffset + 1);

  CursorPos pos(0, leftOffset + 1);  // startIndex=1 in effective coordinates
  Mode mode = Mode::Normal;
  DotRepeat lastEdit;

  // Use a sequence that exercises boundary-aware prefix replay and leaves
  // `X` applicable afterward.
  auto parsedPrefix = Edit::parseEdits("DdaW");
  assert(parsedPrefix);
  for (const ParsedEdit& op : *parsedPrefix) {
    Edit::applyEdit(replayLines, pos, mode, op, &lastEdit,
                    boundary.hasLinesBelow(), boundary.leftColOffset(),
                    boundary.rightColOffset(), boundary.hasLinesAbove());
  }

  EXPECT_EQ(mode, Mode::Normal);
  EXPECT_EQ(lastEdit.base, "daW");
  EXPECT_EQ(lastEdit.count, 0);
  EXPECT_GT(pos.col, 0) << "Prefix replay should leave room for following X";

  const ParsedEdit x("X");
  Edit::applyEdit(replayLines, pos, mode, x, &lastEdit,
                  boundary.hasLinesBelow(), boundary.leftColOffset(),
                  boundary.rightColOffset(), boundary.hasLinesAbove());
  EXPECT_EQ(mode, Mode::Normal);
}

TEST_F(TransformOptimizer_ManualTest, EmptySourceChangeDoesNotEmitEmptySequence) {
  const Lines source = {""};
  const Lines goal = {";"};
  TransformBoundary boundary(source, CursorPos(0, 0), source.endPos());

  TransformResult result =
      opt.optimizeTransform(source, goal, boundary, regressionParams(10));

  auto bucket = result.resultsAt(0, 0);
  ASSERT_FALSE(bucket.empty());
  EXPECT_FALSE(bucket.front().getSequence().empty());
  expectBucketReplays(bucket, source, CursorPos(0, 0), goal, "empty source change");
}

TEST_F(TransformOptimizer_ManualTest, VisualDeleteFallbackRejectsExclusiveNextLineStart) {
  Lines effective = {"~YYY===", " "};

  auto visual = TransformPostExplorer::tryVisualDelete(
      effective, 1, 1, TransformOptimizerParams{}, config);

  EXPECT_FALSE(visual.has_value());
}

TEST_F(TransformOptimizer_ManualTest, BackwardWordChangeFromEmptyLineMatchesVim) {
  // `cb` has no property coverage (only the d-variants are generated).
  OracleReplay::expectModelMatchesOracle(*oracle, {"~~~~", ""}, {1, 0}, "cb<Esc>");
}

TEST_F(TransformOptimizer_ManualTest, BackwardWordChangeReplayPreservesTrailingEmptyLine) {
  const Lines source = {"~~~~", ""};
  const Lines goal = {"&", "\\"};
  TransformBoundary boundary(source, CursorPos(0, 0), source.endPos());

  TransformResult result =
      opt.optimizeTransform(source, goal, boundary, regressionParams(40));

  expectBucketReplays(result.resultsAt(1, 0), source, CursorPos(1, 0), goal,
                      "cb empty-line change");
}

TEST_F(TransformOptimizer_ManualTest, ForwardSentenceDeleteStopsAtBlankBoundary) {
  const Lines source = {"++", "", " !    "};
  TransformBoundary boundary(source, CursorPos(0, 0), source.endPos());

  TransformResult result =
      opt.optimizePureDeletion(source, boundary, regressionParams(40));

  expectBucketReplays(result.resultsAt(0, 0), source, CursorPos(0, 0), Lines{""},
                      "d) blank sentence boundary");
}

TEST_F(TransformOptimizer_ManualTest, EmbeddedSentenceDeleteAcrossSuffixLineIsPruned) {
  EffortBank bank(config);

  const Lines fullBuffer = {"  yo", "   ", " 8 M~"};
  const Lines editRegion = {" yo", "   ", " 8 M"};
  TransformBoundary boundary(fullBuffer, CursorPos(0, 1), CursorPos(2, 4));
  Lines effective = boundary.withBoundary(editRegion);
  ASSERT_EQ(effective, fullBuffer);

  TransformOptimizerParams explorerParams;
  TransformExplorer explorer(
      boundary, explorerParams, config, bank,
      boundary.leftColOffset(), boundary.rightColOffset());

  bool sawDParen = false;
  explorer.exploreSentenceEdits<true>(
      Edit::FORWARD_SENTENCE_EDITS, CursorPos(0, 1), effective,
      [&](const ResolvedEditAction&, const SequenceBinding& cmd) {
        if (cmd.base.seq.view() != "d)") return;
        sawDParen = true;
      });

  EXPECT_FALSE(sawDParen);
}

TEST_F(TransformOptimizer_ManualTest, EmbeddedDgeWithBlankPrefixIsInvalid) {
  EffortBank bank(config);

  const Lines fullBuffer = {" Q", "\""};
  const Lines editRegion = {"Q", "\""};
  TransformBoundary boundary(fullBuffer, CursorPos(0, 1), CursorPos(1, 1));
  Lines effective = boundary.withBoundary(editRegion);
  ASSERT_EQ(effective, fullBuffer);

  TransformOptimizerParams explorerParams;
  TransformExplorer explorer(
      boundary, explorerParams, config, bank,
      boundary.leftColOffset(), boundary.rightColOffset());

  bool sawDge = false;
  explorer.exploreWordEdits(
      Edit::BACKWARD_WORD_EDITS, CursorPos(1, 0), effective,
      [&](const ResolvedEditAction& action, const SequenceBinding& cmd) {
        if (cmd.base.seq.view() != "dge") return;
        sawDge = true;
        EXPECT_FALSE(action.deleteEffectValid);
      });

  EXPECT_TRUE(sawDge);
}

TEST_F(TransformOptimizer_ManualTest, OneLineClearedShellRequiresBoundaryCursor) {
  const Lines fullBuffer = {"<~ ~", "~ w", " XH  ~~"};
  const Lines editRegion = {"~ ~", "~ w", " XH "};
  const Lines goalLines = {"", "     ", ""};
  const Lines expectedFull = {"<", "     ", " ~~"};
  TransformBoundary boundary(fullBuffer, CursorPos(0, 1), CursorPos(2, 4));

  TransformResult result =
      opt.optimizeTransform(editRegion, goalLines, boundary, regressionParams(80));

  expectBucketReplays(result.resultsAt(2, 2), fullBuffer, CursorPos(2, 2), expectedFull,
                      "one-line cleared shell cursor", 3);
}

TEST_F(TransformOptimizer_ManualTest, CountedDwWithLinesBelowBoundaryMatchesLocalSemantics) {
  const ParsedEdit fourDw("dw", 4);

  Lines withBoundary = {" ecbb.abec"};
  CursorPos posWithBoundary(0, 0);
  Mode modeWithBoundary = Mode::Normal;
  DotRepeat lastEditWithBoundary;
  Edit::applyEdit(withBoundary, posWithBoundary, modeWithBoundary, fourDw,
                  &lastEditWithBoundary,
                  /*hasLinesBelow=*/true,
                  /*leftColOffset=*/0,
                  /*rightColOffset=*/0,
                  /*hasLinesAbove=*/false);

  Lines localOnly = {" ecbb.abec"};
  CursorPos posLocal(0, 0);
  Mode modeLocal = Mode::Normal;
  DotRepeat lastEditLocal;
  Edit::applyEdit(localOnly, posLocal, modeLocal, fourDw,
                  &lastEditLocal,
                  /*hasLinesBelow=*/false,
                  /*leftColOffset=*/0,
                  /*rightColOffset=*/0,
                  /*hasLinesAbove=*/false);

  EXPECT_EQ(withBoundary, localOnly);
  EXPECT_EQ(posWithBoundary, posLocal);
  EXPECT_EQ(modeWithBoundary, modeLocal);
}

TEST_F(TransformOptimizer_ManualTest, ForwardSentenceGapAtBottomBoundaryIsPruned) {
  Lines effectiveLines = {
      "Xdd,",
      ",e .edd",
      "cac.  ",
  };

  CursorPos endpoint = VimCore::sentenceOperatorEndpoint(
      CursorPos(2, 4), effectiveLines, true, 0, true);

  EXPECT_EQ(endpoint, POSITION_OUTSIDE_BOUNDARY);
}

TEST_F(TransformOptimizer_ManualTest, BackwardParagraphExplorerEmitsDLeftBrace) {
  Lines lines = {"abc", "", "def"};
  TransformBoundary boundary(lines, CursorPos(0, 0), lines.endPos());
  TransformOptimizerParams explorerParams;
  EffortBank bank(config);
  TransformExplorer explorer(
      boundary, explorerParams, config, bank,
      boundary.leftColOffset(), boundary.rightColOffset());

  bool sawLinewiseDLeftBrace = false;
  bool sawUnexpectedCharwiseDLeftBrace = false;

  explorer.exploreParagraphEdits<false>(
      Edit::BACKWARD_PARAGRAPH_EDITS, CursorPos(2, 0), lines,
      [&](const ResolvedEditAction& action, const SequenceBinding& cmd) {
        const VimCore::ResolvedDeleteRange& resolved = action.deleteEffect;
        if (cmd.base.seq.view() == "d{") {
          if (resolved.kind != VimCore::ResolvedDeleteRangeKind::Linewise) {
            sawUnexpectedCharwiseDLeftBrace = true;
          } else {
            LineRange range = resolved.lineRange;
            if (range.beginLine == 1 && range.endLine == 2) {
              sawLinewiseDLeftBrace = true;
            }
          }
        }
      });

  EXPECT_TRUE(sawLinewiseDLeftBrace);
  EXPECT_FALSE(sawUnexpectedCharwiseDLeftBrace);
}

TEST_F(TransformOptimizer_ManualTest, SuffixCacheExpandsMismatchedDotRepeatContext) {
  const Lines fullBuffer = {"ce.d.d ", "aa.eb", "f. e", ".e ae"};
  const Lines editRegion = {".d.d ", "aa.eb", "f. e", ".e a"};
  const Lines goalLines = {"fbbccbae", "c.dead", "aaadd   ", "e.b,"};
  const Lines expectedFull = {
      "cefbbccbae",
      "c.dead",
      "aaadd   ",
      "e.b,e",
  };
  TransformBoundary boundary(fullBuffer, CursorPos(0, 2), CursorPos(3, 4));

  TransformResult result =
      opt.optimizeTransform(editRegion, goalLines, boundary, regressionParams(200));

  expectBucketReplays(result.resultsAt(2, 2), fullBuffer, CursorPos(2, 2), expectedFull,
                      "transform suffix cache dot context", 3);
}

TEST_F(TransformOptimizer_ManualTest, ParagraphChangeAtEofCollapsesIntoPrefixLine) {
  const Lines fullBuffer = {
      "fe,d ,",
      "eba,c",
      "b cb",
      "c,dbea.f ",
  };
  const Lines editRegion = {
      "",
      "eba,c",
      "b cb",
      "c,dbea.f ",
  };
  const Lines goalLines = {"b cb"};
  const Lines expectedFull = {"fe,d ,b cb"};
  TransformBoundary boundary(fullBuffer, CursorPos(0, 6), CursorPos(3, 9));

  TransformResult result = opt.optimizeTransform(
      editRegion, goalLines, boundary, regressionParams(200), 0, 6, CursorPos(0, 9));

  auto bucket = result.resultsAt(1, 0);
  ASSERT_FALSE(bucket.empty());
  EXPECT_NE(bucket.front().getSequence().str().find("<BS>"), string::npos)
      << "sequence=" << bucket.front().getSequence().str();
  expectBucketReplays(bucket, fullBuffer, CursorPos(1, 0), expectedFull,
                      "paragraph c} prefix collapse", 3, CursorPos(0, 9));
}

TEST_F(TransformOptimizer_ManualTest, LinewiseChangeDoesNotTypeIntoHiddenContext) {
  const Lines fullBuffer = {
      "afaddcd,cbfa",
      " d. c,b,",
      ".b ,",
      " ,bfb.cf",
  };
  const Lines editRegion = {
      " d. c,b,",
      ".b ,",
      " ,bfb.cf",
  };
  const Lines goalLines = {".b ,"};
  const Lines expectedFull = {
      "afaddcd,cbfa",
      ".b ,",
  };
  TransformBoundary boundary(fullBuffer, CursorPos(1, 0), CursorPos(3, 8));

  TransformResult result = opt.optimizeTransform(
      editRegion, goalLines, boundary, regressionParams(50), 1, 0, CursorPos(1, 3));

  expectBucketReplays(result.resultsAt(1, 1), fullBuffer, CursorPos(1, 1), expectedFull,
                      "linewise change hidden context");
}

TEST_F(TransformOptimizer_ManualTest, TailParagraphDeletePreservesCursorColumn) {
  const Lines source = {
      "dfdee",
      " ac,",
      "b  a.a",
  };
  TransformBoundary boundary(source, CursorPos(0, 0), source.endPos());

  TransformResult result =
      opt.optimizePureDeletion(source, boundary, regressionParams(80, 1));

  expectBucketReplays(result.resultsAt(1, 1), source, CursorPos(1, 1), Lines{""},
                      "tail paragraph delete cursor column");
}

TEST_F(TransformOptimizer_ManualTest, BlankBufferPureDeletionCandidatesReplayToGoal) {
  const Lines source = {"   ", "", "  "};
  TransformBoundary boundary(source, CursorPos(0, 0), source.endPos());
  TransformResult result =
      opt.optimizePureDeletion(source, boundary, regressionParams(120, 5));

  ASSERT_GT(result.resultCount(), 0u);
  for (size_t i = 0; i < result.resultCount(); i++) {
    const auto& bucket = result.getResults()[i];
    if (bucket.empty()) continue;
    CursorPos start = source.cursorFromFlatIndexClamped(static_cast<int>(i));
    ASSERT_TRUE(start.isValid()) << "bucket=" << i;
    expectBucketReplays(bucket, source, start, Lines{""}, "blank pure deletion");
  }
}

TEST_F(TransformOptimizer_ManualTest, EmbeddedPureDeletionDoesNotUseVisualShortcutAcrossBoundary) {
  const Lines fullBuffer = {"cdf .e.,", "d.a ", ",abb"};
  const Lines editRegion = {" .e.,", "d.a ", ",ab"};
  const Lines expectedFull = {"cdfb"};
  TransformBoundary boundary(fullBuffer, CursorPos(0, 3), CursorPos(2, 3));

  TransformResult result =
      opt.optimizePureDeletion(editRegion, boundary, regressionParams(100, 1));

  expectBucketReplays(result.resultsAt(0, 0), fullBuffer, CursorPos(0, 3), expectedFull,
                      "embedded visual-delete shortcut");
}

TEST_F(TransformOptimizer_ManualTest, ContentOnlyPureDeletionKeepsParentLine) {
  const Lines fullBuffer = {"prefix", "delete me"};
  const Lines editRegion = {"delete me"};
  const Lines expectedFull = {"prefix", ""};
  TransformBoundary boundary(fullBuffer, CursorPos(1, 0), CursorPos(1, 9));

  TransformResult result = opt.optimizePureDeletion(
      editRegion, boundary,
      regressionParams(40, 5).withLinewisePureDeletion(false),
      1, 0, CursorPos(1, 0));

  expectBucketReplays(result.resultsAt(1, 0), fullBuffer, CursorPos(1, 0), expectedFull,
                      "content-only pure deletion");
}

TEST_F(TransformOptimizer_ManualTest, StraddledPureDeletionAllResultsReplayToGoal) {
  struct Case {
    CursorPos begin;
    CursorPos end;
    Lines goal;
  };

  const Lines fullBuffer = {
    "arstn arstn",
    "arstn arstn",
  };
  const vector<Case> cases = {
    {CursorPos(0, 5), CursorPos(1, 11), Lines{"arstn"}},
    {CursorPos(0, 5), CursorPos(1, 6), Lines{"arstnarstn"}},
  };

  for (const Case& test : cases) {
    Lines editRegion = fullBuffer.getSpan(test.begin, test.end);
    TransformBoundary boundary(fullBuffer, test.begin, test.end);

    TransformResult result =
        opt.optimizePureDeletion(editRegion, boundary, regressionParams(200, 1));

    ASSERT_GT(result.resultCount(), 0u);
    int checked = 0;
    for (size_t i = 0; i < result.resultCount(); i++) {
      const auto& bucket = result.getResults()[i];
      if (bucket.empty()) continue;

      CursorPos localPos = editRegion.cursorFromFlatIndexClamped(static_cast<int>(i));
      ASSERT_TRUE(localPos.isValid()) << "Invalid local position at bucket " << i;
      expectBucketReplays(bucket, fullBuffer, toFullBufferPos(localPos, test.begin),
                          test.goal, "straddled pure deletion all results");
      checked += bucket.size();
    }
    EXPECT_GT(checked, 0);
  }
}
