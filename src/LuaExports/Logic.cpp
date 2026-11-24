module vf.LuaExports.Logic;

import std.compat;
import vf.LuaExports.Common;
import vf.LuaExports.Payload;
import vf.Optimizer.OptimizerParamOverrides;
import vf.Interpreter.SequenceParser;
import vf.Types.KeyNotation;

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

TypedKeys typedKeys(span<const KeyInput> inputs) {
  deque<string_view> fed;
  TypedKeys out;
  for (const KeyInput& input : inputs) {
    for (const string_view token : splitKeys(input.keys)) {
      if (input.fed) {
        fed.push_back(token);
      } else if (!fed.empty() && fed.front() == token) {
        fed.pop_front();
      } else {
        // Fed keys sit at the front of typeahead, so a read that is not the
        // next one means the rest were consumed without being reported.
        fed.clear();
        out.typed += token;
        if (input.insert) out.typedInsert += token;
      }
    }
  }
  return out;
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
