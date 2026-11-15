#include "LuaExports/Api.h"
#include "Utils/CheckMacro.h"
#if VIMF_PROTOBUF_SPIKE
#include "explore.pb.h"
#endif

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

// The pack* functions below write the `VF.Explore.*` shapes declared in
// lua/vimficiency/types.lua, field for field.

void writePosition(MsgpackWriter& w, int row, int col) {
  w.map(2).key("row").integer(row).key("col").integer(col);
}

// `is_completed` is the C++-side derived predicate (cursor has reached goalPos
// at the post-final-edit nav segment). The UI decides whether to render
// `target_range` based on phase (e.g. hidden during Insert).
Packed packState(const View& v) {
  const CharRange target = v.currentTargetRange();
  const Explore::State& st = v.state();
  MsgpackWriter w;
  w.map(9);
  w.key("phase").map(2)
      .key("kind").str(Explore::phaseName(st.phase))
      .key("edit_index").integer(Explore::phaseIndex(st.phase));
  w.key("is_completed").boolean(v.isCompleted());
  w.key("cursor");
  writePosition(w, st.cursor.line, st.cursor.col);
  w.key("total_edits").integer(v.totalEdits());
  w.key("cost").number(st.cost);
  w.key("seq").str(st.seq);
  w.key("can_undo").boolean(v.canUndo());
  w.key("can_redo").boolean(v.canRedo());
  w.key("target_range").map(2);
  w.key("begin_pos");
  writePosition(w, target.begin.line, target.begin.col);
  w.key("end_pos");
  writePosition(w, target.end.line, target.end.col);
  return w.take();
}

// The new view state is read separately via vf_explore_state — Lua callers
// refresh state after every action anyway.
Packed packOutcome(const Outcome& outcome) {
  MsgpackWriter w;
  if (outcome.has_value()) {
    w.map(1).key("status").str("Applied");
  } else {
    w.map(2).key("status").str("Rejected").key("reason").str(outcome.error().reason);
  }
  return w.take();
}

// The recommendation's "kind" (motion vs edit-structural vs typed-text) is
// implicit in the view's current phase, which Lua already reads off
// `state.phase.kind`.
Packed packSuggestions(const vector<Suggestion>& recs) {
  MsgpackWriter w;
  w.array(recs.size());
  for (const Suggestion& item : recs) {
    w.map(6);
    w.key("text").str(item.token);
    w.key("cost_diff").number(item.costDiff);
    w.key("distance").number(item.distance);
    w.key("score").number(item.score);
    w.key("landing");
    writePosition(w, item.landingPos.line, item.landingPos.col);
    w.key("literal_text").str(item.literalText);
  }
  return w.take();
}

// Each row is a raw key sequence; Lua applies `sequence_display.inline()` per
// row when rendering.
Packed packHeaderRows(const View& v) {
  const auto rows = v.headerRows();
  MsgpackWriter w;
  w.map(2);
  w.key("explored").strings(rows.explored);
  w.key("optimal").array(rows.optimal.size());
  for (const auto& col : rows.optimal) w.strings(col);
  return w.take();
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

PackedResult startAsyncImpl(const VFSearchRequest* request, int deadline_ms) {
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
  const int job_id = g_jobs.create(std::move(job));
  return packInteger(job_id);
}

}  // namespace

