module;
#include <cassert>
#include "Utils/CheckMacro.h"

module vf.Optimizer.CompositionOptimizer.CompositionOptimizer;

import std.compat;
import vf.Optimizer.CompositionOptimizer.CompositionMotionSearch;
import vf.Optimizer.CompositionOptimizer.CompositionSearchContext;
import vf.Optimizer.CompositionOptimizer.CompositionNavParams;
import vf.Optimizer.CompositionOptimizer.CompositionStrategies;
import vf.Optimizer.CompositionOptimizer.EditSequenceSpan;
import vf.Optimizer.DiffPlanner.MyersDiff;
import vf.Optimizer.SearchControl;
import vf.Optimizer.SearchFrontier;
import vf.Optimizer.NavOptimizer.NavOptimizer;
import vf.Optimizer.NavOptimizer.NavRangeConversion;
import vf.Effort.RunningEffort;
import vf.Optimizer.CompositionOptimizer.CompositionState;
import vf.Utils.Debug;
import vf.Utils.PrettyText;

using namespace std;

namespace {

#if defined(__GNUC__) || defined(__clang__)
#define VF_NO_UNIQUE_ADDRESS [[no_unique_address]]
#else
#define VF_NO_UNIQUE_ADDRESS
#endif

// Edit tracers: optionally record one byte span per planned edit so Explore
// can segment Optimal-N rows by plan. `optimize()` uses NoTrace (zero
// per-state cost via [[no_unique_address]]); `optimizeWithEditSpans()` uses
// EditSpanTrace (one uint32 per state, one arena node per edit transition).
//
// Edit transitions carry the half-open byte span they added to the flat
// sequence. `optimizeImpl` reaches the tracer only through the `EditTracer`
// methods, except goal finalize, which checks `collecting` before pushing
// `resultSpans`.

struct EditTraceNode {
  uint32_t previous = 0;     // 0 = root sentinel (never a real node)
  uint8_t editIndex = 0;
  uint32_t beginByte = 0;
  uint32_t endByte = 0;
};

struct NoTrace {
  struct State {};
  static constexpr bool collecting = false;

  State onSearchStart() const { return {}; }
  State onEditTransition(const State&, int /*editIndex*/,
                         uint32_t /*begin*/, uint32_t /*end*/) const {
    return {};
  }
  State onMotionTransition(const State&) const { return {}; }
  vector<EditSequenceSpan> onGoalReached(const State&,
                                         int /*totalEdits*/) const {
    return {};
  }
};

struct EditSpanTrace {
  struct State { uint32_t node = 0; };
  static constexpr bool collecting = true;

  // Index 0 is the root sentinel; real nodes start at 1.
  vector<EditTraceNode> arena{EditTraceNode{}};

  State onSearchStart() const { return {}; }
  State onMotionTransition(const State& parent) const { return parent; }

  State onEditTransition(const State& parent, int editIndex,
                         uint32_t beginByte, uint32_t endByte) {
    assert(parent.node < arena.size() && "trace parent out of range");
    assert(editIndex >= 0 && editIndex < 256 && "editIndex must fit in uint8_t");
    arena.push_back(EditTraceNode{
        .previous = parent.node,
        .editIndex = static_cast<uint8_t>(editIndex),
        .beginByte = beginByte,
        .endByte = endByte,
    });
    return State{static_cast<uint32_t>(arena.size() - 1)};
  }

  vector<EditSequenceSpan> onGoalReached(const State& s, int totalEdits) const {
    vector<EditSequenceSpan> spans(totalEdits);
    vector<bool> seen(totalEdits, false);
    int count = 0;
    uint32_t node = s.node;
    while (node != 0) {
      assert(node < arena.size() && "trace chain out of range");
      const EditTraceNode& n = arena[node];
      assert(n.editIndex < totalEdits && "trace editIndex past totalEdits");
      assert(!seen[n.editIndex] && "trace recorded duplicate editIndex");
      assert(n.beginByte <= n.endByte && "trace span byte range is inverted");
      seen[n.editIndex] = true;
      spans[n.editIndex] = EditSequenceSpan{n.beginByte, n.endByte};
      ++count;
      node = n.previous;
    }
    assert(count == totalEdits && "trace chain must include exactly one node per edit");
    return spans;
  }
};

template<class T>
concept EditTracer = requires(
    T t, const T ct, const typename T::State s,
    int editIndex, uint32_t begin, uint32_t end, int totalEdits) {
  typename T::State;
  { T::collecting } -> std::convertible_to<bool>;
  { ct.onSearchStart() } -> std::same_as<typename T::State>;
  { t.onEditTransition(s, editIndex, begin, end) } -> std::same_as<typename T::State>;
  { ct.onMotionTransition(s) } -> std::same_as<typename T::State>;
  { ct.onGoalReached(s, totalEdits) } -> std::same_as<vector<EditSequenceSpan>>;
};

template <EditTracer Trace>
struct QueueEntry {
  CompositionState state;
  VF_NO_UNIQUE_ADDRESS typename Trace::State trace;

