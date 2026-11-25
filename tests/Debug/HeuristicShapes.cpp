// Transform-heuristic loss over the planned edits of recorded journals.
// For each heuristic setting and each start position of each edit:
//   gap  = cost / best known cost - 1   (best over the pure-effort search and all settings)
//   lost = some setting solves the start, this one does not
//   work = nodes explored, relative to the first setting
// Gap and lost are averaged within an edit first, so every edit counts once.
// History is split by file into halves A and B to show whether a ranking holds up.
//   ./build/tests/vimfy_debug --gtest_filter='HeuristicShapes.*'
// with VIMFY_JOURNAL_DIR and/or VIMFY_SESSION_DUMP_DIR set.

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <cstdio>

import std.compat;
import vf.Journal.Evaluate;
import vf.Journal.Replay;
import vf.Journal.Windows;
import vf.Keyboard.Config;
import vf.Optimizer.Analyze;
import vf.Optimizer.CompositionOptimizer.CompositionOptimizer;
import vf.Optimizer.CompositionOptimizer.PlannedEditArtifacts;
import vf.Optimizer.SearchControl;
import vf.Optimizer.TransformOptimizer.TransformTransitionDispatcher;
import vf.Utils.PrettyText;

using namespace std;
namespace fs = std::filesystem;

namespace {

constexpr int REFERENCE_POP_CAP = 20000;
constexpr int ALL_STARTS = 100000;
constexpr double EPS = 1e-9;

struct Setting {
  double distanceWeight, colExponent, breakExponent, typedWeight;
  // Weight chosen per edit from its size N (the unweighted estimate at the start):
  // max(floor, min(1, distanceWeight * N^-sizeExponent)). 0 = the plain weight.
  double sizeExponent = 0, floor = 0;

  double weightFor(double size) const {
    if (sizeExponent == 0) return distanceWeight;
    return max(floor, min(1.0, distanceWeight * pow(max(1.0, size), -sizeExponent)));
  }
  string label() const {
    char buf[64];
    if (sizeExponent == 0) snprintf(buf, sizeof buf, "w=%.2f", distanceWeight);
    else snprintf(buf, sizeof buf, "%.2f*N^-%.2f>=%.2f", distanceWeight, sizeExponent, floor);
    string out = buf;
    if (colExponent != 1.0) out += " col^" + to_string(colExponent).substr(0, 4);
    if (typedWeight == 0.0) out += " typed0";
    return out;
  }
};

string readAll(const fs::path& path) {
  ifstream in(path, ios::binary);
  return string(istreambuf_iterator<char>(in), {});
}

// One canonical edit: `lines` holds the edit's own lines plus a placeholder
// line on each side that had a neighbour.
struct CorpusEdit {
  const char* kind;
  vector<string> lines;
  CursorPos begin, end;
  string deleted, inserted;
  double weight = 1.0;  // history edits this one stands for
  bool heavy = false;   // one of the few edits holding most of the search work
};

struct Problem {
  DiffState diff;
  int half;
  CorpusEdit edit;
  bool isolates;  // `edit` reproduces the boundary the search saw
};

const vector<CorpusEdit> CORPUS = {
#include "HeuristicCorpus.inc"
    // Known failure, kept by hand: qwerty used to answer x...ceomega<Esc>.
    {"change-word", {"delta epsilon"}, {0, 6}, {0, 13}, "epsilon", "omega"},
};

DiffState toDiff(const CorpusEdit& e) {
  return DiffState(e.begin, e.end, e.deleted, e.inserted,
                   TransformBoundary(Lines(e.lines), e.begin, e.end));
}

string rangeText(const CorpusEdit& e) {
  string flat;
  size_t begin = 0, end = 0;
  for (int line = 0; line < ssize(e.lines); line++) {
    if (line == e.begin.line) begin = flat.size() + e.begin.col;
    if (line == e.end.line) end = flat.size() + e.end.col;
    flat += e.lines[line] + '\n';
  }
  return flat.substr(begin, end - begin);
}

vector<Problem> corpusProblems(bool heavy) {
  vector<Problem> problems;
  for (const CorpusEdit& e : CORPUS) {
    if (e.heavy != heavy) continue;
    EXPECT_EQ(rangeText(e), e.deleted) << "corpus entry range does not match its deleted text";
    problems.push_back({toDiff(e), int(problems.size() % 2), e, true});
  }
  return problems;
}

bool hasSpace(const string& s) { return s.find_first_of(" \t") != string::npos; }

const char* kindOf(const DiffState& d) {
  const auto delBreaks = ranges::count(d.deletedText, '\n');
  const auto insBreaks = ranges::count(d.insertedText, '\n');
  if (d.isPureDeletion()) {
    return delBreaks ? "del-lines" : hasSpace(d.deletedText) ? "del-words" : "del-part";
  }
  if (delBreaks != insBreaks) return "change-reflow";
  if (delBreaks) return "change-lines";
  return hasSpace(d.deletedText) ? "change-words" : "change-word";
}

string cppLiteral(const string& s) {
  string out = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (c == '\t') out += "\\t";
    else if (c < 0x20 || c >= 0x7f) out += format("\\{:03o}", c);
    else out += c;
  }
  return out + "\"";
}

// Rebuilds the edit on its own lines; `ok` is false when that does not
// reproduce the boundary the search actually saw.
CorpusEdit isolate(const DiffState& d, const Lines& before, bool& ok) {
  const bool above = d.boundary.hasLinesAbove(), below = d.boundary.hasLinesBelow();
  const int lastLine = min(d.endPos.line, before.lastLine());
  CorpusEdit e{kindOf(d), {}, d.beginPos, d.endPos, d.deletedText, d.insertedText};
  if (above) e.lines.push_back("above");
  for (int line = d.beginPos.line; line <= lastLine; line++) e.lines.push_back(before[line]);
  if (below) e.lines.push_back("below");
  const int shift = d.beginPos.line - (above ? 1 : 0);
  e.begin.line -= shift;
  e.end.line -= shift;
  if (e.end.line >= ssize(e.lines)) { ok = false; return e; }
  const TransformBoundary rebuilt(Lines(e.lines), e.begin, e.end);
  ok = rebuilt.prefix() == d.boundary.prefix() && rebuilt.suffix() == d.boundary.suffix() &&
       rebuilt.hasLinesAbove() == above && rebuilt.hasLinesBelow() == below;
  return e;
}

// Unique non-insertion planned edits, across every source added.
struct Harvest {
  vector<Problem> problems;
  set<string> seen;
  int half = 0;

  bool add(const auto& res) {
    bool contributed = false;
    for (int e = 0; e < res.totalEdits(); e++) {
      const PlannedEditView view = res.plannedEditAt(e);
      const DiffState& d = view.diff;
      if (d.isPureInsertion()) continue;
      string key = d.deletedText + '\0' + d.insertedText + '\0' +
                   d.boundary.prefix() + '\0' + d.boundary.suffix() + '\0' +
                   (d.boundary.hasLinesAbove() ? 'a' : '-') +
                   (d.boundary.hasLinesBelow() ? 'b' : '-');
      if (!seen.insert(std::move(key)).second) continue;
      bool ok = false;
      CorpusEdit edit = isolate(d, view.preFencepost, ok);
      problems.push_back({d, half, std::move(edit), ok});
      contributed = true;
    }
    return contributed;
  }
};

// Dumps store raw key bytes in some fields; bytes outside a well-formed
// UTF-8 sequence become '?' so the rest of the dump still parses.
string validUtf8(string s) {
  const auto continuation = [&](size_t i) { return i < s.size() && (s[i] & 0xC0) == 0x80; };
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = s[i];
    const int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2
                    : (c & 0xF8) == 0xF0 ? 3 : -1;
    bool ok = extra >= 0;
    for (int k = 1; ok && k <= extra; k++) ok = continuation(i + k);
    if (ok) { i += extra + 1; continue; }
    s[i++] = '?';
  }
  return s;
}

Lines toLines(const nlohmann::json& arr) {
  Lines out;
  for (const auto& v : arr) out.push_back(Line(v.get<string>()));
  return out;
}

// Session dumps (`slow/*.json`): whole sessions, older than the journals but
// kept only because they ran slowly, so they skew toward expensive edits.
void addSessionDumps(const char* dir, const Config& config, Harvest& harvest) {
  vector<fs::path> files;
  for (const auto& e : fs::directory_iterator(dir)) {
    if (e.path().extension() == ".json") files.push_back(e.path());
  }
  ranges::sort(files);
  for (const fs::path& file : files) {
    const nlohmann::json root = nlohmann::json::parse(validUtf8(readAll(file)), nullptr, false);
    if (root.is_discarded()) {
      printf("skipped unparseable dump %s\n", file.filename().c_str());
      continue;
    }
    if (!root.contains("goal_lines") || root["goal_lines"].is_null()) continue;
    const Lines initial = toLines(root.at("lines")), goal = toLines(root.at("goal_lines"));
    if (initial == goal || initial.empty() || goal.empty()) continue;
    if (initial.size() > 500 || goal.size() > 500) continue;
    // Only the plan is wanted, so the cursor is pinned: older dumps do not
    // all store positions the current search accepts.
    CompositionOptimizer opt(config);
    auto res = opt.optimize(initial, CursorPos(0, 0), goal, CursorPos(0, 0),
                            CompositionOptimizerParams{}.withMaxResults(1).withMaxNodesPopped(1));
    if (harvest.add(res)) harvest.half ^= 1;
  }
}

void addJournals(const char* dir, const Config& config, Harvest& harvest) {
  const Journal::EvaluateParams params{.slicePadding = 5, .maxSearchLines = 500,
                                       .resultsCalculated = 20, .deadlineMs = 2000};
  const array<int, 1> sizes{8};
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
    bool contributed = false;
    while (replay.next(event)) {
      windows.clear();
      cutter.push(event, windows);
      for (const Journal::Window& w : windows) {
        const Lines& initial = *w.start.lines;
        const Lines& goal = *w.end.lines;
        if (initial == goal) continue;
        const SearchRegion region = computeSearchRegion(
            w.start.row, w.end.row, initial, goal, params.slicePadding);
        if (region.paddedHeight() - 1 > params.maxSearchLines) continue;
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
        contributed |= harvest.add(res);
      }
    }
    if (contributed) harvest.half ^= 1;
  }
}

// $VIMFY_JOURNAL_DIR and/or $VIMFY_SESSION_DUMP_DIR.
vector<Problem> historyProblems(const Config& config) {
  Harvest harvest;
  if (const char* dir = getenv("VIMFY_JOURNAL_DIR")) addJournals(dir, config, harvest);
  if (const char* dir = getenv("VIMFY_SESSION_DUMP_DIR")) addSessionDumps(dir, config, harvest);
  return std::move(harvest.problems);
}

struct Run {
  vector<double> costByStart;  // negative = unsolved
  long nodes = 0;
};

Run run(const DiffState& diff, const Config& config, const TransformOptimizerParams& transform) {
  CompositionOptimizerParams params;
  params.transform = transform;
  params.transformMaxResultsPerStartPos = transform.maxResultsPerStartPos;
  int nodes = 0;
  TransformResult res = computeTransformResultForDiff(diff, params, config, &nodes);
  Run out;
  out.nodes = nodes;
  for (const vector<Result>& bucket : res.getResults()) {
    double cost = -1.0;
    for (const Result& r : bucket) cost = cost < 0 ? r.getCost() : min(cost, r.getCost());
    out.costByStart.push_back(cost);
  }
  return out;
}

// The heuristic's own unweighted estimate at the start: characters plus 2 per line break.
double regionSize(const DiffState& d) {
  return double(d.deletedText.size() + ranges::count(d.deletedText, '\n'));
}

constexpr array<int, 6> SIZE_EDGES{2, 5, 10, 20, 40, 80};
constexpr array<const char*, 7> SIZE_NAMES{"1-2", "3-5", "6-10", "11-20", "21-40", "41-80", "81+"};
constexpr int SIZES = 7;
int sizeOf(double estimate) {
  int b = 0;
  while (b < 6 && estimate > SIZE_EDGES[b]) b++;
  return b;
}

void report(const char* keyboard, const Config& config, const vector<Problem>& problems) {
  // VIMFY_SETTINGS="dist,col^,break^,typed[,size^,floor];..." replaces the grid;
  // the current defaults are always row one, since work is relative to it.
  vector<Setting> settings;
  const char* spec = getenv("VIMFY_SETTINGS");
  if (spec && string_view(spec) == "fine") {
    for (double typed : {0.0, 1.0}) {
      for (double colExp : {1.0, 0.9, 0.8, 0.7, 0.6, 0.5}) {
        for (double dw : {1.0, 0.9, 0.8, 0.7, 0.6, 0.5}) {
          settings.push_back({dw, colExp, 1.0, typed});
        }
      }
    }
  } else if (spec) {
    settings.push_back({0.35, 1.0, 1.0, 1.0});
    for (auto row : string_view(spec) | views::split(';')) {
      Setting s{};
      sscanf(string(string_view(row)).c_str(), "%lf,%lf,%lf,%lf,%lf,%lf", &s.distanceWeight,
             &s.colExponent, &s.breakExponent, &s.typedWeight, &s.sizeExponent, &s.floor);
      settings.push_back(s);
    }
  }
  for (double dw : {1.0, 0.5}) {
    if (!settings.empty()) break;
    for (double colExp : {1.0, 0.75, 0.5}) {
      for (double breakExp : {1.0, 0.5}) {
        for (double typed : {0.0, 0.5, 1.0}) settings.push_back({dw, colExp, breakExp, typed});
      }
    }
  }

  const int P = problems.size(), S = settings.size();
  vector<vector<Run>> runs(S, vector<Run>(P));
  vector<vector<double>> best(P);
  vector<double> distance(P);
  for (int p = 0; p < P; p++) {
    distance[p] = regionSize(problems[p].diff);
    const Run reference = run(problems[p].diff, config,
                              TransformOptimizerParams::dijkstra(ALL_STARTS, REFERENCE_POP_CAP));
    best[p] = reference.costByStart;
    for (int s = 0; s < S; s++) {
      const Setting& v = settings[s];
      runs[s][p] = run(problems[p].diff, config,
                       TransformOptimizerParams{}
                           .withMaxResults(ALL_STARTS)
                           .withDistanceWeight(v.weightFor(distance[p]))
                           .withColExponent(v.colExponent)
                           .withBreakExponent(v.breakExponent)
                           .withTypedWeight(v.typedWeight));
      for (size_t i = 0; i < best[p].size(); i++) {
        const double c = runs[s][p].costByStart[i];
        if (c >= 0 && (best[p][i] < 0 || c < best[p][i])) best[p][i] = c;
      }
    }
  }

  // Every count below is weighted by the history edits a problem stands for.
  double solvable = 0, baselineNodes = 0;
  for (int p = 0; p < P; p++) {
    const double w = problems[p].edit.weight;
    solvable += w * ranges::count_if(best[p], [](double c) { return c >= 0; });
    baselineNodes += w * runs[0][p].nodes;
  }

  // Each edit counts once, however many starts it has: lost%, %worse and the
  // gaps are per-edit averages over its starts, then averaged over edits.
  // `work` is total nodes; `typical` is the geometric mean of per-edit ratios.
  // How much any one edit can sway the table, under the first setting.
  double weightSum = 0, gapTotal = 0, topWeight = 0, topWork = 0, topGap = 0;
  vector<double> editGaps(P, 0.0);
  for (int p = 0; p < P; p++) {
    const double w = problems[p].edit.weight;
    int solved = 0;
    double gap = 0;
    for (size_t i = 0; i < best[p].size(); i++) {
      const double c = runs[0][p].costByStart[i];
      if (best[p][i] < EPS || c < 0) continue;
      solved++;
      gap += c / best[p][i] - 1.0;
    }
    editGaps[p] = solved ? w * gap / solved : 0.0;
    weightSum += w;
    gapTotal += editGaps[p];
    topWeight = max(topWeight, w);
    topWork = max(topWork, w * runs[0][p].nodes);
    topGap = max(topGap, editGaps[p]);
  }
  printf("== %s: %d edits standing for %.0f edits, %.0f solvable starts\n", keyboard, P, weightSum,
         solvable);
  printf("   largest single-edit share: %.0f%% of edits, %.0f%% of work, %.0f%% of gap\n",
         100.0 * topWeight / weightSum, baselineNodes ? 100.0 * topWork / baselineNodes : 0.0,
         gapTotal > EPS ? 100.0 * topGap / gapTotal : 0.0);
  printf("  %5s %5s %6s %6s | %6s %7s %8s %8s %8s %8s | %6s %8s\n", "dist", "col^", "break^",
         "typed", "lost%", "%worse", "meanGap%", "gapA%", "gapB%", "maxGap%", "work", "typical");
  for (int s = 0; s < S; s++) {
    double edits = 0, lost = 0, worse = 0, nodes = 0, gapSum = 0, gapMax = 0, logRatio = 0;
    array<double, 2> halfSum{}, halfCount{};
    for (int p = 0; p < P; p++) {
      const double w = problems[p].edit.weight;
      nodes += w * runs[s][p].nodes;
      int solvableStarts = 0, solved = 0, worseStarts = 0;
      double editGap = 0;
      for (size_t i = 0; i < best[p].size(); i++) {
        if (best[p][i] < 0) continue;
        solvableStarts++;
        const double c = runs[s][p].costByStart[i];
        if (c < 0) continue;
        const double gap = best[p][i] > EPS ? c / best[p][i] - 1.0 : 0.0;
        solved++;
        editGap += gap;
        gapMax = max(gapMax, gap);
        if (gap > EPS) worseStarts++;
      }
      if (solvableStarts == 0) continue;
      edits += w;
      lost += w * (solvableStarts - solved) / solvableStarts;
      logRatio += w * log(max(1.0, double(runs[s][p].nodes)) / max(1.0, double(runs[0][p].nodes)));
      if (solved == 0) continue;
      worse += w * worseStarts / solved;
      gapSum += w * editGap / solved;
      halfSum[problems[p].half] += w * editGap / solved;
      halfCount[problems[p].half] += w;
    }
    const Setting& v = settings[s];
    printf("  %5.2f %5.2f %6.1f %6.1f | %6.2f %6.1f%% %8.2f %8.2f %8.2f %8.1f | %5.2fx %7.2fx"
           "  %s\n",
           v.distanceWeight, v.colExponent, v.breakExponent, v.typedWeight,
           edits ? 100.0 * lost / edits : 0.0, edits ? 100.0 * worse / edits : 0.0,
           edits ? 100.0 * gapSum / edits : 0.0,
           halfCount[0] ? 100.0 * halfSum[0] / halfCount[0] : 0.0,
           halfCount[1] ? 100.0 * halfSum[1] / halfCount[1] : 0.0, 100.0 * gapMax,
           baselineNodes ? nodes / baselineNodes : 0.0, edits ? exp(logRatio / edits) : 0.0,
           v.sizeExponent != 0 ? v.label().c_str() : "");
  }

  if (!getenv("VIMFY_BY_SIZE")) return;
  // The same numbers split by the edit's size. A weight chosen per size is one
  // row per column, so any such scheme can be read off these tables.
  struct Cell {
    double edits = 0, lost = 0, gap = 0, solvedEdits = 0, nodes = 0;
  };
  vector<array<Cell, SIZES>> cells(S);
  for (int s = 0; s < S; s++) {
    for (int p = 0; p < P; p++) {
      const double w = problems[p].edit.weight;
      Cell& c = cells[s][sizeOf(distance[p])];
      c.nodes += w * runs[s][p].nodes;
      int solvableStarts = 0, solved = 0;
      double editGap = 0;
      for (size_t i = 0; i < best[p].size(); i++) {
        if (best[p][i] < 0) continue;
        solvableStarts++;
        const double cost = runs[s][p].costByStart[i];
        if (cost < 0) continue;
        solved++;
        editGap += best[p][i] > EPS ? cost / best[p][i] - 1.0 : 0.0;
      }
      if (solvableStarts == 0) continue;
      c.edits += w;
      c.lost += w * (solvableStarts - solved) / solvableStarts;
      if (solved == 0) continue;
      c.solvedEdits += w;
      c.gap += w * editGap / solved;
    }
  }
  const auto table = [&](const char* title, auto&& value) {
    printf("   %s\n  %-26s", title, "");
    for (int b = 0; b < SIZES; b++) printf(" %7s", SIZE_NAMES[b]);
    printf("\n");
    for (int s = 0; s < S; s++) {
      printf("  %-26s", settings[s].label().c_str());
      for (int b = 0; b < SIZES; b++) printf(" %7.2f", value(s, b));
      printf("\n");
    }
  };
  printf("   by chars to clear (N)\n  %-26s", "edits");
  for (int b = 0; b < SIZES; b++) printf(" %7.0f", cells[0][b].edits);
  printf("\n  %-26s", "share of row-one nodes %");
  for (int b = 0; b < SIZES; b++) printf(" %7.0f", 100.0 * cells[0][b].nodes / max(1.0, baselineNodes));
  printf("\n");
  table("mean gap%", [&](int s, int b) {
    const Cell& c = cells[s][b];
    return c.solvedEdits ? 100.0 * c.gap / c.solvedEdits : NAN;
  });
  table("gap points this size adds to the overall mean", [&](int s, int b) {
    return 100.0 * cells[s][b].gap / max(1.0, weightSum);
  });
  table("nodes relative to row one", [&](int s, int b) {
    return cells[s][b].nodes / max(1.0, cells[0][b].nodes);
  });
  table("lost%", [&](int s, int b) {
    const Cell& c = cells[s][b];
    return c.edits ? 100.0 * c.lost / c.edits : NAN;
  });
}

