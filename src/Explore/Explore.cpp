module;
#include <cassert>

module vf.Explore.Explore;

import std.compat;
import vf.Explore.EditHandler;
import vf.Effort.RunningEffort;
import vf.Keyboard.ToKeys.MovementToKeys;
import vf.Explore.MovementHandler;
import vf.Optimizer.BuildTypedCommands;
import vf.Optimizer.DiffPlanner.DiffState;
import vf.Optimizer.CompositionOptimizer.CompositionFrontier;
import vf.Optimizer.CompositionOptimizer.CompositionStrategies;
import vf.Optimizer.NavOptimizer.NavFrontier;
import vf.Optimizer.OptimizerParamOverrides;
import vf.Optimizer.TransformOptimizer.TransformFrontier;
import vf.Optimizer.TransformOptimizer.TransformOptimizerParams;

using namespace std;

namespace Explore {

namespace {

// Raw keys are evidence of how Vim reached the observed state; they are not
// allowed to veto a state Vim already produced.
void appendRawKeys(State& next,
                   std::string_view rawKeys,
                   const Config& config) {
  if (rawKeys.empty())
    return;
  next.seq.append(rawKeys);
  next.cost = getEffort(next.seq, config);
}

bool isHistoryCheckpoint(const State& state) {
  return !std::holds_alternative<Insert>(state.phase) &&
         !state.hasPartialEditSpan;
}

std::string deriveInsertModeTypedText(const DiffState& diff,
                                    const Lines& lines,
                                    std::string_view autoindent, const IndentOptions& options) {
  if (diff.isPureDeletion()) return "";

  KeyedSequence ks = diff.isPureInsertion()
      ? CompositionStrategies::typedInsertion(diff, lines, options).typed
      : buildTypedCommands(
            diff.insertedLines(), autoindent,
            lines[diff.beginPos.line].substr(0, diff.beginPos.col),
            lines[diff.endPos.line].substr(diff.endPos.col), options);

  std::string_view raw = ks.seq.view();
  static constexpr std::string_view ESC_STR = "<Esc>";
  if (raw.ends_with(ESC_STR)) raw.remove_suffix(ESC_STR.size());
  return std::string(raw);
}

bool containsInsertCursor(const Lines& lines, CursorPos pos) {
  if (pos.line < 0 || pos.line > lines.lastLine())
    return false;
  return pos.col >= 0 && pos.col <= lines.getSize(pos.line);
}

std::optional<DiffState> insertResidualBetween(
    const Lines& from,
    const Lines& to,
    std::optional<CursorPos> insertCursor = std::nullopt) {
  auto diff = DiffText::calculateContiguousResidualDiff(from, to);
  if (!diff) return std::nullopt;
  if (diff->isPureInsertion()) return diff;
  if (!insertCursor || diff->beginPos.line != diff->endPos.line ||
      diff->endPos != *insertCursor || !containsInsertCursor(from, *insertCursor))
    return std::nullopt;
  const Line& line = from[insertCursor->line];
  if (insertCursor->col != line.size() || IndentOptions::leadingIndent(line).size() != line.size())
    return std::nullopt;
  const CursorPos begin(insertCursor->line, 0);
  return DiffState(begin, diff->endPos, line,
                   line.substr(0, diff->beginPos.col) + diff->insertedText,
                   TransformBoundary(from, begin, diff->endPos));
}

DiffState remainingEdit(const PlannedEditView& edit, const Lines& lines) {
  if (lines != edit.preFencepost) {
    if (auto residual = DiffText::calculateContiguousResidualDiff(lines, edit.postFencepost))
      return std::move(*residual);
  }
  return edit.diff;
}

DiffState deletionPrefixDiff(const DiffState& diff) {
  return DiffState(
      diff.beginPos,
      diff.endPos,
      diff.deletedText,
      "",
      diff.boundary);
}

CharRange cursorPointRange(CursorPos pos) {
  return CharRange{pos, CursorPos(pos.line, pos.col + 1)};
}

void appendUniqueSuggestions(vector<Suggestion>& dest, vector<Suggestion>&& src) {
  for (auto& suggestion : src) {
    const bool duplicate = any_of(dest.begin(), dest.end(),
        [&](const Suggestion& item) { return item.token == suggestion.token; });
    if (!duplicate) dest.push_back(std::move(suggestion));
  }
}

bool sameTransformResult(const TransformResult& a, const TransformResult& b) {
  if (a.getGoalPos() != b.getGoalPos()) return false;
  if (a.startPositions() != b.startPositions()) return false;
  if (a.hasResultGoals() != b.hasResultGoals()) return false;
  if (a.getResults() != b.getResults()) return false;
  for (CursorPos start : a.startPositions()) {
    auto aBucket = a.resultsAt(start.line, start.col);
    auto bBucket = b.resultsAt(start.line, start.col);
    if (aBucket.size() != bBucket.size()) return false;
    for (size_t j = 0; j < aBucket.size(); j++) {
      if (a.goalPosAt(start.line, start.col, j) !=
          b.goalPosAt(start.line, start.col, j)) {
        return false;
      }
    }
  }
  return true;
}

bool sameDiff(const DiffState& a, const DiffState& b) {
  return a.beginPos == b.beginPos &&
         a.endPos == b.endPos &&
         a.deletedText == b.deletedText &&
         a.insertedText == b.insertedText;
}

bool sameCompositionResult(const CompositionResult& a,
                           const CompositionResult& b) {
  const CompositionPlan& ap = a.getPlan();
  const CompositionPlan& bp = b.getPlan();
  if (ap.finalGoalPos != bp.finalGoalPos) return false;
  if (ap.fenceposts != bp.fenceposts) return false;
  if (ap.diffs.size() != bp.diffs.size()) return false;
  for (int i = 0; i < ssize(ap.diffs); ++i) {
    if (!sameDiff(ap.diffs[i], bp.diffs[i])) return false;
    if (!sameTransformResult(a.plannedEditAt(i).transformResult,
                             b.plannedEditAt(i).transformResult)) {
      return false;
    }
  }
  return a.getResults() == b.getResults();
}

} // namespace

// =============================================================================
// View construction
// =============================================================================

View::View(Lines initialLines, CursorPos initialPos, Lines goalLines,
           CursorPos goalPos, NavBoundary boundary, NavContext navContext,
           Config config, string_view userSequence,
           CompositionOptimizerParams compositionParams)
    : View(initialLines, initialPos, goalLines, goalPos, boundary, navContext,
           config, userSequence, compositionParams,
           computeTrace(initialLines, initialPos, goalLines, goalPos,
                        compositionParams, userSequence, boundary, navContext,
                        config, SearchControl::NEVER_STOP)) {}

View::View(Lines initialLines, CursorPos initialPos, Lines goalLines,
           CursorPos goalPos, NavBoundary boundary, NavContext navContext,
           Config config, string_view userSequence,
           CompositionOptimizerParams compositionParams,
           CompositionTraceResult precomputedPlan)
    : initialLines_(std::move(initialLines)), initialPos_(initialPos),
      goalLines_(std::move(goalLines)), goalPos_(goalPos),
      boundary_(std::move(boundary)), navContext_(navContext),
      config_(std::move(config)),
      bank_(config_),
      userSequence_(userSequence),
      compositionParams_(std::move(compositionParams)) {

  resetToInitial();
  installPlan(std::move(precomputedPlan));
  state_.phase = phaseForCursor(0, state_.lines, state_.cursor);
}

// =============================================================================
// Query
// =============================================================================

CharRange View::currentTargetRange() const {
  return plan_.getPlan().navTarget(phaseIndex(state_.phase));
}

bool View::isCompleted() const {
  auto* nav = std::get_if<Navigate>(&state_.phase);
  return nav && nav->index == totalEdits_ && state_.cursor == goalPos_;
}

View::HeaderRows View::headerRows() const {
  HeaderRows out;
  out.explored = rowsFromEditSpans(
      state_.seq,
      std::span<const EditSequenceSpan>(
          state_.editSpans.spans.data(), state_.editSpans.size));

  const auto& results = plan_.getResults();
  assert(results.size() == optimalEditSpans_.size() &&
         "optimal results must align with traced edit spans");
  out.optimal.reserve(results.size());
  for (size_t i = 0; i < results.size(); ++i) {
    out.optimal.push_back(rowsFromEditSpans(
        results[i].getSequence().view(), optimalEditSpans_[i]));
  }
  return out;
}

vector<Suggestion> View::recommendations(
    int maxCount,
    const OptimizerParamOverrides* overrides,
    SuggestionSortMode sortMode) const {
  if (maxCount <= 0) return {};
  return std::visit(overload{
      [&](const Navigate& nav) {
        return recommendNavigate(nav.index, maxCount, overrides, sortMode);
      },
      [&](const Transform& transform) {
        auto diff = remainingEdit(plan_.plannedEditAt(transform.index), state_.lines);
        auto items = transformEdits(diff, makeFrontierQuery(overrides));
        sortAndCapSuggestions(items, maxCount, sortMode);
        return items;
      },
      [&](const Insert& insert) {
        return recommendInsert(insert.index, maxCount);
      },
  }, state_.phase);
}

bool View::reconfigurePlan(CompositionOptimizerParams compositionParams) {
  compositionParams.transform.indent = compositionParams_.transform.indent;
  CompositionTraceResult traced = computePlan(compositionParams);
  const bool resetState = !sameCompositionResult(plan_, traced.result);

  compositionParams_ = std::move(compositionParams);
  installPlan(std::move(traced));

  if (resetState) {
    resetToInitial();
    state_.phase = phaseForCursor(0, state_.lines, state_.cursor);
  }
  return resetState;
}

// =============================================================================
// Internal helpers
// =============================================================================

CompositionTraceResult View::computeTrace(
    const Lines& initialLines, CursorPos initialPos,
    const Lines& goalLines, CursorPos goalPos,
    const CompositionOptimizerParams& compositionParams,
    string_view userSequence,
    const NavBoundary& boundary, const NavContext& navContext,
    const Config& config, const SearchControl& control) {
  // Explore opts into the traced variant because it needs plan-aligned edit
  // spans to render the Optimal-N header columns.
  CompositionOptimizer opt(config);
  return opt.optimizeWithEditSpans(
      initialLines, initialPos, goalLines, goalPos,
      compositionParams, userSequence, boundary, navContext, control);
}

CompositionTraceResult View::computePlan(
    CompositionOptimizerParams compositionParams) const {
  return computeTrace(initialLines_, initialPos_, goalLines_, goalPos_,
                      compositionParams, userSequence_, boundary_, navContext_,
                      config_, SearchControl::NEVER_STOP);
}

void View::installPlan(CompositionTraceResult traced) {
  plan_ = std::move(traced.result);
  optimalEditSpans_ = std::move(traced.editSpansByResult);
  totalEdits_ = plan_.totalEdits();
}

void View::resetToInitial() {
  state_ = State{};
  state_.lines = initialLines_;
  state_.cursor = initialPos_;
  undo_.clear();
  redo_.clear();
}

Outcome View::commit(State next) {
  // Insert and partial edit states are internal pieces of one Vim change.
  if (isHistoryCheckpoint(state_)) {
    undo_.push_back(state_);
  }
  redo_.clear();
  state_ = std::move(next);
  return {};
}

FrontierQuery View::makeFrontierQuery(const OptimizerParamOverrides* overrides) const {
  return FrontierQuery{
      .lines = state_.lines,
      .cursor = state_.cursor,
      .seq = state_.seq,
      .overrides = overrides,
  };
}

vector<Suggestion> View::recommendNavigate(
    int navIndex,
    int maxCount,
    const OptimizerParamOverrides* overrides,
    SuggestionSortMode sortMode) const {
  FrontierQuery frontierQuery = makeFrontierQuery(overrides);
  vector<Suggestion> nav;

  if (navIndex < totalEdits_) {
    for (CursorPos start : executableEditStartPositions(navIndex, state_.lines)) {
      appendUniqueSuggestions(nav, rankNavFrontier(
          NavFrontierQuery{
            frontierQuery,
            cursorPointRange(start),
            boundary_,
            navContext_,
            maxCount,
            sortMode,
        }, config_, bank_));
    }

    auto diff = remainingEdit(plan_.plannedEditAt(navIndex), state_.lines);
    appendUniqueSuggestions(nav, compositionEdits(diff, frontierQuery));
  } else {
    CharRange target = plan_.getPlan().navTarget(navIndex);
    nav = rankNavFrontier(
        NavFrontierQuery{
          frontierQuery,
          target.isEmpty() ? cursorPointRange(target.begin) : target,
          boundary_,
          navContext_,
          maxCount,
          sortMode,
      }, config_, bank_);
  }

  sortAndCapSuggestions(nav, maxCount, sortMode);
  return nav;
}

vector<Suggestion> View::compositionEdits(const DiffState& diff, const FrontierQuery& query) const {
  const auto params = query.overrides
      ? query.overrides->resolved(compositionParams_)
      : compositionParams_;
  return collectCompositionEdits({query, diff, boundary_, navContext_}, params, config_);
}

vector<Suggestion> View::transformEdits(const DiffState& diff, const FrontierQuery& query) const {
  auto primary = collectTransformEdits({query, diff, boundary_.hasLinesAbove(), compositionParams_.transform.indent}, config_, bank_);
  if (!diff.isReplacement()) return primary;

  auto alternatives = collectTransformEdits(
      {query, deletionPrefixDiff(diff), boundary_.hasLinesAbove(), compositionParams_.transform.indent}, config_, bank_);
  const auto transformParams = query.overrides
      ? query.overrides->resolved(TransformOptimizerParams{})
      : TransformOptimizerParams{};
  const double residualInsertDistance =
      textDistanceEstimate(diff.insertedLines());
  for (auto& alt : alternatives) {
    const bool duplicate = any_of(primary.begin(), primary.end(),
        [&](const Suggestion& item) { return item.token == alt.token; });
    if (duplicate) continue;
    updateSuggestionMetrics(
        alt, alt.costDiff, alt.distance + residualInsertDistance,
        transformParams.effortWeight, transformParams.distanceWeight);
    primary.push_back(std::move(alt));
  }
  return primary;
}

vector<Suggestion> View::recommendInsert(int editIndex, int maxCount) const {
  if (maxCount <= 0) return {};
  assert(editIndex >= 0 && editIndex < totalEdits_ &&
         "Insert editIndex out of range");

  // Mode entry can change the text, including copying an opened line's indent.
  const auto plannedEdit = plan_.plannedEditAt(editIndex);
  auto currentResidual = insertResidualBetween(
      state_.lines, plannedEdit.postFencepost, state_.cursor);
  const DiffState& insertDiff = currentResidual
      ? *currentResidual
      : plannedEdit.diff;
  std::string typedText =
      deriveInsertModeTypedText(insertDiff, state_.lines,
                                currentResidual ? currentResidual->deletedText : "", compositionParams_.transform.indent);
  if (typedText.empty()) return {};

  // Running effort is contextual across the accepted-sequence boundary.
  RunningEffort acceptedEffort(
      globalSequenceToKeys().tokenize(state_.seq), config_);
  const double acceptedCost = acceptedEffort.getEffort();
  RunningEffort candidate(globalSequenceToKeys().tokenize(typedText), config_);
  RunningEffort merged = RunningEffort::merge(acceptedEffort, candidate, config_);
  const double costDiff = merged.getEffort() - acceptedCost;

  Suggestion suggestion{
      .token = Token{typedText},
      .landingPos = currentResidual
          ? (insertDiff.isPureInsertion()
                 ? CompositionStrategies::typedInsertion(insertDiff, state_.lines, compositionParams_.transform.indent).exitCursor
                 : typedCommandsExitCursor(insertDiff.beginPos, insertDiff.insertedLines()))
          : plannedEdit.transformResult.getGoalPos(),
  };
  suggestion.literalText = insertDiff.insertedText;
  updateSuggestionMetrics(suggestion, costDiff, 0.0, 1.0, 1.0);
  return {std::move(suggestion)};
}

bool View::hasExecutableEditStartAt(
    int editIndex,
    const Lines& lines,
    CursorPos cursor) const {
  assert(editIndex >= 0 && editIndex <= totalEdits_ &&
         "edit-start query index out of plan range");
  if (editIndex == totalEdits_) return false;

  const auto plannedEdit = plan_.plannedEditAt(editIndex);
  auto currentResidual = insertResidualBetween(
      lines, plannedEdit.postFencepost);
  if (currentResidual && currentResidual->isPureInsertion()) {
    return cursor == currentResidual->beginPos &&
           containsInsertCursor(lines, cursor);
  }
  return !plannedEdit.transformResult.resultsAt(cursor.line, cursor.col).empty();
}

vector<CursorPos> View::executableEditStartPositions(
    int editIndex,
    const Lines& lines) const {
  assert(editIndex >= 0 && editIndex <= totalEdits_ &&
         "edit-start query index out of plan range");
  if (editIndex == totalEdits_) return {};

  const auto plannedEdit = plan_.plannedEditAt(editIndex);
  auto currentResidual = insertResidualBetween(
      lines, plannedEdit.postFencepost);
  if (currentResidual && currentResidual->isPureInsertion()) {
    return {currentResidual->beginPos};
  }
  return plannedEdit.transformResult.startPositions();
}

Phase View::phaseForCursor(int editIndex, const Lines& lines, CursorPos cursor) const {
  assert(editIndex >= 0 && editIndex <= totalEdits_ &&
         "phaseForCursor index out of plan range");
  if (hasExecutableEditStartAt(editIndex, lines, cursor))
    return Transform{editIndex};
  return Navigate{editIndex};
}

expected<int, Rejected>
View::requireNavigateOrTransform(string_view action) const {
  if (!std::holds_alternative<Navigate>(state_.phase) &&
      !std::holds_alternative<Transform>(state_.phase)) {
    return unexpected(Rejected{
        string(action) + " only accepted while navigating or transforming"});
  }
  return phaseIndex(state_.phase);
}

expected<int, Rejected>
View::requirePlannedEditTarget(string_view action) const {
  auto gated = requireNavigateOrTransform(action);
  if (!gated)
    return unexpected(std::move(gated.error()));

  if (*gated == totalEdits_) {
    return unexpected(
        Rejected{string(action) + " not accepted post-final-edit"});
  }

  return *gated;
}

expected<int, Rejected> View::requireInsert(string_view action) const {
  auto* i = std::get_if<Insert>(&state_.phase);
  if (!i) {
    return unexpected(
        Rejected{string(action) + " only accepted during insert"});
  }
  return i->index;
}

Outcome View::afterEditCompleted(State next, int editIndex) {
  const int nextEdit = editIndex + 1;
  next.lines = plan_.getPlan().fencepostAt(nextEdit);
  next.hasPartialEditSpan = false;
  next.phase = phaseForCursor(nextEdit, next.lines, next.cursor);
  return commit(std::move(next));
}

// =============================================================================
// Actions
// =============================================================================

Outcome View::applyMovement(string_view movementText) {
  auto gated = requireNavigateOrTransform("motions");
  if (!gated)
    return unexpected(std::move(gated.error()));

  auto eff = MovementHandler::applyMovement(
      state_.lines, state_.cursor, movementText,
      boundary_, navContext_);
  if (!eff)
    return unexpected(std::move(eff.error()));

  State next = state_;
  next.cursor = eff->newCursor;
  next.seq.append(movementText);
  next.cost = getEffort(next.seq, config_);
  next.phase = phaseForCursor(*gated, next.lines, next.cursor);
  return commit(std::move(next));
}

Outcome View::acceptCursorMove(CursorPos newCursor, string_view rawKeys) {
  auto gated = requireNavigateOrTransform("cursor moves");
  if (!gated)
    return unexpected(std::move(gated.error()));

  auto eff = MovementHandler::acceptCursorMove(
      state_.lines, newCursor, boundary_);
  if (!eff)
    return unexpected(std::move(eff.error()));

  State next = state_;
  next.cursor = eff->newCursor;
  appendRawKeys(next, rawKeys, config_);
  next.phase = phaseForCursor(*gated, next.lines, next.cursor);
  return commit(std::move(next));
}

Outcome View::applyEdit(string_view text, const OptimizerParamOverrides* overrides) {
  auto gated = requirePlannedEditTarget("edits");
  if (!gated) return unexpected(std::move(gated.error()));
  if (text.empty()) return unexpected(Rejected{"edit text must be non-empty"});
  const int editIndex = *gated;
  const auto plannedEdit = plan_.plannedEditAt(editIndex);
  const auto diff = remainingEdit(plannedEdit, state_.lines);
  const auto query = makeFrontierQuery(overrides);
  auto matches = [&](const Suggestion& item) { return item.token == text; };
  bool allowed = holds_alternative<Transform>(state_.phase) &&
                 ranges::any_of(transformEdits(diff, query), matches);
  if (!allowed) allowed = ranges::any_of(compositionEdits(diff, query), matches);
  if (!allowed && state_.lines == plannedEdit.preFencepost) {
    plannedEdit.transformResult.forEachEditFrom(state_.cursor, plannedEdit.postFencepost,
        [&](const Result& r, CursorPos) {
          if (r.getSequence().view() == text) allowed = true;
        });
  }
  if (!allowed)
    return unexpected(Rejected{"edit command is not part of the planned edit scope at this cursor"});

  auto eff = EditHandler::applyEdit(plannedEdit.postFencepost, state_.lines, state_.cursor, text, compositionParams_.transform.indent);
  if (!eff)
    return unexpected(std::move(eff.error()));

  State next = state_;
  next.cursor = eff->postCursor;
  const uint32_t spanBegin = next.seqEnd();
  next.seq.append(text);
  const uint32_t spanEnd = next.seqEnd();
  next.cost = getEffort(next.seq, config_);

  if (eff->entersInsert) {
    // Insert-entering composition token (`A`, `ci(`, ...): apply the
    // structural effect and transition to Insert phase. The typed payload
    // is supplied later via acceptInsertExit / acceptSnapshot.
    next.lines = std::move(eff->postLines);
    next.phase = Insert{editIndex};
    next.hasPartialEditSpan = true;
    next.partialEditSpanBegin = spanBegin;
    return commit(std::move(next));
  }

  if (eff->reachesPostFencepost) {
    // Normal-mode token that completes the planned edit (existing Transform
    // path, plus pure-deletion text objects and J-as-completion).
    next.editSpans.push(EditSequenceSpan{spanBegin, spanEnd});
    next.hasPartialEditSpan = false;
    return afterEditCompleted(std::move(next), editIndex);
  }

  // Normal-mode token that made partial progress (e.g. J that did not reach
  // the fencepost). Keep the simulated buffer/cursor and recompute phase.
  next.lines = std::move(eff->postLines);
  next.phase = phaseForCursor(editIndex, next.lines, next.cursor);
  if (!next.hasPartialEditSpan) {
    next.hasPartialEditSpan = true;
    next.partialEditSpanBegin = spanBegin;
  }
  return commit(std::move(next));
}

Outcome View::acceptBufferState(const Lines& newLines, CursorPos newCursor,
                                string_view rawKeys) {
  auto gated = requirePlannedEditTarget("buffer state changes");
  if (!gated)
    return unexpected(std::move(gated.error()));
  const int editIndex = *gated;

  const auto plannedEdit = plan_.plannedEditAt(editIndex);

  auto advance = EditHandler::validateBufferState(
      newLines, plannedEdit.preFencepost, plannedEdit.postFencepost);
  if (!advance)
    return unexpected(std::move(advance.error()));
  if (!newLines.contains(newCursor)) {
    return unexpected(
        Rejected{"buffer state reported an invalid cursor position"});
  }

  State next = state_;
  next.cursor = newCursor;
  // Capture span begin before appending raw keys so the edit's start byte is
  // exactly where the rawKeys (which performed this edit) begin in seq.
  const uint32_t spanBegin = next.spanBegin();
  appendRawKeys(next, rawKeys, config_);
  if (*advance) {
    next.editSpans.push(EditSequenceSpan{spanBegin, next.seqEnd()});
    next.hasPartialEditSpan = false;
    return afterEditCompleted(std::move(next), editIndex);
  }
  // No-op: buffer already matches current fencepost (e.g. native undo or
  // programmatic re-sync). Sync cursor only — no span pushed because no
  // planned edit advanced.
  next.phase = phaseForCursor(editIndex, next.lines, next.cursor);
  return commit(std::move(next));
}

Outcome View::beginInsert() {
  auto gated = requirePlannedEditTarget("insert-mode edit");
  if (!gated)
    return unexpected(std::move(gated.error()));
  const int editIndex = *gated;

  State next = state_;
  next.phase = Insert{editIndex};
  return commit(std::move(next));
}

Outcome View::acceptInsertExit(const Lines& newLines, CursorPos newCursor,
                               string_view rawKeys) {
  auto gated = requireInsert("insert-mode exit with buffer state");
  if (!gated)
    return unexpected(std::move(gated.error()));
  const int editIndex = *gated;
  const auto plannedEdit = plan_.plannedEditAt(editIndex);

  if (newLines != plannedEdit.postFencepost) {
    return unexpected(
        Rejected{"buffer state after insert doesn't match planned fencepost"});
  }
  if (!newLines.contains(newCursor)) {
    return unexpected(
        Rejected{"buffer state reported an invalid cursor position"});
  }

  State next = state_;
  // MIRROR applyEdit's shape: set cursor + seq/cost, then hand off to the
  // uniform fencepost advancement path. Lua keeps the full insert command in
  // rawKeys (`s`/`cl`/etc. + typed text + `<Esc>`), so this span covers the
  // complete planned edit.
  next.cursor = newCursor;
  const uint32_t spanBegin = next.spanBegin();
  appendRawKeys(next, rawKeys, config_);
  next.editSpans.push(EditSequenceSpan{spanBegin, next.seqEnd()});
  next.hasPartialEditSpan = false;
  return afterEditCompleted(std::move(next), editIndex);
}

Outcome View::acceptSnapshot(const Lines& liveLines, CursorPos liveCursor,
                             string_view rawKeys, bool insertMode) {
  if (std::holds_alternative<Insert>(state_.phase)) {
    if (insertMode)
      return {};

    if (liveLines == state_.lines) {
      return cancelInsert();
    }

    auto outcome = acceptInsertExit(liveLines, liveCursor, rawKeys);
    if (!outcome) {
      if (auto cancelled = cancelInsert(); !cancelled)
        return cancelled;
    }
    return outcome;
  }

  const int editIndex = phaseIndex(state_.phase);
  const bool hasPlannedEdit = editIndex < totalEdits_;

  if (hasPlannedEdit) {
    const auto plannedEdit = plan_.plannedEditAt(editIndex);
    auto residualInsertion = insertResidualBetween(
        liveLines, plannedEdit.postFencepost,
        insertMode ? std::optional(liveCursor) : std::nullopt);

    if (insertMode && liveLines == state_.lines && residualInsertion &&
        liveCursor == residualInsertion->endPos &&
        containsInsertCursor(liveLines, liveCursor)) {
      State next = state_;
      next.cursor = liveCursor;
      next.phase = Insert{editIndex};
      return commit(std::move(next));
    }

    if (liveLines != plannedEdit.postFencepost &&
        residualInsertion) {
      const bool validInsertCursor =
          liveCursor == residualInsertion->endPos &&
          containsInsertCursor(liveLines, liveCursor);
      const bool validDeleteCursor =
          std::holds_alternative<Transform>(state_.phase) &&
          plannedEdit.diff.isReplacement() &&
          liveLines.contains(liveCursor);
      if ((insertMode && validInsertCursor) ||
          (!insertMode && validDeleteCursor)) {
        State next = state_;
        next.lines = liveLines;
        next.cursor = liveCursor;
        if (insertMode) {
          next.phase = Insert{editIndex};
        } else {
          if (!next.hasPartialEditSpan) {
            next.hasPartialEditSpan = true;
            next.partialEditSpanBegin = next.seqEnd();
          }
          appendRawKeys(next, rawKeys, config_);
          // The deletion may leave the cursor off the residual insertion start
          // (e.g. deleting the last char clamps it back one column). Derive the
          // phase so a non-start cursor becomes Navigate(i) rather than a
          // Transform(i) that recommendTransform would reject.
          next.phase = phaseForCursor(editIndex, next.lines, next.cursor);
        }
        return commit(std::move(next));
      }
    }
  }

  if (liveLines == state_.lines) {
    if (insertMode) {
      if (!containsInsertCursor(liveLines, liveCursor)) {
        return unexpected(
            Rejected{"snapshot reported an invalid insert cursor position"});
      }
      State next = state_;
      next.cursor = liveCursor;
      next.phase = phaseForCursor(editIndex, next.lines, next.cursor);
      if (hasPlannedEdit && std::holds_alternative<Transform>(next.phase)) {
        next.phase = Insert{editIndex};
        return commit(std::move(next));
      }
      return unexpected(
          Rejected{"insert-mode entry outside planned edit range"});
    }

    if (!liveLines.contains(liveCursor)) {
      return unexpected(
          Rejected{"snapshot reported an invalid cursor position"});
    }
    if (liveCursor == state_.cursor && rawKeys.empty())
      return {};

    State next = state_;
    next.cursor = liveCursor;
    appendRawKeys(next, rawKeys, config_);
    next.phase = phaseForCursor(editIndex, next.lines, next.cursor);
    return commit(std::move(next));
  }

  return acceptBufferState(liveLines, liveCursor, rawKeys);
}

Outcome View::cancelInsert() {
  auto gated = requireInsert("insert cancel");
  if (!gated)
    return unexpected(std::move(gated.error()));

  if (state_.hasPartialEditSpan) {
    state_.phase = Transform{*gated};
    return {};
  }

  assert(!undo_.empty() && "Insert without a beginInsert undo snapshot");
  state_ = undo_.back();
  undo_.pop_back();
  return {};
}

Outcome View::undo() {
  if (undo_.empty())
    return unexpected(Rejected{"nothing to undo"});
  if (isHistoryCheckpoint(state_)) {
    redo_.push_back(state_);
  }
  state_ = undo_.back();
  undo_.pop_back();
  return {};
}

Outcome View::redo() {
  if (redo_.empty())
    return unexpected(Rejected{"nothing to redo"});
  if (isHistoryCheckpoint(state_)) {
    undo_.push_back(state_);
  }
  state_ = redo_.back();
  redo_.pop_back();
  return {};
}

} // namespace Explore