  // Min-heap by composition cost; trace state is invisible to ordering.
  bool operator>(const QueueEntry& other) const {
    return state > other.state;
  }
};

// Returns the results plus a parallel vector of per-result edit spans, which
// is empty under NoTrace.
template <EditTracer Trace>
static std::pair<vector<Result>, vector<vector<EditSequenceSpan>>>
optimizeImpl(
    const Config& config,
    const Lines& initialLines, const CursorPos initialPos,
    const Lines& goalLines, const CursorPos goalPos,
    const CompositionOptimizerParams& params, std::string_view userSequence,
    const NavBoundary& boundary, const NavContext& navigationContext,
    CompositionSearchContext& ctx,
    Trace& trace) {
  NavOptimizer navOptimizer(config);

  vector<Result> results;
  vector<vector<EditSequenceSpan>> resultSpans;

  using Entry = QueueEntry<Trace>;
  std::priority_queue<Entry, vector<Entry>, std::greater<Entry>> pq;

  CompositionStateFactory states(
      config, [&](const CompositionState& state) { return ctx.heuristic(state); });

  CompositionState startingState = states.initial(initialPos);
  pq.push(Entry{startingState, trace.onSearchStart()});
  ctx.costMap[startingState.getKey()] = startingState.getCost();

  // Terminal states are not cached, so distinct paths to the goal each emit a
  // result.
  auto isTerminal = [&](const CompositionState& s) {
    return s.getEditsCompleted() == ctx.totalEdits() && s.getPos() == ctx.goalPos;
  };
  auto enqueueState = [&](CompositionState&& newState,
                          typename Trace::State traceState) {
    if (newState.getEffort() > ctx.maxEffort) {
      ctx.statesOverEffortCap++;
      return;
    }
    if (Search::admitRangeState(newState, ctx.costMap, ctx.maxEffort, isTerminal)) {
      pq.push(Entry{std::move(newState), traceState});
    }
  };
  // editOffset: bytes of leading motion in editSequence, excluded from the
  // traced edit span.
  auto enqueueEditTransition = [&](const Entry& parent,
                                   const Sequence& editSequence,
                                   double penalty,
                                   const CursorPos& newPos,
                                   int editsAfter,
                                   uint32_t editOffset = 0) {
    const uint32_t base = static_cast<uint32_t>(parent.state.getSequence().size());
    const uint32_t seqLen = static_cast<uint32_t>(editSequence.size());
    assert(editOffset <= seqLen && "editOffset must lie inside transition sequence");
    CompositionState newState = states.afterEditTransition(
        parent.state, editSequence, penalty, newPos);
    const int editIndex = editsAfter - 1;
    typename Trace::State childTrace = trace.onEditTransition(
        parent.trace, editIndex, base + editOffset, base + seqLen);
    enqueueState(std::move(newState), std::move(childTrace));
  };
  auto enqueueMotionTransition = [&](const Entry& parent,
                                     const Sequence& moveSequence,
                                     double penalty,
                                     const CursorPos& newPos) {
    CompositionState newState = states.afterNavResult(
        parent.state, moveSequence, penalty, newPos);
    enqueueState(std::move(newState), trace.onMotionTransition(parent.trace));
  };

  // Padded line subset covering the cursor and target lines, plus the boundary
  // NavOptimizer sees for it.
  auto sliceMotionSubset = [&](CursorPos pos, int targetBeginLine, int targetEndLine,
                               const Lines& fromLines) {
    struct Slice {
      Lines subset;
      int beginLine;
      int endLine;
      CursorPos localPos;
      NavBoundary subsetBoundary;
      BufferIndex bufferIndex;
      int bufferIndexLineOffset;
    };
    auto [beginLine, endLine] = fromLines.minmaxBoundWithPadding(
        min(pos.line, targetBeginLine), max(pos.line + 1, targetEndLine + 1),
        params.navPaddingAbove, params.navPaddingBelow);
    Lines subset = fromLines.getLineRange(beginLine, endLine);
    CursorPos localPos(pos.line - beginLine, pos.col, pos.targetCol);
    NavBoundary subsetBoundary(subset, CursorPos(0, 0), subset.endPos(),
        beginLine > 0 || boundary.hasLinesAbove(),
        endLine <= fromLines.lastLine() || boundary.hasLinesBelow());
    auto bufferIndexWindow = buildCompositionBufferIndexWindow(
        fromLines, beginLine, endLine);
    return Slice{
        std::move(subset),
        beginLine,
        endLine,
        localPos,
        std::move(subsetBoundary),
        std::move(bufferIndexWindow.index),
        bufferIndexWindow.lineOffset,
    };
  };

  // makeLocalInterval(subset, beginLine) gives the target in subset-local
  // coords. keepMultiplePerLanding keeps several sequences per landing instead
  // of the cheapest one, for single-point targets.
  auto exploreMotionsToInterval = [&](
      const Entry& parent, CursorPos pos,
      int targetBeginLine, int targetEndLine,
      const Lines& fromLines,
      int maxResults, bool keepMultiplePerLanding,
      auto&& makeLocalInterval) {
    auto slice = sliceMotionSubset(pos, targetBeginLine, targetEndLine, fromLines);
    CharInterval localInterval = makeLocalInterval(slice.subset, slice.beginLine);

    auto navParams = navParamsForCompositionMotion(params)
        .withMaxResults(maxResults)
        .withMaxResultsPerEndPos(keepMultiplePerLanding ? maxResults : 1);
    auto navResult = navOptimizer.optimize(
        slice.subset, slice.localPos, localInterval,
        navParams, "", slice.subsetBoundary, navigationContext,
        slice.bufferIndex, slice.bufferIndexLineOffset, ctx.control);
    ctx.navNodesExplored += navResult.getStats().nodesExplored();

    for (const LandingResult& movResult : navResult.getResults()) {
      if (movResult.getSequence().empty()) continue;
      CursorPos landing = movResult.getGoalPos();
      landing.line += slice.beginLine;
      enqueueMotionTransition(parent, movResult.getSequence(), movResult.getPenalty(), landing);
    }
  };

  debug("=== CompositionOptimizer A* search ===");

  while (!pq.empty() && ctx.totalPops < params.maxNodesPopped) {
    if (ctx.control.cancelledOrPastDeadline(ctx.totalPops)) break;
    Entry current = pq.top();
    pq.pop();
    const CompositionState& s = current.state;
    ctx.totalPops++;
    CursorPos pos = s.getPos();
    int editsCompleted = s.getEditsCompleted();

    if (editsCompleted == ctx.totalEdits() && pos == goalPos) {
      ctx.nodesProcessed++;
      double effort = s.getRunningEffort().getEffort();
      debug("GOAL #" + to_string(results.size()) + ":",
            "\"" + s.getSequence().str() + "\"", "effort:", effort);
      results.emplace_back(s.getSequence(), s.getRunningEffort());
      if constexpr (Trace::collecting) {
        resultSpans.push_back(
            trace.onGoalReached(current.trace, ctx.totalEdits()));
      }
      if (results.size() >= static_cast<size_t>(params.maxResults)) {
        debug("maximum result count reached");
        break;
      }
      continue;
    }

    auto costIt = ctx.costMap.find(s.getKey());
    if (costIt != ctx.costMap.end() && costIt->second < s.getCost()) {
      ctx.statesSkipped++;
      continue;
    }

    ctx.nodesProcessed++;
    ctx.trackState(s);

    debug("pop:", "\"" + s.getSequence().str() + "\"",
          "pos:", pos, "edits:", editsCompleted,
          "cost:", s.getCost(), "effort:", s.getEffort());

    // Post-final-edit nav: all edits applied, only motions toward goalPos.
    if (editsCompleted == ctx.totalEdits()) {
      debug("  post-edit nav from", pos, "to goalPos", goalPos);
      exploreMotionsToInterval(
          current, pos, goalPos.line, goalPos.line,
          ctx.getLinesAfter(editsCompleted),
          clamp(params.maxResults, 1, 10), true,
          [&](const Lines& subset, int beginLine) -> CharInterval {
            CursorPos localGoal(goalPos.line - beginLine, goalPos.col, goalPos.targetCol);
            return CharInterval(localGoal, localGoal);
          });
      continue;
    }

    const Lines& currentLines = ctx.getLinesAfter(editsCompleted);
    const DiffState& nextEdit = ctx.getDiffState(editsCompleted);

    // Pure insertion: no edit region to enter. Strategies are shared with
    // CompositionFrontier so the two never drift.
    if (nextEdit.isPureInsertion()) {
      debug("  pure insertion at", nextEdit.beginPos,
            "text='" + VF::prettify(nextEdit.insertedText) + "'");

      auto dispatchInsertion = [&](const CompositionStrategies::Insertion& s) {
        const bool inRange = (pos.line == s.targetLine &&
                              pos.col >= s.beginCol && pos.col < s.endCol);

        if (inRange) {
          enqueueEditTransition(
              current, Sequence(s.insertCmd), 0.0, s.goalPos, editsCompleted + 1);
          return;
        }

        auto search = buildCompositionRangeMotionSearch(
            currentLines, pos, s.targetLine, s.beginCol, s.endCol,
            boundary, params);
        if (!search) return;
        auto navResult = optimizeCompositionRangeMotion(
            navOptimizer, *search, navigationContext, params, 1, ctx.control);
        ctx.navNodesExplored += navResult.getStats().nodesExplored();

        for (const LandingResult& movResult : navResult.getResults()) {
          if (movResult.getSequence().empty()) continue;

          const CursorPos& localGoal = movResult.getGoalPos();
          assert(localGoal >= search->localRangeBegin && localGoal < search->localRangeEnd &&
                 "pure insertion motion goal must be subset-local and inside target range");
          // localGoal stays subset-local: the transition lands on the
          // strategy's post-insert position, not the motion endpoint.
          Sequence fullSeq = movResult.getSequence();
          const uint32_t motionBytes = static_cast<uint32_t>(fullSeq.size());
          fullSeq.append(s.insertCmd);
          enqueueEditTransition(
              current, fullSeq, movResult.getPenalty(), s.goalPos, editsCompleted + 1, motionBytes);
        }
      };

      CompositionStrategies::enumerateInsertions(nextEdit, currentLines, dispatchInsertion);
      continue;
    }

    const TransformResult& transformResult = ctx.edits[editsCompleted].transformResult;

    // Edit: every Transform result applicable from this cursor.
    const bool canEditHere = transformResult.forEachEditFrom(
        pos, ctx.getLinesAfter(editsCompleted + 1),
        [&](const Result& res, const CursorPos& editGoalPos) {
          debug("  edit found at", pos, "seq:", "\"" + res.getSequence().str() + "\"",
                "cost:", res.getCost(), "goalPos:", editGoalPos);
          enqueueEditTransition(
              current, res.getSequence(), res.getPenalty(), editGoalPos, editsCompleted + 1);
        });

    const BracketQuoteContext& bqContext = ctx.edits[editsCompleted].bracketQuoteContext;
    // Edit: bracket/quote text-object strategies anchored at this cursor.
    if (bqContext.line == pos.line) {
      CompositionStrategies::enumerateBracketQuotes(
          nextEdit, currentLines, bqContext,
          [&](const CompositionStrategies::BracketQuote& s) {
            if (s.line != pos.line || s.col != pos.col) return;
            debug("    text-object strategy at col", s.col, ":", s.body);
            enqueueEditTransition(
                current, Sequence(s.body), 0.0, s.goalPos, editsCompleted + 1);
          });
    }

    // Edit: J plan. It always starts with `J`, so activation is column-insensitive.
    const auto& joinPlan = ctx.edits[editsCompleted].joinPlan;
    if (joinPlan && pos.line == joinPlan->entryLine) {
      debug("  J plan at", pos, "seq:", "\"" + joinPlan->sequence.str() + "\"",
            "effort:", joinPlan->effort);
      enqueueEditTransition(
          current, joinPlan->sequence, joinPlan->penalty, joinPlan->goalPos, editsCompleted + 1);
    }

    // Movement: no Transform edit applies here, so navigate toward one.
    if (!canEditHere) {
      // Toward the J plan's entry line.
      if (joinPlan && pos.line != joinPlan->entryLine) {
        int entryLine = joinPlan->entryLine;
        exploreMotionsToInterval(
            current, pos, entryLine, entryLine, currentLines, 1, false,
            [&](const Lines& subset, int beginLine) -> CharInterval {
              return wholeLineMotionInterval(subset, entryLine - beginLine);
            });
      }

      debug("  motion search from", pos, "to edit region [" +
            to_string(nextEdit.beginPos.line) + "," + to_string(nextEdit.beginPos.col)
            + ")-[" + to_string(nextEdit.endPos.line) + "," +
            to_string(nextEdit.endPos.col) + ")");

      // Toward the starts the edit was solved from, split at the cursor: they
      // can have gaps, and the nav search never leaves a start inside its goal.
      const vector<CursorPos>& starts = transformResult.startPositions();
      auto exploreToStarts = [&](auto first, auto last) {
        if (first == last) return;
        const CursorPos& firstStart = *first;
        const CursorPos& lastStart = *(last - 1);
        exploreMotionsToInterval(
            current, pos, firstStart.line, lastStart.line,
            currentLines, static_cast<int>(last - first), false,
            [&](const Lines&, int beginLine) -> CharInterval {
              return CharInterval(CursorPos(firstStart.line - beginLine, firstStart.col),
                                  CursorPos(lastStart.line - beginLine, lastStart.col));
            });
      };
      auto before = partition_point(starts.begin(), starts.end(),
                                    [&](const CursorPos& s) { return s < pos; });
      auto after = partition_point(before, starts.end(),
                                   [&](const CursorPos& s) { return s <= pos; });
      exploreToStarts(starts.begin(), before);
      exploreToStarts(after, starts.end());
    }
  }

  debug("=== CompositionOptimizer done ===");
  debug("results:", static_cast<int>(results.size()),
        "nodes:", ctx.nodesProcessed,
        "pops:", ctx.totalPops,
        "skipped:", ctx.statesSkipped,
        "queueRemaining:", static_cast<int>(pq.size()));

  ctx.lastPqRemaining = static_cast<int>(pq.size());
  ctx.lastFullyExplored = pq.empty();
  return std::pair{std::move(results), std::move(resultSpans)};
}

} // anonymous namespace (templated optimizeImpl)