// ---- Anatomy: per-state traces of the search ------------------------------

// One popped state. `cols` holds the region characters left on each line, so
// any shape of the estimate can be recomputed from it.
struct Traced {
  TransformTraceEvent event;
  int start;
  int depth;  // commands so far
  double effort, priority;
  vector<int> cols;
  int left;  // region characters before the cursor
  string seq;
  int words = 0, bigWords = 0;  // `w` and `W` units left in the region
  size_t key = 0;       // the search's own state key (buffer, cursor, mode, start)
  size_t keyAndDot = 0; // the same plus the dot register
  // Structure around the cursor, for the fitted estimate.
  int leftOnLine = 0, rightOnLine = 0;            // region characters on the cursor's line
  int wordsLeftOnLine = 0, wordsRightOnLine = 0;  // `w` units among them
  int linesAbove = 0, linesBelow = 0;
  bool startsLine = false, endsLine = false;      // the region reaches column 0 / the line end

  int chars() const { return reduce(cols.begin(), cols.end(), 0); }
  int breaks() const { return ssize(cols) - 1; }
  double shaped(double exponent) const {
    double d = 2.0 * breaks();
    for (int c : cols) d += c ? pow(c, exponent) : 0.0;
    return d;
  }
  // Sides of the cursor that still hold region text: 0, 1 or 2.
  int pieces() const { return (left > 0) + (chars() - left > 0); }
};

struct Tracer {
  int leftOffset, rightOffset;
  vector<Traced> events;

  static void record(void* ctx, TransformTraceEvent event, const TransformState& s) {
    Tracer& self = *static_cast<Tracer*>(ctx);
    Traced t{event, s.getStartIndex(), 0, s.getEffort(), s.getCost(), {}, 0, s.getSeq()};
    for (auto step = s.getPathTail(); step; step = step->prev) t.depth++;
    t.key = TransformStateKeyHash{}(s.getKey());
    t.keyAndDot = t.key ^ (hash<string>{}(s.getLastEdit().base) * 31 + s.getLastEdit().count + 1);
    const Lines& lines = s.getLines();
    const CursorPos pos = s.getPos();
    const int last = ssize(lines) - 1;
    t.linesAbove = clamp(pos.line, 0, max(0, last));
    t.linesBelow = max(0, last - pos.line);
    t.startsLine = self.leftOffset == 0;
    t.endsLine = self.rightOffset == 0;
    for (int i = 0; i <= last; i++) {
      const int lead = i == 0 ? self.leftOffset : 0;
      const int len = max(0, int(lines[i].size()) - lead - (i == last ? self.rightOffset : 0));
      t.cols.push_back(len);
      if (i < pos.line) t.left += len;
      else if (i == pos.line) t.left += clamp(pos.col - lead, 0, len);
      if (i == pos.line) {
        const int cut = clamp(pos.col - lead, 0, len);
        t.leftOnLine = cut;
        t.rightOnLine = len - cut;
        int prev = 0;
        for (int c = 0; c < len; c++) {
          if (c == cut) prev = 0;
          const unsigned char ch = lines[i][lead + c];
          const int cls = ch == ' ' || ch == '\t' ? 0 : isalnum(ch) || ch == '_' || ch >= 0x80 ? 1 : 2;
          if (cls != 0 && cls != prev) (c < cut ? t.wordsLeftOnLine : t.wordsRightOnLine)++;
          prev = cls;
        }
      }
      // 0 = blank, 1 = keyword, 2 = punctuation, as `w` sees them.
      int before = 0;
      for (int c = lead; c < lead + len; c++) {
        const unsigned char ch = lines[i][c];
        const int cls = ch == ' ' || ch == '\t' ? 0 : isalnum(ch) || ch == '_' || ch >= 0x80 ? 1 : 2;
        t.words += cls != 0 && cls != before;
        t.bigWords += cls != 0 && before == 0;
        before = cls;
      }
    }
    self.events.push_back(std::move(t));
  }
};

struct TracedRun {
  Run run;
  vector<string> seqByStart;
  vector<Traced> events;
  vector<vector<int>> eventsByStart;
  SearchStopReason stop = SearchStopReason::Unknown;
  int pops = 0;  // includes stale states and states of starts already settled

  // The Goal or CacheHit that settled a start; -1 if none.
  int closing(int start) const {
    for (int e : eventsByStart[start]) {
      if (events[e].event == TransformTraceEvent::Goal ||
          events[e].event == TransformTraceEvent::CacheHit) return e;
    }
    return -1;
  }
  bool cutOff(int start) const {
    return ranges::any_of(eventsByStart[start], [&](int e) {
      return events[e].event == TransformTraceEvent::WorkCutoff;
    });
  }
  const Traced* initial(int start) const {
    for (int e : eventsByStart[start]) {
      if (events[e].depth == 0) return &events[e];
    }
    return nullptr;
  }
  // Non-goal states on the way to this start's answer, start state excluded.
  vector<const Traced*> pathStates(int start) const {
    const string& seq = seqByStart[start];
    vector<const Traced*> out;
    for (int e : eventsByStart[start]) {
      const Traced& t = events[e];
      if (t.event == TransformTraceEvent::Goal || t.event == TransformTraceEvent::WorkCutoff) continue;
      if (t.seq.empty() || t.seq.size() >= seq.size() || !seq.starts_with(t.seq)) continue;
      out.push_back(&t);
    }
    ranges::sort(out, {}, [](const Traced* t) { return t->seq.size(); });
    return out;
  }
  // Expansions of a state this start had already expanded; `withDot` only
  // counts those that also arrived with the same dot register.
  int repeats(int start, bool withDot) const {
    unordered_set<size_t> seen;
    int n = 0;
    for (int e : eventsByStart[start]) {
      if (events[e].event != TransformTraceEvent::Expand) continue;
      n += !seen.insert(withDot ? events[e].keyAndDot : events[e].key).second;
    }
    return n;
  }
  // Expansions that are ancestors of (or are) the start's own answer.
  int onPath(int start) const {
    const string& seq = seqByStart[start];
    if (run.costByStart[start] < 0) return 0;
    int n = 0;
    for (int e : eventsByStart[start]) {
      const Traced& t = events[e];
      if (t.event != TransformTraceEvent::WorkCutoff && seq.starts_with(t.seq)) n++;
    }
    return n;
  }
};

TracedRun runTraced(const DiffState& diff, const Config& config,
                    const TransformOptimizerParams& transform) {
  Tracer tracer{diff.boundary.leftColOffset(), diff.boundary.rightColOffset(), {}};
  transformTraceSink = {&Tracer::record, &tracer};
  CompositionOptimizerParams params;
  params.transform = transform;
  params.transformMaxResultsPerStartPos = transform.maxResultsPerStartPos;
  int nodes = 0;
  TransformResult res = computeTransformResultForDiff(diff, params, config, &nodes);
  transformTraceSink = {};

  TracedRun out;
  out.run.nodes = nodes;
  out.stop = res.getStats().stopReason();
  out.pops = res.getStats().totalPops();
  for (const vector<Result>& bucket : res.getResults()) {
    double cost = -1.0;
    string seq;
    for (const Result& r : bucket) {
      if (cost < 0 || r.getCost() < cost) { cost = r.getCost(); seq = r.getSequence().str(); }
    }
    out.run.costByStart.push_back(cost);
    out.seqByStart.push_back(std::move(seq));
  }
  out.events = std::move(tracer.events);
  out.eventsByStart.resize(out.seqByStart.size());
  for (int e = 0; e < ssize(out.events); e++) out.eventsByStart[out.events[e].start].push_back(e);
  return out;
}

double weightedMedian(vector<pair<double, double>> v) {
  if (v.empty()) return NAN;
  ranges::sort(v);
  double total = 0, acc = 0;
  for (const auto& [x, w] : v) total += w;
  for (const auto& [x, w] : v) {
    acc += w;
    if (acc >= total / 2) return x;
  }
  return v.back().first;
}

struct Mean {
  double sum = 0, weight = 0;
  void add(double x, double w) { sum += w * x; weight += w; }
  double get() const { return weight > 0 ? sum / weight : NAN; }
};

// Largest weight (exponent) at which every state on a better path still
// outranks the event that settled the start under the linear setting.
double neededWeight(const vector<const Traced*>& path, double closingPriority, double typed) {
  double need = 1e9;
  for (const Traced* t : path) {
    const double budget = closingPriority - typed - t->effort;
    need = min(need, budget / t->shaped(1.0));
  }
  return need;
}
double neededExponent(const vector<const Traced*>& path, double closingPriority, double typed) {
  double need = 1.0;
  for (const Traced* t : path) {
    const double budget = closingPriority - typed - t->effort;
    if (t->shaped(1.0) < budget) continue;
    if (t->shaped(0.0) >= budget) return -1.0;
    double lo = 0.0, hi = 1.0;
    for (int it = 0; it < 40; it++) {
      const double mid = (lo + hi) / 2;
      (t->shaped(mid) < budget ? lo : hi) = mid;
    }
    need = min(need, lo);
  }
  return need;
}

struct Shape {
  const char* name;
  double weight, exponent;
};
constexpr array<Shape, 3> SHAPES{{{"linear", 1.0, 1.0}, {"half", 0.5, 1.0}, {"sqrt", 1.0, 0.5}}};
constexpr int LINEAR = 0;

TransformOptimizerParams shapeParams(const Shape& s) {
  return TransformOptimizerParams{}
      .withMaxResults(ALL_STARTS)
      .withDistanceWeight(s.weight)
      .withColExponent(s.exponent)
      .withTypedWeight(1.0);
}

struct Anatomy {
  TracedRun reference;
  array<TracedRun, 3> runs;
  int starts = 0;
  double typed = 0;      // effort of typing the goal text
  double estimate = 0;   // linear estimate at the start
  vector<double> best;   // negative = unsolved everywhere
  vector<int> bestFrom;  // -1 = reference, else index into `runs`

  const TracedRun& source(int start) const {
    return bestFrom[start] < 0 ? reference : runs[bestFrom[start]];
  }
  const Traced* initial(int start) const {
    if (const Traced* t = reference.initial(start)) return t;
    for (const TracedRun& r : runs) {
      if (const Traced* t = r.initial(start)) return t;
    }
    return nullptr;
  }
};

Anatomy dissect(const DiffState& diff, const Config& config) {
  Anatomy a;
  a.reference = runTraced(diff, config,
                          TransformOptimizerParams::dijkstra(ALL_STARTS, REFERENCE_POP_CAP));
  for (int s = 0; s < ssize(SHAPES); s++) a.runs[s] = runTraced(diff, config, shapeParams(SHAPES[s]));
  a.starts = a.reference.run.costByStart.size();
  a.best = a.reference.run.costByStart;
  a.bestFrom.assign(a.starts, -1);
  for (int s = 0; s < ssize(SHAPES); s++) {
    for (int i = 0; i < a.starts; i++) {
      const double c = a.runs[s].run.costByStart[i];
      if (c >= 0 && (a.best[i] < 0 || c < a.best[i] - EPS)) { a.best[i] = c; a.bestFrom[i] = s; }
    }
  }
  for (int i = 0; i < a.starts; i++) {
    if (const Traced* t = a.runs[LINEAR].initial(i)) {
      a.estimate = t->shaped(1.0);
      a.typed = t->priority - a.estimate;
      break;
    }
  }
  return a;
}

// Terms of the fitted estimate. What one command can clear depends on whether
// each side of the cursor runs to a line boundary; the character count is last
// so the fit can be scored with and without it.
constexpr int FEATURES = 17;
constexpr int STRUCTURE = FEATURES - 1;
using Features = array<double, FEATURES>;
constexpr array<const char*, FEATURES> FEATURE_NAMES{
    "constant",
    "left of the cursor runs to the line start",
    "left of the cursor stops mid-line",
    "    x log(1 + words there)",
    "    x log(1 + chars there)",
    "right of the cursor runs to the line end",
    "right of the cursor stops mid-line",
    "    x log(1 + words there)",
    "    x log(1 + chars there)",
    "text on both sides of the cursor",
    "lines above the cursor",
    "    x log(1 + how many)",
    "    and the first one starts mid-line",
    "lines below the cursor",
    "    x log(1 + how many)",
    "    and the last one stops mid-line",
    "log(1 + all chars left)",
};
Features features(const Traced& t) {
  const bool left = t.leftOnLine > 0, right = t.rightOnLine > 0;
  const bool leftWhole = left && (t.linesAbove > 0 || t.startsLine);
  const bool rightWhole = right && (t.linesBelow > 0 || t.endsLine);
  const bool leftPart = left && !leftWhole, rightPart = right && !rightWhole;
  const bool above = t.linesAbove > 0, below = t.linesBelow > 0;
  return {
      1.0,
      double(leftWhole), double(leftPart),
      leftPart ? log1p(t.wordsLeftOnLine) : 0.0, leftPart ? log1p(t.leftOnLine) : 0.0,
      double(rightWhole), double(rightPart),
      rightPart ? log1p(t.wordsRightOnLine) : 0.0, rightPart ? log1p(t.rightOnLine) : 0.0,
      double(left && right),
      double(above), above ? log1p(t.linesAbove) : 0.0, double(above && !t.startsLine),
      double(below), below ? log1p(t.linesBelow) : 0.0, double(below && !t.endsLine),
      log1p(t.chars()),
  };
}

// Weighted least squares over the first `terms` features, with a small ridge.
template<class Row, class Use>
Features fitLeastSquares(const vector<Row>& rows, int terms, Use&& use) {
  array<array<double, FEATURES + 1>, FEATURES> m{};
  for (const Row& r : rows) {
    if (!use(r)) continue;
    for (int i = 0; i < terms; i++) {
      for (int j = 0; j < terms; j++) m[i][j] += r.weight * r.x[i] * r.x[j];
      m[i][FEATURES] += r.weight * r.x[i] * r.cost;
    }
  }
  for (int i = 0; i < FEATURES; i++) m[i][i] = i < terms ? m[i][i] * (1 + 1e-6) + 1e-9 : 1.0;
  for (int col = 0; col < FEATURES; col++) {
    int pivot = col;
    for (int r = col + 1; r < FEATURES; r++) {
      if (abs(m[r][col]) > abs(m[pivot][col])) pivot = r;
    }
    swap(m[col], m[pivot]);
    if (m[col][col] == 0) continue;
    for (int r = 0; r < FEATURES; r++) {
      if (r == col) continue;
      const double f = m[r][col] / m[col][col];
      for (int k = col; k <= FEATURES; k++) m[r][k] -= f * m[col][k];
    }
  }
  Features beta{};
  for (int i = 0; i < FEATURES; i++) beta[i] = m[i][i] != 0 ? m[i][FEATURES] / m[i][i] : 0.0;
  return beta;
}

