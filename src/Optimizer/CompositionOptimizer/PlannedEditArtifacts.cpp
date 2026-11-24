module;
#include <cassert>
#include "Utils/CheckMacro.h"

module vf.Optimizer.CompositionOptimizer.PlannedEditArtifacts;

import std.compat;
import vf.Effort.RunningEffort;
import vf.Optimizer.BuildTypedCommands;
import vf.Optimizer.TransformOptimizer.TransformExplorer;
import vf.Utils.Debug;
import vf.VimCore.VimEditUtils;

using namespace std;

namespace {

tuple<int, int, bool> findMatchingQuotePair(
    const string& line,
    int beginCol,
    int endCol,
    char quote) {
  vector<int> quotePositions;
  for (int i = 0; i < static_cast<int>(line.size()); i++) {
    if (line[i] == quote) quotePositions.push_back(i);
  }

  for (size_t i = 0; i + 1 < quotePositions.size(); i += 2) {
    int open = quotePositions[i];
    int close = quotePositions[i + 1];
    if (open + 1 == beginCol && close == endCol) return {open, close, false};
    if (open == beginCol && close + 1 == endCol) return {open, close, true};
  }
  return {-1, -1, false};
}

tuple<int, int, bool> findMatchingBracketPair(
    const string& line,
    int beginCol,
    int endCol,
    char open,
    char close) {
  int bestOpen = -1;
  int bestClose = -1;
  bool bestIsAround = false;

  vector<int> openStack;
  for (int i = 0; i < static_cast<int>(line.size()); i++) {
    if (line[i] == open) {
      openStack.push_back(i);
    } else if (line[i] == close && !openStack.empty()) {
      int openPos = openStack.back();
      openStack.pop_back();

      if (openPos + 1 == beginCol && i == endCol &&
          (bestOpen == -1 || openPos > bestOpen)) {
        bestOpen = openPos;
        bestClose = i;
        bestIsAround = false;
      }

      if (openPos == beginCol && i + 1 == endCol &&
          (bestOpen == -1 || openPos > bestOpen)) {
        bestOpen = openPos;
        bestClose = i;
        bestIsAround = true;
      }
    }
  }

  return {bestOpen, bestClose, bestIsAround};
}

void scanQuotesForEdit(
    BracketQuoteContext& ctx,
    const string& line,
    int beginCol,
    int endCol) {
  int lineLen = static_cast<int>(line.size());
  if (lineLen == 0) return;

  ctx.validQuoteMask.resize(lineLen);

  for (char quote : {'"', '\'', '`'}) {
    auto [openCol, closeCol, isAround] =
        findMatchingQuotePair(line, beginCol, endCol, quote);
    if (openCol == -1 || closeCol >= lineLen) continue;

    if (isAround) ctx.useAroundQuote.add(quote);

    int firstQuoteOfType = -1;
    for (int col = 0; col < lineLen; col++) {
      if (line[col] == quote) {
        firstQuoteOfType = col;
        break;
      }
    }

    if (firstQuoteOfType == openCol) {
      for (int col = 0; col <= closeCol; col++) {
        ctx.validQuoteMask[col].add(quote);
      }
    } else {
      for (int col = openCol; col <= closeCol; col++) {
        ctx.validQuoteMask[col].add(quote);
      }
    }
  }
}

void scanBracketsForEdit(
    BracketQuoteContext& ctx,
    const string& line,
    int beginCol,
    int endCol) {
  int lineLen = static_cast<int>(line.size());
  if (lineLen == 0) return;

  ctx.validBracketMask.resize(lineLen);

  for (auto [open, close] :
       vector<pair<char, char>>{{'(', ')'}, {'[', ']'}, {'{', '}'}, {'<', '>'}}) {
    auto [openCol, closeCol, isAround] =
        findMatchingBracketPair(line, beginCol, endCol, open, close);
    if (openCol == -1 || openCol >= lineLen || closeCol >= lineLen) continue;

    if (isAround) ctx.useAroundBracket.add(open);

    vector<int> firstOpenForward(lineLen, -1);
    int nextOpen = -1;
    for (int col = lineLen - 1; col >= 0; col--) {
      if (line[col] == open) nextOpen = col;
      firstOpenForward[col] = nextOpen;
    }

    int balance = 0;
    for (int col = 0; col < lineLen; col++) {
      bool insidePair = (col >= openCol && col <= closeCol);
      bool forwardReachesPair =
          (col < openCol && balance == 0 && firstOpenForward[col] == openCol);
      if (insidePair || forwardReachesPair) {
        ctx.validBracketMask[col].add(open);
      }
      if (line[col] == open) balance++;
      else if (line[col] == close) balance--;
    }
  }
}

struct JoinSimulation {
  string joinedLine;
  vector<int> cursorCols;