CompositionResult::CompositionResult(vector<Result> results, CompositionSearchStats stats,
                                     CompositionPlan plan,
                                     vector<CompositionExploredState> exploredStates,
                                     vector<TransformResult> transformResults)
    : BaseOptimizerResult(std::move(results)),
      stats_(std::move(stats)),
      plan_(std::move(plan)),
      exploredStates_(std::move(exploredStates)),
      editResults_(std::move(transformResults)) {
  assert(plan_.fenceposts.size() == plan_.diffs.size() + 1 &&
         "CompositionResult requires one more fencepost than diffs");
  assert(editResults_.size() == plan_.diffs.size() &&
         "CompositionResult requires one TransformResult per diff");
}

// Consumes ctx.edits and ctx's explored states; do not touch ctx afterwards.
static CompositionResult buildCompositionResult(
    vector<Result> results,
    const Lines& initialLines, CursorPos goalPos,
    CompositionSearchContext& ctx) {
  vector<Lines> fenceposts;
  vector<DiffState> diffs;
  vector<TransformResult> transformResults;
  fenceposts.reserve(ctx.edits.size() + 1);
  diffs.reserve(ctx.edits.size());
  transformResults.reserve(ctx.edits.size());
  fenceposts.push_back(initialLines);
  for (auto& e : ctx.edits) {
    fenceposts.push_back(MyersDiff::applyDiffState(e.diffState, fenceposts.back()));
    diffs.push_back(std::move(e.diffState));
    transformResults.push_back(std::move(e.transformResult));
  }

  int numResults = static_cast<int>(results.size());
  CompositionPlan plan{
      .finalGoalPos = goalPos,
      .diffs = std::move(diffs),
      .fenceposts = std::move(fenceposts),
  };
  return CompositionResult(std::move(results),
                           ctx.getStats(numResults,
                                        ctx.lastPqRemaining,
                                        ctx.lastFullyExplored),
                           std::move(plan),
                           ctx.takeExploredStates(),
                           std::move(transformResults));
}