void anatomy(const char* label, const Config& config, const vector<Problem>& problems) {
  struct StateRow {
    int edit;
    double cost, estimate, root, weight;
    int pieces;
    double words, bigWords, lines;
    Features x;
  };
  // How a worse answer won under the linear setting.
  enum Mechanism { OUTRANKED, HIT_OTHER_PATH, HIT_SAME_PREFIX, UNEXPLAINED, MECHANISMS };
  array<array<double, MECHANISMS>, SIZES> lostEffort{};
  array<Mean, SIZES> unstarted[3], popsPerNode[3], hog[3];
  array<double, SIZES> repeated[3]{}, repeatedSameDot[3]{}, expanded[3]{};
  array<double, SIZES> hitEffort{}, hitDeferrable{};
  array<Mean, SIZES> estLinear, estRoot, bestStart, meanStart, worstStart, onePiece, twoPiece;
  array<Mean, SIZES> midCost, midLinear, midRoot, midOne, midTwo;
  array<double, SIZES> editsBySize{};
  array<array<Mean, SIZES>, 3> logNodes, lost, lostToRule, lostToCap, byCache, useful;
  array<array<array<Mean, 4>, SIZES>, 3> byDepth;
  array<array<double, SIZES>, 3> nodeSum{};
  array<Mean, SIZES> worseShare, worseGap;
  array<vector<pair<double, double>>, SIZES> needWeight, needExponent, barrierShare;
  array<double, SIZES> halfFixes{}, rootFixes{}, orderTotal{};
  vector<StateRow> startRows, midRows;

  for (int p = 0; p < ssize(problems); p++) {
    const Anatomy a = dissect(problems[p].diff, config);
    const double w = problems[p].edit.weight;
    const int size = sizeOf(a.estimate);
    int solvable = 0;
    for (int i = 0; i < a.starts; i++) solvable += a.best[i] >= 0 && a.initial(i);
    if (solvable == 0) continue;
    editsBySize[size] += w;
    const double each = w / solvable;

    // Start states: true cost of the deletion part against the estimates.
    double lo = 1e18, hi = 0;
    Mean all, one, two;
    for (int i = 0; i < a.starts; i++) {
      const Traced* t = a.initial(i);
      if (a.best[i] < 0 || !t) continue;
      const double cost = a.best[i] - a.typed;
      lo = min(lo, cost);
      hi = max(hi, cost);
      all.add(cost, 1);
      (t->pieces() == 2 ? two : one).add(cost, 1);
      startRows.push_back({p, cost, t->shaped(1.0), t->shaped(0.5), each, t->pieces(),
                           double(t->words), double(t->bigWords), double(ssize(t->cols)),
                           features(*t)});
      for (const Traced* m : a.source(i).pathStates(i)) {
        const double remaining = a.best[i] - a.typed - m->effort;
        const int ms = sizeOf(m->shaped(1.0));
        midCost[ms].add(remaining, each);
        midLinear[ms].add(m->shaped(1.0), each);
        midRoot[ms].add(m->shaped(0.5), each);
        (m->pieces() == 2 ? midTwo : midOne)[ms].add(remaining, each);
        midRows.push_back({p, remaining, m->shaped(1.0), m->shaped(0.5), each, m->pieces(),
                           double(m->words), double(m->bigWords), double(ssize(m->cols)),
                           features(*m)});
      }
    }
    estLinear[size].add(a.estimate, w);
    estRoot[size].add(a.initial(0) ? a.initial(0)->shaped(0.5) : a.estimate, w);
    bestStart[size].add(lo, w);
    meanStart[size].add(all.get(), w);
    worstStart[size].add(hi, w);
    if (one.weight > 0) onePiece[size].add(one.get(), w);
    if (two.weight > 0) twoPiece[size].add(two.get(), w);

    // Work and losses per shape.
    for (int s = 0; s < ssize(SHAPES); s++) {
      const TracedRun& r = a.runs[s];
      nodeSum[s][size] += w * r.run.nodes;
      logNodes[s][size].add(log(max(1.0, double(r.run.nodes)) /
                                max(1.0, double(a.runs[LINEAR].run.nodes))), w);
      int expansions = 0, kept = 0, largest = 0;
      popsPerNode[s][size].add(double(r.pops) / max(1L, r.run.nodes), w);
      for (int i = 0; i < a.starts; i++) {
        if (a.best[i] < 0) continue;
        const bool solved = r.run.costByStart[i] >= 0;
        unstarted[s][size].add(!r.initial(i), each);
        largest = max(largest, int(r.eventsByStart[i].size()));
        repeated[s][size] += w * r.repeats(i, false);
        repeatedSameDot[s][size] += w * r.repeats(i, true);
        expanded[s][size] += w * r.eventsByStart[i].size();
        lost[s][size].add(!solved, each);
        lostToRule[s][size].add(!solved && r.cutOff(i), each);
        lostToCap[s][size].add(!solved && !r.cutOff(i) && r.stop == SearchStopReason::MaxPopsReached,
                               each);
        const int c = r.closing(i);
        if (solved && c >= 0) {
          byCache[s][size].add(r.events[c].event == TransformTraceEvent::CacheHit, each);
        }
        array<int, 4> depths{};
        for (int e : r.eventsByStart[i]) {
          if (r.events[e].event == TransformTraceEvent::WorkCutoff) continue;
          depths[min(3, r.events[e].depth)]++;
          expansions++;
        }
        kept += r.onPath(i);
        for (int d = 0; d < 4; d++) byDepth[s][size][d].add(depths[d], each);
      }
      if (expansions) useful[s][size].add(double(kept) / expansions, w);
      if (expansions) hog[s][size].add(double(largest) / expansions, w);
    }

    // Starts where the linear setting settled for a worse answer, and why.
    const TracedRun& lin = a.runs[LINEAR];
    for (int i = 0; i < a.starts; i++) {
      if (a.best[i] < 0 || lin.run.costByStart[i] < 0) continue;
      const bool worse = lin.run.costByStart[i] > a.best[i] + EPS;
      worseShare[size].add(worse, each);
      if (!worse) continue;
      worseGap[size].add(lin.run.costByStart[i] / max(EPS, a.best[i]) - 1.0, each);
      const int c = lin.closing(i);
      const vector<const Traced*> path = a.source(i).pathStates(i);
      const bool cache = c >= 0 && lin.events[c].event == TransformTraceEvent::CacheHit;
      const bool samePrefix = cache && a.source(i).seqByStart[i].starts_with(lin.events[c].seq);
      const Mechanism how = c < 0 || path.empty() ? UNEXPLAINED
                          : samePrefix ? HIT_SAME_PREFIX
                          : cache ? HIT_OTHER_PATH : OUTRANKED;
      lostEffort[size][how] += each * (lin.run.costByStart[i] - a.best[i]);
      if (how != OUTRANKED && how != HIT_OTHER_PATH) continue;
      // Had the hit waited its turn at the cost of the answer it implies, would
      // the better path's states have been looked at first?
      if (how == HIT_OTHER_PATH) {
        hitEffort[size] += each * (lin.run.costByStart[i] - a.best[i]);
        if (neededWeight(path, lin.run.costByStart[i], a.typed) > 1.0) {
          hitDeferrable[size] += each * (lin.run.costByStart[i] - a.best[i]);
        }
      }
      const double closing = lin.events[c].priority;
      const double nw = neededWeight(path, closing, a.typed);
      const double ne = neededExponent(path, closing, a.typed);
      needWeight[size].push_back({nw, each});
      needExponent[size].push_back({ne, each});
      double barrier = 0;
      for (const Traced* t : path) barrier = max(barrier, t->shaped(1.0) / a.estimate);
      barrierShare[size].push_back({barrier, each});
      orderTotal[size] += each;
      if (nw > 0.5) halfFixes[size] += each;
      if (ne > 0.5) rootFixes[size] += each;
    }
  }

  printf("\n== anatomy / %s: %zu edits\n", label, problems.size());
  printf("\n-- 1. Start states: what clearing the region really costs, by region size\n");
  printf("   (cost excludes typing the new text; estimates are before any weight)\n");
  printf("  %-6s %6s | %7s %7s | %6s %6s %6s | %8s %8s | %9s\n", "chars", "edits", "linear",
         "sqrt", "best", "mean", "worst", "1 side", "2 sides", "cost/char");
  for (int b = 0; b < SIZES; b++) {
    if (editsBySize[b] == 0) continue;
    printf("  %-6s %6.0f | %7.1f %7.1f | %6.2f %6.2f %6.2f | %8.2f %8.2f | %9.2f\n", SIZE_NAMES[b],
           editsBySize[b], estLinear[b].get(), estRoot[b].get(), bestStart[b].get(),
           meanStart[b].get(), worstStart[b].get(), onePiece[b].get(), twoPiece[b].get(),
           meanStart[b].get() / estLinear[b].get());
  }

  // How much of the spread in true cost each kind of estimate can explain.
  const auto explain = [](const vector<StateRow>& rows, const char* what) {
    double total = 0, mean = 0;
    for (const StateRow& r : rows) { total += r.weight; mean += r.weight * r.cost; }
    mean /= total;
    double variance = 0;
    for (const StateRow& r : rows) variance += r.weight * (r.cost - mean) * (r.cost - mean);
    const auto residual = [&](auto keyOf) {
      map<long, Mean> groups;
      for (const StateRow& r : rows) groups[keyOf(r)].add(r.cost, r.weight);
      double rest = 0;
      for (const StateRow& r : rows) {
        const double g = groups[keyOf(r)].get();
        rest += r.weight * (r.cost - g) * (r.cost - g);
      }
      return 100.0 * (1.0 - rest / variance);
    };
    // Least squares in log space for cost ~ a * estimate^p.
    double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (const StateRow& r : rows) {
      if (r.cost <= 0 || r.estimate <= 0) continue;
      const double x = log(r.estimate), y = log(r.cost);
      sw += r.weight; sx += r.weight * x; sy += r.weight * y;
      sxx += r.weight * x * x; sxy += r.weight * x * y;
    }
    const double slope = (sw * sxy - sx * sy) / (sw * sxx - sx * sx);
    const double scale = exp((sy - slope * sx) / sw);
    double rest = 0, linearErr = 0, halfErr = 0, rootErr = 0, fitErr = 0;
    for (const StateRow& r : rows) {
      const double fit = scale * pow(max(r.estimate, 1e-9), slope);
      rest += r.weight * (r.cost - fit) * (r.cost - fit);
      linearErr += r.weight * abs(r.estimate - r.cost);
      halfErr += r.weight * abs(0.5 * r.estimate - r.cost);
      rootErr += r.weight * abs(r.root - r.cost);
      fitErr += r.weight * abs(fit - r.cost);
    }
    printf("  %s (mean true cost %.2f):\n", what, mean);
    printf("    best power law of the character count: cost = %.2f * chars^%.2f\n", scale, slope);
    printf("    average error  linear %.2f | half weight %.2f | sqrt %.2f | fitted power law %.2f\n",
           linearErr / total, halfErr / total, rootErr / total, fitErr / total);
    printf("    spread explained  fitted power law %.0f%% | size bin %.0f%% | sides of cursor %.0f%%"
           " | size bin + sides %.0f%% | everything but the cursor %.0f%%\n",
           100.0 * (1.0 - rest / variance),
           residual([](const StateRow& r) { return long(sizeOf(r.estimate)); }),
           residual([](const StateRow& r) { return long(r.pieces); }),
           residual([](const StateRow& r) { return long(sizeOf(r.estimate)) * 10 + r.pieces; }),
           residual([](const StateRow& r) { return long(r.edit); }));
    // The same power-law fit with other units in place of characters.
    const auto unit = [&](const char* name, double StateRow::* field) {
      double w = 0, x1 = 0, y1 = 0, xx = 0, xy = 0;
      for (const StateRow& r : rows) {
        if (r.cost <= 0 || r.*field <= 0) continue;
        const double x = log(r.*field), y = log(r.cost);
        w += r.weight; x1 += r.weight * x; y1 += r.weight * y;
        xx += r.weight * x * x; xy += r.weight * x * y;
      }
      const double p = (w * xy - x1 * y1) / (w * xx - x1 * x1);
      const double a = exp((y1 - p * x1) / w);
      double left = 0;
      for (const StateRow& r : rows) {
        const double fit = a * pow(max(r.*field, 0.5), p);
        left += r.weight * (r.cost - fit) * (r.cost - fit);
      }
      printf("      %-10s cost = %.2f * n^%.2f explains %.0f%%\n", name, a, p,
             100.0 * (1.0 - left / variance));
    };
    printf("    by unit counted:\n");
    unit("characters", &StateRow::estimate);
    unit("words", &StateRow::words);
    unit("WORDs", &StateRow::bigWords);
    unit("lines", &StateRow::lines);

    // A fit on structure around the cursor, scored on edits it was not fitted on.
    constexpr int FOLDS = 5;
    const auto predict = [](const Features& beta, const StateRow& r) {
      double y = 0;
      for (int i = 0; i < FEATURES; i++) y += beta[i] * r.x[i];
      return max(0.0, y);
    };
    printf("    fitted on structure around the cursor (scored on the edits fitted, then on edits held out):\n");
    Features coefficients{};
    for (int terms : {STRUCTURE, FEATURES}) {
      const Features all = fitLeastSquares(rows, terms, [](const StateRow&) { return true; });
      array<Features, FOLDS> held;
      for (int f = 0; f < FOLDS; f++) {
        held[f] = fitLeastSquares(rows, terms, [f](const StateRow& r) { return r.edit % FOLDS != f; });
      }
      double inRest = 0, outRest = 0, inErr = 0, outErr = 0;
      for (const StateRow& r : rows) {
        const double in = predict(all, r), out = predict(held[r.edit % FOLDS], r);
        inRest += r.weight * (r.cost - in) * (r.cost - in);
        outRest += r.weight * (r.cost - out) * (r.cost - out);
        inErr += r.weight * abs(r.cost - in);
        outErr += r.weight * abs(r.cost - out);
      }
      printf("      %-28s explains %3.0f%% / %3.0f%% held out, average error %.2f / %.2f held out\n",
             terms == STRUCTURE ? "structure only (16 terms)" : "structure + character count",
             100.0 * (1.0 - inRest / variance), 100.0 * (1.0 - outRest / variance), inErr / total,
             outErr / total);
      coefficients = all;
    }
    for (int i = 0; i < FEATURES; i++) {
      printf("      %+7.2f  %s\n", coefficients[i], FEATURE_NAMES[i]);
    }

    // Which terms carry the fit: held-out share with only a group, and without it.
    struct Group {
      const char* name;
      vector<int> terms;
    };
    const vector<Group> groups{
        {"character count", {16}},
        {"which sides hold text, reaching a line boundary or not", {1, 2, 5, 6, 9}},
        {"words and chars on a mid-line side", {3, 4, 7, 8}},
        {"lines above and below", {10, 11, 12, 13, 14, 15}},
    };
    const auto heldOutShare = [&](const array<bool, FEATURES>& on) {
      vector<StateRow> masked = rows;
      for (StateRow& r : masked) {
        for (int i = 1; i < FEATURES; i++) {
          if (!on[i]) r.x[i] = 0;
        }
      }
      array<Features, FOLDS> held;
      for (int f = 0; f < FOLDS; f++) {
        held[f] = fitLeastSquares(masked, FEATURES, [f](const StateRow& r) { return r.edit % FOLDS != f; });
      }
      double left = 0;
      for (const StateRow& r : masked) {
        const double out = predict(held[r.edit % FOLDS], r);
        left += r.weight * (r.cost - out) * (r.cost - out);
      }
      return 100.0 * (1.0 - left / variance);
    };
    printf("    spread explained on held-out edits by each group of terms:\n");
    printf("      %-56s %6s %9s\n", "", "alone", "without");
    for (const Group& g : groups) {
      array<bool, FEATURES> only{}, rest;
      rest.fill(true);
      for (int i : g.terms) {
        only[i] = true;
        rest[i] = false;
      }
      printf("      %-56s %5.0f%% %8.0f%%\n", g.name, heldOutShare(only), heldOutShare(rest));
    }
  };
  printf("\n-- 2. How well each estimate tracks true remaining cost\n");
  explain(startRows, "start states");
  if (!midRows.empty()) explain(midRows, "states partway through the best answer");

  printf("\n-- 3. States partway through the best answer, by characters still to clear\n");
  printf("  %-6s | %7s %7s | %9s | %8s %8s\n", "chars", "linear", "sqrt", "true cost", "1 side",
         "2 sides");
  for (int b = 0; b < SIZES; b++) {
    if (midCost[b].weight == 0) continue;
    printf("  %-6s | %7.1f %7.1f | %9.2f | %8.2f %8.2f\n", SIZE_NAMES[b], midLinear[b].get(),
           midRoot[b].get(), midCost[b].get(), midOne[b].get(), midTwo[b].get());
  }

  printf("\n-- 4. Where the linear setting settled for a worse answer, and what would fix it\n");
  printf("   worse%% = share of starts, gap%% = how much worse they are.\n");
  printf("   Share of the lost effort by how the worse answer won:\n");
  printf("     outrank = its goal popped before the better path's states were looked at\n");
  printf("     hit-oth = a suffix-cache hit on another path settled the start first\n");
  printf("     hit-same = same first steps, but the cached ending was the worse one\n");
  printf("   need w / need ^ = median weight / exponent at which the better path gets looked at\n");
  printf("   (outrank and hit-oth only; -1 = no exponent is low enough)\n");
  printf("   defer ok = share of the hit-oth loss that goes away if a hit waits its turn at\n");
  printf("   the cost of the answer it implies, instead of settling the start when it pops\n");
  printf("  %-6s | %6s %6s | %7s %7s %8s %6s | %7s %7s %8s | %8s %8s %9s\n", "chars", "worse%",
         "gap%", "outrank", "hit-oth", "hit-same", "other", "need w", "need ^", "barrier%",
         "half ok%", "sqrt ok%", "defer ok%");
  for (int b = 0; b < SIZES; b++) {
    if (editsBySize[b] == 0) continue;
    const double all = max(EPS, reduce(lostEffort[b].begin(), lostEffort[b].end(), 0.0));
    printf("  %-6s | %6.1f %6.1f | %6.0f%% %6.0f%% %7.0f%% %5.0f%% | %7.2f %7.2f %8.0f | %8.0f %8.0f"
           " %9.0f\n",
           SIZE_NAMES[b], 100.0 * worseShare[b].get(), 100.0 * worseGap[b].get(),
           100.0 * lostEffort[b][OUTRANKED] / all, 100.0 * lostEffort[b][HIT_OTHER_PATH] / all,
           100.0 * lostEffort[b][HIT_SAME_PREFIX] / all, 100.0 * lostEffort[b][UNEXPLAINED] / all,
           weightedMedian(needWeight[b]), weightedMedian(needExponent[b]),
           100.0 * weightedMedian(barrierShare[b]),
           orderTotal[b] ? 100.0 * halfFixes[b] / orderTotal[b] : NAN,
           orderTotal[b] ? 100.0 * rootFixes[b] / orderTotal[b] : NAN,
           hitEffort[b] > 0 ? 100.0 * hitDeferrable[b] / hitEffort[b] : NAN);
  }

  printf("\n-- 5. Where the nodes go\n");
  for (int s = 0; s < ssize(SHAPES); s++) {
    printf("  %s (weight %.1f, exponent %.1f)\n", SHAPES[s].name, SHAPES[s].weight,
           SHAPES[s].exponent);
    printf("   repeat%% = expansions of a state the start had already expanded;"
           " same-dot = with the same dot register too\n");
    printf("  %-6s | %9s %8s | %6s %6s %6s %7s | %6s %7s %6s %9s | %7s %8s | %6s %6s %6s %6s\n",
           "chars", "nodes", "typical", "lost%", "rule%", "cap%", "unbegun", "cache%", "onpath%",
           "hog%", "pops/node", "repeat%", "same-dot", "d0", "d1", "d2", "d3+");
    for (int b = 0; b < SIZES; b++) {
      if (editsBySize[b] == 0) continue;
      printf("  %-6s | %9.0f %7.2fx | %6.1f %6.1f %6.1f %6.1f%% | %6.0f %7.0f %6.0f %9.1f |"
             " %6.0f%% %7.0f%% | %6.1f %6.1f %6.1f %6.1f\n",
             SIZE_NAMES[b], nodeSum[s][b], exp(logNodes[s][b].get()), 100.0 * lost[s][b].get(),
             100.0 * lostToRule[s][b].get(), 100.0 * lostToCap[s][b].get(),
             100.0 * unstarted[s][b].get(), 100.0 * byCache[s][b].get(),
             100.0 * useful[s][b].get(), 100.0 * hog[s][b].get(), popsPerNode[s][b].get(),
             100.0 * repeated[s][b] / max(1.0, expanded[s][b]),
             100.0 * repeatedSameDot[s][b] / max(1.0, expanded[s][b]),
             byDepth[s][b][0].get(), byDepth[s][b][1].get(), byDepth[s][b][2].get(),
             byDepth[s][b][3].get());
    }
  }
}

