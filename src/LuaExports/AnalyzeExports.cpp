#include "LuaExports/Api.h"
#include "Utils/CheckMacro.h"
#include "proto/common.pb.h"
#include "proto/search.pb.h"

import std.compat;
import vf.LuaExports.Common;
import vf.LuaExports.Logic;
import vf.LuaExports.Payload;
import vf.LuaExports.AsyncJob;
import vf.LuaExports.AsyncJobRegistry;
import vf.Keyboard.Config;
import vf.Optimizer.CompositionOptimizer.CompositionOptimizer;
import vf.Optimizer.DiffPlanner.DiffState;
import vf.Optimizer.NavOptimizer.NavOptimizer;
import vf.Optimizer.SearchControl;
import vf.Utils.Debug;

using namespace std;
using namespace VF::LuaExports;
using logic::SearchInputs;

namespace {

void setSpan(vf::search::Span* out, const CursorPos& begin, const CursorPos& end) {
  out->set_begin_row(begin.line);
  out->set_begin_col(begin.col);
  out->set_end_row(end.line);
  out->set_end_col(end.col);
}

// Synchronous run of the full analyze entry point.
Proto runAnalyze(
    const SearchInputs& in,
    int resultsCalculated,
    const Config& config,
    const SearchControl& control) {
  vector<Result> results;
  vector<DiffState> diffs;

  if (in.initialLines == in.goalLines) {
    NavOptimizer opt(config);
    const NavOptimizerParams navParams = in.overrides.resolved(
        NavOptimizerParams{}.withMaxResults(resultsCalculated).withMaxResultsPerEndPos(resultsCalculated));

    const vector<LandingResult> navResults = opt.optimize(
        in.initialLines,
        in.initialPos,
        in.goalPos,
        navParams,
        in.userSeq,
        in.boundary,
        in.navContext,
        control).getResults();
    // NavOptimizer returns LandingResult (with goalPos); for analyze we only
    // surface (sequence, cost), so down-convert to Result for shared output.
    results.reserve(navResults.size());
    for (const LandingResult& r : navResults) {
      results.emplace_back(r.getSequence(), r.getCost());
    }
  } else {
    CompositionOptimizer opt(config);

    const CompositionOptimizerParams compParams = in.overrides.resolved(
        CompositionOptimizerParams{}.withMaxResults(resultsCalculated));

    const CompositionResult composed = opt.optimize(
        in.initialLines,
        in.initialPos,
        in.goalLines,
        in.goalPos,
        compParams,
        in.userSeq,
        in.boundary,
        in.navContext,
        control);
    results = composed.getResults();
    diffs = composed.getDiffs();
  }

  const double userCost = getEffort(in.userSeq, config);

  // Erase empty sequences so that "no action" can be recognized by lua to mean "no recommendations"
  erase_if(results, [](const Result& r) { return r.getSequence().empty(); });

  // Sort results by cost
  sort(results.begin(), results.end(),
       [](const Result& a, const Result& b) { return a.getCost() < b.getCost(); });

  // The worker thread's debug stream is thread-local, so it has to travel in
  // the result.
  string debug;
  if constexpr (DEBUG_ENABLED) {
    debug = consume_debug_output();
  }

  vf::search::AnalyzeResult msg;
  for (const Result& result : results) {
    vf::search::Result* out = msg.add_results();
    out->set_seq(result.getSequence().view());
    out->set_cost(result.getCost());
  }
  msg.set_user_cost(userCost);
  // Both spans share begin (the common prefix is identical in both buffers).
  for (const DiffState& d : diffs) {
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

}  // namespace

extern "C" {

VFByteSlice vf_analyze(const char* request, size_t request_len) {
  static string storage;
  return guardedRequest<vf::search::AnalyzeRequest>(
      storage, request, request_len, [](const auto& req) -> ProtoResult {
        CHECK(req.results_calculated() >= 0, "results_calculated must be non-negative");
        auto inputs = logic::decodeSearchRequest(req.search());
        if (!inputs) return unexpected(inputs.error());
        return runAnalyze(
            *inputs, req.results_calculated(), g_config_internal, SearchControl::NEVER_STOP);
      });
}

// Kicks off the analyze search on a worker thread and returns a job id
// immediately. Poll with vf_analyze_poll until it stops returning pending.
VFByteSlice vf_analyze_start_async(const char* request, size_t request_len) {
  static string storage;
  return guardedRequest<vf::search::AnalyzeRequest>(
      storage, request, request_len, [](const auto& req) -> ProtoResult {
        CHECK(req.results_calculated() >= 0, "results_calculated must be non-negative");
        auto inputs = logic::decodeSearchRequest(req.search());
        if (!inputs) return unexpected(inputs.error());

        auto job = std::make_unique<AnalyzeJob>(
            AnalyzeState{std::move(*inputs), req.results_calculated(), g_config_internal, {}},
            req.deadline_ms(),
            [](AnalyzeState& s, const SearchControl& control) {
              s.result = runAnalyze(s.inputs, s.resultsCalculated, s.config, control);
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
  if (!g_analyze_jobs.get(job_id).ready()) return storePending(storage);
  return guarded(storage, [&]() -> ProtoResult {
    std::unique_ptr<AnalyzeJob> owned = g_analyze_jobs.take(job_id);
    if (!owned->error().empty()) return unexpectedError(owned->error());
    return std::move(owned->state().result);
  });
}

// Signals cancellation and frees the job; dropping it joins the worker on this
// (main) thread before any job state is torn down (see AsyncJob). The flag is
// polled between composition setup phases and per pop in every search loop,
// so the join is bounded by one diff's precompute or one pop expansion.
int vf_analyze_cancel(int job_id) {
  return noUnwind([&] {
    std::unique_ptr<AnalyzeJob> owned = g_analyze_jobs.take(job_id);
    if (!owned) return 0;
    owned->cancel();
    return 1;
  });
}

}

