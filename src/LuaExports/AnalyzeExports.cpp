#include "LuaExports/Api.h"
#include "proto/common.pb.h"
#include "proto/search.pb.h"

import std.compat;
import vf.LuaExports.Common;
import vf.LuaExports.Logic;
import vf.LuaExports.Payload;
import vf.LuaExports.AsyncJob;
import vf.LuaExports.AsyncJobRegistry;
import vf.Keyboard.Config;
import vf.Optimizer.Analyze;
import vf.Optimizer.DiffPlanner.DiffState;
import vf.Optimizer.Result;
import vf.Optimizer.SearchControl;
import vf.Types.Lines;
import vf.Utils.Debug;

using namespace std;
using namespace VF::LuaExports;

namespace {

void setSpan(vf::search::Span* out, const CursorPos& begin,
             const CursorPos& end) {
  out->set_begin_row(begin.line);
  out->set_begin_col(begin.col);
  out->set_end_row(end.line);
  out->set_end_col(end.col);
}

// Synchronous run of the full analyze entry point.
Proto runAnalyze(const SearchInputs& in, int resultsCalculated,
                 const Config& config, const SearchControl& control) {
  const AnalyzeOutput analyzed = analyze(in, resultsCalculated, config, control);

  // The worker thread's debug stream is thread-local, so it has to travel in
  // the result.
  string debug;
  if constexpr (DEBUG_ENABLED) {
    debug = consume_debug_output();
  }

  vf::search::AnalyzeResult msg;
  for (const Result& result : analyzed.results) {
    vf::search::Result* out = msg.add_results();
    out->set_seq(result.getSequence().view());
    out->set_cost(result.getCost());
  }
  msg.set_user_cost(analyzed.userCost);
  msg.set_unbeaten(analyzed.unbeaten);
  // Both spans share begin (the common prefix is identical in both buffers).
  for (const DiffState& d : analyzed.diffs) {
    vf::search::DiffRegion* region = msg.add_diffs();
    setSpan(region->mutable_init(), d.beginPos, d.endPos);
    setSpan(region->mutable_goal(), d.beginPos,
            DiffText::advancePositionByText(d.beginPos, d.insertedText));
  }
  msg.set_dbg(debug);
  return proto(msg);
}

// Background analyze search. The job owns its inputs and a Config snapshot so
// the worker is independent of the request bytes and the main thread. The
// result is read once `vf_analyze_poll` observes `ready()`.
struct AnalyzeState {
  SearchInputs inputs;
  int resultsCalculated;
  Config config;
  Proto result;
};
using AnalyzeJob = AsyncJob<AnalyzeState>;

JobRegistry<AnalyzeJob> g_analyze_jobs;

} // namespace

extern "C" {

VFByteSlice vf_build_search_request(const char* request, size_t request_len) {
  static string storage;
  return guardedRequest<vf::search::BuildSearchRequest>(
      storage, request, request_len,
      [](const vf::search::BuildSearchRequest& req) -> ProtoResult {
        auto initialLines = payload::linesFrom(req.initial_lines());
        if (!initialLines) return unexpected(initialLines.error());
        auto goalLines = payload::linesFrom(req.goal_lines());
        if (!goalLines) return unexpected(goalLines.error());

        auto indent = logic::decodeIndentOptions(req);
        if (!indent) return unexpected(indent.error());
        UntouchedRows untouched;
        if (req.has_untouched_above()) untouched.above = req.untouched_above();
        if (req.has_untouched_below()) untouched.below = req.untouched_below();
        if (untouched.above < 0 || untouched.below < 0) return unexpectedError("negative untouched rows");
        const SearchRegion region = computeSearchRegion(
            req.start_row(), req.end_row(), *initialLines, *goalLines, req.padding(), untouched);
        if (region.paddedHeight() - 1 > req.max_search_lines())
          return unexpectedError("search range exceeds MAX_SEARCH_LINES");

        const SearchSlice slice = sliceSearch(
            *initialLines, *goalLines,
            SessionEnds{
                .startRow = req.start_row(),
                .startCol = req.start_col(),
                .endRow = req.end_row(),
                .endCol = req.end_col(),
                .windowHeight = req.window_height(),
                .scrollAmount = req.scroll_amount(),
                .indent = *indent,
            },
            req.user_seq(), req.user_typed(), region);

        vf::search::SearchRequest msg;
        for (const Line& line : slice.initialLines) msg.add_initial_lines(line);
        for (const Line& line : slice.goalLines) msg.add_goal_lines(line);
        msg.set_start_row(slice.ends.startRow);
        msg.set_start_col(slice.ends.startCol);
        msg.set_end_row(slice.ends.endRow);
        msg.set_end_col(slice.ends.endCol);
        msg.set_has_lines_above(slice.hasLinesAbove);
        msg.set_has_lines_below(slice.hasLinesBelow);
        msg.set_window_height(slice.ends.windowHeight);
        msg.set_scroll_amount(slice.ends.scrollAmount);
        if (req.has_indent()) *msg.mutable_indent() = req.indent();
        msg.set_user_seq(slice.userSeq);
        msg.set_user_typed(slice.userTyped);
        return proto(msg);
      });
}

VFByteSlice vf_analyze(const char* request, size_t request_len) {
  static string storage;

  return guardedRequest<vf::search::AnalyzeRequest>(
    storage, request, request_len,
    [](const vf::search::AnalyzeRequest& req) -> ProtoResult {
      if (req.results_calculated() < 0)
        return unexpectedError("results_calculated must be non-negative");

      auto inputs = logic::decodeSearchRequest(req.search());
      if (!inputs)
        return unexpected(inputs.error());

      return runAnalyze(*inputs, req.results_calculated(), g_config_internal,
                        SearchControl::NEVER_STOP);
    }
  );
}

// Kicks off the analyze search on a worker thread and returns a job id
// immediately. Poll with vf_analyze_poll until it stops returning pending.
VFByteSlice vf_analyze_start_async(const char* request, size_t request_len) {
  static string storage;
  return guardedRequest<vf::search::AnalyzeRequest>(
      storage, request, request_len,
      [](const vf::search::AnalyzeRequest& req) -> ProtoResult {
        if (req.results_calculated() < 0)
          return unexpectedError("results_calculated must be non-negative");
        auto inputs = logic::decodeSearchRequest(req.search());
        if (!inputs)
          return unexpected(inputs.error());

        auto job = std::make_unique<AnalyzeJob>(
            AnalyzeState{std::move(*inputs),
                         req.results_calculated(),
                         g_config_internal,
                         {}},
            req.deadline_ms(),
            [](AnalyzeState& s, const SearchControl& control) {
              s.result =
                  runAnalyze(s.inputs, s.resultsCalculated, s.config, control);
            });
        vf::JobHandle handle;
        handle.set_job_id(g_analyze_jobs.create(std::move(job)));
        return proto(handle);
      });
}

// Returns pending while the worker runs; once done, frees the job and returns
// the same result vf_analyze would produce, or the worker's failure.
VFByteSlice vf_analyze_poll(int job_id) {
  static string storage;
  return guardedPoll(storage, g_analyze_jobs, job_id,
                     [](AnalyzeState& s) { return std::move(s.result); });
}

int vf_analyze_cancel(int job_id) {
  return noUnwind([&] { return g_analyze_jobs.cancel(job_id) ? 1 : 0; });
}
}