string shown(const string& s, size_t limit = 70) {
  string flat;
  for (char c : s) flat += c == '\n' ? string("\\n") : string(1, c);
  return flat.size() <= limit ? flat : flat.substr(0, limit) + "...";
}

void printEdit(int index, const CorpusEdit& e, const Anatomy& a) {
  printf("\n   corpus edit %d (%s%s): %d starts, %.0f chars to clear, typing costs %.1f\n", index,
         e.kind, e.heavy ? ", heavy" : "", a.starts, a.estimate, a.typed);
  printf("     delete: \"%s\"\n     insert: \"%s\"\n", shown(e.deleted).c_str(),
         shown(e.inserted).c_str());
}

void printEvents(const TracedRun& r, int start, double weight, double exponent, double typed,
                 int limit) {
  printf("       %-9s %5s %7s %6s %7s  %s\n", "event", "depth", "effort", "chars", "priority",
         "sequence");
  int shownRows = 0;
  for (int e : r.eventsByStart[start]) {
    const Traced& t = r.events[e];
    if (shownRows++ == limit) { printf("       ... %zu events in all\n", r.eventsByStart[start].size()); break; }
    const char* kind = t.event == TransformTraceEvent::Expand ? "expand"
                     : t.event == TransformTraceEvent::Goal ? "GOAL"
                     : t.event == TransformTraceEvent::CacheHit ? "cache-hit" : "CUT OFF";
    const bool goal = t.event == TransformTraceEvent::Goal;
    printf("       %-9s %5d %7.1f %6s %7.2f  %s\n", kind, t.depth, t.effort,
           goal ? "-" : to_string(t.chars()).c_str(),
           goal ? t.priority : t.effort + weight * t.shaped(exponent) + typed,
           VF::prettify(t.seq).c_str());
  }
}

// A start where the linear setting settled for a worse answer by pop order.
void traceOrdering(const vector<Problem>& problems, const Config& config, double minChars,
                   double maxChars, const char* title,
                   TransformTraceEvent settledBy = TransformTraceEvent::Goal) {
  int bestEdit = -1, bestStart = -1;
  double bestGap = 0;
  for (int p = 0; p < ssize(problems); p++) {
    if (problems[p].edit.heavy) continue;
    const Anatomy a = dissect(problems[p].diff, config);
    if (a.estimate < minChars || a.estimate > maxChars) continue;
    const TracedRun& lin = a.runs[LINEAR];
    for (int i = 0; i < a.starts; i++) {
      if (a.best[i] < 0 || lin.run.costByStart[i] < 0) continue;
      const int c = lin.closing(i);
      if (c < 0 || lin.events[c].event != settledBy) continue;
      if (a.source(i).pathStates(i).empty()) continue;
      if (a.source(i).seqByStart[i].starts_with(lin.events[c].seq)) continue;
      const double gap = lin.run.costByStart[i] - a.best[i];
      if (gap > bestGap + EPS) { bestGap = gap; bestEdit = p; bestStart = i; }
    }
  }
  printf("\n== %s\n", title);
  if (bestEdit < 0) { printf("   none found\n"); return; }
  const Anatomy a = dissect(problems[bestEdit].diff, config);
  const TracedRun& lin = a.runs[LINEAR];
  const TracedRun& src = a.source(bestStart);
  printEdit(bestEdit, problems[bestEdit].edit, a);
  const Traced* t0 = a.initial(bestStart);
  printf("   start %d: %d chars before the cursor, %d from it on\n", bestStart, t0->left,
         t0->chars() - t0->left);
  printf("     linear answer %-28s cost %.1f\n", VF::prettify(lin.seqByStart[bestStart]).c_str(),
         lin.run.costByStart[bestStart]);
  printf("     best answer   %-28s cost %.1f\n", VF::prettify(src.seqByStart[bestStart]).c_str(),
         a.best[bestStart]);
  printf("   what the linear search did for this start, in pop order:\n");
  printEvents(lin, bestStart, 1.0, 1.0, a.typed, 25);
  const double closing = lin.events[lin.closing(bestStart)].priority;
  printf("   the better path's states, and the priority each shape gives them\n");
  printf("   (they are only looked at if that priority is below %.2f):\n", closing);
  printf("       %7s %6s | %8s %8s %8s  %s\n", "effort", "chars", "linear", "half", "sqrt",
         "sequence");
  const vector<const Traced*> path = src.pathStates(bestStart);
  for (const Traced* t : path) {
    printf("       %7.1f %6d | %8.2f %8.2f %8.2f  %s\n", t->effort, t->chars(),
           t->effort + t->shaped(1.0) + a.typed, t->effort + 0.5 * t->shaped(1.0) + a.typed,
           t->effort + t->shaped(0.5) + a.typed, VF::prettify(t->seq).c_str());
  }
  printf("   needed weight %.2f, needed exponent %.2f\n", neededWeight(path, closing, a.typed),
         neededExponent(path, closing, a.typed));
  for (int s = 1; s < ssize(SHAPES); s++) {
    printf("   %-6s gives %-28s cost %.1f, %ld nodes for the edit (linear: %ld)\n", SHAPES[s].name,
           VF::prettify(a.runs[s].seqByStart[bestStart]).c_str(),
           a.runs[s].run.costByStart[bestStart], a.runs[s].run.nodes, lin.run.nodes);
  }
}

// How one edit's search spends its nodes under each shape.
void traceWork(int index, const Problem& problem, const Config& config) {
  const Anatomy a = dissect(problem.diff, config);
  printEdit(index, problem.edit, a);
  for (int s = 0; s < ssize(SHAPES); s++) {
    const TracedRun& r = a.runs[s];
    int solved = 0, solvable = 0, ruled = 0, hardest = 0, viaCache = 0;
    array<long, 4> depths{};
    vector<int> solvedNodes, lostNodes;
    long kept = 0, expansions = 0;
    for (int i = 0; i < a.starts; i++) {
      if (a.best[i] < 0) continue;
      solvable++;
      int nodes = 0;
      for (int e : r.eventsByStart[i]) {
        if (r.events[e].event == TransformTraceEvent::WorkCutoff) continue;
        nodes++;
        depths[min(3, r.events[e].depth)]++;
      }
      expansions += nodes;
      kept += r.onPath(i);
      if (r.run.costByStart[i] >= 0) {
        solved++;
        solvedNodes.push_back(nodes);
        hardest = max(hardest, nodes);
        const int c = r.closing(i);
        viaCache += c >= 0 && r.events[c].event == TransformTraceEvent::CacheHit;
      } else {
        lostNodes.push_back(nodes);
        ruled += r.cutOff(i);
      }
    }
    ranges::sort(solvedNodes);
    ranges::sort(lostNodes);
    const auto mid = [](const vector<int>& v) { return v.empty() ? 0 : v[v.size() / 2]; };
    printf("   %-6s %6ld nodes, stop=%s | solved %d of %d (%d by cache hit), %d cut by the work"
           " rule\n", SHAPES[s].name, r.run.nodes, to_string(r.stop).c_str(), solved, solvable,
           viaCache, ruled);
    printf("          nodes per solved start: median %d, most %d | per lost start: median %d, most"
           " %d\n", mid(solvedNodes), hardest, mid(lostNodes),
           lostNodes.empty() ? 0 : lostNodes.back());
    printf("          nodes by depth: %ld / %ld / %ld / %ld+ | on the way to an answer: %.0f%%\n",
           depths[0], depths[1], depths[2], depths[3],
           expansions ? 100.0 * kept / expansions : 0.0);
  }
}

// What the start that used the most nodes under `shape` spent them on.
void traceHog(int index, const Problem& problem, const Config& config, int shape) {
  const Anatomy a = dissect(problem.diff, config);
  const TracedRun& r = a.runs[shape];
  int hog = 0;
  for (int i = 0; i < a.starts; i++) {
    if (r.eventsByStart[i].size() > r.eventsByStart[hog].size()) hog = i;
  }
  printEdit(index, problem.edit, a);
  const vector<int>& events = r.eventsByStart[hog];
  const int c = r.closing(hog);
  printf("   %s: start %d used %zu of the edit's %ld nodes (%d pops in all)\n", SHAPES[shape].name,
         hog, events.size(), r.run.nodes, r.pops);
  printf("     its answer: %s cost %.1f (linear: %s cost %.1f)\n",
         VF::prettify(r.seqByStart[hog]).c_str(), r.run.costByStart[hog],
         VF::prettify(a.runs[LINEAR].seqByStart[hog]).c_str(),
         a.runs[LINEAR].run.costByStart[hog]);
  if (c >= 0) printf("     settled at priority %.2f\n", r.events[c].priority);

  map<int, int> byDepth, byChars, byLines;
  map<string, int> byLastKey;
  double minPriority = 1e18, maxPriority = 0;
  for (int e : events) {
    const Traced& t = r.events[e];
    if (t.event != TransformTraceEvent::Expand) continue;
    byDepth[min(t.depth, 12)]++;
    byChars[sizeOf(t.chars())]++;
    byLines[ssize(t.cols)]++;
    minPriority = min(minPriority, t.priority);
    maxPriority = max(maxPriority, t.priority);
    if (!t.seq.empty()) byLastKey[t.seq.substr(t.seq.size() - 1)]++;
  }
  printf("     priorities of expanded states: %.2f .. %.2f\n", minPriority, maxPriority);
  printf("     expanded states by commands so far:");
  for (const auto& [d, n] : byDepth) printf(" %d:%d", d, n);
  printf("\n     by characters still to clear:");
  for (const auto& [b, n] : byChars) printf(" %s:%d", SIZE_NAMES[b], n);
  printf("\n     by lines left:");
  for (const auto& [l, n] : byLines) printf(" %d:%d", l, n);
  printf("\n     by last key of the sequence:");
  for (const auto& [k, n] : byLastKey) printf(" %s:%d", VF::prettify(k).c_str(), n);
  printf("\n     every %zuth expanded state:\n", max<size_t>(1, events.size() / 30));
  printf("       %7s %6s %5s %8s  %s\n", "effort", "chars", "lines", "priority", "sequence");
  for (size_t k = 0; k < events.size(); k += max<size_t>(1, events.size() / 30)) {
    const Traced& t = r.events[events[k]];
    printf("       %7.1f %6d %5zu %8.2f  %s\n", t.effort, t.chars(), t.cols.size(), t.priority,
           VF::prettify(t.seq).c_str());
  }
}

// ---- Policy: which weight an edit should get ------------------------------
// Every edit is searched once per grid weight. Any rule mapping an edit to a
// weight is then scored by lookup, so rules can be fitted, checked on the
// other half of the history and resampled without searching again.

constexpr array<double, 9> GRID{1.0, 0.7, 0.5, 0.35, 0.25, 0.18, 0.12, 0.08, 0.05};
constexpr int WEIGHTS = 9;
constexpr int WEIGHT_ONE = 0;
// Nodes one is willing to search to save one unit of effort.
constexpr array<double, 6> RATES{1e3, 3e3, 1e4, 3e4, 1e5, 1e12};
constexpr array<const char*, 6> RATE_NAMES{"1k", "3k", "10k", "30k", "100k", "any"};
const vector<int> SHOWN_RATES{2, 4};
constexpr array<const char*, 7> KINDS{"del-part", "del-words", "del-lines", "change-word",
                                      "change-words", "change-lines", "change-reflow"};

// What a rule may look at: all of it known before the search starts.
enum Explainer {
  SIZE, LINES, WORDS, CHARS_PER_WORD, INSERTED, STARTS, PURE_DELETION, FROM_LINE_START,
  TO_LINE_END, WHOLE_LINES, AFTER_INDENT, CHARS_BEFORE, CHARS_AFTER, EXPLAINERS
};
constexpr array<const char*, EXPLAINERS> EXPLAINER_NAMES{
    "size N", "lines touched", "words to clear", "chars per word", "chars to type",
    "start positions", "nothing to type", "starts at a line start", "runs to a line end",
    "covers whole lines", "starts at column 0 or after indent", "chars before it on its line",
    "chars after it on its line"};

struct Response {
  double weight = 1;  // history edits this one stands for
  int half = 0, kind = 0;
  array<double, EXPLAINERS> x{};
  // Best known effort of clearing the region per unit of N, typing excluded.
  // NAN where the typing effort is unknown: changes, without the trace sink.
  double price = NAN;
  array<double, 3> predicted{};  // price from a model fitted on half A, half B, everything
  vector<double> best;                  // per start answered at weight 1
  vector<array<double, WEIGHTS>> cost;  // negative = unanswered at that weight
  array<double, WEIGHTS> searched{}, spent{};  // nodes and ms of each search alone
  // A start the search leaves unanswered takes the weight-1 answer, and the
  // edit pays for the weight-1 search as well.
  array<double, WEIGHTS> loss{}, gap{}, fallback{}, nodes{}, ms{};
  // The same for MORE_WEIGHTS x MORE_ANSWERS: one search asked for several
  // answers per start, the best one kept.
  array<array<double, 4>, 3> moreLoss{}, moreNodes{}, moreMs{};
  // Per start at each of MORE_WEIGHTS, from the trace: what settled it, and how
  // far the answer's cost was above the priority its state popped at.
  vector<array<int, 3>> how;
  vector<array<double, 3>> early;
};
constexpr array<int, 3> MORE_WEIGHTS{0, 3, 6};
constexpr array<int, 4> MORE_ANSWERS{1, 2, 4, 8};
enum How { GOAL_POPPED, REMEMBERED_ENDING, UNANSWERED, HOWS };
constexpr array<const char*, HOWS> HOW_NAMES{"a popped goal", "a remembered ending", "nothing"};

constexpr int PRICE_TERMS = 11;
constexpr array<const char*, PRICE_TERMS> PRICE_TERM_NAMES{
    "constant", "log N", "log lines", "starts at a line start", "runs to a line end",
    "covers whole lines", "    and more than one line", "log(1 + words)", "log chars per word",
    "nothing to type", "log(1 + chars to type)"};
Features priceTerms(const Response& r) {
  const bool whole = r.x[WHOLE_LINES] > 0;
  return {1.0, log(max(1.0, r.x[SIZE])), log(r.x[LINES]), r.x[FROM_LINE_START], r.x[TO_LINE_END],
          double(whole), double(whole && r.x[LINES] > 1), log1p(r.x[WORDS]),
          log(max(1.0, r.x[CHARS_PER_WORD])), r.x[PURE_DELETION], log1p(r.x[INSERTED])};
}

// Least squares for log(price). fold: -1 = every edit, else only that half.
Features fitPrice(const vector<Response>& rows, int fold) {
  struct Row {
    double weight, cost;
    Features x;
  };
  vector<Row> data;
  for (const Response& r : rows) {
    if ((fold < 0 || r.half == fold) && isfinite(r.price)) {
      data.push_back({r.weight, log(r.price), priceTerms(r)});
    }
  }
  return fitLeastSquares(data, PRICE_TERMS, [](const Row&) { return true; });
}

double predictPrice(const Features& beta, const Response& r) {
  const Features x = priceTerms(r);
  double y = 0;
  for (int i = 0; i < PRICE_TERMS; i++) y += beta[i] * x[i];
  return exp(y);
}

