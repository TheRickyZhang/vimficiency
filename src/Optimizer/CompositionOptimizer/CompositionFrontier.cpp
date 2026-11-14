module vf.Optimizer.CompositionOptimizer.CompositionFrontier;

import std.compat;
import vf.Optimizer.CompositionOptimizer.CompositionOptimizerParams;
import vf.Optimizer.CompositionOptimizer.CompositionStrategies;
import vf.Optimizer.CompositionOptimizer.PlannedEditArtifacts;

using namespace std;

namespace {

void emitInsertionStrategy(const CompositionFrontierQuery& query,
                           const CompositionStrategies::Insertion& s,
                           SuggestionEmitter& emitter) {
  if (!cursorInInsertionRange(query.cursor, s.targetLine, s.beginCol, s.endCol)) {
    return;
  }
  emitter.emit(s.structural, s.goalPos);
}

void emitBracketQuoteStrategy(const CompositionFrontierQuery& query,
                              const CompositionStrategies::BracketQuote& s,
                              SuggestionEmitter& emitter) {
  if (query.cursor.line != s.line || query.cursor.col != s.col) return;
  emitter.emit(s.structural, s.goalPos);
}

// Activation gate: any column on the join plan's entry line.
void emitJoinAction(const CompositionFrontierQuery& query,
                    const JoinPlan& joinPlan,
                    SuggestionEmitter& emitter) {
  if (query.cursor.line != joinPlan.entryLine) return;
  emitProgressJoin(query.lines, query.cursor, query.diff, emitter);
}

}  // namespace

vector<Suggestion> rankCompositionFrontier(
    const CompositionFrontierQuery& query,
    const Config& config) {
  vector<Suggestion> items;
  RunningEffort acceptedEffort(globalSequenceToKeys().tokenize(query.seq), config);
  const double acceptedCost = acceptedEffort.getEffort();
  const CompositionOptimizerParams params = query.overrides
      ? query.overrides->resolved(CompositionOptimizerParams{})
      : CompositionOptimizerParams{};
  SuggestionEmitter emitter{
      items, {}, acceptedEffort, acceptedCost,
      params.effortWeight, params.distanceWeight, config};

  CompositionStrategies::enumerateInsertions(
      query.diff, query.lines,
      [&](const CompositionStrategies::Insertion& s) {
        emitInsertionStrategy(query, s, emitter);
      });

  if (!query.diff.isPureInsertion()) {
    BracketQuoteContext bqContext =
        computeTextObjectContextForDiff(query.diff, query.lines);
    CompositionStrategies::enumerateBracketQuotes(
        query.diff, query.lines, bqContext,
        [&](const CompositionStrategies::BracketQuote& s) {
          emitBracketQuoteStrategy(query, s, emitter);
        });

    optional<JoinPlan> joinPlan =
        computeJoinPlanForDiff(query.diff, query.lines, params, config);
    if (joinPlan) emitJoinAction(query, *joinPlan, emitter);
  }

  sortAndCapSuggestions(items, query.maxCount, query.sortMode);
  return items;
}
