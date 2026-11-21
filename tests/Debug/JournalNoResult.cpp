// Replays every journal in $VIMFY_JOURNAL_DIR into runs of 8 commands and
// prints the ones whose composition search returns no result.
//   VIMFY_JOURNAL_DIR=<dir> ./build/tests/vimfy_debug --gtest_filter='JournalNoResult.*'

#include <gtest/gtest.h>

import std.compat;
import vf.Journal.Evaluate;
import vf.Journal.Replay;
import vf.Journal.Windows;
import vf.Keyboard.Config;
import vf.Optimizer.Analyze;
import vf.Optimizer.CompositionOptimizer.CompositionOptimizer;
import vf.Optimizer.CompositionOptimizer.PlannedEditArtifacts;
import vf.Optimizer.SearchControl;
import vf.Utils.PrettyText;

using namespace std;
namespace fs = std::filesystem;

namespace {

string readAll(const fs::path& path) {
  ifstream in(path, ios::binary);
  return string(istreambuf_iterator<char>(in), {});
}

bool inDiffRange(const DiffState& d, CursorPos p) {
  return !(p < d.beginPos) && p < d.endPos;
}

TEST(JournalNoResult, Classify) {
  const char* dir = getenv("VIMFY_JOURNAL_DIR");
  if (!dir) GTEST_SKIP() << "set VIMFY_JOURNAL_DIR";
  const Config config = Config::uniform();
  const Journal::EvaluateParams params{.slicePadding = 5, .maxSearchLines = 500,
                                       .resultsCalculated = 20, .deadlineMs = 2000};
  const array<int, 1> sizes{8};
  int total = 0, noResult = 0, startsInside = 0, capped = 0;

  vector<fs::path> files;
  for (const auto& e : fs::directory_iterator(dir)) {
    if (e.path().extension() == ".vfj") files.push_back(e.path());
  }
  ranges::sort(files);
  for (const fs::path& file : files) {
    const string bytes = readAll(file);
    Journal::Replay replay(bytes);
    Journal::WindowCutter cutter(sizes, 30000);
    Journal::Event event;
    vector<Journal::Window> windows;
    while (replay.next(event)) {
      windows.clear();
      cutter.push(event, windows);
      for (const Journal::Window& w : windows) {
        Journal::Sample s = Journal::evaluate(w, params, config);
        total++;
        if (s.outcome != Journal::Outcome::NoResult) continue;
        noResult++;

        const Lines& initial = *w.start.lines;
        const Lines& goal = *w.end.lines;
        const pair<int, int> region = computeSearchRegion(
            w.start.row, w.end.row, initial, goal, params.slicePadding);
        SearchSlice slice = sliceSearch(
            initial, goal,
            SessionEnds{.startRow = w.start.row, .startCol = w.start.col,
                        .endRow = w.end.row, .endCol = w.end.col,
                        .windowHeight = w.start.windowHeight,
                        .scrollAmount = w.start.scrollAmount},
            w.keys, w.typed, region);
        const SearchInputs in = searchInputs(std::move(slice), params.overrides);
        CompositionOptimizer opt(config);
        auto res = opt.optimize(in.initialLines, in.initialPos, in.goalLines, in.goalPos,
                                CompositionOptimizerParams{}.withMaxResults(20), in.userSeq,
                                in.boundary, in.navContext, SearchControl::NEVER_STOP);
        auto uncapped = opt.optimize(in.initialLines, in.initialPos, in.goalLines, in.goalPos,
                                     CompositionOptimizerParams{}.withMaxResults(20), "",
                                     in.boundary, in.navContext, SearchControl::NEVER_STOP);
        const bool capOnly = !uncapped.getResults().empty();
        if (capOnly) capped++;
        const auto& diffs = res.getDiffs();
        const bool inside = !diffs.empty() && inDiffRange(diffs.front(), in.initialPos);
        if (inside) startsInside++;

        cout << "== " << file.filename().string() << " atoms " << w.firstAtom << "-"
             << w.lastAtom << " keys " << VF::prettify(w.keys) << "\n";
        cout << "   cursor " << in.initialPos << " goal " << in.goalPos
             << " results(no deadline) " << res.getResults().size()
             << " pops " << res.getStats().totalPops()
             << (inside ? " CURSOR-INSIDE-FIRST-DIFF" : "")
             << (capOnly ? " EFFORT-CAP" : " UNREACHABLE") << "\n";
        for (const DiffState& d : diffs) {
          TransformResult tr = computeTransformResultForDiff(d, CompositionOptimizerParams{}, config);
          cout << "   diff " << d << " " << d.beginPos << "-" << d.endPos
               << " starts " << tr.startPositions().size();
          if (!tr.startPositions().empty()) cout << " first " << tr.startPositions().front();
          cout << "\n";
        }
      }
    }
  }
  cout << "total " << total << " no_result " << noResult
       << " cursor_inside_first_diff " << startsInside << " effort_cap " << capped << "\n";
}

// Shape of each diff's solved start positions: region size, solved count, and
// how many contiguous runs (in buffer order over the region) they form.
TEST(JournalNoResult, StartDistribution) {
  const char* dir = getenv("VIMFY_JOURNAL_DIR");
  if (!dir) GTEST_SKIP() << "set VIMFY_JOURNAL_DIR";
  const Config config = Config::uniform();
  const Journal::EvaluateParams params{.slicePadding = 5, .maxSearchLines = 500,
                                       .resultsCalculated = 20, .deadlineMs = 2000};
  const array<int, 1> sizes{8};
  map<int, int> runHistogram;
  int diffs = 0, contiguousFromBegin = 0, singleRun = 0;
  long long regionTotal = 0, solvedTotal = 0;

  vector<fs::path> files;
  for (const auto& e : fs::directory_iterator(dir)) {
    if (e.path().extension() == ".vfj") files.push_back(e.path());
  }
  ranges::sort(files);
  for (const fs::path& file : files) {
    const string bytes = readAll(file);
    Journal::Replay replay(bytes);
    Journal::WindowCutter cutter(sizes, 30000);
    Journal::Event event;
    vector<Journal::Window> windows;
    while (replay.next(event)) {
      windows.clear();
      cutter.push(event, windows);
      for (const Journal::Window& w : windows) {
        const Lines& initial = *w.start.lines;
        const Lines& goal = *w.end.lines;
        if (initial == goal) continue;
        const pair<int, int> region = computeSearchRegion(
            w.start.row, w.end.row, initial, goal, params.slicePadding);
        if (region.second - region.first > params.maxSearchLines) continue;
        SearchSlice slice = sliceSearch(
            initial, goal,
            SessionEnds{.startRow = w.start.row, .startCol = w.start.col,
                        .endRow = w.end.row, .endCol = w.end.col,
                        .windowHeight = w.start.windowHeight,
                        .scrollAmount = w.start.scrollAmount},
            w.keys, w.typed, region);
        const SearchInputs in = searchInputs(std::move(slice), params.overrides);
        CompositionOptimizer opt(config);
        auto res = opt.optimize(in.initialLines, in.initialPos, in.goalLines, in.goalPos,
                                CompositionOptimizerParams{}.withMaxResults(1), in.userSeq,
                                in.boundary, in.navContext, SearchControl::NEVER_STOP);
        for (int e = 0; e < res.totalEdits(); e++) {
          const PlannedEditView view = res.plannedEditAt(e);
          const DiffState& d = view.diff;
          const TransformResult& tr = view.transformResult;
          if (d.isPureInsertion()) continue;
          // Walk the region's positions in buffer order; count solved runs.
          int regionSize = 0, solved = 0, runs = 0;
          bool prevSolved = false, firstSolved = false;
          for (int line = d.beginPos.line; line <= d.endPos.line; line++) {
            if (line == d.endPos.line && d.endPos.col == 0) break;
            const int lineEnd = line == d.endPos.line
                ? d.endPos.col : max(1, static_cast<int>(view.preFencepost[line].size()));
            for (int col = line == d.beginPos.line ? d.beginPos.col : 0; col < lineEnd; col++) {
              const bool s = !tr.resultsAt(line, col).empty();
              if (regionSize == 0) firstSolved = s;
              regionSize++;
              if (s) solved++;
              if (s && !prevSolved) runs++;
              prevSolved = s;
            }
          }
          if (regionSize < 2) continue;
          diffs++;
          regionTotal += regionSize;
          solvedTotal += solved;
          runHistogram[min(runs, 5)]++;
          if (runs == 1) singleRun++;
          if (runs == 1 && firstSolved) contiguousFromBegin++;
          if (runs >= 3) {
            cout << "  " << file.filename().string() << " atoms " << w.firstAtom
                 << " region " << regionSize << " solved " << solved << " runs " << runs
                 << " diff " << d << "\n";
          }
        }
      }
    }
  }
  cout << "multi-position regions " << diffs << " avg size "
       << (diffs ? double(regionTotal) / diffs : 0) << " avg solved "
       << (diffs ? double(solvedTotal) / diffs : 0) << "\n";
  cout << "single run " << singleRun << " (starting at region begin " << contiguousFromBegin << ")\n";
  for (auto [runs, count] : runHistogram) {
    cout << "  runs " << (runs == 5 ? "5+" : to_string(runs)) << ": " << count << "\n";
  }
}

}  // namespace