vector<Response> respond(const Config& config, const vector<Problem>& problems) {
  const int deepPops = getenv("VIMFY_DEEP_POPS") ? atoi(getenv("VIMFY_DEEP_POPS")) : 400000;
  const auto params = [](double weight) {
    return TransformOptimizerParams{}.withMaxResults(ALL_STARTS).withDistanceWeight(weight);
  };
  vector<Response> rows;
  int unanswered = 0, deepened = 0;
  const bool tracing =
      !problems.empty() &&
      !runTraced(problems[0].diff, config, params(1.0).withMaxNodesPopped(1)).events.empty();
  for (const Problem& p : problems) {
    const DiffState& d = p.diff;
    Response r;
    array<Run, WEIGHTS> runs;
    for (int g = 0; g < WEIGHTS; g++) {
      const auto began = chrono::steady_clock::now();
      runs[g] = run(d, config, params(GRID[g]));
      r.spent[g] = chrono::duration<double, milli>(chrono::steady_clock::now() - began).count();
      r.searched[g] = runs[g].nodes;
    }
    vector<double> best =
        run(d, config, TransformOptimizerParams::dijkstra(ALL_STARTS, REFERENCE_POP_CAP)).costByStart;
    const auto lower = [&](const Run& other) {
      for (size_t i = 0; i < best.size(); i++) {
        const double c = other.costByStart[i];
        if (c >= 0 && (best[i] < 0 || c < best[i])) best[i] = c;
      }
    };
    for (const Run& other : runs) lower(other);
    const Run& anchor = runs[WEIGHT_ONE];
    array<array<Run, 4>, 3> more;
    for (int w = 0; w < 3; w++) {
      more[w][0] = runs[MORE_WEIGHTS[w]];
      r.moreMs[w][0] = r.spent[MORE_WEIGHTS[w]];
      for (int k = 1; k < 4; k++) {
        const auto began = chrono::steady_clock::now();
        more[w][k] = run(d, config, params(GRID[MORE_WEIGHTS[w]])
                                        .withMaxResultsPerStartPos(MORE_ANSWERS[k]));
        r.moreMs[w][k] = chrono::duration<double, milli>(chrono::steady_clock::now() - began).count();
        lower(more[w][k]);
      }
    }
    // Where the pop cap cut a search short, the same search with room to
    // finish is the better reference.
    bool cut = false;
    for (const Run& other : runs) {
      for (size_t i = 0; i < best.size(); i++) cut |= other.costByStart[i] < 0 && anchor.costByStart[i] >= 0;
    }
    if (cut) {
      deepened++;
      for (double weight : {0.25, 0.08}) lower(run(d, config, params(weight).withMaxNodesPopped(deepPops)));
    }

    double bestSum = 0;
    array<double, WEIGHTS> fell{};
    for (size_t i = 0; i < best.size(); i++) {
      if (anchor.costByStart[i] < 0) { unanswered += best[i] >= 0; continue; }
      bestSum += best[i];
      r.best.push_back(best[i]);
      r.cost.emplace_back();
      for (int g = 0; g < WEIGHTS; g++) {
        double c = runs[g].costByStart[i];
        r.cost.back()[g] = c;
        if (c < 0) { c = anchor.costByStart[i]; fell[g]++; }
        r.loss[g] += c - best[i];
        r.gap[g] += best[i] > EPS ? c / best[i] - 1.0 : 0.0;
      }
    }
    const int starts = r.best.size();
    if (starts == 0) continue;
    for (int g = 0; g < WEIGHTS; g++) {
      r.loss[g] /= starts;
      r.gap[g] /= starts;
      r.fallback[g] = fell[g] / starts;
      r.nodes[g] = r.searched[g] + (fell[g] > 0 ? r.searched[WEIGHT_ONE] : 0);
      r.ms[g] = r.spent[g] + (fell[g] > 0 ? r.spent[WEIGHT_ONE] : 0);
    }
    for (int w = 0; w < 3; w++) {
      for (int k = 0; k < 4; k++) {
        bool fellBack = false;
        int at = 0;
        for (size_t i = 0; i < best.size(); i++) {
          if (anchor.costByStart[i] < 0) continue;
          double c = more[w][k].costByStart[i];
          if (c < 0) { c = anchor.costByStart[i]; fellBack = true; }
          r.moreLoss[w][k] += (c - r.best[at++]) / starts;
        }
        r.moreNodes[w][k] = more[w][k].nodes + (fellBack ? r.searched[WEIGHT_ONE] : 0);
        if (fellBack) r.moreMs[w][k] += r.spent[WEIGHT_ONE];
      }
    }
    if (tracing) {
      r.how.resize(starts);
      r.early.resize(starts);
      for (int w = 0; w < 3; w++) {
        const TracedRun traced = runTraced(d, config, params(GRID[MORE_WEIGHTS[w]]));
        int at = 0;
        for (size_t i = 0; i < best.size(); i++) {
          if (anchor.costByStart[i] < 0) continue;
          const double cost = traced.run.costByStart[i];
          EXPECT_NEAR(cost, runs[MORE_WEIGHTS[w]].costByStart[i], 1e-6);
          const int closing = traced.closing(i);
          const bool ending =
              closing >= 0 && traced.events[closing].event == TransformTraceEvent::CacheHit;
          r.how[at][w] = cost < 0 ? UNANSWERED : ending ? REMEMBERED_ENDING : GOAL_POPPED;
          r.early[at][w] = cost >= 0 && closing >= 0 ? cost - traced.events[closing].priority : 0.0;
          at++;
        }
      }
    }

    const double breaks = ranges::count(d.deletedText, '\n');
    int words = 0, before = 0;
    for (unsigned char ch : d.deletedText) {
      const int cls = ch == '\n' || ch == ' ' || ch == '\t' ? 0
                    : isalnum(ch) || ch == '_' || ch >= 0x80 ? 1 : 2;
      words += cls != 0 && cls != before;
      before = cls;
    }
    r.weight = p.edit.weight;
    r.half = p.half;
    r.kind = ranges::find(KINDS, string_view(kindOf(d)), [](const char* k) { return string_view(k); }) -
             KINDS.begin();
    r.x[SIZE] = regionSize(d);
    r.x[LINES] = breaks + 1;
    r.x[WORDS] = words;
    r.x[CHARS_PER_WORD] = (d.deletedText.size() - breaks) / max(1, words);
    r.x[INSERTED] = d.insertedText.size();
    r.x[STARTS] = starts;
    r.x[PURE_DELETION] = d.isPureDeletion();
    r.x[FROM_LINE_START] = d.boundary.leftColOffset() == 0;
    r.x[TO_LINE_END] = d.boundary.rightColOffset() == 0;
    r.x[WHOLE_LINES] = r.x[FROM_LINE_START] > 0 && r.x[TO_LINE_END] > 0;
    r.x[AFTER_INDENT] = ranges::all_of(d.boundary.prefix(), [](char c) { return c == ' ' || c == '\t'; });
    r.x[CHARS_BEFORE] = d.boundary.leftColOffset();
    r.x[CHARS_AFTER] = d.boundary.rightColOffset();

    double typed = d.isPureDeletion() ? 0.0 : NAN;
    if (!d.isPureDeletion()) {
      const TracedRun probe = runTraced(d, config, params(1.0).withMaxNodesPopped(1));
      if (!probe.events.empty()) {
        typed = probe.events.front().priority - probe.events.front().shaped(1.0);
      }
    }
    if (isfinite(typed)) r.price = max(0.01, (bestSum / starts - typed) / max(1.0, r.x[SIZE]));
    rows.push_back(std::move(r));
  }
  for (int which = 0; which < 3; which++) {
    const Features beta = fitPrice(rows, which < 2 ? which : -1);
    for (Response& r : rows) r.predicted[which] = predictPrice(beta, r);
  }
  printf("   %d edits hit the pop cap at some weight and got a deeper reference (%d pops);"
         " %d starts are answered only below weight 1 and are left out\n",
         deepened, deepPops, unanswered);
  return rows;
}

struct Outcome {
  double edits = 0, loss = 0, gap = 0, fallback = 0, nodes = 0, ms = 0, slowest = 0;
  void add(const Response& r, int g) {
    edits += r.weight;
    loss += r.weight * r.loss[g];
    gap += r.weight * r.gap[g];
    fallback += r.weight * r.fallback[g];
    nodes += r.weight * r.nodes[g];
    ms += r.weight * r.ms[g];
    slowest = max(slowest, r.ms[g]);
  }
  // What a rule is fitted to minimise.
  double objective(double rate) const { return loss + nodes / rate; }
};

using Rule = function<int(const Response&)>;
using Keep = function<bool(const Response&)>;
using Value = function<double(const Response&)>;
struct Fitted {
  Rule rule;
  string label;
};

Rule flat(int g) {
  return [g](const Response&) { return g; };
}

// fold: -1 = every edit, else only that half.
Outcome score(const vector<Response>& rows, const Rule& rule, int fold) {
  Outcome o;
  for (const Response& r : rows) {
    if (fold < 0 || r.half == fold) o.add(r, rule(r));
  }
  return o;
}

vector<int> apply(const vector<Response>& rows, const Rule& rule) {
  vector<int> choice;
  for (const Response& r : rows) choice.push_back(rule(r));
  return choice;
}

Outcome tally(const vector<Response>& rows, const vector<int>& choice, const Keep& keep) {
  Outcome o;
  for (size_t i = 0; i < rows.size(); i++) {
    if (keep(rows[i])) o.add(rows[i], choice[i]);
  }
  return o;
}

int snap(double weight) {
  int nearest = 0;
  for (int g = 1; g < WEIGHTS; g++) {
    if (abs(log(GRID[g] / weight)) < abs(log(GRID[nearest] / weight))) nearest = g;
  }
  return nearest;
}

struct Chooser {
  const vector<Response>& rows;
  int fold;
  double rate;
  Fitted best{};
  double cost = 1e300;

  void offer(Rule rule, string label) {
    const double c = score(rows, rule, fold).objective(rate);
    if (c < cost - 1e-12) {
      cost = c;
      best = {std::move(rule), std::move(label)};
    }
  }
};

enum Family { FLAT, POWER, BANDS, EVERY_PROPERTY, PRICE_FITTED, PRICE_TRUE, PER_EDIT, FAMILIES };
constexpr array<const char*, FAMILIES> FAMILY_NAMES{
    "one weight", "a * N^-b", "one weight per size band", "fitted on every property",
    "c * predicted price", "c * true price", "best weight per edit"};

// weight = exp(beta . priceTerms), fitted to loss + nodes / rate itself by
// coordinate descent from two starts: least squares on each edit's best weight
// (weighted by what the edit has at stake), and the best single weight.
Features fitWeightModel(const vector<Response>& rows, int fold, double rate) {
  struct Row {
    double weight, cost;
    Features x;
  };
  const auto objective = [rate](const Response& r, int g) { return r.loss[g] + r.nodes[g] / rate; };
  vector<Row> data;
  vector<const Response*> used;
  array<double, WEIGHTS> flatCost{};
  for (const Response& r : rows) {
    if (fold >= 0 && r.half != fold) continue;
    int pick = 0;
    double worst = objective(r, 0);
    for (int g = 0; g < WEIGHTS; g++) {
      if (objective(r, g) < objective(r, pick) - 1e-12) pick = g;
      worst = max(worst, objective(r, g));
      flatCost[g] += r.weight * objective(r, g);
    }
    data.push_back({r.weight * (worst - objective(r, pick) + 1e-6), log(GRID[pick]), priceTerms(r)});
    used.push_back(&r);
  }
  const auto total = [&](const Features& b) {
    double sum = 0;
    for (const Response* r : used) {
      sum += r->weight * objective(*r, snap(clamp(predictPrice(b, *r), GRID.back(), 1.0)));
    }
    return sum;
  };
  const auto descend = [&](Features beta) {
    double best = total(beta);
    for (double step : {0.4, 0.2, 0.1, 0.05}) {
      for (int pass = 0; pass < 3; pass++) {
        for (int i = 0; i < PRICE_TERMS; i++) {
          for (double delta : {step, -step}) {
            Features trial = beta;
            trial[i] += delta;
            const double t = total(trial);
            if (t < best - 1e-12) {
              best = t;
              beta = trial;
            }
          }
        }
      }
    }
    return pair{best, beta};
  };
  Features single{};
  single[0] = log(GRID[ranges::min_element(flatCost) - flatCost.begin()]);
  const auto fromFit = descend(fitLeastSquares(data, PRICE_TERMS, [](const Row&) { return true; }));
  const auto fromSingle = descend(single);
  return fromFit.first < fromSingle.first ? fromFit.second : fromSingle.second;
}

Fitted fitFamily(Family family, const vector<Response>& rows, int fold, double rate) {
  Chooser c{rows, fold, rate};
  for (int g = 0; g < WEIGHTS; g++) c.offer(flat(g), format("{:.2f}", GRID[g]));
  if (family == EVERY_PROPERTY) {
    const Features beta = fitWeightModel(rows, fold, rate);
    c.offer([beta](const Response& r) { return snap(clamp(predictPrice(beta, r), GRID.back(), 1.0)); },
            "see the coefficients below");
  }
  if (family == POWER) {
    for (double a : {0.5, 0.7, 1.0, 1.4, 2.0, 2.8, 4.0}) {
      for (int tenths = 1; tenths <= 9; tenths++) {
        for (double floor : {0.05, 0.08, 0.12, 0.18}) {
          const double b = 0.1 * tenths;
          c.offer([=](const Response& r) {
                    return snap(clamp(a * pow(max(1.0, r.x[SIZE]), -b), floor, 1.0));
                  },
                  format("{:.1f} * N^-{:.1f}, at least {:.2f}", a, b, floor));
        }
      }
    }
  }
  if (family == BANDS) {
    array<array<double, WEIGHTS>, SIZES> cost{};
    for (const Response& r : rows) {
      if (fold >= 0 && r.half != fold) continue;
      for (int g = 0; g < WEIGHTS; g++) {
        cost[sizeOf(r.x[SIZE])][g] += r.weight * (r.loss[g] + r.nodes[g] / rate);
      }
    }
    array<int, SIZES> pick{};
    string label;
    for (int b = 0; b < SIZES; b++) {
      pick[b] = ranges::min_element(cost[b]) - cost[b].begin();
      label += format("{:g} ", GRID[pick[b]]);
    }
    c.offer([pick](const Response& r) { return pick[sizeOf(r.x[SIZE])]; }, label);
  }
  if (family == PRICE_FITTED || family == PRICE_TRUE) {
    const int model = fold < 0 ? 2 : fold;
    for (double factor : {0.25, 0.35, 0.5, 0.7, 1.0, 1.4, 2.0, 2.8, 4.0, 5.6, 8.0, 11.0, 16.0}) {
      c.offer([=](const Response& r) {
                const double price = family == PRICE_TRUE ? r.price : r.predicted[model];
                return snap(clamp(factor * price, GRID.back(), 1.0));
              },
              format("{:.2f} * price", factor));
    }
  }
  // Not a rule anyone could run: it reads the edit's own results. It bounds
  // what any rule choosing one weight per edit can reach.
  if (family == PER_EDIT) {
    c.offer([rate](const Response& r) {
              int pick = 0;
              for (int g = 1; g < WEIGHTS; g++) {
                if (r.loss[g] + r.nodes[g] / rate < r.loss[pick] + r.nodes[pick] / rate - 1e-12) pick = g;
              }
              return pick;
            },
            "-");
  }
  return c.best;
}

// Two weights, split at one value of a property.
Fitted fitSplit(const vector<Response>& rows, int fold, double rate, Value value) {
  Chooser c{rows, fold, rate};
  vector<double> seen;
  for (const Response& r : rows) {
    if (fold < 0 || r.half == fold) seen.push_back(value(r));
  }
  ranges::sort(seen);
  vector<double> cuts;
  for (int q = 1; q < 10 && !seen.empty(); q++) cuts.push_back(seen[seen.size() * q / 10]);
  cuts.erase(ranges::unique(cuts).begin(), cuts.end());
  for (int g = 0; g < WEIGHTS; g++) c.offer(flat(g), format("{:.2f}", GRID[g]));
  for (double cut : cuts) {
    for (int lo = 0; lo < WEIGHTS; lo++) {
      for (int hi = 0; hi < WEIGHTS; hi++) {
        if (lo == hi) continue;
        c.offer([=](const Response& r) { return value(r) < cut ? lo : hi; },
                format("below {:.3g}: {:.2f}, else {:.2f}", cut, GRID[lo], GRID[hi]));
      }
    }
  }
  return c.best;
}

// Each edit's weight under a rule fitted on the other half.
template<class FitOn>
vector<int> heldOut(const vector<Response>& rows, FitOn&& fitOn) {
  vector<int> choice(rows.size());
  for (int fold : {0, 1}) {
    const Fitted f = fitOn(fold);
    for (size_t i = 0; i < rows.size(); i++) {
      if (rows[i].half != fold) choice[i] = f.rule(rows[i]);
    }
  }
  return choice;
}

// Weighted mean of per-edit values with a 95% interval from resampling edits.
string interval(const vector<pair<double, double>>& valueAndWeight) {
  if (valueAndWeight.empty()) return "-";
  double sum = 0, weight = 0;
  for (const auto& [v, w] : valueAndWeight) { sum += w * v; weight += w; }
  mt19937_64 rng(20261124);
  uniform_int_distribution<size_t> any(0, valueAndWeight.size() - 1);
  vector<double> means;
  for (int rep = 0; rep < 2000; rep++) {
    double s = 0, total = 0;
    for (size_t k = 0; k < valueAndWeight.size(); k++) {
      const auto& [v, w] = valueAndWeight[any(rng)];
      s += w * v;
      total += w;
    }
    means.push_back(s / total);
  }
  ranges::sort(means);
  return format("{:6.3f} [{:6.3f}, {:6.3f}]", sum / weight, means[50], means[1949]);
}

// Searches the rungs in order within `budget` nodes per edit; each start keeps
// its best answer so far. A search that does not fit uses the budget up for
// nothing. The first rung is weight 1, which always answers.
struct Climbed {
  double loss = 0, gap = 0, nodes = 0, ms = 0;
};
Climbed climbOne(const Response& r, const vector<int>& rungs, double budget) {
  Climbed out;
  vector<double> answer(r.best.size(), -1.0);
  for (int g : rungs) {
    if (out.nodes > 0 && out.nodes + r.searched[g] > budget) {
      if (out.nodes < budget) {
        out.ms += r.spent[g] * (budget - out.nodes) / max(1.0, r.searched[g]);
        out.nodes = budget;
      }
      break;
    }
    out.nodes += r.searched[g];
    out.ms += r.spent[g];
    for (size_t i = 0; i < answer.size(); i++) {
      const double c = r.cost[i][g];
      if (c >= 0 && (answer[i] < 0 || c < answer[i])) answer[i] = c;
    }
  }
  for (size_t i = 0; i < answer.size(); i++) {
    out.loss += (answer[i] - r.best[i]) / answer.size();
    out.gap += (r.best[i] > EPS ? answer[i] / r.best[i] - 1.0 : 0.0) / answer.size();
  }
  return out;
}

Outcome climb(const vector<Response>& rows, const vector<int>& rungs, double budget,
              const Keep& keep) {
  Outcome o;
  for (const Response& r : rows) {
    if (!keep(r)) continue;
    const Climbed c = climbOne(r, rungs, budget);
    o.edits += r.weight;
    o.loss += r.weight * c.loss;
    o.gap += r.weight * c.gap;
    o.nodes += r.weight * c.nodes;
    o.ms += r.weight * c.ms;
    o.slowest = max(o.slowest, c.ms);
  }
  return o;
}

struct Comparison {
  array<array<Fitted, RATES.size()>, FAMILIES> fitted;
  array<array<vector<int>, RATES.size()>, FAMILIES> held;
};

// Fits every family at each rate and prints one row per fit.
Comparison compare(const vector<Response>& rows, const vector<Family>& families,
                   const vector<int>& rates) {
  const Keep all = [](const Response&) { return true; };
  const double baseNodes = score(rows, flat(WEIGHT_ONE), -1).nodes;
  Comparison out;
  printf("  %-5s %-24s %-32s | %7s %6s %8s | %8s %6s %8s\n", "rate", "rule", "chosen", "loss",
         "work", "captured", "held out", "work", "captured");
  for (int k : rates) {
    for (Family f : families) {
      out.fitted[f][k] = fitFamily(f, rows, -1, RATES[k]);
      out.held[f][k] = heldOut(rows, [&](int fold) { return fitFamily(f, rows, fold, RATES[k]); });
    }
    const auto objective = [&](const vector<int>& choice) {
      return tally(rows, choice, all).objective(RATES[k]);
    };
    const double one = objective(apply(rows, out.fitted[FLAT][k].rule));
    const double oneHeld = objective(out.held[FLAT][k]);
    const double each = objective(apply(rows, out.fitted[PER_EDIT][k].rule));
    for (Family f : families) {
      const Outcome in = tally(rows, apply(rows, out.fitted[f][k].rule), all);
      const Outcome held = tally(rows, out.held[f][k], all);
      printf("  %-5s %-24s %-32s | %7.3f %5.2fx %7.0f%% |", RATE_NAMES[k], FAMILY_NAMES[f],
             out.fitted[f][k].label.c_str(), in.loss / in.edits, in.nodes / baseNodes,
             100 * (one - in.objective(RATES[k])) / max(EPS, one - each));
      if (f == PER_EDIT) {
        printf(" %8s %6s %8s\n", "-", "-", "-");
      } else {
        printf(" %8.3f %5.2fx %7.0f%%\n", held.loss / held.edits, held.nodes / baseNodes,
               100 * (oneHeld - held.objective(RATES[k])) / max(EPS, oneHeld - each));
      }
    }
  }
  return out;
}

