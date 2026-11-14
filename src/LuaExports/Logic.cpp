module;
#include "LuaExports/Api.h"

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

Expected<SearchInputs> decodeSearchRequest(const VFSearchRequest* request) {
  if (!request) return unexpectedError("missing search request");
  const VFSearchRequest& r = *request;

  auto initialText = requiredBytes(
      r.encoded_initial_lines, r.encoded_initial_lines_len, "encoded_initial_lines");
  if (!initialText) return unexpected(initialText.error());
  auto goalText = requiredBytes(
      r.encoded_goal_lines, r.encoded_goal_lines_len, "encoded_goal_lines");
  if (!goalText) return unexpected(goalText.error());
  auto userSeq = requiredBytes(r.user_seq, r.user_seq_len, "user_seq");
  if (!userSeq) return unexpected(userSeq.error());
  auto overridesText = requiredBytes(
      r.optimizer_overrides, r.optimizer_overrides_len, "optimizer_overrides");
  if (!overridesText) return unexpected(overridesText.error());

  auto initialLines = payload::decodeLineArray(*initialText);
  if (!initialLines) return unexpected(initialLines.error());
  auto goalLines = payload::decodeLineArray(*goalText);
  if (!goalLines) return unexpected(goalLines.error());
  auto overrides = parseOverrides(*overridesText);
  if (!overrides) return unexpected(overrides.error());

  SearchInputs in;
  in.initialLines = std::move(*initialLines);
  in.goalLines = std::move(*goalLines);
  in.userSeq.assign(*userSeq);
  in.overrides = std::move(*overrides);
  // Neovim can capture an insert-mode / 'virtualedit' cursor one past
  // end-of-line, or (for recall windows) a row outside the sliced buffer. The
  // optimizer requires normal-mode-valid endpoints, so clamp at the boundary
  // rather than aborting on the downstream CHECK.
  in.initialPos = in.initialLines.clamp(CursorPos(r.start_row, r.start_col));
  in.goalPos = in.goalLines.clamp(CursorPos(r.end_row, r.end_col));
  in.boundary = sliceBoundary(
      in.initialLines, r.boundary_first_col, r.boundary_last_col,
      r.has_lines_above, r.has_lines_below);
  in.navContext = NavContext(r.window_height, r.scroll_amount);
  return in;
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

Expected<string> buildKeySequence(string_view encoded) {
  return payload::decodeKeyTrackingEvents(encoded).transform([](const vector<payload::KeyTrackingEvent>& events) {
    string out;
    out.reserve(events.size() * 2);

    size_t i = 0;
    while (i < events.size()) {
      const auto& curr = events[i];
      bool dominated = false;
      if (i + 1 < events.size()) {
        const auto& next = events[i + 1];
        const bool sameKey = curr.keyTyped == next.keyTyped;
        const string_view currMode = modePrefix(curr.mode);
        const string_view nextMode = modePrefix(next.mode);
        if (sameKey && currMode == "no" && nextMode == "no" &&
            endsWithOperatorEdit(out + curr.keyTyped)) {
          out += curr.keyTyped;
          i += 2;
          dominated = true;
        } else if (sameKey && currMode == "no" && nextMode != "no" &&
                   i + 2 < events.size() &&
                   endsWithOperatorEdit(out + curr.keyTyped + events[i + 2].keyTyped)) {
          out += curr.keyTyped;
          i += 2;
          dominated = true;
        }
      }
      if (!dominated) {
        out += curr.keyTyped;
        i += 1;
      }
    }

    return out;
  });
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
