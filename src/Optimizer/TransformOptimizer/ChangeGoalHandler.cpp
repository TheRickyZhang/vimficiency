module;
#include <cassert>
#include "Utils/CheckMacro.h"

module vf.Optimizer.TransformOptimizer.ChangeGoalHandler;

import std.compat;
import vf.Keyboard.ToKeys.MovementToKeys;
import vf.Optimizer.BuildTypedCommands;
import vf.Optimizer.TransformOptimizer.TransformPostExplorerEmissions;
import vf.Utils.Debug;

using namespace std;

namespace {

int firstDotBeforeDotContextReset(
    const vector<ChangeGoalHandler::SuffixCommandInfo>& commands,
    int start) {
  for (int i = start; i < static_cast<int>(commands.size()); i++) {
    if (commands[i].isDotRepeat) return i;
    if (commands[i].updatesDotRepeat) return -1;
  }
  return -1;
}

vector<const TransformPathStep*> collectPath(
    shared_ptr<const TransformPathStep> tail) {
  vector<const TransformPathStep*> path;
  for (auto node = std::move(tail); node; node = node->prev) {
    path.push_back(node.get());
  }
  reverse(path.begin(), path.end());
  return path;
}

KeyedSequence buildExpandedDotPrefix(const SuffixProgram& program,
                                     int startIndex,
                                     int dotIndex,
                                     string_view expandedDotCmd) {
  KeyedSequence prefix;
  for (int i = startIndex; i < dotIndex; i++) {
    prefix += program.editCmds[i];
  }
  prefix += KeyedSequence(
      expandedDotCmd, globalSequenceToKeys().tokenize(expandedDotCmd));
  return prefix;
}

struct LinewiseChangeShape {
  int lineCount;
  int cursorLine;
};

LinewiseChangeShape linewiseChangeShapeForCommand(
    const TransformState& base,
    LineRange range) {
  int rangeLineCount = range.endLine - range.beginLine;
  return {
      static_cast<int>(base.getLines().size()) - rangeLineCount + 1,
      range.beginLine};
}

}  // namespace

ChangeGoalHandler::ChangeGoalHandler(
    const Lines& effectiveLines, int leftColOffset, int,
    double effortWeight, const Config& config,
    const TransformBoundary&, const Lines& initialLines,
    const Lines& goalLines, const string& pre,
    const string& suf, const string& preSuf, const IndentOptions& indent)
    : effectiveLines(effectiveLines),
      leftColOffset(leftColOffset),
      effortWeight(effortWeight),
      config(config),
      initialLines(initialLines),
      goalLines(goalLines),
      pre(pre),
      suf(suf),
      preSuf(preSuf), indent(indent) {
  typed = buildTypedCommands(goalLines, "", pre, suf, indent);
  typedEffort = RunningEffort(typed.keys, config);

  if (indent.autoindent) {
    goalFirstIndent = IndentOptions::leadingIndent(goalLines[0]);
    if (!goalFirstIndent.empty()) {
      typedAfterIndent = buildTypedCommands(
          goalLines, goalFirstIndent, pre, suf, indent);
      afterIndentEffort = RunningEffort(typedAfterIndent.keys, config);
    }
  }
}

bool ChangeGoalHandler::isGoalReached(const Lines& lines) const {
  if (lines.size() == 1) return lines[0] == preSuf;
  if (lines[0] != pre) return false;
  if (lines.back() != suf) return false;
  for (size_t i = 1; i < lines.size() - 1; i++) {
    if (!lines[i].empty()) return false;
  }
  return true;
}

bool ChangeGoalHandler::isGoalReached(const TransformEditorState& state) const {
  const Lines& lines = state.getLines();
  if (!isGoalReached(lines)) return false;
  if (lines.size() != 1) return true;

  CursorPos pos = state.getPos();
  return pos.line == 0 && pos.col == static_cast<int>(pre.size());
}

bool ChangeGoalHandler::isDotRepeat(const TransformState& base, const SequenceBinding& sourceCmd) const {
  const DotRepeat& last = base.getLastEdit();
  return !last.empty() &&
         last.count == sourceCmd.count &&
         last.base == sourceCmd.base.seq.view();
}

// Static helpers

