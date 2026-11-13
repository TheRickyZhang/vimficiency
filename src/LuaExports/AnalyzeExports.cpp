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

// Syncronous run of full analyze entry point
string runAnalyze(
    const SearchInputs& in,
    int resultsCalculated,
    const Config& config,
    const SearchControl* control) {
  vector<Result> results;
  vector<DiffState> diffs;

  if (in.initialLines == in.goalLines) {
    NavOptimizer opt(config);
    // Resolve nav params from default + overrides
    NavOptimizerParams navParams = NavOptimizerParams{}
        .withMaxResults(resultsCalculated)
        .withMaxResultsPerEndPos(2);
    in.overrides.applyTo(navParams);

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

    // Resolve composition params from default + overrides
    CompositionOptimizerParams compParams =
        CompositionOptimizerParams{}.withMaxResults(resultsCalculated);
    in.overrides.applyTo(compParams);

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

  // Necessary to print header counts; review dev/lua/ffi-separators.md § Convention 3.
  ostringstream oss;
  oss << fixed << setprecision(3);
  oss << "size: " << results.size() << " user_cost: " << userCost << "\n";

  // Raw bytes so Lua can parse the sequence back.
  for (const Result& result : results) {
    oss << result.getSequence().view() << EVENT_FIELD_SEP << result.getCost() << "\n";
  }

  // Partition used (for edits)
  oss << "diffs: " << diffs.size() << "\n" << payload::encodeDiffRegions(diffs);

  if constexpr (DEBUG_ENABLED) {
    oss << "\n ----------------DEBUG---------------- \n" << consume_debug_output();
  }
  return oss.str();
}

// Background analyze search. The job owns its inputs and a Config snapshot so
// the worker is independent of the FFI buffers and the main thread. The result
// string is read once `vf_analyze_poll` observes `ready()`.
struct AnalyzeState {
  SearchInputs inputs;
  int resultsCalculated;
  Config config;
  string result;
};
using AnalyzeJob = AsyncJob<AnalyzeState>;

JobRegistry<AnalyzeJob> g_analyze_jobs;

VF::LuaExports::Expected<string> startAsyncImpl(
    const VFSearchRequest* request, int results_calculated, int deadline_ms) {
  CHECK(results_calculated >= 0, "results_calculated must be non-negative");
  auto inputsRes = logic::decodeSearchRequest(request);
  if (!inputsRes) return unexpected(inputsRes.error());

  auto job = std::make_unique<AnalyzeJob>(
      AnalyzeState{std::move(*inputsRes), results_calculated, g_config_internal, {}},
      deadline_ms,
      [](AnalyzeState& s, const SearchControl* control) {
        s.result = runAnalyze(s.inputs, s.resultsCalculated, s.config, control);
      });
  const int job_id = g_analyze_jobs.create(std::move(job));
  return to_string(job_id);
}

}  // namespace

extern "C" {

VFByteSlice vf_analyze(const VFSearchRequest* request, int results_calculated) {
  static string storage;
  CHECK(results_calculated >= 0, "results_calculated must be non-negative");
  auto inputsRes = logic::decodeSearchRequest(request);
  if (!inputsRes) return storeBytes(storage, unexpected(inputsRes.error()));
  return storeBytes(storage, runAnalyze(*inputsRes, results_calculated, g_config_internal, nullptr));
}

// Kicks off the analyze search on a worker thread and returns a job-id string
// immediately. Poll with vf_analyze_poll until it stops returning "pending".
VFByteSlice vf_analyze_start_async(
    const VFSearchRequest* request, int results_calculated, int deadline_ms) {
  static string storage;
  return storeBytes(storage, startAsyncImpl(request, results_calculated, deadline_ms));
}

// Returns "pending" while the worker runs; once done, returns the same result
// string vf_analyze would produce and frees the job.
VFByteSlice vf_analyze_poll(int job_id) {
  static string storage;
  AnalyzeJob& job = g_analyze_jobs.get(job_id);
  if (!job.ready()) {
    storage = "pending";
    return byteSlice(storage);
  }
  std::unique_ptr<AnalyzeJob> owned = g_analyze_jobs.take(job_id);
  storage = std::move(owned->state().result);
  return byteSlice(storage);
}

// Signals cancellation and frees the job; dropping it joins the worker on this
// (main) thread before any job state is torn down (see AsyncJob). The flag is
// polled between composition setup phases and per pop in every search loop,
// so the join is bounded by one diff's precompute or one pop expansion.
int vf_analyze_cancel(int job_id) {
  std::unique_ptr<AnalyzeJob> owned = g_analyze_jobs.take(job_id);
  if (!owned) return 0;
  owned->cancel();
  return 1;
}

}