  static JoinSimulation simulate(const Lines& srcLines, int begin, int end,
                                 bool addSpace = true) {
    JoinSimulation sim;
    Lines workLines(srcLines.begin() + begin, srcLines.begin() + end);
    CursorPos pos(0, 0);

    for (int l = begin + 1; l < end; l++) {
      VimCore::joinLineRange(workLines, pos, 2, addSpace);
      sim.cursorCols.push_back(pos.col);
    }
    sim.joinedLine = workLines[0];
    return sim;
  }
};

int commonPrefixLen(string_view a, string_view b) {
  int n = static_cast<int>(min(a.size(), b.size()));
  for (int i = 0; i < n; i++) {
    if (a[i] != b[i]) return i;
  }
  return n;
}

int commonSuffixLen(string_view a, string_view b, int prefixLen) {
  int la = static_cast<int>(a.size());
  int lb = static_cast<int>(b.size());
  int maxSuffix = min(la, lb) - prefixLen;
  int count = 0;
  for (int i = 0; i < maxSuffix; i++) {
    if (a[la - 1 - i] != b[lb - 1 - i]) break;
    count++;
  }
  return count;
}

// Cheapest spelling of `numJoins` consecutive joins from one line: `JJJ`,
// `J..`, or `4J`. All three leave the same text and cursor, so the count is
// fixed by the group (lines joined = numJoins + 1) and only the spelling is
// chosen. Returns the count penalty of the spelling appended.
double appendJoins(Sequence& seq, bool useGJ, int numJoins,
                   const CompositionOptimizerParams& params, const Config& config) {
  if (numJoins == 0) return 0.0;
  const KSId id = useGJ ? KSId::gJ : KSId::J;
  const KeyedSequence& join = KeyedSequence::byId(id);
  if (numJoins == 1) {
    seq.append(join.seq.view());
    return 0.0;
  }

  KeyedSequence repeated;
  repeated.append(join, numJoins);
  KeyedSequence dotted = join;
  dotted.append(KeyedSequence::Period, numJoins - 1);
  const double repeatedEffort = RunningEffort(repeated.keys, config).getEffort();
  const double dottedEffort = RunningEffort(dotted.keys, config).getEffort();
  const KeyedSequence& uncounted = dottedEffort < repeatedEffort ? dotted : repeated;

  const int lineCount = numJoins + 1;
  if (lineCount >= params.minPrefixCount && lineCount <= params.maxPrefixCount) {
    RunningEffort counted = TransformExplorerDetail::buildCountedEffort<CountClass::Join>(
        config, lineCount, id, lineCount);
    if (counted.getEffort() < min(repeatedEffort, dottedEffort)) {
      seq.append(to_string(lineCount));
      seq.append(join.seq.view());
      return counted.getPenalty();
    }
  }
  seq.append(uncounted.seq.view());
  return 0.0;
}

// Score a join simulation against a target line: characters retained without
// a residual edit. Higher is better; an exact match scores the full target
// length. J (addSpace=true) inserts a space between joined lines; gJ does
// not. We pick whichever lands closer to the target so a pure-newline
// deletion goal can collapse to bare `gJ` without a residual space-delete.
int joinSimMatchScore(string_view joined, string_view target) {
  int cp = commonPrefixLen(joined, target);
  int cs = commonSuffixLen(joined, target, cp);
  if (joined == target) return static_cast<int>(target.size()) + 1;
  return cp + cs;
}

}