void policy(const char* label, const vector<Response>& rows) {
  const Keep all = [](const Response&) { return true; };
  const vector<int> ones(rows.size(), WEIGHT_ONE);
  const Outcome base = tally(rows, ones, all);
  const auto perEdit = [](double v, const Outcome& o) { return o.edits ? v / o.edits : NAN; };
  const auto inBand = [](int b) -> Keep {
    return [b](const Response& r) { return b == SIZES || sizeOf(r.x[SIZE]) == b; };
  };
  double msTotal = 0, nodeTotal = 0;
  for (const Response& r : rows) {
    msTotal += reduce(r.spent.begin(), r.spent.end());
    nodeTotal += reduce(r.searched.begin(), r.searched.end());
  }

  printf("\n== policy / %s: %zu edits standing for %.0f\n", label, rows.size(), base.edits);
  printf("   loss = effort above the best known answer: mean over an edit's starts, then over edits\n"
         "   work = nodes relative to weight 1; 1,000 nodes take about %.1f ms in this build\n"
         "   fallback = starts the search left unanswered; they take the weight-1 answer and the\n"
         "   edit pays for the weight-1 search too\n"
         "   short/none = edits with a start left unanswered / with every start left unanswered\n",
         1000 * msTotal / max(1.0, nodeTotal));

  printf("\n-- A. One weight for every edit\n");
  printf("  %6s | %7s %6s %9s %10s | %6s %10s %8s %7s %7s %8s | %s\n", "weight", "loss", "gap%",
         "fallback%", "short/none", "work", "nodes/edit", "ms/edit", "p99 ms", "max ms", ">100 ms",
         "ms per unit of effort saved, from the row above");
  Outcome previous;
  for (int g = 0; g < WEIGHTS; g++) {
    const Outcome o = tally(rows, vector<int>(rows.size(), g), all);
    vector<double> times;
    double slow = 0, shortOf = 0, none = 0;
    for (const Response& r : rows) {
      times.push_back(r.ms[g]);
      slow += r.weight * (r.ms[g] > 100);
      shortOf += r.weight * (r.fallback[g] > 0);
      none += r.weight * (r.fallback[g] > 1 - EPS);
    }
    ranges::sort(times);
    printf("  %6.2f | %7.3f %6.2f %9.2f %10s | %5.2fx %10.0f %8.2f %7.0f %7.0f %8.0f |", GRID[g],
           perEdit(o.loss, o), 100 * perEdit(o.gap, o), 100 * perEdit(o.fallback, o),
           format("{:.0f}/{:.0f}", shortOf, none).c_str(),
           o.nodes / base.nodes, perEdit(o.nodes, o), perEdit(o.ms, o),
           times[times.size() * 99 / 100], o.slowest, slow);
    if (g > 0 && previous.loss - o.loss > EPS) printf(" %.0f\n", (o.ms - previous.ms) / (previous.loss - o.loss));
    else printf(" -\n");
    previous = o;
  }

  printf("\n-- B. Where the loss sits at weight 1\n");
  const auto breakdown = [&](const char* title, int groups, const Rule& groupOf,
                             const function<const char*(int)>& nameOf) {
    vector<Outcome> by(groups);
    vector<double> depends(groups, 0.0);
    for (const Response& r : rows) {
      const int k = groupOf(r);
      by[k].add(r, WEIGHT_ONE);
      depends[k] += r.weight * (ranges::max(r.loss) - ranges::min(r.loss) > EPS);
    }
    printf("  %-14s %6s | %9s %7s %6s | %7s | %s\n", title, "edits", "loss/edit", "loss %",
           "gap%", "nodes %", "answer depends on the weight %");
    for (int k = 0; k < groups; k++) {
      if (by[k].edits == 0) continue;
      printf("  %-14s %6.0f | %9.3f %7.1f %6.2f | %7.1f | %6.1f\n", nameOf(k), by[k].edits,
             perEdit(by[k].loss, by[k]), 100 * by[k].loss / max(EPS, base.loss),
             100 * perEdit(by[k].gap, by[k]), 100 * by[k].nodes / base.nodes,
             100 * depends[k] / by[k].edits);
    }
  };
  breakdown("size", SIZES, [](const Response& r) { return sizeOf(r.x[SIZE]); },
            [](int b) { return SIZE_NAMES[b]; });
  breakdown("kind", ssize(KINDS), [](const Response& r) { return r.kind; },
            [](int k) { return KINDS[k]; });

  printf("\n-- C. Each size band at each weight\n");
  const auto surface = [&](const char* title, auto&& value) {
    printf("   %s\n  %-8s %6s", title, "size", "edits");
    for (double w : GRID) printf(" %6.2f", w);
    printf("\n");
    for (int b = 0; b <= SIZES; b++) {
      printf("  %-8s %6.0f", b < SIZES ? SIZE_NAMES[b] : "all", tally(rows, ones, inBand(b)).edits);
      for (int g = 0; g < WEIGHTS; g++) {
        printf(" %6.2f", value(tally(rows, vector<int>(rows.size(), g), inBand(b))));
      }
      printf("\n");
    }
  };
  surface("loss per edit", [&](const Outcome& o) { return perEdit(o.loss, o); });
  surface("ms per edit", [&](const Outcome& o) { return perEdit(o.ms, o); });
  surface("slowest edit, ms", [](const Outcome& o) { return o.slowest; });
  surface("fallback%", [&](const Outcome& o) { return 100 * perEdit(o.fallback, o); });

  const bool allPriced = ranges::all_of(rows, [](const Response& r) { return isfinite(r.price); });
  vector<Family> families{FLAT, POWER, BANDS, EVERY_PROPERTY};
  if (allPriced) families.insert(families.end(), {PRICE_FITTED, PRICE_TRUE});
  families.push_back(PER_EDIT);
  vector<int> everyRate(RATES.size());
  ranges::iota(everyRate, 0);
  printf("\n-- D. Rules fitted to minimise loss + nodes / rate\n");
  printf("   rate = nodes one is willing to search to save one unit of effort\n"
         "   captured = share of the way from one weight to the best weight per edit\n"
         "   held out = fitted on one half of the history, scored on the other\n");
  const Comparison found = compare(rows, families, everyRate);
  printf("   'fitted on every property': log weight = sum of the terms below, fitted on every edit\n");
  for (int k : SHOWN_RATES) {
    const Features beta = fitWeightModel(rows, -1, RATES[k]);
    printf("   %-5s", RATE_NAMES[k]);
    for (int i = 0; i < PRICE_TERMS; i++) printf("%s %+.2f [%s]", i ? "," : "", beta[i], PRICE_TERM_NAMES[i]);
    printf("\n");
  }

  for (int k : SHOWN_RATES) {
    const auto objective = [&](const vector<int>& choice) {
      return tally(rows, choice, all).objective(RATES[k]);
    };
    const double one = objective(apply(rows, found.fitted[FLAT][k].rule));
    const double oneHeld = objective(found.held[FLAT][k]);
    const double each = objective(apply(rows, found.fitted[PER_EDIT][k].rule));
    printf("\n-- E. One split on each property, at rate %s\n", RATE_NAMES[k]);
    printf("  %-36s %-30s | %7s %8s | %8s %8s\n", "rule looks at", "chosen", "loss", "captured",
           "held out", "captured");
    for (int e = 0; e < EXPLAINERS; e++) {
      const Value value = [e](const Response& r) { return r.x[e]; };
      const Fitted in = fitSplit(rows, -1, RATES[k], value);
      const vector<int> out =
          heldOut(rows, [&](int fold) { return fitSplit(rows, fold, RATES[k], value); });
      const Outcome i = tally(rows, apply(rows, in.rule), all), h = tally(rows, out, all);
      printf("  %-36s %-30s | %7.3f %7.0f%% | %8.3f %7.0f%%\n", EXPLAINER_NAMES[e],
             in.label.c_str(), perEdit(i.loss, i),
             100 * (one - i.objective(RATES[k])) / max(EPS, one - each), perEdit(h.loss, h),
             100 * (oneHeld - h.objective(RATES[k])) / max(EPS, oneHeld - each));
    }
  }

  for (int k : SHOWN_RATES) {
    printf("\n-- F. By size, rules fitted at rate %s (on the other half, except the last)\n",
           RATE_NAMES[k]);
    const auto table = [&](const char* title, auto&& value) {
      printf("   %s\n  %-26s", title, "");
      for (int b = 0; b < SIZES; b++) printf(" %7s", SIZE_NAMES[b]);
      printf(" %7s\n", "all");
      const auto line = [&](const char* name, const vector<int>& choice) {
        printf("  %-26s", name);
        for (int b = 0; b <= SIZES; b++) {
          printf(" %7.2f", value(tally(rows, choice, inBand(b)), tally(rows, ones, inBand(b))));
        }
        printf("\n");
      };
      line("weight 1", ones);
      for (Family f : families) {
        line(FAMILY_NAMES[f],
             f == PER_EDIT ? apply(rows, found.fitted[f][k].rule) : found.held[f][k]);
      }
    };
    table("loss per edit", [&](const Outcome& o, const Outcome&) { return perEdit(o.loss, o); });
    table("work relative to weight 1",
          [](const Outcome& o, const Outcome& b) { return b.nodes ? o.nodes / b.nodes : NAN; });
    table("ms per edit", [&](const Outcome& o, const Outcome&) { return perEdit(o.ms, o); });
  }

  printf("\n-- G. How sure: 95%% intervals from resampling edits\n");
  const int mid = snap(0.35), low = snap(0.12);
  printf("   loss saved per edit by lowering one weight for every edit\n");
  printf("  %-8s %6s | %-24s | %-24s\n", "size", "edits", "1.00 -> 0.35", "0.35 -> 0.12");
  for (int b = 0; b <= SIZES; b++) {
    vector<pair<double, double>> first, second;
    double edits = 0;
    for (const Response& r : rows) {
      if (!inBand(b)(r)) continue;
      edits += r.weight;
      first.push_back({r.loss[WEIGHT_ONE] - r.loss[mid], r.weight});
      second.push_back({r.loss[mid] - r.loss[low], r.weight});
    }
    printf("  %-8s %6.0f | %-24s | %-24s\n", b < SIZES ? SIZE_NAMES[b] : "all", edits,
           interval(first).c_str(), interval(second).c_str());
  }
  printf("   saved per edit over one weight, both fitted on the other half\n");
  printf("  %-5s %-24s | %-24s | %-24s %-24s\n", "rate", "rule", "loss + nodes / rate, all",
         "loss, 21-80", "loss, 81+");
  for (int k : SHOWN_RATES) {
    for (Family f : families) {
      if (f == FLAT || f == PER_EDIT) continue;
      vector<pair<double, double>> whole, middle, large;
      for (size_t i = 0; i < rows.size(); i++) {
        const Response& r = rows[i];
        const int a = found.held[FLAT][k][i], b = found.held[f][k][i];
        const int band = sizeOf(r.x[SIZE]);
        whole.push_back({r.loss[a] - r.loss[b] + (r.nodes[a] - r.nodes[b]) / RATES[k], r.weight});
        if (band == 4 || band == 5) middle.push_back({r.loss[a] - r.loss[b], r.weight});
        if (band == 6) large.push_back({r.loss[a] - r.loss[b], r.weight});
      }
      printf("  %-5s %-24s | %-24s | %-24s %-24s\n", RATE_NAMES[k], FAMILY_NAMES[f],
             interval(whole).c_str(), interval(middle).c_str(), interval(large).c_str());
    }
  }

  vector<Response> priced;
  for (const Response& r : rows) {
    if (isfinite(r.price)) priced.push_back(r);
  }
  if (priced.size() >= 20) {
    printf("\n-- H. True price: best known effort of clearing the region per unit of N (%zu edits%s)\n",
           priced.size(), allPriced ? "" : ", pure deletions only in this build");
    array<Mean, SIZES> price, cost;
    for (const Response& r : priced) {
      price[sizeOf(r.x[SIZE])].add(r.price, r.weight);
      cost[sizeOf(r.x[SIZE])].add(r.price * r.x[SIZE], r.weight);
    }
    printf("  %-28s", "");
    for (int b = 0; b < SIZES; b++) printf(" %6s", SIZE_NAMES[b]);
    printf("\n  %-28s", "edits");
    for (int b = 0; b < SIZES; b++) printf(" %6.0f", price[b].weight);
    printf("\n  %-28s", "mean effort to clear");
    for (int b = 0; b < SIZES; b++) printf(" %6.2f", cost[b].get());
    printf("\n  %-28s", "mean true price");
    for (int b = 0; b < SIZES; b++) printf(" %6.2f", price[b].get());
    printf("\n");
    Mean mean;
    for (const Response& r : priced) mean.add(log(r.price), r.weight);
    double spread = 0, inside = 0, outside = 0, sizeOnly = 0;
    {
      // log price ~ a + b log N, as the size-only comparison.
      double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
      for (const Response& r : priced) {
        const double x = log(max(1.0, r.x[SIZE])), y = log(r.price);
        sw += r.weight; sx += r.weight * x; sy += r.weight * y;
        sxx += r.weight * x * x; sxy += r.weight * x * y;
      }
      const double slope = (sw * sxy - sx * sy) / (sw * sxx - sx * sx);
      const double offset = (sy - slope * sx) / sw;
      for (const Response& r : priced) {
        const double y = log(r.price);
        spread += r.weight * (y - mean.get()) * (y - mean.get());
        inside += r.weight * pow(y - log(r.predicted[2]), 2);
        outside += r.weight * pow(y - log(r.predicted[1 - r.half]), 2);
        sizeOnly += r.weight * pow(y - offset - slope * log(max(1.0, r.x[SIZE])), 2);
      }
      printf("   spread in log price explained: size alone (price = %.2f * N^%.2f) %.0f%%;"
             " fitted on these edits %.0f%%; fitted on the other half %.0f%%\n",
             exp(offset), slope, 100 * (1 - sizeOnly / spread), 100 * (1 - inside / spread),
             100 * (1 - outside / spread));
    }
    const Features beta = fitPrice(priced, -1);
    printf("   log price =");
    for (int i = 0; i < PRICE_TERMS; i++) printf("%s %+.2f [%s]", i ? "," : "", beta[i], PRICE_TERM_NAMES[i]);
    printf("\n");
    if (!allPriced) {
      compare(priced, {FLAT, POWER, BANDS, EVERY_PROPERTY, PRICE_FITTED, PRICE_TRUE, PER_EDIT},
              SHOWN_RATES);
    }
  }

  printf("\n-- I. Searching again at lower weights within a node budget per edit\n");
  printf("   each start keeps its best answer so far; a search that does not fit the budget uses\n"
         "   it up for nothing; nothing is reused between searches\n");
  printf("  %-30s %7s | %7s %6s %6s %8s %7s | %7s %7s %7s\n", "weights tried", "budget", "loss",
         "gap%", "work", "ms/edit", "max ms", "21-40", "41-80", "81+");
  const auto line = [&](const string& name, const char* budget, const function<Outcome(const Keep&)>& of) {
    const Outcome o = of(all);
    printf("  %-30s %7s | %7.3f %6.2f %5.2fx %8.2f %7.0f |", name.c_str(), budget, perEdit(o.loss, o),
           100 * perEdit(o.gap, o), o.nodes / base.nodes, perEdit(o.ms, o), o.slowest);
    for (int b : {4, 5, 6}) {
      const Outcome part = of(inBand(b));
      printf(" %7.2f", perEdit(part.loss, part));
    }
    printf("\n");
  };
  for (int g : {0, 3, 6, 8}) {
    line(format("{:.2f} alone", GRID[g]), "-",
         [&](const Keep& keep) { return tally(rows, vector<int>(rows.size(), g), keep); });
  }
  line("best weight per edit", "-", [&](const Keep& keep) {
    return tally(rows, apply(rows, found.fitted[PER_EDIT][RATES.size() - 1].rule), keep);
  });
  struct Ladder {
    const char* name;
    vector<int> rungs;
  };
  const vector<Ladder> ladders{{"1 > 0.12", {0, 6}},
                               {"1 > 0.05", {0, 8}},
                               {"1 > 0.35 > 0.12", {0, 3, 6}},
                               {"1 > 0.25 > 0.05", {0, 4, 8}},
                               {"1 > 0.5 > 0.25 > 0.12 > 0.05", {0, 2, 4, 6, 8}},
                               {"every grid weight", {0, 1, 2, 3, 4, 5, 6, 7, 8}},
                               {"1 > 0.35", {0, 3}},
                               {"1 > 0.18", {0, 5}}};
  for (const Ladder& ladder : ladders) {
    for (double budget : {1e3, 3e3, 1e4, 3e4, 1e12}) {
      line(ladder.name, budget > 1e9 ? "any" : format("{:.0f}k", budget / 1000).c_str(),
           [&](const Keep& keep) { return climb(rows, ladder.rungs, budget, keep); });
    }
  }
  printf("   loss saved per edit by 1 > 0.35 > 0.12 within 10k nodes, with a 95%% interval\n");
  printf("  %-8s %6s | %-24s | %-24s | %-24s\n", "size", "edits", "over 1.00 alone",
         "over 0.35 alone", "over 0.12 alone");
  for (int b = 0; b <= SIZES; b++) {
    array<vector<pair<double, double>>, 3> saved;
    double edits = 0;
    for (const Response& r : rows) {
      if (!inBand(b)(r)) continue;
      edits += r.weight;
      const double climbed = climbOne(r, ladders[2].rungs, 1e4).loss;
      saved[0].push_back({r.loss[WEIGHT_ONE] - climbed, r.weight});
      saved[1].push_back({r.loss[mid] - climbed, r.weight});
      saved[2].push_back({r.loss[low] - climbed, r.weight});
    }
    printf("  %-8s %6.0f | %-24s | %-24s | %-24s\n", b < SIZES ? SIZE_NAMES[b] : "all", edits,
           interval(saved[0]).c_str(), interval(saved[1]).c_str(), interval(saved[2]).c_str());
  }

  printf("\n-- J. One search that keeps going: several answers per start, the best one kept\n");
  printf("  %6s %7s | %7s %6s %8s %7s | %7s %7s %7s\n", "weight", "answers", "loss", "work",
         "ms/edit", "max ms", "21-40", "41-80", "81+");
  for (int w = 0; w < 3; w++) {
    for (int k = 0; k < 4; k++) {
      array<double, SIZES + 1> loss{}, edits{};
      double nodes = 0, ms = 0, slowest = 0;
      for (const Response& r : rows) {
        for (int b : {sizeOf(r.x[SIZE]), SIZES}) {
          loss[b] += r.weight * r.moreLoss[w][k];
          edits[b] += r.weight;
        }
        nodes += r.weight * r.moreNodes[w][k];
        ms += r.weight * r.moreMs[w][k];
        slowest = max(slowest, r.moreMs[w][k]);
      }
      printf("  %6.2f %7d | %7.3f %5.2fx %8.2f %7.0f | %7.2f %7.2f %7.2f\n", GRID[MORE_WEIGHTS[w]],
             MORE_ANSWERS[k], loss[SIZES] / edits[SIZES], nodes / base.nodes, ms / edits[SIZES],
             slowest, loss[4] / edits[4], loss[5] / edits[5], loss[6] / edits[6]);
    }
  }

  // What a start gets at a weight, the weight-1 answer standing in when unanswered.
  const auto answered = [](const Response& r, size_t i, int g) {
    return r.cost[i][g] < 0 ? r.cost[i][WEIGHT_ONE] : r.cost[i][g];
  };
  printf("\n-- K. Loss a heavier weight had already avoided\n");
  printf("   regret = the part of the loss on starts some heavier grid weight answered better\n");
  printf("  %6s | %7s %7s %8s | %-15s | %-15s | %-15s\n", "weight", "loss", "regret", "starts%",
         "nothing to type", "something", "81+");
  for (int g = 0; g < WEIGHTS; g++) {
    array<Mean, 4> loss, regret;  // all, nothing to type, something to type, 81+
    Mean worse;
    for (const Response& r : rows) {
      double sum = 0, count = 0;
      for (size_t i = 0; i < r.best.size(); i++) {
        double floor = answered(r, i, g);
        for (int h = 0; h < g; h++) floor = min(floor, answered(r, i, h));
        sum += answered(r, i, g) - floor;
        count += answered(r, i, g) > floor + EPS;
      }
      const int starts = r.best.size();
      for (int group : {0, r.x[PURE_DELETION] > 0 ? 1 : 2, sizeOf(r.x[SIZE]) == 6 ? 3 : -1}) {
        if (group < 0) continue;
        loss[group].add(r.loss[g], r.weight);
        regret[group].add(sum / starts, r.weight);
      }
      worse.add(count / starts, r.weight);
    }
    printf("  %6.2f | %7.3f %7.3f %8.1f |", GRID[g], loss[0].get(), regret[0].get(), 100 * worse.get());
    for (int group : {1, 2, 3}) printf(" %7.3f %7.3f |", loss[group].get(), regret[group].get());
    printf("\n");
  }

  if (!rows.empty() && !rows.front().how.empty()) {
    printf("\n-- L. What settled each start, on edits with something to type\n");
    printf("   a remembered ending closes the start when its state pops, at prefix cost + ending cost;\n"
           "   early = how far that cost is above the priority the state popped at\n");
    printf("  %6s %-20s | %7s | %9s %9s | %10s %10s\n", "weight", "settled by", "starts%",
           "loss/edit", "loss %", "loss/start", "early");
    for (int w = 0; w < 3; w++) {
      const int g = MORE_WEIGHTS[w];
      array<Mean, HOWS> share, carried, perStart, early;
      Mean total;
      for (const Response& r : rows) {
        if (r.x[PURE_DELETION] > 0) continue;
        const double starts = r.best.size();
        array<double, HOWS> count{}, loss{};
        for (size_t i = 0; i < r.best.size(); i++) {
          const int how = r.how[i][w];
          const double lost = answered(r, i, g) - r.best[i];
          count[how]++;
          loss[how] += lost;
          perStart[how].add(lost, r.weight / starts);
          early[how].add(r.early[i][w], r.weight / starts);
        }
        for (int how = 0; how < HOWS; how++) {
          share[how].add(count[how] / starts, r.weight);
          carried[how].add(loss[how] / starts, r.weight);
        }
        total.add(r.loss[g], r.weight);
      }
      for (int how = 0; how < HOWS; how++) {
        printf("  %6.2f %-20s | %7.1f | %9.3f %9.1f | %10.3f %10.2f\n", GRID[g], HOW_NAMES[how],
               100 * share[how].get(), carried[how].get(),
               100 * carried[how].get() / max(EPS, total.get()), perStart[how].get(),
               early[how].get());
      }
    }
  }

  printf("\n-- M. Edits that start at a line start, against the rest\n");
  const array<pair<const char*, Keep>, 2> groups{{
      {"at a line start", [](const Response& r) { return r.x[FROM_LINE_START] > 0; }},
      {"elsewhere", [](const Response& r) { return r.x[FROM_LINE_START] == 0; }},
  }};
  printf("  %-16s", "edits");
  for (int b = 0; b < SIZES; b++) printf(" %7s", SIZE_NAMES[b]);
  printf(" %7s\n", "all");
  for (const auto& [name, keep] : groups) {
    printf("  %-16s", name);
    for (int b = 0; b <= SIZES; b++) {
      const Keep both = [&, b](const Response& r) { return keep(r) && inBand(b)(r); };
      printf(" %7.0f", tally(rows, ones, both).edits);
    }
    printf("\n");
  }
  const auto halves = [&](const char* title, auto&& value) {
    printf("   %s\n  %-16s", title, "");
    for (double w : GRID) printf(" %6.2f", w);
    printf("\n");
    for (const auto& [name, keep] : groups) {
      printf("  %-16s", name);
      for (int g = 0; g < WEIGHTS; g++) {
        printf(" %6.2f", value(tally(rows, vector<int>(rows.size(), g), keep)));
      }
      printf("\n");
    }
  };
  halves("loss per edit", [&](const Outcome& o) { return perEdit(o.loss, o); });
  halves("ms per edit", [&](const Outcome& o) { return perEdit(o.ms, o); });
  halves("slowest edit, ms", [](const Outcome& o) { return o.slowest; });
  halves("fallback%", [&](const Outcome& o) { return 100 * perEdit(o.fallback, o); });
  printf("   one weight for edits at a line start, another for the rest\n");
  printf("  %9s %10s | %7s %6s %8s %7s %7s %8s | %s\n", "elsewhere", "line start", "loss", "work",
         "ms/edit", "p99 ms", "max ms", ">100 ms",
         "loss saved per edit over the 'elsewhere' weight for every edit");
  for (int rest : {0, 2, 3, 5}) {
    for (int start : {-1, 3, 6, 8}) {
      if (start < 0) start = rest;
      else if (start <= rest) continue;
      vector<int> choice;
      vector<double> times;
      vector<pair<double, double>> saved;
      double slow = 0;
      for (const Response& r : rows) {
        const int g = groups[0].second(r) ? start : rest;
        choice.push_back(g);
        times.push_back(r.ms[g]);
        slow += r.weight * (r.ms[g] > 100);
        saved.push_back({r.loss[rest] - r.loss[g], r.weight});
      }
      ranges::sort(times);
      const Outcome o = tally(rows, choice, all);
      printf("  %9.2f %10.2f | %7.3f %5.2fx %8.2f %7.0f %7.0f %8.0f | %s\n", GRID[rest], GRID[start],
             perEdit(o.loss, o), o.nodes / base.nodes, perEdit(o.ms, o),
             times[times.size() * 99 / 100], o.slowest, slow,
             start == rest ? "-" : interval(saved).c_str());
    }
  }

  printf("\n-- N. What the line-start flag stands for: edits by where the region begins and ends\n");
  printf("   k nodes = thousands of nodes per edit\n");
  constexpr array<int, 4> SHOWN{0, 3, 6, 8};
  printf("  %-44s %5s | %-27s | %-27s | %-13s\n", "", "edits", "loss at 1 / 0.35 / 0.12 / 0.05",
         "k nodes at the same", "max ms 0.35 / 0.12");
  const auto shape = [&](const char* name, const Keep& keep) {
    const Outcome first = tally(rows, ones, keep);
    if (first.edits == 0) return;
    printf("  %-44s %5.0f |", name, first.edits);
    array<Outcome, 4> at;
    for (int k = 0; k < 4; k++) at[k] = tally(rows, vector<int>(rows.size(), SHOWN[k]), keep);
    for (const Outcome& o : at) printf(" %6.2f", perEdit(o.loss, o));
    printf(" |");
    for (const Outcome& o : at) printf(" %6.2f", perEdit(o.nodes, o) / 1000);
    printf(" | %6.0f %6.0f\n", at[1].slowest, at[2].slowest);
  };
  for (int multi = 0; multi < 2; multi++) {
    for (int from = 1; from >= 0; from--) {
      for (int to = 1; to >= 0; to--) {
        shape(format("{}, {}, {}", multi ? "several lines" : "one line",
                     from ? "from column 0" : "from mid-line", to ? "to the line end" : "to mid-line")
                  .c_str(),
              [=](const Response& r) {
                return (r.x[LINES] > 1) == multi && (r.x[FROM_LINE_START] > 0) == from &&
                       (r.x[TO_LINE_END] > 0) == to;
              });
      }
    }
  }
  shape("after indentation only (not column 0)",
        [](const Response& r) { return r.x[AFTER_INDENT] > 0 && r.x[FROM_LINE_START] == 0; });

  printf("\n-- O. No flag: the lightest weight whose search finishes within a node budget\n");
  printf("   a search over budget is abandoned at the budget and the next heavier weight is run\n");
  constexpr int HEAVY = 3;
  struct Tried {
    const char* name;
    vector<int> lightFirst;
    int last = HEAVY;
  };
  const vector<Tried> tries{{"0.12, else 0.35", {6}},
                            {"0.05, else 0.35", {8}},
                            {"0.05, else 0.12, else 0.35", {8, 6}},
                            {"0.18, else 0.35", {5}},
                            {"0.35, else 1", {3}, 0},
                            {"0.18, else 0.35, else 1", {5, 3}, 0},
                            {"0.12, else 0.35, else 1", {6, 3}, 0},
                            {"0.12, else 1", {6}, 0}};
  printf("  %-28s %6s | %7s %6s %8s %7s %7s | %-13s | %s\n", "weights tried", "budget", "loss", "work",
         "ms/edit", "p99 ms", "max ms", "1st fits: col 0 / rest",
         "loss saved per edit over 0.35 for every edit");
  const auto budgeted = [&](const char* name, const char* budgetName, auto&& choose) {
    Outcome o;
    vector<double> times;
    vector<pair<double, double>> saved;
    array<Mean, 2> light;
    for (const Response& r : rows) {
      double extraNodes = 0, extraMs = 0;
      const int g = choose(r, extraNodes, extraMs);
      o.edits += r.weight;
      o.loss += r.weight * r.loss[g];
      o.nodes += r.weight * (r.nodes[g] + extraNodes);
      o.ms += r.weight * (r.ms[g] + extraMs);
      o.slowest = max(o.slowest, r.ms[g] + extraMs);
      times.push_back(r.ms[g] + extraMs);
      saved.push_back({r.loss[HEAVY] - r.loss[g], r.weight});
      light[r.x[FROM_LINE_START] > 0 ? 0 : 1].add(g >= HEAVY && extraNodes == 0, r.weight);
    }
    ranges::sort(times);
    printf("  %-28s %6s | %7.3f %5.2fx %8.2f %7.0f %7.0f | %5.0f%% / %3.0f%%  | %s\n", name, budgetName,
           perEdit(o.loss, o), o.nodes / base.nodes, perEdit(o.ms, o), times[times.size() * 99 / 100],
           o.slowest, 100 * light[0].get(), 100 * light[1].get(), interval(saved).c_str());
  };
  budgeted("0.35 for every edit", "-", [](const Response&, double&, double&) { return HEAVY; });
  budgeted("0.12 at column 0, else 0.35", "-", [](const Response& r, double&, double&) {
    return r.x[FROM_LINE_START] > 0 ? 6 : HEAVY;
  });
  for (const Tried& t : tries) {
    for (double budget : {1e3, 2e3, 5e3, 1e4, 2e4}) {
      budgeted(t.name, format("{:.0f}k", budget / 1000).c_str(),
               [&](const Response& r, double& extraNodes, double& extraMs) {
                 for (int g : t.lightFirst) {
                   if (r.searched[g] <= budget) return g;
                   extraNodes += budget;
                   extraMs += r.spent[g] * budget / r.searched[g];
                 }
                 return t.last;
               });
    }
  }
}

}  // namespace

