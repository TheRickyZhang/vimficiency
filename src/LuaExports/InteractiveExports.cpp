#include "LuaExports/Api.h"
#include "proto/common.pb.h"
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
import vf.Optimizer.Analyze;
import vf.Optimizer.OptimizerParamOverrides;
import vf.Types.CursorPos;
import vf.Types.Lines;

using namespace std;
using namespace VF::LuaExports;

namespace {

using Explore::View;
using Explore::Outcome;

ViewRegistry g_registry;

SuggestionSortMode suggestionSortMode(vf::explore::RecommendationsRequest::SortMode mode) {
  switch (mode) {
    case vf::explore::RecommendationsRequest::distance: return SuggestionSortMode::Distance;
    case vf::explore::RecommendationsRequest::score:    return SuggestionSortMode::Score;
    default:                                            return SuggestionSortMode::Effort;
  }
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
// worker is independent of the request bytes and the main thread. The View is
// constructed from the precomputed plan only once `vf_explore_poll` observes
// `ready()`.
struct ExploreState {
  SearchInputs inputs;
  CompositionOptimizerParams compositionParams;
  Config config;
  CompositionTraceResult trace;
};
using ExploreJob = AsyncJob<ExploreState>;

JobRegistry<ExploreJob> g_jobs;

}  // namespace

extern "C" {

VFByteSlice vf_explore_start_async(const char* request, size_t request_len) {
  static string storage;
  return guardedRequest<vf::explore::StartRequest>(
      storage, request, request_len, [](const vf::explore::StartRequest& req) -> ProtoResult {
        auto inputs = logic::decodeSearchRequest(req.search());
        if (!inputs) return unexpected(inputs.error());
        CompositionOptimizerParams compositionParams =
            inputs->overrides.resolved(CompositionOptimizerParams{});

        auto job = std::make_unique<ExploreJob>(
            ExploreState{std::move(*inputs), std::move(compositionParams), g_config_internal, {}},
            req.deadline_ms(),
            [](ExploreState& s, const SearchControl& control) {
              s.trace = View::computeTrace(
                  s.inputs.initialLines, s.inputs.initialPos,
                  s.inputs.goalLines, s.inputs.goalPos,
                  s.compositionParams, s.inputs.userSeq,
                  s.inputs.boundary, s.inputs.navContext, s.config, control);
            });
        vf::JobHandle handle;
        handle.set_job_id(g_jobs.create(std::move(job)));
        return proto(handle);
      });
}

// Returns pending while the worker runs; once done, frees the job and either
// builds the View from the precomputed plan, registers it and returns the
// view_id, or returns the worker's failure.
VFByteSlice vf_explore_poll(int job_id) {
  static string storage;
  return guardedPoll(storage, g_jobs, job_id, [](ExploreState& s) {
    SearchInputs& in = s.inputs;
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

int vf_explore_cancel(int job_id) {
  return noUnwind([&] { return g_jobs.cancel(job_id) ? 1 : 0; });
}

int vf_explore_destroy(int view_id) {
  return noUnwind([&] { return g_registry.destroy(view_id) ? 1 : 0; });
}

VFByteSlice vf_explore_state(int view_id) {
  static string storage;
  return guarded(storage, [&] { return g_registry.with(view_id, stateMessage); });
}

VFByteSlice vf_explore_recommendations(const char* request, size_t request_len) {
  static string storage;
  return guardedRequest<vf::explore::RecommendationsRequest>(
      storage, request, request_len,
      [](const vf::explore::RecommendationsRequest& req) -> ProtoResult {
        if (req.max_count() < 0) return unexpectedError("max_count must be non-negative");
        auto overrides = logic::parseOverrides(req.optimizer_overrides());
        if (!overrides) return unexpected(overrides.error());
        return g_registry.with(req.view_id(), [&](View& v) {
          return recommendationsMessage(v.recommendations(
              req.max_count(), &*overrides, suggestionSortMode(req.sort_mode())));
        });
      });
}

VFByteSlice vf_explore_reconfigure_plan(const char* request, size_t request_len) {
  static string storage;
  return guardedRequest<vf::explore::ReconfigurePlanRequest>(
      storage, request, request_len,
      [](const vf::explore::ReconfigurePlanRequest& req) -> ProtoResult {
        auto overrides = logic::parseOverrides(req.optimizer_overrides());
        if (!overrides) return unexpected(overrides.error());
        return g_registry.with(req.view_id(), [&](View& v) {
          vf::explore::ReconfigurePlanResult msg;
          msg.set_reset(v.reconfigurePlan(overrides->resolved(CompositionOptimizerParams{})));
          return proto(msg);
        });
      });
}

VFByteSlice vf_explore_current_lines(int view_id) {
  static string storage;
  return guarded(storage, [&] {
    return g_registry.with(view_id, [](View& v) {
      vf::Lines msg;
      for (const auto& line : v.state().lines) msg.add_lines(line);
      return proto(msg);
    });
  });
}

VFByteSlice vf_explore_accept_snapshot(const char* request, size_t request_len) {
  static string storage;
  return guardedRequest<vf::explore::SnapshotRequest>(
      storage, request, request_len, [](const vf::explore::SnapshotRequest& req) -> ProtoResult {
        auto liveLines = payload::linesFrom(req.lines());
        if (!liveLines) return unexpected(liveLines.error());
        return g_registry.with(req.view_id(), [&](View& v) {
          return outcomeMessage(v.acceptSnapshot(
              *liveLines,
              CursorPos(req.cursor().row(), req.cursor().col()),
              req.raw_keys(),
              req.insert_mode()));
        });
      });
}

VFByteSlice vf_explore_undo(int view_id) {
  static string storage;
  return guarded(storage, [&] {
    return g_registry.with(view_id, [](View& v) { return outcomeMessage(v.undo()); });
  });
}

VFByteSlice vf_explore_redo(int view_id) {
  static string storage;
  return guarded(storage, [&] {
    return g_registry.with(view_id, [](View& v) { return outcomeMessage(v.redo()); });
  });
}

VFByteSlice vf_explore_header_rows(int view_id) {
  static string storage;
  return guarded(storage, [&] { return g_registry.with(view_id, headerRowsMessage); });
}

}  // extern "C"