TransformResult computeTransformResultForDiff(
    const DiffState& diff,
    const CompositionOptimizerParams& params,
    const Config& config,
    int* nodesExplored) {
  if (nodesExplored) *nodesExplored = 0;

  TransformOptimizer transformOptimizer(config);

  if (diff.isPureInsertion()) {
    Lines insertLines = Lines::unflatten(diff.insertedText);
    KeyedSequence full = KeyedSequence::i;
    full += buildTypedCommands(
        insertLines, "", diff.boundary.prefix(), diff.boundary.suffix());
    RunningEffort runningEffort(full.keys, config);

    vector<vector<Result>> insertResultsByStart(1);
    insertResultsByStart[0].emplace_back(std::move(full.seq), runningEffort);

    CursorPos goalPos = typedCommandsExitCursor(
        diff.beginPos, insertLines, diff.boundary.suffix());
    Lines singlePoint = {""};
    return TransformResult(std::move(insertResultsByStart), {}, singlePoint,
                      diff.beginPos.line, diff.beginPos.col, goalPos);
  }

  TransformOptimizerParams editParams =
      TransformOptimizerParams(params.transform)
          .withMinCountRepeat(params.minPrefixCount)
          .withMaxCountRepeat(params.maxPrefixCount)
          .withMaxResultsPerStartPos(params.transformMaxResultsPerStartPos);

  if (diff.isPureDeletion()) {
    if (diff.deletedText.find('\n') == string::npos &&
        (diff.boundary.hasLinesAbove() || diff.boundary.hasLinesBelow())) {
      editParams.withLinewisePureDeletion(false);
    }
    TransformResult result = transformOptimizer.optimizePureDeletion(
        diff.deletedLines(), diff.boundary, editParams,
        diff.beginPos.line, diff.beginPos.col, diff.beginPos);
    result.restrictValidStartRegion(CharRange(diff.beginPos, diff.endPos));
    if (nodesExplored) *nodesExplored = result.getStats().nodesExplored();
    return result;
  }

  CursorPos goalPos = typedCommandsExitCursor(
      diff.beginPos, diff.insertedLines(), diff.boundary.suffix());

  TransformResult result = transformOptimizer.optimizeTransform(
      diff.deletedLines(), diff.insertedLines(), diff.boundary, editParams,
      diff.beginPos.line, diff.beginPos.col, goalPos);
  result.restrictValidStartRegion(CharRange(diff.beginPos, diff.endPos));
  if (nodesExplored) *nodesExplored = result.getStats().nodesExplored();
  return result;
}

BracketQuoteContext computeTextObjectContextForDiff(
    const DiffState& diff,
    const Lines& preEditLines) {
  BracketQuoteContext ctx;

  if (diff.isPureInsertion()) return ctx;
  if (diff.beginPos.line != diff.endPos.line) return ctx;
  if (diff.beginPos.line >= static_cast<int>(preEditLines.size())) return ctx;

  const string& line = preEditLines[diff.beginPos.line];
  ctx.line = diff.beginPos.line;
  scanQuotesForEdit(ctx, line, diff.beginPos.col, diff.endPos.col);
  scanBracketsForEdit(ctx, line, diff.beginPos.col, diff.endPos.col);
  return ctx;
}