// In a multiline shell <Del> joins only after the prefix, and <BS> joins only at column zero.
void ChangeGoalHandler::enterAndCollapseClearedShell(
    KeyedSequence& keys, const Lines& lines, CursorPos pos) {
  if (lines.size() > 1 && pos.line == 0 && !lines.front().empty()) {
    keys += KeyedSequence::A;
  } else {
    if (pos.line > 0 && pos.col != 0) keys += KeyedSequence::Zero;
    keys += KeyedSequence::i;
  }
  keys += buildCollapseSequence(lines.size(), pos.line);
}

void ChangeGoalHandler::appendOptionalCount(KeyedSequence& out, int count, const KeyedSequence& base) {
  assert(count >= 0 && count <= CountPrefixLimits::MAX_PREFIX_COUNT);
  if (count <= 1) {
    out += base;
  } else {
    out.appendCounted(count, base);
  }
}

KeyedSequence ChangeGoalHandler::withOptionalCount(int count, const KeyedSequence& base) {
  assert(count >= 0 && count <= CountPrefixLimits::MAX_PREFIX_COUNT);
  if (count <= 1) {
    return base;
  }
  return KeyedSequence(count, base);
}

KeyedSequence ChangeGoalHandler::deleteToChangeChar(const SequenceBinding& sourceCmd) {
  int count = sourceCmd.count;
  const KeyedSequence& baseKS = sourceCmd.base;
  string_view baseCmd = baseKS.seq.view();
  KeyedSequence result;

  if (baseCmd == "D") {
    result = KeyedSequence::C;
  } else if (baseCmd == "x") {
    result = KeyedSequence::s;
  } else if (baseCmd == "X") {
    result = KeyedSequence::hs;
  } else if (baseCmd == "dw" || baseCmd == "dW") {
    appendOptionalCount(result, count, baseKS);
    result += KeyedSequence::i;
    return result;
  } else if (baseCmd.size() > 1 && baseCmd[0] == 'd') {
    result = baseKS.asChange();
  } else {
    assert(false && "unsupported command in TransformOptimizer!");
    return {};
  }
  return withOptionalCount(count, result);
}

KeyedSequence ChangeGoalHandler::deleteToChangeLine(const SequenceBinding& sourceCmd,
                                                    string_view lineContent, bool autoindent) {
  int count = sourceCmd.count;
  const KeyedSequence& baseKS = sourceCmd.base;
  string_view baseCmd = baseKS.seq.view();

  if (baseCmd == "dd") {
    if (count == 0) {
      if (autoindent && !IndentOptions::leadingIndent(lineContent).empty()) return KeyedSequence::ZeroC;
      return KeyedSequence::cc;
    }
    return withOptionalCount(count, KeyedSequence::cc);
  }

  return withOptionalCount(count, baseKS.asChange());
}

RunningEffort ChangeGoalHandler::mergeGoalSuffixEffort(
    const KeyedSequence& prefix,
    const RunningEffort& typedSuffixEffort,
    double completionPenalty,
    const Config& config) {
  RunningEffort prefixEffort(prefix.keys, config);
  if (completionPenalty > 0.0) {
    prefixEffort.addPenalty(completionPenalty);
  }
  return RunningEffort::merge(prefixEffort, typedSuffixEffort, config);
}

CursorPos ChangeGoalHandler::seedPositionForStart(int startIndex, const Lines& initialLines,
                                                  int leftColOffset) {
  CursorPos pos = initialLines.cursorFromFlatIndexClamped(startIndex);
  if (pos.line == 0) pos.setCol(pos.col + leftColOffset);
  return pos;
}

KeyedSequence ChangeGoalHandler::buildChangePrefix(
    const SequenceBinding& sourceCmd,
    const Lines& postDelLines, const CursorPos& postDelPos) {
  KeyedSequence prefix = deleteToChangeChar(sourceCmd);
  prefix += buildCollapseSequence(postDelLines.size(), postDelPos.line);
  return prefix;
}

// Suffix cache methods

