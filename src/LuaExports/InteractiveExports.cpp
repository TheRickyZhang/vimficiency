#include "LuaExports/Api.h"
#include "Utils/CheckMacro.h"

import std.compat;
import vf.LuaExports.Common;
import vf.LuaExports.ViewRegistry;
import vf.LuaExports.AsyncJob;
import vf.LuaExports.AsyncJobRegistry;
import vf.Explore.Explore;
import vf.Optimizer.OptimizerParamOverrides;
import vf.Utils.Debug;

using namespace std;
namespace helpers = VF::LuaExports::helpers;
namespace payload = VF::LuaExports::payload;

namespace {

using Explore::View;
using Explore::Outcome;
using VF::LuaExports::ViewRegistry;
using VF::LuaExports::AsyncJob;
using VF::LuaExports::JobRegistry;

ViewRegistry g_registry;

using payload::encodeField;
using payload::encodeFields;
using payload::encodeStringList;

SuggestionSortMode parseSuggestionSortMode(string_view text) {
  if (text == "distance") return SuggestionSortMode::Distance;
  if (text == "score") return SuggestionSortMode::Score;
  return SuggestionSortMode::Effort;
}

// State payload. Length-prefixed fields so raw bytes in `seq` survive
// round-tripping. `is_completed` is the C++-side derived predicate (cursor
// has reached goalPos at the post-final-edit nav segment). The trailing
// four ints carry the current target range; the UI decides whether to
// render it based on phase (e.g. hidden during Insert).
string encodeState(const View& v) {
  const CharRange target = v.currentTargetRange();
  const Explore::State& st = v.state();
  return encodeFields(
      Explore::phaseName(st.phase),
      Explore::phaseIndex(st.phase),
      v.isCompleted() ? 1 : 0,
      st.cursor.line,
      st.cursor.col,
      v.totalEdits(),
      st.cost,
      string_view(st.seq),
      v.canUndo() ? 1 : 0,
      v.canRedo() ? 1 : 0,
      target.begin.line,
      target.begin.col,
      target.end.line,
      target.end.col);
}

// Apply-result payload. Two fields: status ("Applied" | "Rejected") and
// reason (empty on Applied). The new view state is read separately via
// vf_explore_state — Lua callers refresh state after every action anyway.
string encodeOutcome(const Outcome& outcome) {
  const string_view status = outcome.has_value() ? "Applied" : "Rejected";
  const string_view reason =
      outcome.has_value() ? string_view{} : string_view(outcome.error().reason);
  return encodeFields(status, reason);
}

// Wire encoder for one recommendation. 7 fields — Lua's
// parse_explore_recommendations must match the field count. The recommendation's
// "kind" (motion vs edit-structural vs typed-text) is implicit in the view's
// current phase, which Lua already reads off `state.phase.kind`.
string encodeSuggestion(const Suggestion& item) {
  return encodeFields(
      string_view(item.token),
      item.costDiff,
      item.distance,
      item.score,
      item.landingPos.line,
      item.landingPos.col,
      string_view(item.literalText));
}

string encodeSuggestions(const vector<Suggestion>& recs) {
  string out = encodeField(recs.size());
  for (const auto& r : recs) out += encodeSuggestion(r);
  return out;
}

// Length-prefixed header-rows payload. Each row is a raw key sequence (may
// contain `<Esc>` and arbitrary insert-mode bytes); length-prefix encoding
// avoids the separator-escape problem entirely. Lua applies
// `sequence_display.inline()` per row when rendering.
//
// Layout (all fields are length-prefixed via `encodeField`):
//   explored_count, row_0, row_1, ..., row_{explored_count-1},
//   optimal_count,
//   for each i in [0, optimal_count):
//     col_i_count, row_0, row_1, ..., row_{col_i_count-1}
string encodeHeaderRows(const View& v) {
  const auto rows = v.headerRows();
  string out = encodeField(rows.explored.size());
  out += encodeStringList(rows.explored);
  out += encodeField(rows.optimal.size());
  for (const auto& col : rows.optimal) {
    out += encodeField(col.size());
    out += encodeStringList(col);
  }
  return out;
}

// Fully-owning decode of the explore-start FFI inputs. Owns `userSeq` as a
// std::string (not a borrowed view) so it can outlive the original FFI buffers
// on the async worker thread.
struct StartInputs {
  Lines initialLines;
  CursorPos initialPos{0, 0};
  Lines goalLines;
  CursorPos goalPos{0, 0};
  NavBoundary boundary;
  NavContext navContext;
  std::string userSeq;
  CompositionOptimizerParams compositionParams;
};

VF::LuaExports::Result<StartInputs> decodeStartInputs(
    const char* encoded_initial_lines,
    size_t encoded_initial_lines_len,
    int start_row,
    int start_col,
    const char* encoded_goal_lines,
    size_t encoded_goal_lines_len,
    int end_row,
    int end_col,
    int boundary_first_col,
    int boundary_last_col,
    bool has_lines_above,
    bool has_lines_below,
    int window_height,
    int scroll_amount,
    const char* user_seq,
    size_t user_seq_len,
    const char* optimizer_overrides,
    size_t optimizer_overrides_len) {
  auto initialTextRes = helpers::requiredBytes(
      encoded_initial_lines,
      encoded_initial_lines_len,
      "encoded_initial_lines");
  if (!initialTextRes) return unexpected(initialTextRes.error());
  auto goalTextRes = helpers::requiredBytes(
      encoded_goal_lines,
      encoded_goal_lines_len,
      "encoded_goal_lines");
  if (!goalTextRes) return unexpected(goalTextRes.error());

  auto initialLinesRes = payload::decodeLineArray(*initialTextRes);
  if (!initialLinesRes) return unexpected(initialLinesRes.error());
  auto goalLinesRes = payload::decodeLineArray(*goalTextRes);
  if (!goalLinesRes) return unexpected(goalLinesRes.error());

  auto userSeqRes = helpers::requiredBytes(user_seq, user_seq_len, "user_seq");
  if (!userSeqRes) return unexpected(userSeqRes.error());
  auto overridesText = helpers::requiredBytes(
      optimizer_overrides,
      optimizer_overrides_len,
      "optimizer_overrides");
  if (!overridesText) return unexpected(overridesText.error());
  auto overridesResult = helpers::parseOverrides(*overridesText);
  if (!overridesResult) return unexpected(overridesResult.error());
  const OptimizerParamOverrides overrides = std::move(*overridesResult);

  StartInputs in;
  in.initialLines = std::move(*initialLinesRes);
  in.goalLines = std::move(*goalLinesRes);
  in.initialPos = CursorPos(start_row, start_col);
  in.goalPos = CursorPos(end_row, end_col);
  in.boundary = helpers::sliceBoundary(
      in.initialLines, boundary_first_col, boundary_last_col,
      has_lines_above, has_lines_below);
  in.navContext = NavContext(window_height, scroll_amount);
  in.userSeq.assign(*userSeqRes);
  in.compositionParams =
      OptimizerParamOverrides::resolved<CompositionOptimizerParams>(&overrides);
  return in;
}

// Background composition search. The job owns its inputs and config so the
// worker is independent of the FFI buffers and the main thread. The View is
// constructed from the precomputed plan only once `vf_explore_poll` observes
// `ready()`.
struct ExploreState {
  StartInputs inputs;
  Config config;
  CompositionTraceResult trace;
};
using ExploreJob = AsyncJob<ExploreState>;

JobRegistry<ExploreJob> g_jobs;

VF::LuaExports::Result<string> startAsyncImpl(
    const char* encoded_initial_lines,
    size_t encoded_initial_lines_len,
    int start_row,
    int start_col,
    const char* encoded_goal_lines,
    size_t encoded_goal_lines_len,
    int end_row,
    int end_col,
    int boundary_first_col,
    int boundary_last_col,
    bool has_lines_above,
    bool has_lines_below,
    int window_height,
    int scroll_amount,
    const char* user_seq,
    size_t user_seq_len,
    const char* optimizer_overrides,
    size_t optimizer_overrides_len,
    int deadline_ms) {
  auto inputsRes = decodeStartInputs(
      encoded_initial_lines, encoded_initial_lines_len, start_row, start_col,
      encoded_goal_lines, encoded_goal_lines_len, end_row, end_col,
      boundary_first_col, boundary_last_col, has_lines_above, has_lines_below,
      window_height, scroll_amount, user_seq, user_seq_len,
      optimizer_overrides, optimizer_overrides_len);
  if (!inputsRes) return unexpected(inputsRes.error());

  auto job = std::make_unique<ExploreJob>(
      ExploreState{std::move(*inputsRes), VF::LuaExports::g_config_internal, {}}, deadline_ms,
      [](ExploreState& s, const SearchControl* control) {
        s.trace = View::computeTrace(
            s.inputs.initialLines, s.inputs.initialPos,
            s.inputs.goalLines, s.inputs.goalPos,
            s.inputs.compositionParams, s.inputs.userSeq,
            s.inputs.boundary, s.inputs.navContext, s.config, control);
      });
  const int job_id = g_jobs.create(std::move(job));
  return to_string(job_id);
}

}  // namespace

