#include "LuaExports/Api.h"
#include "Utils/CheckMacro.h"
#include "proto/explore.pb.h"

import std.compat;
import vf.LuaExports.Common;
import vf.LuaExports.Logic;
import vf.LuaExports.Payload;
import vf.LuaExports.ViewRegistry;
import vf.LuaExports.AsyncJob;
import vf.LuaExports.AsyncJobRegistry;
import vf.Explore.Explore;
import vf.Keyboard.Config;
import vf.Optimizer.OptimizerParamOverrides;
import vf.Types.CursorPos;
import vf.Types.Lines;
import vf.Utils.Debug;

using namespace std;
using namespace VF::LuaExports;

namespace {

using Explore::View;
using Explore::Outcome;

ViewRegistry g_registry;

SuggestionSortMode parseSuggestionSortMode(string_view text) {
  if (text == "distance") return SuggestionSortMode::Distance;
  if (text == "score") return SuggestionSortMode::Score;
  return SuggestionSortMode::Effort;
}

void setPosition(vf::Position* out, int row, int col) {
  out->set_row(row);
  out->set_col(col);
}

vf::explore::Phase::Kind phaseKind(const Explore::Phase& phase) {
  return std::visit(Explore::overload{
      [](const Explore::Navigate&)  { return vf::explore::Phase::Navigate; },
      [](const Explore::Transform&) { return vf::explore::Phase::Transform; },
      [](const Explore::Insert&)    { return vf::explore::Phase::Insert; },
  }, phase);
}

// The UI decides whether to render `target_range` based on phase (e.g. hidden
// during Insert).
Proto stateMessage(const View& v) {
  const CharRange target = v.currentTargetRange();
  const Explore::State& st = v.state();
  vf::explore::State msg;
  msg.mutable_phase()->set_kind(phaseKind(st.phase));
  msg.mutable_phase()->set_edit_index(Explore::phaseIndex(st.phase));
  msg.set_is_completed(v.isCompleted());
  setPosition(msg.mutable_cursor(), st.cursor.line, st.cursor.col);
  msg.set_total_edits(v.totalEdits());
  msg.set_cost(st.cost);
  msg.set_seq(st.seq);
  msg.set_can_undo(v.canUndo());
  msg.set_can_redo(v.canRedo());
  setPosition(msg.mutable_target_range()->mutable_begin_pos(), target.begin.line, target.begin.col);
  setPosition(msg.mutable_target_range()->mutable_end_pos(), target.end.line, target.end.col);
  return proto(msg);
}

// The new view state is read separately via vf_explore_state — Lua callers
// refresh state after every action anyway.
Proto outcomeMessage(const Outcome& outcome) {
  vf::explore::ApplyResult msg;
  if (outcome.has_value()) {
    msg.mutable_applied();
  } else {
    msg.mutable_rejected()->set_reason(outcome.error().reason);
  }
  return proto(msg);
}

Proto recommendationsMessage(const vector<Suggestion>& recs) {
  vf::explore::Recommendations msg;
  for (const Suggestion& item : recs) {
    vf::explore::Recommendation* rec = msg.add_items();
    rec->set_text(item.token);
    rec->set_cost_diff(item.costDiff);
    rec->set_distance(item.distance);
    rec->set_score(item.score);
    setPosition(rec->mutable_landing(), item.landingPos.line, item.landingPos.col);
    rec->set_literal_text(item.literalText);
  }
  return proto(msg);
}

Proto headerRowsMessage(const View& v) {
  const auto rows = v.headerRows();
  vf::explore::HeaderRows msg;
  for (const auto& row : rows.explored) msg.add_explored(row);
  for (const auto& result : rows.optimal) {
    vf::explore::HeaderRows::Result* out = msg.add_optimal();
    for (const auto& row : result) out->add_rows(row);
  }
  return proto(msg);
}

// Background composition search. The job owns its inputs and config so the
// worker is independent of the FFI buffers and the main thread. The View is
// constructed from the precomputed plan only once `vf_explore_poll` observes
// `ready()`.
struct ExploreState {
  logic::SearchInputs inputs;
  CompositionOptimizerParams compositionParams;
  Config config;
  CompositionTraceResult trace;
};
using ExploreJob = AsyncJob<ExploreState>;

JobRegistry<ExploreJob> g_jobs;

ProtoResult startAsyncImpl(const VFSearchRequest* request, int deadline_ms) {
  auto inputsRes = logic::decodeSearchRequest(request);
  if (!inputsRes) return unexpected(inputsRes.error());
  CompositionOptimizerParams compositionParams =
      inputsRes->overrides.resolved(CompositionOptimizerParams{});

  auto job = std::make_unique<ExploreJob>(
      ExploreState{std::move(*inputsRes), std::move(compositionParams), g_config_internal, {}},
      deadline_ms,
      [](ExploreState& s, const SearchControl& control) {
        s.trace = View::computeTrace(
            s.inputs.initialLines, s.inputs.initialPos,
            s.inputs.goalLines, s.inputs.goalPos,
            s.compositionParams, s.inputs.userSeq,
            s.inputs.boundary, s.inputs.navContext, s.config, control);
      });
  vf::explore::JobHandle handle;
  handle.set_job_id(g_jobs.create(std::move(job)));
  return proto(handle);
}

}  // namespace

