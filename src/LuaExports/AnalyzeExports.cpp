#include "LuaExports/Api.h"
#include "Utils/CheckMacro.h"

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

// Synchronous run of the full analyze entry point. Packs a
// `VF.Optimizer.AnalyzeResult` (lua/vimficiency/ffi/optimizer.lua).
Packed runAnalyze(
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

  MsgpackWriter w;
  w.map(4);
  w.key("results").array(results.size());
  for (const Result& result : results) {
    w.map(2)
        .key("seq").str(result.getSequence().view())
        .key("cost").number(result.getCost());
  }
  w.key("user_cost").number(userCost);
  w.key("diffs");
  payload::writeDiffRegions(w, diffs);
  w.key("dbg").str(debug);
  return w.take();
}

// Background analyze search. The job owns its inputs and a Config snapshot so
// the worker is independent of the FFI buffers and the main thread. The result
// is read once `vf_analyze_poll` observes `ready()`.
struct AnalyzeState {
  SearchInputs inputs;
  int resultsCalculated;
  Config config;
  Packed result;
};
using AnalyzeJob = AsyncJob<AnalyzeState>;

JobRegistry<AnalyzeJob> g_analyze_jobs;

PackedResult startAsyncImpl(
    const VFSearchRequest* request, int results_calculated, int deadline_ms) {
  CHECK(results_calculated >= 0, "results_calculated must be non-negative");
  auto inputsRes = logic::decodeSearchRequest(request);
  if (!inputsRes) return unexpected(inputsRes.error());

  auto job = std::make_unique<AnalyzeJob>(
      AnalyzeState{std::move(*inputsRes), results_calculated, g_config_internal, {}},
      deadline_ms,
      [](AnalyzeState& s, const SearchControl& control) {
        s.result = runAnalyze(s.inputs, s.resultsCalculated, s.config, control);
      });
  const int job_id = g_analyze_jobs.create(std::move(job));
  return packInteger(job_id);
}

}  // namespace

extern "C" {

VFByteSlice vf_analyze(const VFSearchRequest* request, int results_calculated) {
  static string storage;
  return guarded(storage, [&]() -> PackedResult {
    CHECK(results_calculated >= 0, "results_calculated must be non-negative");
    auto inputsRes = logic::decodeSearchRequest(request);
    if (!inputsRes) return unexpected(inputsRes.error());
    return runAnalyze(*inputsRes, results_calculated, g_config_internal, SearchControl::NEVER_STOP);
  });
}

// Kicks off the analyze search on a worker thread and returns a job id
// immediately. Poll with vf_analyze_poll until it stops returning pending.
VFByteSlice vf_analyze_start_async(
    const VFSearchRequest* request, int results_calculated, int deadline_ms) {
  static string storage;
  return guarded(storage, [&] {
    return startAsyncImpl(request, results_calculated, deadline_ms);
  });
}

// Returns pending while the worker runs; once done, frees the job and returns
// the same result vf_analyze would produce, or the worker's failure.
VFByteSlice vf_analyze_poll(int job_id) {
  static string storage;
  if (!g_analyze_jobs.get(job_id).ready()) return storePending(storage);
  return guarded(storage, [&]() -> PackedResult {
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
