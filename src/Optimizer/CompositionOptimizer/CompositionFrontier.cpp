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
  emitter.emit(s.structural, s.entryPos);
}

void emitBracketQuoteStrategy(const CompositionFrontierQuery& query,
                              const CompositionStrategies::BracketQuote& s,
                              SuggestionEmitter& emitter) {
  if (query.cursor.line != s.line || query.cursor.col != s.col) return;
  emitter.emit(s.structural, query.diff.isPureDeletion() ? s.goalPos : query.diff.beginPos);
}

// Activation gate: any column on the join plan's entry line.
void emitJoinAction(const CompositionFrontierQuery& query,
                    const JoinPlan& joinPlan,
                    SuggestionEmitter& emitter) {
  if (query.cursor.line != joinPlan.entryLine) return;
  emitProgressJoin(query.lines, query.cursor, query.diff, emitter, true);
}

}  // namespace

vector<Suggestion> collectCompositionEdits(
    const CompositionFrontierQuery& query,
    const CompositionOptimizerParams& params, const Config& config) {
  vector<Suggestion> items;
  RunningEffort acceptedEffort(globalSequenceToKeys().tokenize(query.seq), config);
  const double acceptedCost = acceptedEffort.getEffort();
  SuggestionEmitter emitter{
      items, {}, acceptedEffort, acceptedCost,
      params.effortWeight, params.distanceWeight, config};

  CompositionStrategies::enumerateInsertions(
      query.diff, query.lines, params.transform.indent,
      [&](const CompositionStrategies::Insertion& s) {
        emitInsertionStrategy(query, s, emitter);
      });

  if (!query.diff.isPureInsertion()) {
    BracketQuoteContext bqContext =
        computeTextObjectContextForDiff(query.diff, query.lines, query.boundary.hasLinesAbove());
    CompositionStrategies::enumerateBracketQuotes(
        query.diff, query.lines, bqContext, params.transform.indent,
        [&](const CompositionStrategies::BracketQuote& s) {
          emitBracketQuoteStrategy(query, s, emitter);
        });

    optional<JoinPlan> joinPlan =
        computeJoinPlanForDiff(query.diff, query.lines, params, config);
    if (joinPlan) emitJoinAction(query, *joinPlan, emitter);
  }

  return items;
}