vector<RunningEffort> ChangeGoalHandler::buildRawSuffixEfforts(
    const SuffixProgram& program,
    const RunningEffort& terminalSuffixEffort,
    int extraPenaltyIndex,
    double extraPenalty) const {
  int n = program.size();
  vector<RunningEffort> suffixEfforts(n + 1);
  suffixEfforts[n] = terminalSuffixEffort;

  for (int i = n - 1; i >= 0; i--) {
    RunningEffort editEffort(program.editCmds[i].keys, config);
    if (i == extraPenaltyIndex && extraPenalty > 0.0) {
      editEffort.addPenalty(extraPenalty);
    }
    suffixEfforts[i] = RunningEffort::merge(editEffort, suffixEfforts[i + 1], config);
  }
  return suffixEfforts;
}

SuffixValue ChangeGoalHandler::buildSuffixValueForNextIndex(
    const shared_ptr<const SuffixProgram>& suffixProgram,
    const vector<SuffixCommandInfo>& commandInfos,
    const vector<RunningEffort>& rawSuffixEfforts,
    int nextIndex,
    const DotRepeat& lastEdit) const {
  int programSize = suffixProgram->size();
  assert(nextIndex >= 0 && nextIndex <= programSize);

  int dotIndex = firstDotBeforeDotContextReset(commandInfos, nextIndex);
  if (dotIndex < 0 || lastEdit.empty()) {
    return SuffixValue(suffixProgram, nextIndex, rawSuffixEfforts[nextIndex]);
  }

  string expectedDotCmd = countedCommandString(lastEdit.count, lastEdit.base);
  KeyedSequence expandedPrefix = buildExpandedDotPrefix(
      *suffixProgram, nextIndex, dotIndex, expectedDotCmd);
  RunningEffort expandedPrefixEffort(expandedPrefix.keys, config);

  RunningEffort expandedEffort =
      RunningEffort::merge(expandedPrefixEffort, rawSuffixEfforts[dotIndex + 1], config);

  return SuffixValue(
      suffixProgram, dotIndex + 1, std::move(expandedPrefix), expandedEffort,
      expectedDotCmd, nextIndex, rawSuffixEfforts[nextIndex]);
}

void ChangeGoalHandler::cacheSuffixesOfGoal(const TransformState& goal) {
  const shared_ptr<const TransformPathStep> goalStep = goal.getPathTail();
  // A state already on the cleared shell is a goal no completion produced.
  if (!goalStep || !goalStep->goalCompletion) return;
  const GoalCompletion& completion = *goalStep->goalCompletion;

  vector<const TransformPathStep*> path = collectPath(goalStep->prev);
  // Suffix cache is populated only from completed Normal-mode goal paths.
  // The path-step chain's initial state is the search start (no preceding
  // command), which is always Normal.
  CHECK(path.empty() || path.back()->modeAfter == Mode::Normal,
        "cacheSuffixesOfGoal called for a goal completed from a non-Normal state");
  cachePopulations++;

  vector<KeyedSequence> commands;
  vector<SuffixCommandInfo> commandInfos;
  commands.reserve(path.size() + 1);
  commandInfos.reserve(path.size() + 1);
  for (const TransformPathStep* step : path) {
    commands.emplace_back(
        step->command, globalSequenceToKeys().tokenize(step->command));
    commandInfos.push_back(SuffixCommandInfo{
        step->isDotRepeat, step->updatesDotRepeat});
  }
  commands.push_back(completion.command);
  commandInfos.push_back(SuffixCommandInfo{
      false, true});

  int prefixCommandCount = static_cast<int>(path.size());
  int programCommandCount = static_cast<int>(commands.size());
  auto suffixProgram = make_shared<SuffixProgram>(std::move(commands), completion.typed);

  vector<RunningEffort> rawSuffixEfforts =
      buildRawSuffixEfforts(*suffixProgram, completion.typedEffort,
                            prefixCommandCount, completion.penalty);

  uint64_t initialHash = hashLines(effectiveLines);
  int initialLineCount = static_cast<int>(effectiveLines.size());
  CursorPos initialPos =
      seedPositionForStart(goal.getStartIndex(), initialLines, leftColOffset);
  for (int nextIndex = 0; nextIndex < programCommandCount; nextIndex++) {
    uint64_t linesHash = initialHash;
    int lineCount = initialLineCount;
    CursorPos replayPos = initialPos;
    Mode replayMode = Mode::Normal;
    DotRepeat lastEdit;
    if (nextIndex > 0) {
      const TransformPathStep* previous = path[static_cast<size_t>(nextIndex - 1)];
      linesHash = previous->linesHashAfter;
      lineCount = previous->lineCountAfter;
      replayPos = previous->posAfter;
      replayMode = previous->modeAfter;
      lastEdit = previous->lastEditAfter;
    }

    SuffixKey sk(linesHash, lineCount, replayPos, replayMode);
    if (suffixCache.find(sk) == suffixCache.end()) {
      suffixCache[sk] = buildSuffixValueForNextIndex(
          suffixProgram, commandInfos, rawSuffixEfforts, nextIndex, lastEdit);
    }
  }
}