// Resamples the canonical corpus. Search work is concentrated in a few edits,
// so the heaviest are all kept at weight 1. The rest are cut, within each
// kind, into bins of equal total work; each bin contributes the edit nearest
// its mean work, weighted by the bin's edit count, so weighted corpus figures
// estimate the history's.
// Writes HeuristicCorpus.inc to $VIMFY_CORPUS_OUT. The output is raw history
// text: read it, then rerun with $VIMFY_CORPUS_PRIVATE naming anything
// private, before it goes anywhere near the repo.
TEST(HeuristicShapes, PickCorpus) {
  const char* outPath = getenv("VIMFY_CORPUS_OUT");
  if (!outPath) GTEST_SKIP() << "set VIMFY_CORPUS_OUT and a history directory";
  constexpr double HEAVY_SHARE = 0.05, MAX_SHARE = 0.05;
  const Config config = Config::uniform();
  const TransformOptimizerParams defaults = TransformOptimizerParams{}.withMaxResults(ALL_STARTS);
  const TransformOptimizerParams probe =
      TransformOptimizerParams(defaults).withDistanceWeight(0.6).withColExponent(0.9);

  struct Candidate {
    CorpusEdit edit;
    long nodes;  // under the defaults
    long work;   // defaults + probe
  };
  const vector<Problem> history = historyProblems(config);
  vector<Candidate> all;
  long historyNodes = 0;
  for (const Problem& p : history) {
    const long nodes = run(p.diff, config, defaults).nodes;
    historyNodes += nodes;
    if (p.isolates) all.push_back({p.edit, nodes, nodes + run(p.diff, config, probe).nodes});
  }
  ranges::stable_sort(all, greater{}, &Candidate::work);

  ofstream out(outPath);
  double corpusNodes = 0;
  // $VIMFY_CORPUS_PRIVATE="needle|needle": an edit containing a needle gets
  // every letter and digit replaced at random, keeping case, word shapes,
  // punctuation and positions.
  vector<string> needles;
  if (const char* spec = getenv("VIMFY_CORPUS_PRIVATE")) {
    for (auto part : string_view(spec) | views::split('|')) needles.emplace_back(string_view(part));
  }
  mt19937 rng(20261124);
  const auto anonymise = [&](string& s) {
    for (char& c : s) {
      if (islower(static_cast<unsigned char>(c))) c = 'a' + rng() % 26;
      else if (isupper(static_cast<unsigned char>(c))) c = 'A' + rng() % 26;
      else if (isdigit(static_cast<unsigned char>(c))) c = '0' + rng() % 10;
    }
  };
  int anonymised = 0;
  const auto emit = [&](const Candidate& c, int weight, bool heavyEdit) {
    CorpusEdit e = c.edit;
    const bool isPrivate = ranges::any_of(needles, [&](const string& needle) {
      return ranges::any_of(e.lines, [&](const string& l) { return l.contains(needle); }) ||
             e.inserted.contains(needle);
    });
    if (isPrivate) {
      for (size_t l = 0; l < e.lines.size(); l++) {
        if (e.lines[l] != "above" && e.lines[l] != "below") anonymise(e.lines[l]);
      }
      e.deleted = rangeText(e);
      anonymise(e.inserted);
      anonymised++;
    }
    corpusNodes += double(weight) * c.nodes;
    out << "    {\"" << e.kind << "\", {";
    for (size_t l = 0; l < e.lines.size(); l++) out << (l ? ", " : "") << cppLiteral(e.lines[l]);
    out << "}, {" << e.begin.line << ", " << e.begin.col << "}, {" << e.end.line << ", "
        << e.end.col << "}, " << cppLiteral(e.deleted) << ", " << cppLiteral(e.inserted)
        << ", " << weight << ", " << (heavyEdit ? "true" : "false") << "},\n";
  };

  // Heavy tail: peel off the heaviest edit until none left holds more than
  // HEAVY_SHARE of the remaining work. They are kept, but flagged, so they
  // are reported apart and cannot swamp the typical edits.
  long totalWork = 0;
  for (const Candidate& c : all) totalWork += c.work;
  long typicalWork = totalWork;
  int heavy = 0;
  while (heavy < ssize(all) && all[heavy].work > HEAVY_SHARE * typicalWork) {
    typicalWork -= all[heavy].work;
    emit(all[heavy++], 1, true);
  }
  const int typicalEdits = ssize(all) - heavy;
  printf("harvested %zu edits; %d heavy ones hold %.0f%% of the work and are set apart\n",
         history.size(), heavy, 100.0 * (totalWork - typicalWork) / totalWork);

  // Typical edits: within a kind, in work order, a bin closes before it would
  // stand for more than MAX_SHARE of the edits or of the work.
  const int maxBinEdits = max(1, int(MAX_SHARE * typicalEdits));
  const double maxBinWork = MAX_SHARE * typicalWork;
  map<string, vector<Candidate>> byKind;
  for (int i = heavy; i < ssize(all); i++) byKind[all[i].edit.kind].push_back(all[i]);
  for (auto& [kind, edits] : byKind) {
    ranges::stable_sort(edits, {}, &Candidate::work);
    const int n = edits.size();
    int taken = 0;
    for (int first = 0; first < n;) {
      int end = first;
      double binWork = 0;
      while (end < n && (end == first || (end - first < maxBinEdits &&
                                          binWork + edits[end].work <= maxBinWork))) {
        binWork += edits[end++].work;
      }
      const double mean = binWork / (end - first);
      int pick = first;
      for (int i = first; i < end; i++) {
        if (abs(edits[i].work - mean) < abs(edits[pick].work - mean)) pick = i;
      }
      emit(edits[pick], end - first, false);
      taken++;
      first = end;
    }
    printf("  %-14s %3d edits, %d taken, work %ld..%ld\n", kind.c_str(), n, taken,
           edits.front().work, edits.back().work);
  }
  printf("weighted corpus nodes %.0f (%.2fx history), %d edits anonymised\n", corpusNodes,
         corpusNodes / historyNodes, anonymised);
}

TEST(HeuristicShapes, Corpus) {
  const vector<Problem> problems = corpusProblems(false);
  report("corpus / uniform", Config::uniform(), problems);
  report("corpus / qwerty", Config::qwerty(), problems);
}

// The few edits that hold most of the search work, reported on their own.
TEST(HeuristicShapes, Heavy) {
  const vector<Problem> problems = corpusProblems(true);
  report("heavy / uniform", Config::uniform(), problems);
  report("heavy / qwerty", Config::qwerty(), problems);
}

// Why the shapes behave as they do: true cost against the estimate, how worse
// answers win, and where the nodes go. Runs on the history when a directory is
// set, otherwise on the whole corpus. $VIMFY_KEYBOARD=qwerty switches keyboard.
TEST(HeuristicShapes, Anatomy) {
  const bool qwerty = getenv("VIMFY_KEYBOARD") && string_view(getenv("VIMFY_KEYBOARD")) == "qwerty";
  const Config config = qwerty ? Config::qwerty() : Config::uniform();
  const bool history = getenv("VIMFY_JOURNAL_DIR") || getenv("VIMFY_SESSION_DUMP_DIR");
  vector<Problem> problems = history ? historyProblems(config) : corpusProblems(false);
  if (!history) ranges::copy(corpusProblems(true), back_inserter(problems));
  anatomy(format("{} / {}", history ? "history" : "corpus", qwerty ? "qwerty" : "uniform").c_str(),
          config, problems);
}

