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

// The heavy half: the Nav-vs-Composition branch plus result-string formatting.
// Runs on the calling thread for `vf_analyze` and on a worker for the async
// trio. `control` bounds whichever search runs.
string runAnalyze(
    const SearchInputs& in,
    int resultsCalculated,
    const Config& config,
    const SearchControl* control) {
  vector<::Result> results;
  vector<DiffState> diffs;

  if (in.initialLines == in.goalLines) {
    // NavOptimizer returns LandingResult (with goalPos); for analyze we only
    // surface (sequence, cost), so down-convert to Result for shared output.
    NavOptimizer opt(config);
    NavOptimizerParams navParams = NavOptimizerParams{}
        .withMaxResults(resultsCalculated)
        .withMaxResultsPerEndPos(2);
    in.overrides.applyTo(navParams);
    auto navResults = opt.optimize(
        in.initialLines,
        in.initialPos,
        in.goalPos,
        navParams,
        in.userSeq,
        in.boundary,
        in.navContext,
        control).getResults();
    results.reserve(navResults.size());
    for (const auto& r : navResults) {
      results.emplace_back(r.getSequence(), r.getCost());
    }
  } else {
    CompositionOptimizer opt(config);
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
  vector<const ::Result*> validResults;
  for (const ::Result& result : results) {
    if (result.getSequence().empty()) continue;
    validResults.push_back(&result);
  }

  sort(validResults.begin(), validResults.end(),
            [](const ::Result* a, const ::Result* b) { return a->getCost() < b->getCost(); });

  // Count-framed sections; the Lua parser reads exactly `size` result lines
  // and `diffs` region lines, so neither body can be mistaken for a header.
  // Layout is documented in `dev/lua/ffi-separators.md` § Convention 3.
  ostringstream oss;
  oss << "size: " << validResults.size() << " user_cost: "
      << fixed << setprecision(3) << userCost << "\n";
  for (const ::Result* result : validResults) {
    // Machine-readable export for the Lua bridge: use the raw sequence
    // bytes, not Sequence's human formatter, so insert-mode text and
    // <Esc> survive round-tripping through ffi.lua's line parser.
    // Field separator is the shared `EVENT_FIELD_SEP` (0x1F Unit Sep).
    oss << result->getSequence().view()
        << EVENT_FIELD_SEP
        << fixed << setprecision(3) << result->getCost() << "\n";
  }
  // The planner's own regions, so views highlight the partition the search
  // actually used (empty on the pure-navigation branch).
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

VF::LuaExports::Result<string> analyzeImpl(const VFSearchRequest* request, int results_calculated) {
  CHECK(results_calculated >= 0, "results_calculated must be non-negative");
  auto inputsRes = logic::decodeSearchRequest(request);
  if (!inputsRes) return unexpected(inputsRes.error());
  return runAnalyze(*inputsRes, results_calculated, g_config_internal, nullptr);
}

VF::LuaExports::Result<string> startAsyncImpl(
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
  return storeBytes(storage, analyzeImpl(request, results_calculated));
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
