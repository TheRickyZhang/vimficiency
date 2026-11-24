module vf.Optimizer.CompositionOptimizer.CompositionState;

import std.compat;
import vf.Keyboard.ToKeys.MovementToKeys;

using namespace std;

void CompositionState::appendSequence(string_view s, double penalty, const Config& config) {
  sequence.append(s);
  runningEffort.append(globalSequenceToKeys().tokenize(s), config);
  runningEffort.addPenalty(penalty);
  effort = runningEffort.getEffort();
}

void CompositionState::applyEditTransitionImpl(
    const Sequence& editSequence,
    double penalty,
    const CursorPos& newPos,
    const Config& config) {
  pos = newPos;
  editsCompleted++;
  appendSequence(editSequence.view(), penalty, config);
}

void CompositionState::applyNavResultImpl(
    const Sequence& moveSequence,
    double penalty,
    const CursorPos& newPos, const Config& config) {
  pos = newPos;
  appendSequence(moveSequence.view(), penalty, config);
}