// Pop-by-pop traces of corpus edits. Corpus text only, so it is safe to print.
TEST(HeuristicShapes, Trace) {
  const Config config = Config::uniform();
  vector<Problem> problems = corpusProblems(false);
  ranges::copy(corpusProblems(true), back_inserter(problems));
  traceOrdering(problems, config, 1, 12, "a small edit where linear settles for a worse answer");
  traceOrdering(problems, config, 13, 40, "a medium edit where linear settles for a worse answer");
  traceOrdering(problems, config, 41, 1e9, "a large edit where linear settles for a worse answer");
  traceOrdering(problems, config, 1, 40, "a small or medium edit settled early by a cache hit",
                TransformTraceEvent::CacheHit);
  traceOrdering(problems, config, 41, 1e9, "a large edit settled early by a cache hit",
                TransformTraceEvent::CacheHit);
  if (getenv("VIMFY_TRACE_ORDER_ONLY")) return;

  printf("\n== how each shape spends its nodes\n");
  int worstRoot = -1, worstHalf = -1;
  double rootRatio = 0, halfLost = -1;
  for (int p = 0; p < ssize(problems); p++) {
    const Anatomy a = dissect(problems[p].diff, config);
    const double ratio = double(a.runs[2].run.nodes) / max(1L, a.runs[LINEAR].run.nodes);
    if (!problems[p].edit.heavy && ratio > rootRatio) { rootRatio = ratio; worstRoot = p; }
    double lostStarts = 0;
    for (int i = 0; i < a.starts; i++) {
      lostStarts += a.best[i] >= 0 && a.runs[1].run.costByStart[i] < 0;
    }
    if (problems[p].edit.heavy && lostStarts > halfLost) { halfLost = lostStarts; worstHalf = p; }
  }
  if (worstRoot >= 0) traceWork(worstRoot, problems[worstRoot], config);
  if (worstHalf >= 0) traceWork(worstHalf, problems[worstHalf], config);
}

// The single start that eats the pop budget: $VIMFY_HOG_EDIT picks the corpus
// edit (default: the heavy edit that loses most starts at half weight),
// $VIMFY_HOG_SHAPE the shape index (default 1 = half).
TEST(HeuristicShapes, Hog) {
  const bool qwerty = getenv("VIMFY_KEYBOARD") && string_view(getenv("VIMFY_KEYBOARD")) == "qwerty";
  const Config config = qwerty ? Config::qwerty() : Config::uniform();
  vector<Problem> problems = corpusProblems(false);
  ranges::copy(corpusProblems(true), back_inserter(problems));
  const int shape = getenv("VIMFY_HOG_SHAPE") ? atoi(getenv("VIMFY_HOG_SHAPE")) : 1;
  int pick = getenv("VIMFY_HOG_EDIT") ? atoi(getenv("VIMFY_HOG_EDIT")) : -1;
  double mostLost = -1;
  for (int p = 0; pick < 0 && p < ssize(problems); p++) {
    if (!problems[p].edit.heavy) continue;
    const Anatomy a = dissect(problems[p].diff, config);
    double lostStarts = 0;
    for (int i = 0; i < a.starts; i++) {
      lostStarts += a.best[i] >= 0 && a.runs[shape].run.costByStart[i] < 0;
    }
    if (lostStarts > mostLost) { mostLost = lostStarts; pick = p; }
  }
  if (getenv("VIMFY_HOG_EDIT")) { traceHog(pick, problems[pick], config, shape); return; }
  for (int p = 0; p < ssize(problems); p++) {
    if (problems[p].edit.heavy) traceHog(p, problems[p], config, shape);
  }
}

// What the typed-text term changes, by how much the edit has to type.
TEST(HeuristicShapes, Typed) {
  const bool qwerty = getenv("VIMFY_KEYBOARD") && string_view(getenv("VIMFY_KEYBOARD")) == "qwerty";
  const Config config = qwerty ? Config::qwerty() : Config::uniform();
  const bool history = getenv("VIMFY_JOURNAL_DIR") || getenv("VIMFY_SESSION_DUMP_DIR");
  vector<Problem> problems = history ? historyProblems(config) : corpusProblems(false);
  if (!history) ranges::copy(corpusProblems(true), back_inserter(problems));

  constexpr array<const char*, 4> NAMES{"none", "up to 10", "10 to 30", "over 30"};
  struct Bucket {
    double edits = 0;
    array<double, 2> nodes{};
    array<Mean, 2> beforeFirst, lost;
    Mean changed, costChange;
  };
  array<Bucket, 4> buckets;
  for (const Problem& problem : problems) {
    const double w = problem.edit.weight;
    array<TracedRun, 2> r;
    for (int typed = 0; typed < 2; typed++) {
      r[typed] = runTraced(problem.diff, config,
                           shapeParams(SHAPES[LINEAR]).withTypedWeight(double(typed)));
    }
    if (r[1].events.empty()) continue;
    const Traced& first = r[1].events.front();
    const double typedEffort = first.depth == 0 ? first.priority - first.shaped(1.0) : 0.0;
    Bucket& b = buckets[typedEffort < EPS ? 0 : typedEffort <= 10 ? 1 : typedEffort <= 30 ? 2 : 3];
    b.edits += w;
    const int starts = r[0].run.costByStart.size();
    for (int typed = 0; typed < 2; typed++) {
      b.nodes[typed] += w * r[typed].run.nodes;
      // Share of the edit's popped states that came before its first answer.
      int firstAnswer = r[typed].events.size();
      for (int e = 0; e < ssize(r[typed].events); e++) {
        const TransformTraceEvent ev = r[typed].events[e].event;
        if (ev == TransformTraceEvent::Goal || ev == TransformTraceEvent::CacheHit) { firstAnswer = e; break; }
      }
      b.beforeFirst[typed].add(double(firstAnswer) / max<size_t>(1, r[typed].events.size()), w);
      for (int i = 0; i < starts; i++) {
        const bool other = r[1 - typed].run.costByStart[i] >= 0;
        if (other) b.lost[typed].add(r[typed].run.costByStart[i] < 0, w / starts);
      }
    }
    for (int i = 0; i < starts; i++) {
      const double c0 = r[0].run.costByStart[i], c1 = r[1].run.costByStart[i];
      if (c0 < EPS || c1 < EPS) continue;
      b.changed.add(abs(c0 - c1) > EPS, w / starts);
      b.costChange.add(c1 / c0 - 1.0, w / starts);
    }
  }
  printf("\n== typed-text term off (0) vs on (1) / %s / %s\n", history ? "history" : "corpus",
         qwerty ? "qwerty" : "uniform");
  printf("   before 1st = share of the edit's popped states that came before its first answer\n");
  printf("  %-10s %6s | %9s %9s %7s | %10s %10s | %9s %11s | %6s %6s\n", "typing", "edits",
         "nodes 0", "nodes 1", "1 / 0", "before1st0", "before1st1", "changed%", "cost 1 vs 0",
         "lost0%", "lost1%");
  for (int k = 0; k < 4; k++) {
    const Bucket& b = buckets[k];
    if (b.edits == 0) continue;
    printf("  %-10s %6.0f | %9.0f %9.0f %6.2fx | %9.0f%% %9.0f%% | %8.1f%% %+10.2f%% | %6.2f %6.2f\n",
           NAMES[k], b.edits, b.nodes[0], b.nodes[1], b.nodes[1] / max(1.0, b.nodes[0]),
           100.0 * b.beforeFirst[0].get(), 100.0 * b.beforeFirst[1].get(), 100.0 * b.changed.get(),
           100.0 * b.costChange.get(), 100.0 * b.lost[0].get(), 100.0 * b.lost[1].get());
  }
}

// ---- Breaks: the line-break term of the estimate ---------------------------

struct BreakShape {
  double cost, exponent;
};
constexpr array<BreakShape, 17> BREAK_SHAPES{
    {{0, 1}, {0.5, 1}, {1, 1}, {1.2, 1}, {1.4, 1}, {1.6, 1}, {1.8, 1}, {2, 1}, {2.2, 1}, {2.4, 1},
     {2.6, 1}, {2.8, 1}, {3, 1}, {4, 1}, {6, 1}, {10, 1}, {2, 0.5}}};
constexpr int BREAK_SHAPE_COUNT = BREAK_SHAPES.size();
constexpr int DEFAULT_BREAK = 7;
static_assert(BREAK_SHAPES[DEFAULT_BREAK].cost == 2 && BREAK_SHAPES[DEFAULT_BREAK].exponent == 1);
constexpr array<double, 3> BREAK_WEIGHTS{1.0, 0.35, 0.12};
// One past the plain weights: 0.35, or 0.12 for an edit that starts at a line start.
constexpr int BREAK_RULE = BREAK_WEIGHTS.size();
constexpr array<const char*, BREAK_RULE + 1> BREAK_WEIGHT_NAMES{
    "1.00", "0.35", "0.12", "0.35, or 0.12 at a line start"};

void breakTerm(const char* label, const Config& config, const vector<Problem>& problems) {
  struct Row {
    double weight = 1;
    int half = 0;
    bool multiLine = false, lineStart = false;
    array<array<double, BREAK_SHAPE_COUNT>, BREAK_RULE + 1> loss{}, nodes{}, ms{}, fell{};
  };
  const int deepPops = getenv("VIMFY_DEEP_POPS") ? atoi(getenv("VIMFY_DEEP_POPS")) : 400000;
  const auto params = [](double weight, const BreakShape& shape) {
    return TransformOptimizerParams{}
        .withMaxResults(ALL_STARTS)
        .withDistanceWeight(weight)
        .withBreakCost(shape.cost)
        .withBreakExponent(shape.exponent);
  };
  vector<Row> rows;
  for (const Problem& p : problems) {
    const DiffState& d = p.diff;
    array<array<Run, BREAK_SHAPE_COUNT>, BREAK_RULE> runs;
    array<array<double, BREAK_SHAPE_COUNT>, BREAK_RULE> spent{};
    for (int w = 0; w < BREAK_RULE; w++) {
      for (int s = 0; s < BREAK_SHAPE_COUNT; s++) {
        const auto began = chrono::steady_clock::now();
        runs[w][s] = run(d, config, params(BREAK_WEIGHTS[w], BREAK_SHAPES[s]));
        spent[w][s] = chrono::duration<double, milli>(chrono::steady_clock::now() - began).count();
      }
    }
    const Run& anchor = runs[0][DEFAULT_BREAK];
    vector<double> best =
        run(d, config, TransformOptimizerParams::dijkstra(ALL_STARTS, REFERENCE_POP_CAP)).costByStart;
    const auto lower = [&](const Run& other) {
      for (size_t i = 0; i < best.size(); i++) {
        const double c = other.costByStart[i];
        if (c >= 0 && (best[i] < 0 || c < best[i])) best[i] = c;
      }
    };
    bool cut = false;
    for (const auto& byShape : runs) {
      for (const Run& other : byShape) {
        lower(other);
        for (size_t i = 0; i < best.size(); i++) cut |= other.costByStart[i] < 0 && anchor.costByStart[i] >= 0;
      }
    }
    if (cut) {
      for (double weight : {0.25, 0.08}) {
        lower(run(d, config, params(weight, BREAK_SHAPES[DEFAULT_BREAK]).withMaxNodesPopped(deepPops)));
      }
    }

    Row r;
    int starts = 0;
    for (size_t i = 0; i < best.size(); i++) {
      if (anchor.costByStart[i] < 0) continue;
      starts++;
      for (int w = 0; w < BREAK_RULE; w++) {
        for (int s = 0; s < BREAK_SHAPE_COUNT; s++) {
          double c = runs[w][s].costByStart[i];
          if (c < 0) {
            c = anchor.costByStart[i];
            r.fell[w][s] = 1;
          }
          r.loss[w][s] += c - best[i];
        }
      }
    }
    if (starts == 0) continue;
    r.weight = p.edit.weight;
    r.half = p.half;
    r.multiLine = ranges::count(d.deletedText, '\n') > 0;
    r.lineStart = d.boundary.leftColOffset() == 0;
    for (int w = 0; w < BREAK_RULE; w++) {
      for (int s = 0; s < BREAK_SHAPE_COUNT; s++) {
        r.loss[w][s] /= starts;
        r.nodes[w][s] = runs[w][s].nodes + (r.fell[w][s] > 0 ? anchor.nodes : 0);
        r.ms[w][s] = spent[w][s] + (r.fell[w][s] > 0 ? spent[0][DEFAULT_BREAK] : 0);
      }
    }
    const int pick = r.lineStart ? 2 : 1;
    r.loss[BREAK_RULE] = r.loss[pick];
    r.nodes[BREAK_RULE] = r.nodes[pick];
    r.ms[BREAK_RULE] = r.ms[pick];
    r.fell[BREAK_RULE] = r.fell[pick];
    rows.push_back(r);
  }

  double edits = 0, multi = 0, baseNodes = 0, moved = 0;
  for (const Row& r : rows) {
    edits += r.weight;
    multi += r.weight * r.multiLine;
    baseNodes += r.weight * r.nodes[0][DEFAULT_BREAK];
    bool differs = false;
    for (int w = 0; w < BREAK_RULE; w++) {
      for (int s = 0; s < BREAK_SHAPE_COUNT; s++) {
        differs |= abs(r.loss[w][s] - r.loss[w][DEFAULT_BREAK]) > EPS;
      }
    }
    moved += r.weight * (differs && !r.multiLine);
  }
  printf("\n== break term / %s: %.0f edits, %.0f spanning more than one line\n", label, edits, multi);
  printf("   estimate = weight x (chars left + cost x line breaks left ^ exp); today cost 2, exp 1\n"
         "   loss = effort above the best known answer of this run, per edit\n"
         "   work = nodes relative to weight 1 with today's term\n"
         "   saved = loss per edit below today's term at the same weight, 95%% interval\n"
         "   single-line edits whose answer moves with the term: %.0f\n", moved);
  for (int w = 0; w <= BREAK_RULE; w++) {
    printf("\n-- weight %s\n", BREAK_WEIGHT_NAMES[w]);
    printf("  %4s %4s | %7s %10s | %6s %8s %7s %6s | %-24s | %s\n", "cost", "exp", "loss",
           "multi-line", "work", "ms/edit", "max ms", "short", "saved, all edits",
           "saved, multi-line edits");
    for (int s = 0; s < BREAK_SHAPE_COUNT; s++) {
      double loss = 0, multiLoss = 0, nodes = 0, ms = 0, slowest = 0, shortOf = 0;
      vector<pair<double, double>> saved, savedMulti;
      for (const Row& r : rows) {
        loss += r.weight * r.loss[w][s];
        multiLoss += r.weight * r.multiLine * r.loss[w][s];
        nodes += r.weight * r.nodes[w][s];
        ms += r.weight * r.ms[w][s];
        slowest = max(slowest, r.ms[w][s]);
        shortOf += r.weight * r.fell[w][s];
        const double gain = r.loss[w][DEFAULT_BREAK] - r.loss[w][s];
        saved.push_back({gain, r.weight});
        if (r.multiLine) savedMulti.push_back({gain, r.weight});
      }
      const bool today = s == DEFAULT_BREAK;
      printf("  %4.1f %4.1f | %7.3f %10.3f | %5.2fx %8.2f %7.0f %6.0f | %-24s | %s\n",
             BREAK_SHAPES[s].cost, BREAK_SHAPES[s].exponent, loss / edits, multiLoss / max(1.0, multi),
             nodes / baseNodes, ms / edits, slowest, shortOf,
             today ? "today" : interval(saved).c_str(), today ? "" : interval(savedMulti).c_str());
    }
  }

  constexpr double RATE = 1e4;
  printf("\n-- Term picked on one half of the history (lowest loss + nodes / 10k), scored on the other\n");
  for (int w = 0; w <= BREAK_RULE; w++) {
    array<int, 2> pick{};
    for (int fold = 0; fold < 2; fold++) {
      array<double, BREAK_SHAPE_COUNT> cost{};
      for (const Row& r : rows) {
        if (r.half != fold) continue;
        for (int s = 0; s < BREAK_SHAPE_COUNT; s++) cost[s] += r.weight * (r.loss[w][s] + r.nodes[w][s] / RATE);
      }
      pick[fold] = ranges::min_element(cost) - cost.begin();
    }
    vector<pair<double, double>> savedLoss, savedBoth;
    for (const Row& r : rows) {
      const int s = pick[1 - r.half];
      savedLoss.push_back({r.loss[w][DEFAULT_BREAK] - r.loss[w][s], r.weight});
      savedBoth.push_back({r.loss[w][DEFAULT_BREAK] - r.loss[w][s] +
                               (r.nodes[w][DEFAULT_BREAK] - r.nodes[w][s]) / RATE,
                           r.weight});
    }
    printf("  weight %-30s picked cost %.1f exp %.1f and cost %.1f exp %.1f | saved: loss %s,"
           " loss + nodes / 10k %s\n",
           BREAK_WEIGHT_NAMES[w], BREAK_SHAPES[pick[0]].cost, BREAK_SHAPES[pick[0]].exponent,
           BREAK_SHAPES[pick[1]].cost, BREAK_SHAPES[pick[1]].exponent, interval(savedLoss).c_str(),
           interval(savedBoth).c_str());
  }
}

// Whether 2 per line break is the right charge. History only; timings mean
// something only from a Release build.
TEST(HeuristicShapes, Breaks) {
  if (!getenv("VIMFY_JOURNAL_DIR") && !getenv("VIMFY_SESSION_DUMP_DIR")) {
    GTEST_SKIP() << "set VIMFY_JOURNAL_DIR and/or VIMFY_SESSION_DUMP_DIR";
  }
  const bool qwerty = getenv("VIMFY_KEYBOARD") && string_view(getenv("VIMFY_KEYBOARD")) == "qwerty";
  const Config config = qwerty ? Config::qwerty() : Config::uniform();
  breakTerm(qwerty ? "history / qwerty" : "history / uniform", config, historyProblems(config));
}

// Which weight each edit should get, and what about an edit tells it. Runs on
// the history when a directory is set, otherwise on the whole corpus.
// Timings mean something only from a Release build.
TEST(HeuristicShapes, Policy) {
  const bool qwerty = getenv("VIMFY_KEYBOARD") && string_view(getenv("VIMFY_KEYBOARD")) == "qwerty";
  const Config config = qwerty ? Config::qwerty() : Config::uniform();
  const bool history = getenv("VIMFY_JOURNAL_DIR") || getenv("VIMFY_SESSION_DUMP_DIR");
  vector<Problem> problems = history ? historyProblems(config) : corpusProblems(false);
  if (!history) ranges::copy(corpusProblems(true), back_inserter(problems));
  const string label = format("{} / {}", history ? "history" : "corpus", qwerty ? "qwerty" : "uniform");
  policy(label.c_str(), respond(config, problems));
}

TEST(HeuristicShapes, History) {
  if (!getenv("VIMFY_JOURNAL_DIR") && !getenv("VIMFY_SESSION_DUMP_DIR")) {
    GTEST_SKIP() << "set VIMFY_JOURNAL_DIR and/or VIMFY_SESSION_DUMP_DIR";
  }
  const string_view only = getenv("VIMFY_KEYBOARD") ? getenv("VIMFY_KEYBOARD") : "";
  if (only != "qwerty") {
    report("history / uniform", Config::uniform(), historyProblems(Config::uniform()));
  }
  if (only != "uniform") {
    report("history / qwerty", Config::qwerty(), historyProblems(Config::qwerty()));
  }
}
