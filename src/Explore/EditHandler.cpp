module vf.Explore.EditHandler;

import std.compat;
import vf.Interpreter.EditInterpreter;
import vf.Optimizer.CompositionOptimizer.CompositionSearchContext;
import vf.Optimizer.CompositionOptimizer.CompositionStrategies;
import vf.Optimizer.CompositionOptimizer.PlannedEditArtifacts;
import vf.Optimizer.DiffPlanner.MyersDiff;
import vf.Optimizer.FrontierCommon;
import vf.Types.Mode;
import vf.VimCore.VimEditUtils;

using namespace std;

namespace Explore::EditHandler {

std::expected<bool, Rejected> validateBufferState(
    const Lines& newLines,
    const Lines& currentFencepost,
    const Lines& nextFencepost) {
  if (newLines == nextFencepost) return true;
  if (newLines == currentFencepost) return false;
  return std::unexpected(
      Rejected{"buffer state drifted outside the planned edit scope"});
}

namespace {

// Token validation against the planned scope at the supplied cursor.
// MIRROR: must accept everything CompositionFrontier / TransformFrontier can
// emit; their emission lanes are the source of truth for what is sanctioned.
bool tokenInTransformScope(
    const TransformResult& transformResult, CursorPos cursor,
    std::string_view text) {
  span<const Result> starts =
      transformResult.resultsAt(cursor.line, cursor.col);
  for (const Result& r : starts) {
    if (r.getSequence().view() == text) return true;
  }
  return false;
}

bool tokenInInsertionScope(
    const DiffState& diff, const Lines& lines,
    CursorPos cursor, std::string_view text) {
  bool found = false;
  CompositionStrategies::enumerateInsertions(diff, lines,
      [&](const CompositionStrategies::Insertion& s) {
        if (found) return;
        if (cursor.line != s.targetLine) return;
        if (cursor.col < s.beginCol || cursor.col >= s.endCol) return;
        if (text == s.structural) found = true;
      });
  return found;
}

bool tokenInBracketQuoteScope(
    const DiffState& diff, const Lines& lines,
    CursorPos cursor, std::string_view text) {
  if (diff.isPureInsertion()) return false;
  BracketQuoteContext bqContext = computeTextObjectContextForDiff(diff, lines);
  bool found = false;
  CompositionStrategies::enumerateBracketQuotes(diff, lines, bqContext,
      [&](const CompositionStrategies::BracketQuote& s) {
        if (found) return;
        if (cursor.line != s.line || cursor.col != s.col) return;
        if (text == s.structural) found = true;
      });
  return found;
}

bool tokenInJoinScope(const DiffState& diff, const Lines& lines,
                      CursorPos cursor, std::string_view text,
                      const CompositionOptimizerParams& params,
                      const Config& config) {
  if (text != "J") return false;
  auto joinPlan = computeJoinPlanForDiff(diff, lines, params, config);
  if (!joinPlan) return false;
  if (cursor.line != joinPlan->entryLine) return false;
  if (cursor.line >= lines.lastLine()) return false;
  Lines after = lines;
  CursorPos afterPos = cursor;
  VimCore::joinLineRange(after, afterPos, 2, /*addSpace=*/true);
  if (after == lines) return false;

  // Accept J whether it partially progresses the diff or fully completes it
  // (after == applyDiffState). A composition-level join that completes a scoped
  // `\n`->` ` diff has no diff-local transform lane, so this validates both.
  double remaining = residualDistanceAfter(lines, after, diff);
  return remaining < diffDistance(diff);
}

bool tokenIsSanctioned(
    const PlannedEditView& pedView, const Lines& lines,
    CursorPos cursor, std::string_view text,
    const CompositionOptimizerParams& params,
    const Config& config) {
  if (tokenInTransformScope(pedView.transformResult, cursor, text)) return true;
  if (tokenInInsertionScope(pedView.diff, lines, cursor, text)) return true;
  if (tokenInBracketQuoteScope(pedView.diff, lines, cursor, text)) return true;
  if (tokenInJoinScope(pedView.diff, lines, cursor, text, params, config)) {
    return true;
  }
  return false;
}

}  // namespace

std::expected<EditSuccess, Rejected> applyEdit(
    const PlannedEditView& pedView,
    const Lines& currentLines,
    CursorPos cursor,
    std::string_view text,
    const CompositionOptimizerParams& params,
    const Config& config) {
  if (text.empty()) {
    return std::unexpected(Rejected{"edit text must be non-empty"});
  }
  if (!tokenIsSanctioned(pedView, currentLines, cursor, text, params, config)) {
    return std::unexpected(Rejected{
        "edit command is not part of the planned edit scope at this cursor"});
  }

  // Simulate via the canonical interpreter so post-state matches live Vim.
  Lines simLines = currentLines;
  CursorPos simPos = cursor;
  Mode simMode = Mode::Normal;
  DotRepeat lastEdit;
  auto parsed = Edit::parseEdits(text);
  if (!parsed) {
    return std::unexpected(Rejected{"applyEdit: edit text failed to parse"});
  }
  for (const ParsedEdit& e : *parsed) {
    Edit::applyEdit(simLines, simPos, simMode, e, &lastEdit);
  }

  const bool reachesPostFencepost = simLines == pedView.postFencepost;
  return EditSuccess{
      .postLines = std::move(simLines),
      .postCursor = simPos,
      .reachesPostFencepost = reachesPostFencepost,
      .entersInsert = simMode == Mode::Insert,
  };
}

}  // namespace Explore::EditHandler