// Goal emission methods

GoalStates ChangeGoalHandler::emitEditGoal(
    const TransformEditorState& postCompletionState, const TransformState& base,
    const SequenceBinding& sourceCmd,
    const KeyedSequence& goalCompletionCmd,
    const TypedGoalVariants& typedVariants,
    const TransformEditorState* repeatLanding) {
  double completionPenalty = sourceCmd.effort.getPenalty();

  debug("emitEditGoal:",
        "sourceBase='" + string(sourceCmd.base.seq.view()) + "'",
        "sourceCount=" + to_string(sourceCmd.count),
        "goalCompletion='" + goalCompletionCmd.seq.str() + "'",
        "typed='" + typedVariants.normalTyped.sequence.seq.str() + "'",
        "baseSeq='" + base.getSeq() + "'",
        "postPos=(" + to_string(postCompletionState.getPos().line) + "," +
            to_string(postCompletionState.getPos().col) + ")");

  RunningEffort normalEffort = mergeGoalSuffixEffort(
      goalCompletionCmd, typedVariants.normalTyped.effort, completionPenalty, config);
  KeyedSequence normalSeq = goalCompletionCmd;
  normalSeq += typedVariants.normalTyped.sequence;

  debug("emitEditGoal: fullNormalSeq='" + normalSeq.seq.str() + "'");

  auto completion = make_shared<const GoalCompletion>(GoalCompletion{
      goalCompletionCmd, completionPenalty, typedVariants.normalTyped.sequence,
      typedVariants.normalTyped.effort});

  TransformStateFactory states(config, effortWeight);
  TransformState normalState = states.afterGoalCommand(
      base, postCompletionState, normalSeq.seq.view(), normalEffort,
      DotRepeat{string(sourceCmd.base.seq.view()), sourceCmd.count},
      EntryCurswant::Ignored, completion);

  GoalStates result{std::move(normalState), nullopt};

  if (repeatLanding && isDotRepeat(base, sourceCmd)) {
    KeyedSequence dotSeq(".", KeyedSequence::Period.keys);
    enterAndCollapseClearedShell(dotSeq, repeatLanding->getLines(), repeatLanding->getPos());
    RunningEffort dotPrefixEffort(dotSeq.keys, config);
    RunningEffort dotEffort = RunningEffort::merge(dotPrefixEffort, typedVariants.dotTyped.effort, config);
    dotSeq += typedVariants.dotTyped.sequence;

    result.dotVariant = states.afterGoalCommand(
        base, *repeatLanding, dotSeq.seq.view(), dotEffort, base.getLastEdit(),
        EntryCurswant::Ignored, completion);
  }

  return result;
}

Result ChangeGoalHandler::resultFromClearedGoal(const TransformState& base) const {
  KeyedSequence completion;
  enterAndCollapseClearedShell(completion, base.getLines(), base.getPos());
  completion += typed;

  RunningEffort completionEffort(completion.keys, config);
  RunningEffort totalEffort =
      RunningEffort::merge(base.getRunningEffort(), completionEffort, config);

  Sequence seq = Sequence(base.getSeq());
  seq.append(completion.seq.view());
  return Result(seq, totalEffort);
}

GoalStates ChangeGoalHandler::onDeletionGoal(
    const TransformEditorState& afterDel, const TransformState& base,
    const SequenceBinding& sourceCmd, const TransformEditorState* repeatLanding) {
  KeyedSequence prefix = buildChangePrefix(sourceCmd, afterDel.getLines(), afterDel.getPos());
  TypedGoalVariants typedVariants{{typed, typedEffort}, {typed, typedEffort}};
  return emitEditGoal(afterDel, base, sourceCmd, prefix, typedVariants, repeatLanding);
}