optional<JoinPlan> computeJoinPlanForDiff(
    const DiffState& diff,
    const Lines& preEditLines,
    const CompositionOptimizerParams& params,
    const Config& config,
    int* nodesExplored) {
  if (nodesExplored) *nodesExplored = 0;
  if (diff.isPureInsertion()) return nullopt;
  if (diff.isPureDeletion() && diff.deletedText.find('\n') == string::npos) {
    return nullopt;
  }

  TransformOptimizer transformOptimizer(config);

  int srcFirstLine = diff.beginPos.line;
  Lines delLines = diff.deletedLines();
  int srcEndLine = srcFirstLine + static_cast<int>(delLines.size());
  if (srcEndLine > static_cast<int>(preEditLines.size())) return nullopt;

  int sourceLineCount = srcEndLine - srcFirstLine;
  Lines targetLines = diff.insertedLines();
  int targetLineCount = static_cast<int>(targetLines.size());
  if (sourceLineCount <= targetLineCount) return nullopt;

  const string& prefix = diff.boundary.prefix();
  const string& suffix = diff.boundary.suffix();
  Lines srcLines = preEditLines.getLineRange(srcFirstLine, srcEndLine);

  Lines fullTargetLines;
  for (int t = 0; t < targetLineCount; t++) {
    string line = targetLines[t];
    if (t == 0 && targetLineCount == 1) {
      line = prefix + line + suffix;
    } else if (t == 0) {
      line = prefix + line;
    } else if (t == targetLineCount - 1) {
      line = line + suffix;
    }
    fullTargetLines.push_back(std::move(line));
  }

  vector<pair<int, int>> partition(targetLineCount);
  if (targetLineCount == 1) {
    partition[0] = {0, sourceLineCount};
  } else {
    vector<int> prefLen(sourceLineCount + 1, 0);
    for (int s = 0; s < sourceLineCount; s++) {
      prefLen[s + 1] = prefLen[s] + static_cast<int>(srcLines[s].size());
    }
    auto joinedLen = [&](int a, int b) -> int {
      return prefLen[b + 1] - prefLen[a] + (b - a);
    };

    constexpr int INF = 1000000;
    vector<vector<int>> dp(sourceLineCount + 1, vector<int>(targetLineCount + 1, INF));
    vector<vector<int>> choice(sourceLineCount + 1, vector<int>(targetLineCount + 1, -1));
    dp[sourceLineCount][targetLineCount] = 0;

    for (int t = targetLineCount - 1; t >= 0; t--) {
      int targetLen = static_cast<int>(fullTargetLines[t].size());
      for (int s = sourceLineCount - (targetLineCount - t); s >= t; s--) {
        for (int k = s; k <= sourceLineCount - (targetLineCount - t); k++) {
          int groupCost = abs(joinedLen(s, k) - targetLen);
          int total = groupCost + dp[k + 1][t + 1];
          if (total < dp[s][t]) {
            dp[s][t] = total;
            choice[s][t] = k;
          }
        }
      }
    }

    int s = 0;
    for (int t = 0; t < targetLineCount; t++) {
      int k = choice[s][t];
      assert(k != -1 && "DP partition reconstruction visited an unreachable state");
      partition[t] = {s, k + 1};
      s = k + 1;
    }
  }

  auto [entryBegin, entryEnd] = partition[0];
  if (entryEnd - entryBegin <= 1) return nullopt;

  // Text survives on both sides of the deleted newlines, so only a join can
  // merge it; similarity says nothing about whether the plan is worth it.
  const bool joinRequired =
      targetLineCount == 1 && !prefix.empty() && !suffix.empty();

  constexpr double UNSOLVED = std::numeric_limits<double>::infinity();
  struct GroupPlan {
    Sequence seq;
    double penalty = 0.0;
    double effort = UNSOLVED;
    int goalCol = 0;
  };
  auto planGroup = [&](int g, const JoinSimulation& sim, bool useGJ) {
    auto [begin, end] = partition[g];
    int numJoins = end - begin - 1;
    GroupPlan plan;
    plan.penalty = appendJoins(plan.seq, useGJ, numJoins, params, config);
    plan.goalCol = numJoins > 0 ? sim.cursorCols.back() : 0;

    if (sim.joinedLine != fullTargetLines[g]) {
      Lines residualInitial = {sim.joinedLine};
      CHECK(residualInitial.size() == 1,
            "joinResidualBoundary requires a single-line residual buffer");
      bool hasLinesAbove =
          diff.boundary.hasLinesAbove() || srcFirstLine + begin > 0;
      bool hasLinesBelow =
          diff.boundary.hasLinesBelow() ||
          srcFirstLine + end < static_cast<int>(preEditLines.size());
      TransformBoundary groupBoundary =
          TransformBoundary::joinResidualBoundary(hasLinesAbove, hasLinesBelow);
      CursorPos residualGoalPos(0, fullTargetLines[g].lastCol());
      TransformOptimizerParams residualParams =
          TransformOptimizerParams(params.transform)
              .withMinCountRepeat(params.minPrefixCount)
              .withMaxCountRepeat(params.maxPrefixCount);

      TransformResult residualResult = [&]() -> TransformResult {
        if (fullTargetLines[g].empty()) {
          return transformOptimizer.optimizePureDeletion(
              residualInitial, groupBoundary, residualParams, 0, 0, residualGoalPos);
        }
        Lines residualGoal = {fullTargetLines[g]};
        return transformOptimizer.optimizeTransform(
            residualInitial, residualGoal, groupBoundary, residualParams, 0, 0, residualGoalPos);
      }();

      if (nodesExplored) *nodesExplored += residualResult.getStats().nodesExplored();

      const Result* res = residualResult.resultAt(0, plan.goalCol);
      if (!res) return plan;

      plan.seq.append(res->getSequence().view());
      plan.penalty += res->getPenalty();
      plan.goalCol = residualResult.getGoalPos().col;
    }
    plan.effort = getEffort(plan.seq.view(), config) + plan.penalty;
    return plan;
  };

  Sequence fullSeq;
  double penalty = 0.0;
  int goalCol = 0;

  for (int g = 0; g < targetLineCount; g++) {
    auto [begin, end] = partition[g];
    const bool joins = end - begin > 1;
    auto simJ = JoinSimulation::simulate(srcLines, begin, end, /*addSpace=*/true);
    auto simGJ = JoinSimulation::simulate(srcLines, begin, end, /*addSpace=*/false);
    if (joins && !joinRequired) {
      int matchJ = joinSimMatchScore(simJ.joinedLine, fullTargetLines[g]);
      int matchGJ = joinSimMatchScore(simGJ.joinedLine, fullTargetLines[g]);
      const auto& closer = matchGJ > matchJ ? simGJ : simJ;
      int maxLen = max(static_cast<int>(closer.joinedLine.size()),
                       static_cast<int>(fullTargetLines[g].size()));
      double matchRatio =
          maxLen > 0 ? static_cast<double>(max(matchJ, matchGJ)) / maxLen : 1.0;
      if (matchRatio < 0.3) return nullopt;
    }

    // `J` and `gJ` leave different lines to finish, so cost picks; on the same
    // line `J` is the cheaper spelling.
    GroupPlan best = planGroup(g, simJ, false);
    if (joins && simGJ.joinedLine != simJ.joinedLine) {
      GroupPlan viaGJ = planGroup(g, simGJ, true);
      if (viaGJ.effort < best.effort) best = std::move(viaGJ);
    }
    if (best.effort == UNSOLVED) return nullopt;

    // `j0` (not `j` alone): JoinSimulation and the residual search both assume
    // the group is entered at col 0, and `j` would keep the prior group's
    // curswant.
    if (g > 0) fullSeq.append("j0");
    fullSeq.append(best.seq.view());
    penalty += best.penalty;
    goalCol = best.goalCol;
  }

  CursorPos goalPos(diff.beginPos.line + targetLineCount - 1, goalCol);

  double effort = getEffort(fullSeq.view(), config) + penalty;

  return JoinPlan{
      .sequence = std::move(fullSeq),
      .goalPos = goalPos,
      .effort = effort,
      .penalty = penalty,
      .entryLine = diff.beginPos.line,
  };
}