extern "C" {

VFByteSlice vf_explore_start_async(const VFSearchRequest* request, int deadline_ms) {
  static string storage;
  return guarded(storage, [&] { return startAsyncImpl(request, deadline_ms); });
}

// Returns pending while the worker runs; once done, frees the job and either
// builds the View from the precomputed plan, registers it and returns the
// view_id, or returns the worker's failure.
VFByteSlice vf_explore_poll(int job_id) {
  static string storage;
  if (!g_jobs.get(job_id).ready()) return storePending(storage);
  return guarded(storage, [&]() -> PackedResult {
    std::unique_ptr<ExploreJob> owned = g_jobs.take(job_id);
    if (!owned->error().empty()) return unexpectedError(owned->error());
    ExploreState& s = owned->state();
    logic::SearchInputs& in = s.inputs;
    const int view_id = g_registry.create(
        std::move(in.initialLines),
        in.initialPos,
        std::move(in.goalLines),
        in.goalPos,
        std::move(in.boundary),
        in.navContext,
        std::move(s.config),
        in.userSeq,
        s.compositionParams,
        std::move(s.trace));
    return packInteger(view_id);
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
  return guarded(storage, [&] { return packState(g_registry.get(view_id)); });
}

VFByteSlice vf_explore_recommendations(
    int view_id,
    int max_count,
    const char* optimizer_overrides,
    size_t optimizer_overrides_len,
    const char* sort_mode,
    size_t sort_mode_len) {
  static string storage;
  return guarded(storage, [&]() -> PackedResult {
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
    return packSuggestions(v.recommendations(max_count, &*overrides, mode));
  });
}

VFByteSlice vf_explore_reconfigure_plan(
    int view_id,
    const char* optimizer_overrides,
    size_t optimizer_overrides_len) {
  static string storage;
  return guarded(storage, [&]() -> PackedResult {
    View& v = g_registry.get(view_id);
    auto overridesText = requiredBytes(
        optimizer_overrides,
        optimizer_overrides_len,
        "optimizer_overrides");
    if (!overridesText) return unexpected(overridesText.error());

    auto overrides = logic::parseOverrides(*overridesText);
    if (!overrides) return unexpected(overrides.error());
    const bool reset = v.reconfigurePlan(overrides->resolved(CompositionOptimizerParams{}));
    return MsgpackWriter().map(1).key("reset").boolean(reset).take();
  });
}

VFByteSlice vf_explore_current_lines(int view_id) {
  static string storage;
  return guarded(storage, [&] {
    return packStrings(g_registry.get(view_id).state().lines);
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
  return guarded(storage, [&]() -> PackedResult {
    View& v = g_registry.get(view_id);
    auto rawKeys = requiredBytes(raw_keys, raw_keys_len, "raw_keys");
    if (!rawKeys) return unexpected(rawKeys.error());
    auto liveLines = payload::linesFromSlices(lines, lines_count);
    if (!liveLines) return unexpected(liveLines.error());
    return packOutcome(v.acceptSnapshot(
        *liveLines, CursorPos(new_row, new_col), *rawKeys, insert_mode));
  });
}

VFByteSlice vf_explore_undo(int view_id) {
  static string storage;
  return guarded(storage, [&] { return packOutcome(g_registry.get(view_id).undo()); });
}

VFByteSlice vf_explore_redo(int view_id) {
  static string storage;
  return guarded(storage, [&] { return packOutcome(g_registry.get(view_id).redo()); });
}

VFByteSlice vf_explore_header_rows(int view_id) {
  static string storage;
  return guarded(storage, [&] { return packHeaderRows(g_registry.get(view_id)); });
}

#if VIMF_PROTOBUF_SPIKE
// Protobuf viability spike: raw protobuf bytes, no envelope. Declared by
// spike/protobuf/spike.lua, not Api.def.
VFByteSlice vf_spike_explore_state_pb(int view_id) {
  static string storage;
  return noUnwind([&] {
    const View& v = g_registry.get(view_id);
    const CharRange target = v.currentTargetRange();
    const Explore::State& st = v.state();
    vf::spike::State msg;
    msg.mutable_phase()->set_kind(Explore::phaseName(st.phase));
    msg.mutable_phase()->set_edit_index(Explore::phaseIndex(st.phase));
    msg.set_is_completed(v.isCompleted());
    msg.mutable_cursor()->set_row(st.cursor.line);
    msg.mutable_cursor()->set_col(st.cursor.col);
    msg.set_total_edits(v.totalEdits());
    msg.set_cost(st.cost);
    msg.set_seq(st.seq);
    msg.set_can_undo(v.canUndo());
    msg.set_can_redo(v.canRedo());
    vf::spike::Range* range = msg.mutable_target_range();
    range->mutable_begin_pos()->set_row(target.begin.line);
    range->mutable_begin_pos()->set_col(target.begin.col);
    range->mutable_end_pos()->set_row(target.end.line);
    range->mutable_end_pos()->set_col(target.end.col);
    storage = msg.SerializeAsString();
    return byteSlice(storage);
  });
}

VFByteSlice vf_spike_apply_result_pb(bool rejected) {
  static string storage;
  return noUnwind([&] {
    vf::spike::ApplyResult msg;
    if (rejected) {
      msg.mutable_rejected()->set_reason("cursor left the target range");
    } else {
      msg.mutable_applied();
    }
    storage = msg.SerializeAsString();
    return byteSlice(storage);
  });
}
#endif

}  // extern "C"