GoalStates ChangeGoalHandler::onLinewiseGoal(
    const TransformEditorState& afterDel, const TransformState& base, LineRange range,
    const SequenceBinding& sourceCmd, const TransformEditorState* repeatLanding) {
  const auto [ccLineCount, line] = linewiseChangeShapeForCommand(base, range);
  KeyedSequence changeCmd = deleteToChangeLine(sourceCmd, base.getLines()[line], indent.autoindent);
  KeyedSequence changePrefix = changeCmd;
  const bool keepsIndent = indent.autoindent && changeCmd.seq.view() != "0C";
  const bool joinsPrevious = ccLineCount > 1 && line > 0;
  const bool useAfterIndent = keepsIndent && !joinsPrevious && !goalFirstIndent.empty();
  if (keepsIndent) {
    const auto sourceIndent = IndentOptions::leadingIndent(base.getLines()[line]);
    changePrefix += computeIndentAdjustment(sourceIndent, useAfterIndent ? goalFirstIndent : "", indent);
  }
  changePrefix += buildCollapseSequence(ccLineCount, line);

  const auto& suffixTyped = useAfterIndent ? typedAfterIndent : typed;
  const auto& suffixEffort = useAfterIndent ? afterIndentEffort : typedEffort;
  TypedGoalVariants typedVariants{{suffixTyped, suffixEffort}, {typed, typedEffort}};
  return emitEditGoal(afterDel, base, sourceCmd, changePrefix, typedVariants, repeatLanding);
}

GoalStates ChangeGoalHandler::onJoinGoal(
    const TransformEditorState& afterJn, const TransformState& base,
    const SequenceBinding& sourceCmd) {
  KeyedSequence goalCompletionCmd;
  appendOptionalCount(goalCompletionCmd, sourceCmd.count, sourceCmd.base);
  enterAndCollapseClearedShell(goalCompletionCmd, afterJn.getLines(), afterJn.getPos());
  TypedGoalVariants typedVariants{{typed, typedEffort}, {typed, typedEffort}};
  return emitEditGoal(afterJn, base, sourceCmd, goalCompletionCmd, typedVariants, &afterJn);
}

SuffixCacheHit ChangeGoalHandler::findCachedSuffix(const TransformState& s) {
  SuffixKey sk(
      s.getLinesHash(), static_cast<int>(s.getLines().size()), s.getPos(),
      s.getMode());
  auto cacheIt = suffixCache.find(sk);
  if (cacheIt == suffixCache.end()) return {};

  cacheHits++;
  const SuffixValue& sv = cacheIt->second;
  bool useDot = sv.canUseDot(s.getLastEdit());
  const KeyedSequence& suffix = sv.suffix(useDot);
  debug("suffixCache hit:",
        "prefixSeq='" + s.getSeq() + "'",
        "suffix='" + suffix.seq.str() + "'",
        "pos=(" + to_string(s.getPos().line) + "," + to_string(s.getPos().col) + ")",
        "useDot=" + string(useDot ? "true" : "false"));
  return {&suffix, &sv.suffixEffort(useDot)};
}

TransformResult ChangeGoalHandler::finalize(
    vector<vector<Result>>&& resultsByStart, const Lines& initialLines,
    const Lines& goalLines, const TransformOptimizerParams&,
    int bufferBeginLine, int bufferBeginCol, CursorPos goalPos,
    TransformSearchStats stats,
    vector<vector<CursorPos>>&& resultGoalPositions) {
  auto& bucket = resultsByStart[0];
  if (!bucket.empty() &&
      initialLines.size() == 1 && goalLines.size() == 1 &&
      initialLines[0].size() == goalLines[0].size()) {
    double bestCost = min_element(bucket.begin(), bucket.end(),
        [](const Result& a, const Result& b) { return a.getCost() < b.getCost(); })->getCost();
    auto replacementResult = TransformPostExplorer::tryReplacement(
        initialLines[0], goalLines[0], config, bestCost);
    if (replacementResult.has_value()) {
      bucket.push_back(*replacementResult);
    }
  }

  stats.setCacheStats(cacheHits,
                      static_cast<int>(suffixCache.size()),
                      cachePopulations);

  return TransformResult(
      std::move(resultsByStart), stats, initialLines,
      bufferBeginLine, bufferBeginCol, goalPos, std::move(resultGoalPositions));
}