// The search continues past the last edit with motions until pos == goalPos;
// with no edits it starts there.
CompositionResult CompositionOptimizer::optimize(
    const Lines& initialLines, const CursorPos initialPos, const Lines& goalLines,
    const CursorPos goalPos, CompositionOptimizerParams params,
    string_view userSequence, const NavBoundary& boundary,
    const NavContext& navigationContext, const SearchControl& control,
    const std::vector<DiffState>* forcedDiffs) {
  CHECK(goalLines.contains(goalPos),
        "goalPos must be a valid normal-mode cursor position in goalLines");
  if(goalPos < initialPos) {
    debug("only support forward motion in CompositionOptimizer");
  }

  const bool timePhases = std::getenv("VIMFY_TIME_PHASES") != nullptr;
  auto setupT0 = std::chrono::steady_clock::now();
  CompositionSearchContext ctx(initialLines, initialPos, goalLines, goalPos,
                               userSequence, navigationContext, boundary,
                               params, config, forcedDiffs, control);
  if (timePhases) {
    std::cerr << "[phase] setup (ctx ctor) total: "
              << std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - setupT0).count()
              << " ms, diffs=" << ctx.totalEdits() << "\n";
  }
  if (ctx.aborted) return {};

  // Only the search is declined; the plan still travels (stored session diffs
  // and the explore view read it).
  if (ctx.totalEdits() > 16) {
    debug("Cannot support more than 16 edits");
    return buildCompositionResult({}, initialLines, goalPos, ctx);
  }

  NoTrace trace;
  auto loopT0 = std::chrono::steady_clock::now();
  auto out = optimizeImpl<NoTrace>(
      config, initialLines, initialPos, goalLines, goalPos,
      params, userSequence, boundary, navigationContext, ctx, trace);
  if (timePhases) {
    std::cerr << "[phase] A* loop total: "
              << std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - loopT0).count()
              << " ms\n";
  }
  return buildCompositionResult(std::move(out.first), initialLines, goalPos, ctx);
}

