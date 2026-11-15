#include "LuaExports/Api.h"
#include "Optimizer/OptimizerParamFields.h"
#include "Utils/CheckMacro.h"
#include "proto/common.pb.h"
#include "proto/search.pb.h"
#include "proto/session.pb.h"

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

using vf::session::Token;

// Sending the kind is the point of the tokenize_* exports: without it, the
// Lua side would keep parallel classifier tables to re-derive what the
// parser already knows, and they'd drift. See `dev/lua/replay-precompute.md`.
Token::Kind tokenKind(TokenKind kind) {
  switch (kind) {
    case TokenKind::Movement:  return Token::movement;
    case TokenKind::Delete:    return Token::delete_;
    case TokenKind::Change:    return Token::change;
    case TokenKind::Visual:    return Token::visual;
    case TokenKind::TypedText: return Token::typed;
    case TokenKind::Escape:    return Token::escape;
  }
  CHECK(false, "unhandled TokenKind");
}

void addToken(vf::session::Tokens& msg, TokenKind kind, string_view text) {
  Token* token = msg.add_tokens();
  token->set_text(text);
  token->set_kind(tokenKind(kind));
}

void setDefault(vf::search::ParamDefault* out, bool v) { out->set_bool_value(v); }
void setDefault(vf::search::ParamDefault* out, int v) { out->set_int_value(v); }
void setDefault(vf::search::ParamDefault* out, double v) { out->set_number_value(v); }

}  // namespace

extern "C" {

VFByteSlice vf_tokenize_movements(const char* request, size_t request_len) {
  static string storage;
  return guardedRequest<vf::session::SequenceRequest>(
      storage, request, request_len, [](const auto& req) -> ProtoResult {
        auto motions = parseMovements(req.seq());
        if (!motions) return unexpectedError(formatMovementParseError(motions.error()));

        // All motion-parser output is TokenKind::Movement by construction.
        vf::session::Tokens msg;
        for (const auto& motion : *motions) {
          ostringstream oss;
          oss << motion;
          addToken(msg, TokenKind::Movement, oss.str());
        }
        return proto(msg);
      });
}

VFByteSlice vf_tokenize_sequence(const char* request, size_t request_len) {
  static string storage;
  return guardedRequest<vf::session::SequenceRequest>(
      storage, request, request_len, [](const auto& req) -> ProtoResult {
        auto tokens = parseSequence(req.seq());
        if (!tokens) return unexpectedError(formatSequenceParseError(tokens.error()));

        vf::session::Tokens msg;
        for (const auto& tok : *tokens) addToken(msg, tok.kind, tok.token);
        return proto(msg);
      });
}

VFByteSlice vf_build_sequence(const char* request, size_t request_len) {
  static string storage;
  return guardedRequest<vf::session::BuildSequenceRequest>(
      storage, request, request_len, [](const auto& req) {
        vector<logic::KeyEvent> events;
        events.reserve(req.events_size());
        for (const auto& ev : req.events()) {
          events.push_back({.mode = ev.mode(), .keyTyped = ev.key_typed()});
        }
        vf::Text msg;
        msg.set_text(logic::buildKeySequence(events));
        return proto(msg);
      });
}

VFByteSlice vf_compute_search_region(const char* request, size_t request_len) {
  static string storage;
  return guardedRequest<vf::session::SearchRegionRequest>(
      storage, request, request_len, [](const auto& req) -> ProtoResult {
        auto startLines = payload::linesFrom(req.start_lines());
        if (!startLines) return unexpected(startLines.error());
        auto endLines = payload::linesFrom(req.end_lines());
        if (!endLines) return unexpected(endLines.error());

        const auto [regionStart, regionEnd] = logic::computeSearchRegion(
            req.start_row(), req.end_row(), *startLines, *endLines, req.padding());
        vf::session::SearchRegion msg;
        msg.set_region_start(regionStart);
        msg.set_region_end(regionEnd);
        return proto(msg);
      });
}

VFByteSlice vf_manual_evict_reason(const char* request, size_t request_len) {
  static string storage;
  return guardedRequest<vf::session::ManualEvictRequest>(
      storage, request, request_len, [](const auto& req) {
        CHECK(req.max_search_lines() >= 0, "max_search_lines must be non-negative");
        CHECK(req.manual_idle_timeout_seconds() >= 0,
              "manual_idle_timeout_seconds must be non-negative");
        vf::session::ManualEvict msg;
        msg.set_reason(static_cast<vf::session::ManualEvict::Reason>(logic::manualEvictReason(
            req.start_row(),
            req.cursor_row(),
            req.last_key_time_ns(),
            req.has_last_key_time_ns(),
            req.now_ns(),
            req.max_search_lines(),
            req.manual_idle_timeout_seconds())));
        return proto(msg);
      });
}

// Lua settings_schema consumes these defaults without mirroring C++ values.
VFByteSlice vf_get_optimizer_defaults() {
  static string storage;
  return guarded(storage, [] {
    vf::search::OptimizerDefaults msg;
#define VF_EMIT_FIELD(type, name, withName, def) \
    { auto* d = scope->Add(); d->set_name(#name); setDefault(d, p.name); }
#define VF_EMIT_SCOPE(field, Params, FIELDS) \
    { const Params p; auto* scope = msg.mutable_##field(); FIELDS(VF_EMIT_FIELD) }
    VF_EMIT_SCOPE(shared, OptimizerParamsBase, OPTIMIZER_BASE_FIELDS)
    VF_EMIT_SCOPE(nav, NavOptimizerParams, NAV_ALL_FIELDS)
    VF_EMIT_SCOPE(transform, TransformOptimizerParams, TRANSFORM_ALL_FIELDS)
    VF_EMIT_SCOPE(composition, CompositionOptimizerParams, COMPOSITION_ALL_FIELDS)
#undef VF_EMIT_SCOPE
#undef VF_EMIT_FIELD
    return proto(msg);
  });
}

}
