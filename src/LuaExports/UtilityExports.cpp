#include "LuaExports/Api.h"
#include "Optimizer/OptimizerParamFields.h"
#include "Utils/CheckMacro.h"

import std.compat;
import vf.LuaExports.Common;
import vf.LuaExports.Logic;
import vf.LuaExports.Payload;
import vf.Interpreter.MovementInterpreter;
import vf.Interpreter.SequenceParser;
import vf.Optimizer.CompositionOptimizer.CompositionOptimizerParams;
import vf.Optimizer.NavOptimizer.NavOptimizerParams;
import vf.Optimizer.OptimizerParamsBase;
import vf.Optimizer.TransformOptimizer.TransformOptimizerParams;
import vf.Types.Lines;
import vf.Utils.Debug;

using namespace std;
using namespace VF::LuaExports;

namespace {

// `kind` values of `VF.Sequence.Token` (lua/vimficiency/types.lua).
//
// Sending the kind is the point of the tokenize_* exports: without it, the
// Lua side would keep parallel classifier tables to re-derive what the
// parser already knows, and they'd drift. See `dev/lua/replay-precompute.md`.
string_view tokenKindName(TokenKind kind) {
  switch (kind) {
    case TokenKind::Movement:  return "movement";
    case TokenKind::Delete:    return "delete";
    case TokenKind::Change:    return "change";
    case TokenKind::Visual:    return "visual";
    case TokenKind::TypedText: return "typed";
    case TokenKind::Escape:    return "escape";
  }
  return "?";  // Unreachable — compiler should warn if a case is missed.
}

void writeToken(MsgpackWriter& w, TokenKind kind, string_view text) {
  w.map(2).key("text").str(text).key("kind").str(tokenKindName(kind));
}

}  // namespace

extern "C" {

VFByteSlice vf_tokenize_movements(const char *seq, size_t seq_len) {
  static string result_storage;
  return guarded(result_storage, [&]() -> PackedResult {
    auto seqText = requiredBytes(seq, seq_len, "seq");
    if (!seqText) return unexpected(seqText.error());
    auto motions = parseMovements(*seqText);
    if (!motions) return unexpectedError(formatMovementParseError(motions.error()));

    // All motion-parser output is TokenKind::Movement by construction.
    MsgpackWriter w;
    w.array(motions->size());
    for (const auto& motion : *motions) {
      ostringstream oss;
      oss << motion;
      writeToken(w, TokenKind::Movement, oss.str());
    }
    return w.take();
  });
}

VFByteSlice vf_tokenize_sequence(const char *seq, size_t seq_len) {
  static string result_storage;
  return guarded(result_storage, [&]() -> PackedResult {
    auto seqText = requiredBytes(seq, seq_len, "seq");
    if (!seqText) return unexpected(seqText.error());
    auto tokens = parseSequence(*seqText);
    if (!tokens) return unexpectedError(formatSequenceParseError(tokens.error()));

    MsgpackWriter w;
    w.array(tokens->size());
    for (const auto& tok : *tokens) writeToken(w, tok.kind, tok.token);
    return w.take();
  });
}

VFByteSlice vf_build_sequence(const VFKeyEvent* events, size_t events_count) {
  static string result_storage;
  return guarded(result_storage, [&] {
    return packString(logic::buildKeySequence(events, events_count));
  });
}

VFByteSlice vf_compute_search_region(
    const VFByteSlice* start_lines,
    size_t start_lines_count,
    const VFByteSlice* end_lines,
    size_t end_lines_count,
    int start_row,
    int end_row,
    int padding) {
  static string result_storage;
  return guarded(result_storage, [&]() -> PackedResult {
    auto startLines = payload::linesFromSlices(start_lines, start_lines_count);
    if (!startLines) return unexpected(startLines.error());
    auto endLines = payload::linesFromSlices(end_lines, end_lines_count);
    if (!endLines) return unexpected(endLines.error());

    const auto [regionStart, regionEnd] = logic::computeSearchRegion(
        start_row, end_row, *startLines, *endLines, padding);
    return MsgpackWriter()
        .map(2)
        .key("region_start").integer(regionStart)
        .key("region_end").integer(regionEnd)
        .take();
  });
}

int vf_manual_evict_reason(
    int start_row,
    int cursor_row,
    int64_t last_key_time_ns,
    bool has_last_key,
    int64_t now_ns,
    int max_search_lines,
    int manual_idle_timeout_seconds) {
  return noUnwind([&] {
    CHECK(max_search_lines >= 0, "max_search_lines must be non-negative");
    CHECK(manual_idle_timeout_seconds >= 0, "manual_idle_timeout_seconds must be non-negative");
    return logic::manualEvictReason(
        start_row,
        cursor_row,
        last_key_time_ns,
        has_last_key,
        now_ns,
        max_search_lines,
        manual_idle_timeout_seconds);
  });
}

// Lua settings_schema consumes these defaults without mirroring C++ values.
// Packs `{scope = {key = value}}` with each value in its native type.
VFByteSlice vf_get_optimizer_defaults() {
  static string cached;
  if (cached.empty()) guarded(cached, [] {
    MsgpackWriter w;
    auto emit = [&](string_view key, auto v) {
      w.key(key);
      if constexpr (is_same_v<decltype(v), bool>) w.boolean(v);
      else if constexpr (is_same_v<decltype(v), int>) w.integer(v);
      else w.number(v);
    };
#define VF_COUNT_FIELD(type, name, withName, def) + 1
#define VF_EMIT_FIELD(type, name, withName, def) emit(#name, p.name);
#define VF_EMIT_SCOPE(scopeName, Params, FIELDS) \
    { const Params p; w.key(scopeName).map(0 FIELDS(VF_COUNT_FIELD)); FIELDS(VF_EMIT_FIELD) }
    w.map(4);
    VF_EMIT_SCOPE("shared", OptimizerParamsBase, OPTIMIZER_BASE_FIELDS)
    VF_EMIT_SCOPE("nav", NavOptimizerParams, NAV_ALL_FIELDS)
    VF_EMIT_SCOPE("transform", TransformOptimizerParams, TRANSFORM_ALL_FIELDS)
    VF_EMIT_SCOPE("composition", CompositionOptimizerParams, COMPOSITION_ALL_FIELDS)
#undef VF_EMIT_SCOPE
#undef VF_EMIT_FIELD
#undef VF_COUNT_FIELD
    return w.take();
  });
  return byteSlice(cached);
}

}