extern "C" {

VFByteSlice vf_explore_start_async(
    const char* encoded_initial_lines,
    size_t encoded_initial_lines_len,
    int start_row,
    int start_col,
    const char* encoded_goal_lines,
    size_t encoded_goal_lines_len,
    int end_row,
    int end_col,
    int boundary_first_col,
    int boundary_last_col,
    bool has_lines_above,
    bool has_lines_below,
    int window_height,
    int scroll_amount,
    const char* user_seq,
    size_t user_seq_len,
    const char* optimizer_overrides,
    size_t optimizer_overrides_len,
    int deadline_ms) {
  static string storage;
  return helpers::storeBytes(storage, startAsyncImpl(
      encoded_initial_lines, encoded_initial_lines_len,
      start_row, start_col,
      encoded_goal_lines, encoded_goal_lines_len,
      end_row, end_col,
      boundary_first_col, boundary_last_col,
      has_lines_above, has_lines_below,
      window_height, scroll_amount,
      user_seq, user_seq_len,
      optimizer_overrides, optimizer_overrides_len,
      deadline_ms));
}

// Returns "pending" while the worker runs; once done, builds the View from the
// precomputed plan, registers it, frees the job, and returns the view_id.
VFByteSlice vf_explore_poll(int job_id) {
  static string storage;
  ExploreJob& job = g_jobs.get(job_id);
  if (!job.ready()) {
    storage = "pending";
    return helpers::byteSlice(storage);
  }
  std::unique_ptr<ExploreJob> owned = g_jobs.take(job_id);
  ExploreState& s = owned->state();
  StartInputs& in = s.inputs;
  const int view_id = g_registry.create(
      std::move(in.initialLines),
      in.initialPos,
      std::move(in.goalLines),
      in.goalPos,
      std::move(in.boundary),
      in.navContext,
      std::move(s.config),
      in.userSeq,
      in.compositionParams,
      std::move(s.trace));
  storage = to_string(view_id);
  return helpers::byteSlice(storage);
}

// Signals cancellation and frees the job; dropping it joins the worker on this
// (main) thread before any job state is torn down (see AsyncJob). The flag is
// polled between composition setup phases and per pop in every search loop,
// so the join is bounded by one diff's precompute or one pop expansion.
int vf_explore_cancel(int job_id) {
  std::unique_ptr<ExploreJob> owned = g_jobs.take(job_id);
  if (!owned) return 0;
  owned->cancel();
  return 1;
}

int vf_explore_destroy(int view_id) {
  return g_registry.destroy(view_id) ? 1 : 0;
}

VFByteSlice vf_explore_state(int view_id) {
  static string storage;
  storage = encodeState(g_registry.get(view_id));
  return helpers::byteSlice(storage);
}

VFByteSlice vf_explore_recommendations(
    int view_id,
    int max_count,
    const char* optimizer_overrides,
    size_t optimizer_overrides_len,
    const char* sort_mode,
    size_t sort_mode_len) {
  CHECK(max_count >= 0, "max_count must be non-negative");
  static string storage;
  View& v = g_registry.get(view_id);
  auto overridesText = helpers::requiredBytes(
      optimizer_overrides,
      optimizer_overrides_len,
      "optimizer_overrides");
  if (!overridesText) return helpers::storeBytes(storage, unexpected(overridesText.error()));
  auto sortModeText = helpers::requiredBytes(sort_mode, sort_mode_len, "sort_mode");
  if (!sortModeText) return helpers::storeBytes(storage, unexpected(sortModeText.error()));

  auto overridesResult = helpers::parseOverrides(*overridesText);
  if (!overridesResult) {
    return helpers::storeBytes(storage, unexpected(overridesResult.error()));
  }
  const OptimizerParamOverrides overrides = std::move(*overridesResult);
  const SuggestionSortMode mode = parseSuggestionSortMode(*sortModeText);
  storage = encodeSuggestions(v.recommendations(max_count, &overrides, mode));
  return helpers::byteSlice(storage);
}

VFByteSlice vf_explore_reconfigure_plan(
    int view_id,
    const char* optimizer_overrides,
    size_t optimizer_overrides_len) {
  static string storage;
  View& v = g_registry.get(view_id);
  auto overridesText = helpers::requiredBytes(
      optimizer_overrides,
      optimizer_overrides_len,
      "optimizer_overrides");
  if (!overridesText) return helpers::storeBytes(storage, unexpected(overridesText.error()));

  auto overridesResult = helpers::parseOverrides(*overridesText);
  if (!overridesResult) {
    return helpers::storeBytes(storage, unexpected(overridesResult.error()));
  }
  const OptimizerParamOverrides overrides = std::move(*overridesResult);
  CompositionOptimizerParams compositionParams =
      OptimizerParamOverrides::resolved<CompositionOptimizerParams>(&overrides);
  storage = v.reconfigurePlan(std::move(compositionParams)) ? "1" : "0";
  return helpers::byteSlice(storage);
}

VFByteSlice vf_explore_current_lines(int view_id) {
  static string storage;
  storage = encodeStringList(g_registry.get(view_id).state().lines);
  return helpers::byteSlice(storage);
}

VFByteSlice vf_explore_accept_snapshot(
    int view_id,
    const char* encoded_lines,
    size_t encoded_lines_len,
    int new_row,
    int new_col,
    const char* raw_keys,
    size_t raw_keys_len,
    bool insert_mode) {
  static string storage;
  View& v = g_registry.get(view_id);
  auto rawKeysResult = helpers::requiredBytes(raw_keys, raw_keys_len, "raw_keys");
  if (!rawKeysResult) return helpers::storeBytes(storage, unexpected(rawKeysResult.error()));
  return helpers::storeBytes(storage, helpers::requiredBytes(
          encoded_lines,
          encoded_lines_len,
          "encoded_lines")
      .and_then(payload::decodeLineArray)
      .transform([&](const Lines& liveLines) {
        return encodeOutcome(
            v.acceptSnapshot(liveLines, CursorPos(new_row, new_col),
                             *rawKeysResult, insert_mode));
      }));
}

VFByteSlice vf_explore_undo(int view_id) {
  static string storage;
  View& v = g_registry.get(view_id);
  storage = encodeOutcome(v.undo());
  return helpers::byteSlice(storage);
}

VFByteSlice vf_explore_redo(int view_id) {
  static string storage;
  View& v = g_registry.get(view_id);
  storage = encodeOutcome(v.redo());
  return helpers::byteSlice(storage);
}

VFByteSlice vf_explore_header_rows(int view_id) {
  static string storage;
  View& v = g_registry.get(view_id);
  storage = encodeHeaderRows(v);
  return helpers::byteSlice(storage);
}

}  // extern "C"
