// Transform-heuristic loss over the planned edits of recorded journals.
// For each heuristic setting and each start position of each edit:
//   gap  = cost / best known cost - 1   (best over the pure-effort search and all settings)
//   lost = some setting solves the start, this one does not
//   work = nodes explored, relative to the current defaults
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

using namespace std;
namespace fs = std::filesystem;

namespace {

constexpr int REFERENCE_POP_CAP = 20000;
constexpr int ALL_STARTS = 100000;
constexpr double EPS = 1e-9;

struct Setting {
  double distanceWeight, colExponent, breakExponent, typedWeight;
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

vector<Problem> corpusProblems() {
  vector<Problem> problems;
  for (const CorpusEdit& e : CORPUS) {
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

void report(const char* keyboard, const Config& config, const vector<Problem>& problems) {
  // VIMFY_SETTINGS="dist,col^,break^,typed;..." replaces the grid; the current
  // defaults are always row one, since work is relative to it.
  vector<Setting> settings;
  const char* spec = getenv("VIMFY_SETTINGS");
  if (spec && string_view(spec) == "fine") {
    // Near the defaults, where work stays affordable.
    for (double typed : {0.0, 1.0, 2.0}) {
      for (double colExp : {1.0, 0.9, 0.8}) {
        for (double dw : {1.0, 0.9, 0.8, 0.7, 0.6}) settings.push_back({dw, colExp, 1.0, typed});
      }
    }
  } else if (spec) {
    settings.push_back({1.0, 1.0, 1.0, 0.0});
    for (auto row : string_view(spec) | views::split(';')) {
      Setting s{};
      sscanf(string(string_view(row)).c_str(), "%lf,%lf,%lf,%lf", &s.distanceWeight,
             &s.colExponent, &s.breakExponent, &s.typedWeight);
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
  for (int p = 0; p < P; p++) {
    const Run reference = run(problems[p].diff, config,
                              TransformOptimizerParams::dijkstra(ALL_STARTS, REFERENCE_POP_CAP));
    best[p] = reference.costByStart;
    for (int s = 0; s < S; s++) {
      const Setting& v = settings[s];
      runs[s][p] = run(problems[p].diff, config,
                       TransformOptimizerParams{}
                           .withMaxResults(ALL_STARTS)
                           .withDistanceWeight(v.distanceWeight)
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

  printf("== %s: %d edits, %.0f solvable starts\n", keyboard, P, solvable);
  printf("  %4s %5s %6s %6s | %6s %7s %8s %8s %8s %8s | %6s\n", "dist", "col^", "break^",
         "typed", "lost", "%worse", "meanGap%", "gapA%", "gapB%", "maxGap%", "work");
  for (int s = 0; s < S; s++) {
    double lost = 0, compared = 0, worse = 0, nodes = 0;
    double gapSum = 0, gapMax = 0;
    array<double, 2> halfSum{}, halfCount{};
    for (int p = 0; p < P; p++) {
      const double w = problems[p].edit.weight;
      nodes += w * runs[s][p].nodes;
      for (size_t i = 0; i < best[p].size(); i++) {
        if (best[p][i] < 0) continue;
        const double c = runs[s][p].costByStart[i];
        if (c < 0) { lost += w; continue; }
        const double gap = best[p][i] > EPS ? c / best[p][i] - 1.0 : 0.0;
        compared += w;
        gapSum += w * gap;
        gapMax = max(gapMax, gap);
        halfSum[problems[p].half] += w * gap;
        halfCount[problems[p].half] += w;
        if (gap > EPS) worse += w;
      }
    }
    const Setting& v = settings[s];
    printf("  %4.1f %5.2f %6.1f %6.1f | %6.0f %6.1f%% %8.2f %8.2f %8.2f %8.1f | %5.2fx\n",
           v.distanceWeight, v.colExponent, v.breakExponent, v.typedWeight, lost,
           compared ? 100.0 * worse / compared : 0.0, compared ? 100.0 * gapSum / compared : 0.0,
           halfCount[0] ? 100.0 * halfSum[0] / halfCount[0] : 0.0,
           halfCount[1] ? 100.0 * halfSum[1] / halfCount[1] : 0.0, 100.0 * gapMax,
           baselineNodes ? double(nodes) / baselineNodes : 0.0);
  }
}

}  // namespace

// Resamples the canonical corpus. Within each kind the edits are sorted by
// search work (nodes under the current defaults) and cut into equal-count
// bins; each bin contributes the edit nearest its mean work, weighted by the
// bin's size, so weighted corpus totals estimate the history's.
// Writes HeuristicCorpus.inc to $VIMFY_CORPUS_OUT. The output is raw history
// text: read it and rewrite anything private before committing.
TEST(HeuristicShapes, PickCorpus) {
  const char* outPath = getenv("VIMFY_CORPUS_OUT");
  if (!outPath) GTEST_SKIP() << "set VIMFY_CORPUS_OUT and a history directory";
  constexpr int PER_KIND = 7;
  const Config config = Config::uniform();

  struct Candidate {
    CorpusEdit edit;
    long nodes;
  };
  const vector<Problem> history = historyProblems(config);
  map<string, vector<Candidate>> byKind;
  long historyNodes = 0;
  for (const Problem& p : history) {
    const long nodes = run(p.diff, config, TransformOptimizerParams{}.withMaxResults(ALL_STARTS)).nodes;
    historyNodes += nodes;
    if (p.isolates) byKind[p.edit.kind].push_back({p.edit, nodes});
  }

  ofstream out(outPath);
  double corpusNodes = 0;
  printf("harvested %zu edits, %ld nodes\n", history.size(), historyNodes);
  for (auto& [kind, edits] : byKind) {
    ranges::stable_sort(edits, {}, &Candidate::nodes);
    const int n = edits.size(), take = min(n, PER_KIND);
    printf("  %-14s %3d available, %d taken, work %ld..%ld\n", kind.c_str(), n, take,
           edits.front().nodes, edits.back().nodes);
    for (int bin = 0; bin < take; bin++) {
      const int first = bin * n / take, end = (bin + 1) * n / take;
      double mean = 0;
      for (int i = first; i < end; i++) mean += edits[i].nodes;
      mean /= end - first;
      int pick = first;
      for (int i = first; i < end; i++) {
        if (abs(edits[i].nodes - mean) < abs(edits[pick].nodes - mean)) pick = i;
      }
      const CorpusEdit& e = edits[pick].edit;
      corpusNodes += double(end - first) * edits[pick].nodes;
      out << "    {\"" << e.kind << "\", {";
      for (size_t l = 0; l < e.lines.size(); l++) out << (l ? ", " : "") << cppLiteral(e.lines[l]);
      out << "}, {" << e.begin.line << ", " << e.begin.col << "}, {" << e.end.line << ", "
          << e.end.col << "}, " << cppLiteral(e.deleted) << ", " << cppLiteral(e.inserted)
          << ", " << end - first << "},\n";
    }
  }
  printf("weighted corpus nodes %.0f (%.2fx history)\n", corpusNodes, corpusNodes / historyNodes);
}

TEST(HeuristicShapes, Corpus) {
  const vector<Problem> problems = corpusProblems();
  report("corpus / uniform", Config::uniform(), problems);
  report("corpus / qwerty", Config::qwerty(), problems);
}

TEST(HeuristicShapes, History) {
  if (!getenv("VIMFY_JOURNAL_DIR") && !getenv("VIMFY_SESSION_DUMP_DIR")) {
    GTEST_SKIP() << "set VIMFY_JOURNAL_DIR and/or VIMFY_SESSION_DUMP_DIR";
  }
  report("history / uniform", Config::uniform(), historyProblems(Config::uniform()));
  report("history / qwerty", Config::qwerty(), historyProblems(Config::qwerty()));
}
