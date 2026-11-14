module vf.Optimizer.TransformOptimizer.TransformFrontier;

import std.compat;
import vf.Optimizer.CompositionOptimizer.CompositionStrategies;
import vf.Optimizer.CompositionOptimizer.PlannedEditArtifacts;
import vf.Optimizer.TransformOptimizer.ChangeGoalHandler;
import vf.Optimizer.TransformOptimizer.TransformExplorer;
import vf.Optimizer.TransformOptimizer.TransformOptimizerParams;
import vf.Optimizer.TransformOptimizer.TransformState;
import vf.Types.BracketFlags;
import vf.Types.QuoteFlags;
import vf.Utils.Debug;
import vf.VimCore.VimCore;

using namespace std;

namespace {

size_t skipPositiveCount(string_view seq) {
  size_t i = 0;
  if (i < seq.size() && seq[i] >= '1' && seq[i] <= '9') {
    i++;
    while (i < seq.size() && seq[i] >= '0' && seq[i] <= '9') i++;
  }
  return i;
}

bool isSingleChangeAction(string_view seq) {
  size_t i = skipPositiveCount(seq);
  if (i >= seq.size()) return false;

  char c = seq[i];
  return c == 'c' || c == 's' || c == 'C';
}

void appendInsertionStrategy(
    SuggestionEmitter& emitter,
    CursorPos cursor,
    int targetLine,
    int beginCol,
    int endCol,
    string tokenSequence,
    CursorPos landingPos,
    double distance) {
  if (targetLine < 0) return;
  if (beginCol < 0 || endCol <= beginCol) return;
  if (!cursorInInsertionRange(cursor, targetLine, beginCol, endCol)) return;
  emitter.emit(tokenSequence, landingPos, distance);
}

// Activation gate: the cursor must sit on a deleted line with another deleted
// line below it. The progress/fencepost lane split lives in emitProgressJoin.
void emitJoinFirstAction(
    const TransformFrontierQuery& query,
    SuggestionEmitter& emitter) {
  Lines deleted = query.diff.deletedLines();
  int localLine = query.cursor.line - query.diff.beginPos.line;
  if (localLine < 0 || localLine + 1 >= static_cast<int>(deleted.size())) {
    return;
  }
  emitProgressJoin(query.lines, query.cursor, query.diff, emitter);
}

void emitReplaceCharAction(
    const TransformFrontierQuery& query,
    const TransformOptimizerParams& params,
    SuggestionEmitter& emitter) {
  const DiffState& diff = query.diff;
  if (!diff.isReplacement()) return;
  Lines deletedLines = diff.deletedLines();
  Lines insertedLines = diff.insertedLines();
  if (deletedLines.size() != 1 || insertedLines.size() != 1) return;

  const Line& deleted = deletedLines[0];
  const Line& inserted = insertedLines[0];
  if (deleted.size() != inserted.size() || deleted == inserted) return;
  if (query.cursor.line != diff.beginPos.line) return;

  int offset = query.cursor.col - diff.beginPos.col;
  if (offset < 0 || offset >= static_cast<int>(deleted.size())) return;
  if (deleted[offset] == inserted[offset]) return;

  int run = 1;
  while (offset + run < static_cast<int>(deleted.size()) &&
         deleted[offset + run] != inserted[offset + run] &&
         inserted[offset + run] == inserted[offset]) {
    run++;
  }

  int replaceCount = 1;
  if (params.countPrefixesEnabled() &&
      run >= params.minPrefixCount && run <= params.maxPrefixCount) {
    replaceCount = run;
  }

  string token;
  if (replaceCount > 1) token += to_string(replaceCount);
  token += 'r';
  token += inserted[offset];

  Lines after = query.lines;
  Line& line = after[query.cursor.line];
  for (int i = 0; i < replaceCount; i++) {
    line[query.cursor.col + i] = inserted[offset + i];
  }

  CursorPos landing(query.cursor.line, query.cursor.col + replaceCount - 1);
  emitter.emit(token, landing, residualDistanceAfter(query.lines, after, diff));
}

// Depth-1 structural enumeration via TransformExplorer — replaces the
// optimizer-driven path for diffs with deleted content (pure deletion +
// replacement). Pure insertion is still handled by inline emission below.
//
// Effective-lines wrapping and cursor mapping flow through
// `TransformBoundary::withBoundary` and the local-cursor translation right
// below — same primitives the optimizer's main loop uses, so the two paths
// can't drift on the boundary semantics.
void enumerateDepth1DeletionStructurals(
    const TransformFrontierQuery& query,
    const Config& config,
    const EffortBank& bank,
    const TransformOptimizerParams& params,
    SuggestionEmitter& emitter) {
  const DiffState& diff = query.diff;
  if (!diff.hasDeletedContent()) return;

  Lines deleted = diff.deletedLines();
  if (deleted.empty()) return;
  const auto& boundary = diff.boundary;
  Lines effective = boundary.withBoundary(std::move(deleted));

  // After fully deleting the edit region, effective collapses to one line
  // containing prefix + suffix.
  Lines expectedPost;
  expectedPost.push_back(Line(boundary.prefix() + boundary.suffix()));

  // Map buffer cursor to effective-line local coords. effCol == bufferCol
  // for line 0 (because leftColOffset == prefix.length()) and for line N>0
  // (no shift). See TransformOptimizer::optimizeImpl's setup loop for the
  // canonical translation.
  CursorPos localCursor(query.cursor.line - diff.beginPos.line, query.cursor.col);
  if (localCursor.line < 0 || localCursor.line >= static_cast<int>(effective.size())) return;
  const int lineSize = static_cast<int>(effective[localCursor.line].size());
  if (localCursor.col < 0 || localCursor.col > lineSize) return;

  TransformExplorer explorer(boundary, params, config, bank,
                              boundary.leftColOffset(),
                              boundary.rightColOffset());
  TransformEditorState state(effective, localCursor);

  const bool isReplacement = diff.isReplacement();
  const double replacementInsertDistance = isReplacement
      ? textDistanceEstimate(diff.insertedLines())
      : 0.0;

  // Translate effective-line cursor back to buffer coords.
  auto bufferLanding = [&](CursorPos local) -> CursorPos {
    return CursorPos(local.line + diff.beginPos.line, local.col);
  };

  // For replacements, emit only change forms that are a single Normal-mode
  // action. Multi-action forms like `dwi` continue as delete-first alternatives
  // so Explore can re-enter Transform/Insert after the observed state changes.
  auto charwiseSeq = [&](const SequenceBinding& cmd) -> optional<string> {
    if (!isReplacement) {
      return countedCommandString(cmd.count, cmd.base.seq.view());
    }
    KeyedSequence c = ChangeGoalHandler::deleteToChangeChar(cmd);
    string seq(c.seq.view());
    if (!isSingleChangeAction(seq)) return nullopt;
    return seq;
  };

  auto linewiseSeq = [&](const SequenceBinding& cmd,
                         std::string_view lineContent) -> optional<string> {
    if (!isReplacement) {
      return countedCommandString(cmd.count, cmd.base.seq.view());
    }
    KeyedSequence c = ChangeGoalHandler::deleteToChangeLine(cmd, lineContent);
    string seq(c.seq.view());
    if (!isSingleChangeAction(seq)) return nullopt;
    return seq;
  };

  auto applyAndEmitCharwise = [&](TransformEditorState&& newState, const SequenceBinding& cmd) {
    if (newState.getLines() != expectedPost) return;
    optional<string> seq = charwiseSeq(cmd);
    if (!seq) return;
    emitter.emit(*seq, bufferLanding(newState.getPos()), replacementInsertDistance);
  };

  auto applyAndEmitLinewise = [&](TransformEditorState&& newState, const SequenceBinding& cmd,
                                    std::string_view lineContent) {
    if (newState.getLines() != expectedPost) return;
    optional<string> seq = linewiseSeq(cmd, lineContent);
    if (!seq) return;
    emitter.emit(*seq, bufferLanding(newState.getPos()), replacementInsertDistance);
  };

  const VimCore::LineDeleteContext context = lineDeleteContext(boundary);
  auto onDeletion = [&](const ResolvedEditAction& action,
                        const SequenceBinding& cmd) {
    if (isReplacement ? !action.changeEffectValid : !action.deleteEffectValid) {
      return;
    }
    const VimCore::ResolvedDeleteRange& resolved =
        isReplacement ? action.effectForChange() : action.deleteEffect;
    TransformEditorState after = isReplacement
        ? TransformSimulator::afterResolvedChangeDeletion(state, resolved, context)
        : TransformSimulator::afterResolvedDeletion(state, resolved, context);
    if (resolved.kind != VimCore::ResolvedDeleteRangeKind::Linewise) {
      applyAndEmitCharwise(std::move(after), cmd);
      return;
    }

    LineRange range = resolved.lineRange;
    std::string_view firstLineContent =
        (range.beginLine >= 0 && range.beginLine < static_cast<int>(effective.size()))
            ? std::string_view(effective[range.beginLine])
            : std::string_view{};
    applyAndEmitLinewise(std::move(after), cmd, firstLineContent);
  };
  auto onCountedLinewise = [&](const LineRange& range, const SequenceBinding& cmd) {
    std::string_view firstLineContent =
        (range.beginLine >= 0 && range.beginLine < static_cast<int>(effective.size()))
            ? std::string_view(effective[range.beginLine])
            : std::string_view{};
    applyAndEmitLinewise(
        TransformSimulator::afterMultiLinewiseDeletion(state, range, context),
        cmd, firstLineContent);
  };

  // Joins are split by edit kind. For a pure deletion the cleared shell IS the
  // goal (expectedPost == applyDiffState), so a join reaching it completes the
  // transform: charwiseSeq's non-replacement path emits raw J, and
  // emitJoinFirstAction's fencepost gate yields to it. For a replacement the
  // cleared shell is only delete-progress (expectedPost != goal), which
  // emitJoinFirstAction owns (it carries the residual insert distance); routing
  // it here would both duplicate that "J" and hit deleteToChangeChar, which has
  // no J/gJ case. So skip replacements.
  auto onJoin = [&](bool addSpace, const SequenceBinding& cmd) {
    if (isReplacement) return;
    applyAndEmitCharwise(TransformSimulator::afterJoin(state, addSpace), cmd);
  };
  auto onCountedJoin = [&](bool addSpace, const SequenceBinding& cmd) {
    if (isReplacement) return;
    applyAndEmitCharwise(TransformSimulator::afterMultiJoin(state, cmd.count, addSpace), cmd);
  };

  // Shared depth-1 sweep with TransformOptimizer::optimizeImpl. Adding a new
  // explorer enumeration belongs in sweepExplorerStructurals.
  sweepExplorerStructurals(
      explorer, state, effective, localCursor,
      boundary.leftColOffset(), boundary.rightColOffset(),
      params.minPrefixCount,
      onDeletion, onJoin, onCountedLinewise, onCountedJoin);

  // Visual deletion is a multi-token structural macro. Frontier does not
  // surface it; the full TransformOptimizer batch still emits `v{motion}d`
  // shortcuts. See `dev/architecture/todo.md` item 6 for the re-introduction
  // threshold.
  emitReplaceCharAction(query, params, emitter);
}

}