CompositionTraceResult CompositionOptimizer::optimizeWithEditSpans(
    const Lines& initialLines, const CursorPos initialPos, const Lines& goalLines,
    const CursorPos goalPos, CompositionOptimizerParams params,
    string_view userSequence, const NavBoundary& boundary,
    const NavContext& navigationContext, const SearchControl& control) {
  CHECK(goalLines.contains(goalPos),
        "goalPos must be a valid normal-mode cursor position in goalLines");
  if(goalPos < initialPos) {
    debug("only support forward motion in CompositionOptimizer");
  }

  CompositionSearchContext ctx(initialLines, initialPos, goalLines, goalPos,
                               userSequence, navigationContext, boundary,
                               params, config, nullptr, control);
  if (ctx.aborted) return {};

  if (ctx.totalEdits() > 16) {
    debug("Cannot support more than 16 edits");
    return {buildCompositionResult({}, initialLines, goalPos, ctx), {}};
  }

  EditSpanTrace trace;
  auto out = optimizeImpl<EditSpanTrace>(
      config, initialLines, initialPos, goalLines, goalPos,
      params, userSequence, boundary, navigationContext, ctx, trace);
  CompositionTraceResult result;
  result.editSpansByResult = std::move(out.second);
  result.result = buildCompositionResult(
      std::move(out.first), initialLines, goalPos, ctx);
  assert(result.editSpansByResult.size() == result.result.getResults().size() &&
         "traced edit spans must align 1:1 with results");
  return result;
}

ostream& operator<<(ostream& os, const CompositionResult& cr) {
  os << cr.getStats() << " goalPos=" << cr.getGoalPos() << "\n";
  const auto& diffs = cr.getDiffs();
  const auto& results = cr.getResults();

  // Print diff legend: all diffs get sequential {n} labels.
  if (!diffs.empty()) {
    os << "Diffs:";
    for (size_t i = 0; i < diffs.size(); i++) {
      os << " {" << i << "}=" << diffs[i];
    }
    os << "\n";
  }

  for (size_t i = 0; i < results.size(); i++) {
    os << "  [" << i << "] "
       << VF::prettify(results[i].getSequence().view())
       << " " << results[i].getCost() << "\n";
  }

  return os;
}
