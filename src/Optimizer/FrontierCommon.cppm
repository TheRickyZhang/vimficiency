module;
#include "Utils/CheckMacro.h"

export module vf.Optimizer.FrontierCommon;

import std.compat;
import vf.Utils.Debug;
export import vf.Effort.RunningEffort;
export import vf.Keyboard.Config;
export import vf.Keyboard.ToKeys.MovementToKeys;
export import vf.Optimizer.DiffPlanner.MyersDiff;
export import vf.Optimizer.OptimizerParamOverrides;
export import vf.Types.CursorPos;
export import vf.Types.Lines;
export import vf.Types.Token;
export import vf.VimCore.VimEditUtils;

export {


enum class SuggestionSortMode {
  Effort,
  Distance,
  Score,
};

struct Suggestion {
  Token token;
  CursorPos landingPos{0, 0};
  double costDiff = 0.0;  // Historical name; this is immediate effort.
  double distance = 0.0;
  double score = 0.0;
  std::string literalText;  // Insert-phase only: buffer text represented by token.
};

inline double suggestionSortValue(const Suggestion& suggestion, SuggestionSortMode mode) {
  switch (mode) {
    case SuggestionSortMode::Effort:
      return suggestion.costDiff;
    case SuggestionSortMode::Distance:
      return suggestion.distance;
    case SuggestionSortMode::Score:
      return suggestion.score;
  }
  return suggestion.costDiff;
}

inline bool suggestionLess(
    const Suggestion& a,
    const Suggestion& b,
    SuggestionSortMode mode) {
  const double av = suggestionSortValue(a, mode);
  const double bv = suggestionSortValue(b, mode);
  if (av != bv) return av < bv;
  if (mode != SuggestionSortMode::Distance && a.distance != b.distance)
    return a.distance < b.distance;
  if (mode != SuggestionSortMode::Effort && a.costDiff != b.costDiff)
    return a.costDiff < b.costDiff;
  return std::string_view(a.token) < std::string_view(b.token);
}

inline void updateSuggestionMetrics(
    Suggestion& suggestion,
    double effortDiff,
    double distance,
    double effortWeight,
    double distanceWeight) {
  suggestion.costDiff = effortDiff;
  suggestion.distance = distance;
  suggestion.score = effortWeight * effortDiff + distanceWeight * distance;
}

// Sum of line sizes + 2 per line break. The offsets exclude a protected
// prefix on the first line and suffix on the last (TransformOptimizer's
// effective-lines heuristic); frontiers pass none.
inline double textDistanceEstimate(const Lines& lines,
                                   int leftColOffset = 0,
                                   int rightColOffset = 0) {
  if (lines.empty()) return 0.0;
  int total = 0;
  for (size_t i = 0; i < lines.size(); i++) {
    int lineLen = static_cast<int>(lines[i].size());
    if (i == 0) lineLen -= leftColOffset;
    if (i == lines.size() - 1) lineLen -= rightColOffset;
    total += std::max(0, lineLen);
  }
  total += (static_cast<int>(lines.size()) - 1) * 2;
  return static_cast<double>(total);
}

inline double diffDistance(const DiffState& diff) {
  return textDistanceEstimate(diff.deletedLines()) +
         textDistanceEstimate(diff.insertedLines());
}

inline double residualDistanceAfter(const Lines& before,
                                    const Lines& afterStep,
                                    const DiffState& diff) {
  Lines target = MyersDiff::applyDiffState(diff, before);
  auto residual = DiffText::calculateContiguousResidualDiff(afterStep, target);
  if (!residual) return 0.0;
  return diffDistance(*residual);
}

inline void sortAndCapSuggestions(
    std::vector<Suggestion>& suggestions,
    int maxCount,
    SuggestionSortMode mode) {
  std::stable_sort(suggestions.begin(), suggestions.end(),
                   [mode](const Suggestion& a, const Suggestion& b) {
                     return suggestionLess(a, b, mode);
                   });
  if (std::ssize(suggestions) > maxCount)
    suggestions.resize(maxCount);
}

struct FrontierQuery {
  const Lines& lines;
  CursorPos cursor;
  std::string_view seq = {};
  int maxCount = 0;
  SuggestionSortMode sortMode = SuggestionSortMode::Effort;
  // Null means C++ optimizer defaults.
  const OptimizerParamOverrides* overrides = nullptr;
};

// Single-token collector shared by the edit-bearing frontiers. Each emission
// lane must own a disjoint token family; a duplicate means the frontier
// boundary is too broad and aborts in release.
struct SuggestionEmitter {
  std::vector<Suggestion>& items;
  std::unordered_set<std::string> emittedTokens;
  const RunningEffort& acceptedEffort;
  double acceptedCost;
  double effortWeight;
  double distanceWeight;
  const Config& config;

  inline double costDiffFor(std::string_view token) const {
    RunningEffort candidate(globalSequenceToKeys().tokenize(token), config);
    RunningEffort merged = RunningEffort::merge(acceptedEffort, candidate, config);
    return merged.getEffort() - acceptedCost;
  }

  inline void emit(std::string_view token, CursorPos landingPos, double distance = 0.0) {
    Suggestion suggestion{
        .token = Token{token},
        .landingPos = landingPos,
    };
    updateSuggestionMetrics(
        suggestion, costDiffFor(token), distance,
        effortWeight, distanceWeight);
    CHECK(emittedTokens.insert(std::string(suggestion.token)).second,
          "duplicate frontier token; fix enumerator overlap");
    items.push_back(std::move(suggestion));
  }
};

inline bool cursorInInsertionRange(CursorPos cursor, int targetLine,
                                   int beginCol, int endCol) {
  return cursor.line == targetLine &&
         cursor.col >= beginCol && cursor.col < endCol;
}

// Bare `J` that makes progress on `diff` without exactly reaching the
// post-edit fencepost; the fencepost-equal case is owned by TransformFrontier's
// explorer J lane, so the two lanes stay disjoint. Callers own the activation
// gate (which line the cursor must be on).
inline void emitProgressJoin(const Lines& lines, CursorPos cursor,
                             const DiffState& diff, SuggestionEmitter& emitter) {
  if (cursor.line < 0 || cursor.line + 1 >= static_cast<int>(lines.size())) return;

  Lines afterLines = lines;
  CursorPos afterPos = cursor;
  VimCore::joinLineRange(afterLines, afterPos, 2, /*addSpace=*/true);
  if (afterLines == lines) return;
  if (afterLines == MyersDiff::applyDiffState(diff, lines)) return;

  const double remaining = residualDistanceAfter(lines, afterLines, diff);
  if (remaining >= diffDistance(diff)) return;

  emitter.emit("J", afterPos, remaining);
}

}  // export