extern "C" {

VFByteSlice vf_explore_start_async(const VFSearchRequest* request, int deadline_ms) {
  static string storage;
  return guardedProto(storage, [&] { return startAsyncImpl(request, deadline_ms); });
}

// Returns pending while the worker runs; once done, frees the job and either
// builds the View from the precomputed plan, registers it and returns the
// view_id, or returns the worker's failure.
VFByteSlice vf_explore_poll(int job_id) {
  static string storage;
  if (!g_jobs.get(job_id).ready()) return storeProtoPending(storage);
  return guardedProto(storage, [&]() -> ProtoResult {
    std::unique_ptr<ExploreJob> owned = g_jobs.take(job_id);
    if (!owned->error().empty()) return unexpectedError(owned->error());
    ExploreState& s = owned->state();
    logic::SearchInputs& in = s.inputs;
    vf::explore::ViewHandle handle;
    handle.set_view_id(g_registry.create(
        std::move(in.initialLines),
        in.initialPos,
        std::move(in.goalLines),
        in.goalPos,
        std::move(in.boundary),
        in.navContext,
        std::move(s.config),
        in.userSeq,
        s.compositionParams,
        std::move(s.trace)));
    return proto(handle);
  });
}

// Signals cancellation and frees the job; dropping it joins the worker on this
// (main) thread before any job state is torn down (see AsyncJob). The flag is
// polled between composition setup phases and per pop in every search loop,
// so the join is bounded by one diff's precompute or one pop expansion.
int vf_explore_cancel(int job_id) {
  return noUnwind([&] {
    std::unique_ptr<ExploreJob> owned = g_jobs.take(job_id);
    if (!owned) return 0;
    owned->cancel();
    return 1;
  });
}

int vf_explore_destroy(int view_id) {
  return noUnwind([&] { return g_registry.destroy(view_id) ? 1 : 0; });
}

VFByteSlice vf_explore_state(int view_id) {
  static string storage;
  return guardedProto(storage, [&] { return stateMessage(g_registry.get(view_id)); });
}

VFByteSlice vf_explore_recommendations(
    int view_id,
    int max_count,
    const char* optimizer_overrides,
    size_t optimizer_overrides_len,
    const char* sort_mode,
    size_t sort_mode_len) {
  static string storage;
  return guardedProto(storage, [&]() -> ProtoResult {
    CHECK(max_count >= 0, "max_count must be non-negative");
    View& v = g_registry.get(view_id);
    auto overridesText = requiredBytes(
        optimizer_overrides,
        optimizer_overrides_len,
        "optimizer_overrides");
    if (!overridesText) return unexpected(overridesText.error());
    auto sortModeText = requiredBytes(sort_mode, sort_mode_len, "sort_mode");
    if (!sortModeText) return unexpected(sortModeText.error());

    auto overrides = logic::parseOverrides(*overridesText);
    if (!overrides) return unexpected(overrides.error());
    const SuggestionSortMode mode = parseSuggestionSortMode(*sortModeText);
    return recommendationsMessage(v.recommendations(max_count, &*overrides, mode));
  });
}

VFByteSlice vf_explore_reconfigure_plan(
    int view_id,
    const char* optimizer_overrides,
    size_t optimizer_overrides_len) {
  static string storage;
  return guardedProto(storage, [&]() -> ProtoResult {
    View& v = g_registry.get(view_id);
    auto overridesText = requiredBytes(
        optimizer_overrides,
        optimizer_overrides_len,
        "optimizer_overrides");
    if (!overridesText) return unexpected(overridesText.error());

    auto overrides = logic::parseOverrides(*overridesText);
    if (!overrides) return unexpected(overrides.error());
    vf::explore::ReconfigurePlanResult msg;
    msg.set_reset(v.reconfigurePlan(overrides->resolved(CompositionOptimizerParams{})));
    return proto(msg);
  });
}

VFByteSlice vf_explore_current_lines(int view_id) {
  static string storage;
  return guardedProto(storage, [&] {
    vf::explore::Lines msg;
    for (const auto& line : g_registry.get(view_id).state().lines) msg.add_lines(line);
    return proto(msg);
  });
}

VFByteSlice vf_explore_accept_snapshot(
    int view_id,
    const VFByteSlice* lines,
    size_t lines_count,
    int new_row,
    int new_col,
    const char* raw_keys,
    size_t raw_keys_len,
    bool insert_mode) {
  static string storage;
  return guardedProto(storage, [&]() -> ProtoResult {
    View& v = g_registry.get(view_id);
    auto rawKeys = requiredBytes(raw_keys, raw_keys_len, "raw_keys");
    if (!rawKeys) return unexpected(rawKeys.error());
    auto liveLines = payload::linesFromSlices(lines, lines_count);
    if (!liveLines) return unexpected(liveLines.error());
    return outcomeMessage(v.acceptSnapshot(
        *liveLines, CursorPos(new_row, new_col), *rawKeys, insert_mode));
  });
}

VFByteSlice vf_explore_undo(int view_id) {
  static string storage;
  return guardedProto(storage, [&] { return outcomeMessage(g_registry.get(view_id).undo()); });
}

VFByteSlice vf_explore_redo(int view_id) {
  static string storage;
  return guardedProto(storage, [&] { return outcomeMessage(g_registry.get(view_id).redo()); });
}

VFByteSlice vf_explore_header_rows(int view_id) {
  static string storage;
  return guardedProto(storage, [&] { return headerRowsMessage(g_registry.get(view_id)); });
}

}  // extern "C"
