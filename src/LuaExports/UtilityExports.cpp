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

// Wire format for tokenize_* FFI: one token per line, `<kind>\t<text>\n`.
// `<kind>` is a single ASCII char encoding the `TokenKind` — see switch
// below. `<text>` is the raw token as emitted by the parser. Token text
// never contains `\t` or `\n` (special keys are stored as literal
// `<Key>` strings), so these separators are unambiguous.
//
// The kind payload is the whole point of this format: without it, the
// Lua side would keep parallel classifier tables to re-derive what the
// parser already knows, and they'd drift. See `dev/lua/replay-precompute.md`.
char tokenKindChar(TokenKind kind) {
  switch (kind) {
    case TokenKind::Movement:    return 'M';
    case TokenKind::Delete:    return 'D';
    case TokenKind::Change:    return 'C';
    case TokenKind::Visual:    return 'V';
    case TokenKind::TypedText: return 'T';
    case TokenKind::Escape:    return 'E';
  }
  return '?';  // Unreachable — compiler should warn if a case is missed.
}

}  // namespace

extern "C" {

VFByteSlice vf_tokenize_movements(const char *seq, size_t seq_len) {
  static string result_storage;
  return storeBytes(result_storage,
      requiredBytes(seq, seq_len, "seq")
          .and_then([](string_view seqText) -> Expected<string> {
            return parseMovements(seqText)
          .transform([](const vector<ParsedMovement>& motions) {
            // All motion-parser output is TokenKind::Movement by construction.
            vector<string> rows;
            rows.reserve(motions.size());
            for (const auto& motion : motions) {
              ostringstream oss;
              oss << motion;
              rows.push_back(string{tokenKindChar(TokenKind::Movement)} + "\t" + oss.str());
            }
            return joinWithTrailingNewline(rows);
          })
          .transform_error([](const MovementParseError& error) {
            return ExportError{formatMovementParseError(error)};
          });
          }));
}

VFByteSlice vf_tokenize_sequence(const char *seq, size_t seq_len) {
  static string result_storage;
  return storeBytes(result_storage,
      requiredBytes(seq, seq_len, "seq")
          .and_then([](string_view seqText) {
            return parseSequence(seqText)
          .transform([](const vector<TaggedToken>& tokens) {
            vector<string> rows;
            rows.reserve(tokens.size());
            for (const auto& tok : tokens) {
              rows.push_back(string{tokenKindChar(tok.kind)} + "\t" + tok.token);
            }
            return joinWithTrailingNewline(rows);
          })
          .transform_error([](const SequenceParseError& error) {
            return ExportError{formatSequenceParseError(error)};
          });
          }));
}

VFByteSlice vf_build_sequence(const char *encoded_events, size_t encoded_events_len) {
  static string result_storage;
  return storeBytes(result_storage,
      requiredBytes(encoded_events, encoded_events_len, "encoded_events")
          .and_then(logic::buildKeySequence));
}

VFByteSlice vf_compute_search_region(
    const char *encoded_start_lines,
    size_t encoded_start_lines_len,
    const char *encoded_end_lines,
    size_t encoded_end_lines_len,
    int start_row,
    int end_row,
    int padding) {
  static string result_storage;
  return storeBytes(result_storage,
      requiredBytes(
          encoded_start_lines,
          encoded_start_lines_len,
          "encoded_start_lines")
          .and_then(payload::decodeLineArray)
          .and_then([&](const Lines& startLines) {
            return requiredBytes(
                    encoded_end_lines,
                    encoded_end_lines_len,
                    "encoded_end_lines")
                .and_then(payload::decodeLineArray)
                .transform([&](const Lines& endLines) {
                  const auto [regionStart, regionEnd] = logic::computeSearchRegion(
                      start_row, end_row, startLines, endLines, padding);
                  return packInts(regionStart, regionEnd);
                });
          }));
}

int vf_manual_evict_reason(
    int start_row,
    int cursor_row,
    int64_t last_key_time_ns,
    bool has_last_key,
    int64_t now_ns,
    int max_search_lines,
    int manual_idle_timeout_seconds) {
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
}

// Lua settings_schema consumes these defaults without mirroring C++ values.
// Wire format: `<scope>:<key>:<int|double|bool>=<value>` per line.
VFByteSlice vf_get_optimizer_defaults() {
  static string cached;
  if (cached.empty()) {
    ostringstream out;
    auto emit = [&](string_view scope, string_view key, auto v) {
      out << scope << ':' << key;
      if constexpr (is_same_v<decltype(v), bool>) out << ":bool=" << (v ? '1' : '0');
      else if constexpr (is_same_v<decltype(v), int>) out << ":int=" << v;
      else out << ":double=" << v;
      out << '\n';
    };
#define VF_EMIT_FIELD(type, name, withName, def) emit(scope, #name, p.name);
#define VF_EMIT_SCOPE(scopeName, Params, FIELDS) \
    { const Params p; const string_view scope = scopeName; FIELDS(VF_EMIT_FIELD) }
    VF_EMIT_SCOPE("shared", OptimizerParamsBase, OPTIMIZER_BASE_FIELDS)
    VF_EMIT_SCOPE("nav", NavOptimizerParams, NAV_ALL_FIELDS)
    VF_EMIT_SCOPE("transform", TransformOptimizerParams, TRANSFORM_ALL_FIELDS)
    VF_EMIT_SCOPE("composition", CompositionOptimizerParams, COMPOSITION_ALL_FIELDS)
#undef VF_EMIT_SCOPE
#undef VF_EMIT_FIELD
    cached = out.str();
  }
  return byteSlice(cached);
}

}