vector<Suggestion> rankTransformFrontier(
    const TransformFrontierQuery& query,
    const Config& config,
    const EffortBank& bank) {
  if (query.maxCount <= 0) return {};

  const TransformOptimizerParams transformParams = query.overrides
      ? query.overrides->resolved(TransformOptimizerParams{})
      : TransformOptimizerParams{};
  BracketQuoteContext bqContext = computeTextObjectContextForDiff(query.diff, query.lines);

  vector<Suggestion> items;
  items.reserve(static_cast<size_t>(query.maxCount));
  RunningEffort acceptedEffort(globalSequenceToKeys().tokenize(query.seq), config);
  const double acceptedCost = acceptedEffort.getEffort();
  SuggestionEmitter emitter{
      items,
      {},
      acceptedEffort, acceptedCost,
      transformParams.effortWeight, transformParams.distanceWeight,
      config};
  auto finish = [&]() -> vector<Suggestion> {
    sortAndCapSuggestions(items, query.maxCount, query.sortMode);
    return std::move(items);
  };

  // Depth-1 deletion / replacement enumeration via TransformExplorer.
  // Replaces the prior `computeTransformResultForDiff` call which ran a
  // full A* over multi-token sequences. Pure insertion is still handled by
  // the inline branches further down.
  const size_t itemsBeforeDeletionEnum = items.size();
  enumerateDepth1DeletionStructurals(query, config, bank, transformParams, emitter);
  const bool gotDeletionStructurals = items.size() > itemsBeforeDeletionEnum;

  // Post-edit cursor target for inline emission paths (pure insertion +
  // bracket/quote text-objects). Previously sourced from
  // `transformResult.getGoalPos()`; for depth-1 the equivalent is the
  // diff's beginPos (cursor lands at the deletion/insertion start after
  // the structural completes — the typed content is owned by Insert
  // phase).
  const CursorPos editGoalPos = query.diff.beginPos;

  // Pure-insertion structurals: emit the bare mode-entry command (`o`,
  // `O`, `i`, `I`, `a`, `A`). The typed content + <Esc> are owned by the
  // Insert phase recommendation; the depth-1 transform rec only describes
  // the structural decision the user makes from normal mode.
  if (query.diff.isPureInsertion()) {
    const double insertDistance = textDistanceEstimate(query.diff.insertedLines());
    CompositionStrategies::enumerateInsertions(
        query.diff, query.lines, [&](const CompositionStrategies::Insertion& s) {
          // `o` leaves the cursor on the opened line, not at the diff anchor.
          const CursorPos landing = s.structural == "o"
              ? CursorPos(s.targetLine + 1, 0)
              : editGoalPos;
          appendInsertionStrategy(emitter, query.cursor, s.targetLine, s.beginCol,
                                  s.endCol, s.structural, landing, insertDistance);
        });
  }

  emitJoinFirstAction(query, emitter);

  // Bracket/quote text-objects only fire as a fallback when no deletion
  // structurals were found — preserves the prior behaviour that the
  // optimizer's results path gated.
  if (gotDeletionStructurals) return finish();

  if (bqContext.line != query.cursor.line) return finish();

  // Bracket/quote text-objects: emit the bare structural command. For
  // pure-deletion diffs the structural is the full edit (`di"`, `da{`).
  // For replacement, the structural is the change-mode form (`ci"`,
  // `ca{`); the typed replacement content lives in the Insert phase rec.
  const bool pureDeletion = query.diff.isPureDeletion();
  const char textObjOp = pureDeletion ? 'd' : 'c';
  const double textObjectDistance = pureDeletion
      ? 0.0
      : textDistanceEstimate(query.diff.insertedLines());

  if (query.cursor.col < static_cast<int>(bqContext.validQuoteMask.size())) {
    for (char q : QuoteFlags::ALL_QUOTES) {
      if (!bqContext.validQuoteMask[query.cursor.col].seen(q)) continue;
      string seq = string(1, textObjOp) + bqContext.quoteModifier(q) + q;
      emitter.emit(seq, editGoalPos, textObjectDistance);
    }
  }

  if (query.cursor.col < static_cast<int>(bqContext.validBracketMask.size())) {
    for (char b : BracketFlags::ALL_BRACKETS) {
      if (!bqContext.validBracketMask[query.cursor.col].seen(b)) continue;
      string seq = string(1, textObjOp) + bqContext.bracketModifier(b) + b;
      emitter.emit(seq, editGoalPos, textObjectDistance);
    }
  }

  return finish();
}
