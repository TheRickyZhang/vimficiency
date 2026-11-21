// tests/Debug/StatsTokens.cpp
//
// Re-runs `:Vimfy stats` motion-histogram tokenization over a sessions.jsonl.
// Run:
//   VIMFY_SESSIONS=~/.local/share/nvim/vimficiency/sessions.jsonl \
//     ./build/tests/vimfy_debug --gtest_filter='StatsTokens.*'

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

import std.compat;
import vf.Interpreter.SequenceParser;
import vf.Keyboard.Config;
import vf.Optimizer.CompositionOptimizer.CompositionOptimizer;
import vf.Boundary.NavBoundary;
import vf.Optimizer.NavOptimizer.NavOptimizer;
import vf.Utils.PrettyText;

using namespace std;
using json = nlohmann::json;

namespace {

const char* kindName(TokenKind k) {
  switch (k) {
    case TokenKind::Movement: return "mov";
    case TokenKind::Delete: return "del";
    case TokenKind::Change: return "chg";
    case TokenKind::Visual: return "vis";
    case TokenKind::TypedText: return "typ";
    case TokenKind::Escape: return "esc";
  }
  return "?";
}

string normalize(const string& t) {
  static const regex re(R"(^(\d*)([fFtTrR])(.+)$)");
  smatch m;
  if (regex_match(t, m, re)) {
    string tail = m[3];
    if (tail.size() == 1 || (tail.front() == '<' && tail.back() == '>')) return string(m[1]) + string(m[2]) + "_";
  }
  return t;
}

struct Rec {
  int idx;
  long epoch;
  string u, o;
};

}  // namespace

TEST(StatsTokens, Histogram) {
  const char* path = getenv("VIMFY_SESSIONS");
  if (!path) GTEST_SKIP();
  ifstream in(path);
  vector<Rec> recs;
  string line;
  int idx = 0;
  // TSV from jq: type \t epoch \t user_seq \t best_opt_seq (raw jsonl has invalid UTF-8).
  while (getline(in, line)) {
    vector<string> f;
    size_t s = 0;
    for (size_t p; (p = line.find('\t', s)) != string::npos; s = p + 1) f.push_back(line.substr(s, p - s));
    f.push_back(line.substr(s));
    recs.push_back({idx++, stol(f[1]), f[2], f[3]});
  }

  map<string, array<int, 2>> hist;
  map<pair<int, string>, int> kindHist;
  array<int, 2> parseFail{}, empty{};
  const set<string> watch = {"cW", "cw", "O", "a", "(", ")", ".", "E", "4j", "4k", "5j"};
  map<string, vector<int>> watchRecs[2];
  map<string, int> failAt[2];
  map<string, int> orphanOpt;

  for (const Rec& r : recs) {
    const string* seqs[2] = {&r.u, &r.o};
    const bool userUnusable = r.u.empty() || !parseSequence(r.u);
    for (int side = 0; side < 2; ++side) {
      if (seqs[side]->empty()) { empty[side]++; continue; }
      auto toks = parseSequence(*seqs[side]);
      if (!toks) {
        parseFail[side]++;
        const string& s = *seqs[side];
        size_t off = toks.error().offset;
        size_t close = s[off] == '<' ? s.find('>', off) : string::npos;
        failAt[side][close == string::npos ? s.substr(off, 1) : s.substr(off, close - off + 1)]++;
        cout << "PARSEFAIL side=" << side << " rec=" << r.idx << " " << formatSequenceParseError(toks.error())
             << " seq=" << VF::prettify(*seqs[side]) << "\n";
        continue;
      }
      for (size_t ti = 0; ti < toks->size(); ++ti) {
        const auto& t = (*toks)[ti];
        string key = normalize(t.token);
        if (t.kind == TokenKind::Change && ti > 0 && (*toks)[ti - 1].kind == TokenKind::Visual)
          cout << "VISCHG side=" << side << " rec=" << r.idx << " [" << (*toks)[ti - 1].token << "][" << t.token
               << "] seq=" << VF::prettify(*seqs[side]) << "\n";
        hist[key][side]++;
        if (side == 1 && userUnusable) orphanOpt[key]++;
        kindHist[{side, kindName(t.kind)}]++;
        if (watch.contains(key)) watchRecs[side][key].push_back(r.idx);
      }
    }
  }

  cout << "records=" << recs.size() << " emptyU=" << empty[0] << " emptyO=" << empty[1]
       << " failU=" << parseFail[0] << " failO=" << parseFail[1] << "\n";
  for (int side = 0; side < 2; ++side)
    for (auto& [k, c] : failAt[side]) cout << "failAt side=" << side << " [" << VF::prettify(k) << "] " << c << "\n";
  for (auto& [k, c] : kindHist) cout << "kind side=" << k.first << " " << k.second << " " << c << "\n";

  cout << "--- tokens counted >=10\n";
  for (auto& [k, c] : hist)
    if (c[0] + c[1] >= 10)
      cout << "  [" << VF::prettify(k) << "] " << c[0] << " vs " << c[1] << " (opt from unusable-user recs: "
           << orphanOpt[k] << ")\n";

  for (const string& w : watch) {
    for (int side = 0; side < 2; ++side) {
      auto& v = watchRecs[side][w];
      set<int> distinct(v.begin(), v.end());
      cout << "WATCH " << w << " side=" << side << " occurrences=" << v.size() << " records=" << distinct.size()
           << " :";
      for (int i : distinct) cout << " " << i;
      cout << "\n";
    }
  }
}

// Cursor on the last char, append at EOL: does the optimizer pick `a` or `A`?
TEST(StatsTokens, AppendAtEol) {
  const Lines initial({"keep them", "next line"});
  const Lines goal({"keep them, later", "next line"});
  const CompositionOptimizerParams params = CompositionOptimizerParams{}.withMaxResults(8);
  for (auto [name, config] : {pair{"uniform", Config::uniform()}, pair{"qwerty", Config::qwerty()},
                              pair{"colemakDh", Config::colemakDh()}}) {
    CompositionOptimizer opt(config);
    auto res = opt.optimize(initial, {0, 8}, goal, {0, 15}, params, "", NavBoundary(), NavContext(),
                            SearchControl::NEVER_STOP, nullptr);
    vector<Result> results = res.getResults();
    sort(results.begin(), results.end(), [](const Result& a, const Result& b) { return a.getCost() < b.getCost(); });
    cout << "== " << name << "\n";
    for (const Result& r : results) cout << "   " << r.getCost() << "  " << VF::prettify(r.getSequence().str()) << "\n";
  }
}

// Cursor on line 1, open a line above it: is `O` ever offered?
TEST(StatsTokens, OpenAbove) {
  const Lines initial({"first line", "second line"});
  const Lines goal({"first line", "new", "second line"});
  const CompositionOptimizerParams params = CompositionOptimizerParams{}.withMaxResults(8);
  CompositionOptimizer opt(Config::uniform());
  auto res = opt.optimize(initial, {2 - 1, 3}, goal, {1, 2}, params, "", NavBoundary(), NavContext(),
                          SearchControl::NEVER_STOP, nullptr);
  vector<Result> results = res.getResults();
  sort(results.begin(), results.end(), [](const Result& a, const Result& b) { return a.getCost() < b.getCost(); });
  for (const Result& r : results) cout << "   " << r.getCost() << "  " << VF::prettify(r.getSequence().str()) << "\n";
}
