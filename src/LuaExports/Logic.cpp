module vf.LuaExports.Logic;

import std.compat;
import vf.LuaExports.Common;
import vf.LuaExports.Payload;
import vf.Boundary.NavBoundary;
import vf.Optimizer.OptimizerParamOverrides;
import vf.Types.CursorPos;
import vf.Types.Lines;
import vf.Types.NavContext;
import vf.Interpreter.SequenceParser;

using namespace std;

namespace VF::LuaExports::logic {

Expected<OptimizerParamOverrides> parseOverrides(string_view text) {
  auto parsed = OptimizerParamOverrides::parse(text);
  if (!parsed) {
    return unexpectedError(formatOptimizerParamOverrideErrors(parsed.error()));
  }
  return std::move(*parsed);
}

namespace {

string_view modePrefix(string_view mode) {
  return mode.substr(0, 2);
}

bool endsWithOperatorEdit(const string& sequence) {
  auto parsed = parseSequence(sequence);
  if (!parsed || parsed->empty()) return false;
  const TokenKind kind = parsed->back().kind;
  return kind == TokenKind::Change || kind == TokenKind::Delete;
}

pair<int, int> findDiffRange(const Lines& a, const Lines& b) {
  int firstDiff = -1;
  int lastDiff = -1;
  const int maxLen = max(ssize(a), ssize(b));
  for (int i = 0; i < maxLen; ++i) {
    const string* lhs = i < ssize(a) ? &a[i] : nullptr;
    const string* rhs = i < ssize(b) ? &b[i] : nullptr;
    if (!lhs || !rhs || *lhs != *rhs) {
      if (firstDiff < 0) firstDiff = i;
      lastDiff = i;
    }
  }
  return {firstDiff, lastDiff};
}

}  // namespace

string buildKeySequence(span<const KeyEvent> events) {
  const size_t count = events.size();
  const auto mode = [&](size_t i) { return modePrefix(events[i].mode); };
  const auto keyTyped = [&](size_t i) -> const string& { return events[i].keyTyped; };

  string out;
  out.reserve(count * 2);

  size_t i = 0;
  while (i < count) {
    const string curr = keyTyped(i);
    bool dominated = false;
    if (i + 1 < count) {
      const bool sameKey = curr == keyTyped(i + 1);
      const string_view currMode = mode(i);
      const string_view nextMode = mode(i + 1);
      if (sameKey && currMode == "no" && nextMode == "no" &&
          endsWithOperatorEdit(out + curr)) {
        out += curr;
        i += 2;
        dominated = true;
      } else if (sameKey && currMode == "no" && nextMode != "no" &&
                 i + 2 < count &&
                 endsWithOperatorEdit(out + curr + keyTyped(i + 2))) {
        out += curr;
        i += 2;
        dominated = true;
      }
    }
    if (!dominated) {
      out += curr;
      i += 1;
    }
  }

  return out;
}

pair<int, int> computeSearchRegion(
    int startRow,
    int endRow,
    const Lines& startLines,
    const Lines& endLines,
    int padding) {
  int minRow = min(startRow, endRow);
  int maxRow = max(startRow, endRow);
  const auto [firstDiff, lastDiff] = findDiffRange(startLines, endLines);
  if (firstDiff >= 0) {
    minRow = min(minRow, firstDiff);
    maxRow = max(maxRow, lastDiff);
  }

  const int bufferLen = max(ssize(startLines), ssize(endLines));
  const int regionStart = max(0, minRow - padding);
  const int regionEnd = min(bufferLen - 1, maxRow + padding);
  return {regionStart, regionEnd};
}

int manualEvictReason(
    int startRow,
    int cursorRow,
    int64_t lastKeyTimeNs,
    bool hasLastKey,
    int64_t nowNs,
    int maxSearchLines,
    int manualIdleTimeoutSeconds) {
  if (abs(cursorRow - startRow) + 1 > maxSearchLines) {
    return MANUAL_EVICT_DRIFT;
  }
  if (hasLastKey && nowNs - lastKeyTimeNs > manualIdleTimeoutSeconds * 1000000000LL) {
    return MANUAL_EVICT_IDLE;
  }
  return MANUAL_EVICT_NONE;
}

}  // namespace VF::LuaExports::logic
