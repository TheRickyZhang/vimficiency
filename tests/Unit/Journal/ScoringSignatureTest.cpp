#include <gtest/gtest.h>

import std.compat;
import vf.Effort.RunningEffort;
import vf.Journal.Evaluate;
import vf.Journal.Windows;
import vf.Keyboard.Config;
import vf.Keyboard.KeyedSequence;
import vf.Optimizer.Analyze;
import vf.Optimizer.BuildTypedCommands;
import vf.Optimizer.CountPenalty;
import vf.Optimizer.GlobalRuntimeOptions;
import vf.Optimizer.SearchControl;
import vf.Optimizer.SearchStats;
import vf.Types.Lines;
import vf.Versions;
import vf.VimCore.VimOptions;

using namespace std;
using Journal::Outcome;
using Journal::Window;
using Journal::WindowState;

namespace {

// The plugin's defaults (lua/vimficiency/config/init.lua).
const Journal::EvaluateParams PARAMS{
    .slicePadding = 5,
    .maxSearchLines = 500,
    .resultsCalculated = 20,
};

// ---------------------------------------------------------------------------
// Effort model, dumped exhaustively.
// ---------------------------------------------------------------------------

constexpr array<string_view, KEY_COUNT> KEY_NAMES = {
#define X(name, str) str,
#include "Keyboard/XMacroKey.inc"
#undef X
};

constexpr array<string_view, CountClassCOUNT> COUNT_CLASS_NAMES = {
#define X(name, str) str,
#include "Optimizer/XMacroCountClass.inc"
#undef X
};

// Notations the optimizer emits that are not KeyedSequence constants.
constexpr array<string_view, 5> EXTRA_NOTATIONS = {"<Tab>", "<C-w>", "<C-v>", "<C-b>", "<lt>"};

constexpr int MAX_DUMPED_COUNT = 20;
constexpr int DUMPED_SPAN = 3;

string effortSection(string_view name, const Config& config) {
  auto effort = [&](const PhysicalKeys& keys) { return RunningEffort(keys, config).getEffort(); };
  const ScoreWeights& w = config.weights;
  string out = format("{} weights {} {} {} {} {} {}\n", name, w.key_weight, w.same_finger_weight,
                      w.same_key_weight, w.alt_hand_weight, w.good_roll_weight, w.bad_roll_weight);
  for (int k = 0; k < KEY_COUNT; ++k) {
    const Key key = Key(k);
    out += format("{} key {} {:.6f} {:.6f} {:.6f}\n", name, KEY_NAMES[k], effort({key}),
                  effort({Key::Key_Shift, key}), effort({Key::Key_Ctrl, key}));
  }
  for (int i = 0; i < KS_COUNT; ++i) {
    const KeyedSequence& ks = KeyedSequence::byId(KSId(i));
    out += format("{} keyed {} {:.6f}\n", name, ks.seq.str(), effort(ks.keys));
  }
  for (string_view notation : EXTRA_NOTATIONS) {
    out += format("{} notation {} {:.6f}\n", name, notation, getEffort(notation, config));
  }
  string printable;
  for (char c = ' '; c <= '~'; ++c) printable += c;
  KeyedSequence typed = KeyedSequence::i;
  typed += buildTypedCommands(Lines{printable});
  out += format("{} insert_printable {:.6f}\n", name, effort(typed.keys));
  return out;
}

template <size_t... I>
string countPenaltySection(index_sequence<I...>) {
  using Penalty = double (*)(const CountPenaltyInput&);
  constexpr array<Penalty, CountClassCOUNT> penalties = {&runtimeCountPenalty<CountClass(I)>...};
  string out;
  for (size_t c = 0; c < CountClassCOUNT; ++c) {
    out += format("count_penalty {}", COUNT_CLASS_NAMES[c]);
    for (const int span : {0, DUMPED_SPAN}) {
      for (int count = 1; count <= MAX_DUMPED_COUNT; ++count) {
        out += format(" {:.6f}", penalties[c]({.count = count, .span = span}));
      }
    }
    out += '\n';
  }
  return out;
}

string effortCanonical() {
  return effortSection("uniform", Config::uniform()) + effortSection("qwerty", Config::qwerty()) +
         effortSection("colemak_dh", Config::colemakDh()) +
         countPenaltySection(make_index_sequence<CountClassCOUNT>{});
}

// ---------------------------------------------------------------------------
// Search cases, each built so one component decides its best cost.
// ---------------------------------------------------------------------------

struct Case {
  string name;
  Lines before;
  Lines after;
  pair<int, int> from;
  pair<int, int> to;
  string keys;
  bool uniformToo = false;
};

Lines numbered(int count, string_view stem) {
  Lines lines;
  for (int i = 0; i < count; ++i) lines.push_back(format("{}{} {}", stem, i, stem));
  return lines;
}

vector<Case> referenceCases() {
  const Lines rows = numbered(12, "row");
  Lines paragraphs = {"p one", "p two", "p three", "p four", "p five", "p six", ""};
  for (const string& line : numbered(8, "q")) paragraphs.push_back(line);
  const Lines tall = numbered(20, "row");
  Lines scroll = numbered(30, "line");
  Lines sixteenDeleted = tall;
  sixteenDeleted.erase(sixteenDeleted.begin() + 1, sixteenDeleted.begin() + 17);
  const string findLine = "xxxx xxxx xxxx xxxx qxxxxxxxxxxx";
  string findEdited = findLine;
  findEdited[23] = 'y';
  const Lines code = {
      "#include <vector>",
      "",
      "int sum(const std::vector<int>& xs) {",
      "  int total = 0;",
      "  for (int x : xs) {",
      "    total += x;",
      "  }",
      "  return total;",
      "}",
      "",
      "int main() {",
      "  return sum({1, 2, 3});",
      "}",
  };

  return {
      // w
      {"nav_word", {"sa sb sc sd se", "sf sg"}, {"sa sb sc sd se", "sf sg"}, {0, 0}, {0, 3}, "w"},
      // b; directional pruning
      {"nav_back_word", {"sa sb sc sd se", "sf sg"}, {"sa sb sc sd se", "sf sg"}, {0, 9}, {0, 3},
       "bb"},
      // F
      {"nav_find_back", {"the quick brown fox jumps over"}, {"the quick brown fox jumps over"},
       {0, 29}, {0, 8}, "Fk"},
      // f to the one char exactly fMotionThreshold left of the goal
      {"nav_find_threshold", {findLine}, {findLine}, {0, 0}, {0, 23}, "fqlll"},
      // nav effortWeight/distanceWeight
      {"nav_far_word", code, code, {0, 0}, {11, 9}, "jjjjjjjjjjjww"},
      // $
      {"nav_line_end", {"ab ab ab ab ab ab", "x"}, {"ab ab ab ab ab ab", "x"}, {0, 0}, {0, 16},
       "$"},
      // MovementLine penalty: 6j beats jjjjjj by 0.55
      {"nav_counted_j", rows, rows, {0, 2}, {6, 2}, "jjjjjj"},
      // BufferIndex counted w: 8w beats $bbbb and f;;
      {"nav_counted_w", {"ab ab ab ab ab ab ab ab ab ab ab ab", "x"},
       {"ab ab ab ab ab ab ab ab ab ab ab ab", "x"}, {0, 0}, {0, 24}, "wwwwwwww"},
      // nav exploreFactor: 7k's penalty puts it just under the cap of the user's unpenalized 7k
      {"nav_counted_k", tall, tall, {16, 2}, {9, 2}, "7k"},
      // }
      {"nav_paragraph", paragraphs, paragraphs, {0, 0}, {6, 0}, "}"},
      // )
      {"nav_sentence", {"Tom and Tim ate tea. Then Ted left."},
       {"Tom and Tim ate tea. Then Ted left."}, {0, 0}, {0, 21}, ")"},
      // gg
      {"nav_gg", rows, rows, {11, 0}, {0, 0}, "gg"},
      // <C-d>
      {"nav_scroll", scroll, scroll, {0, 0}, {20, 0}, "<C-d>"},

      // dw
      {"edit_dw", {"alpha beta gamma"}, {"alpha gamma"}, {0, 6}, {0, 6}, "dw"},
      // delete-to-change conversion (C)
      {"edit_C", {"delta epsilon"}, {"delta ruby"}, {0, 6}, {0, 9}, "Cruby<Esc>"},
      // r
      {"edit_r", {"zeta eta theta"}, {"zeta pta theta"}, {0, 5}, {0, 5}, "rp"},
      // minPrefixCount: 3x would beat vlld
      {"edit_3x", {"abcdefgh"}, {"abcgh"}, {0, 3}, {0, 3}, "3x"},
      // minPrefixCount: 4dd; transformMaxResultsPerStartPos
      {"edit_4dd", {"a1", "b2", "c3", "d4", "e5", "f6"}, {"a1", "f6"}, {1, 0}, {1, 0}, "4dd"},
      // maxPrefixCount: 16dd
      {"edit_16dd", tall, sixteenDeleted, {1, 0}, {1, 0}, "16dd"},
      // .
      {"edit_dot", {"aa aa qq"}, {"qq"}, {0, 0}, {0, 0}, "dw."},

      // i
      {"ins_i", {"foo(a)"}, {"foo(b, a)"}, {0, 4}, {0, 6}, "ib, <Esc>"},
      // a; composition maxResults and diffAlgorithm
      {"ins_a", {"alpha gamma"}, {"alpha beta gamma"}, {0, 4}, {0, 9}, "a beta<Esc>", true},
      // I
      {"ins_I", {"  int a;"}, {"  const int a;"}, {0, 7}, {0, 7}, "Iconst <Esc>"},
      // A
      {"ins_A", {"iota kappa"}, {"iota kappa lambda"}, {0, 0}, {0, 16}, "A lambda<Esc>"},
      // o
      {"ins_o", {"aa", "cc"}, {"aa", "bb", "cc"}, {0, 1}, {1, 1}, "obb<Esc>"},
      // O
      {"ins_O", {"aa", "cc"}, {"bb", "aa", "cc"}, {0, 1}, {0, 1}, "Obb<Esc>"},

      // multi-edit planning
      {"comp_two_edits", {"alpha beta gamma", "delta epsilon", "zeta eta theta", "iota kappa"},
       {"alpha gamma", "delta epsilon", "zeta eta theta", "iota"}, {0, 6}, {3, 3}, "dwjjjdaw"},
      // J plan entered from another line
      {"comp_join_far", {"aaa", "bbb", "ccc"}, {"aaa bbb", "ccc"}, {2, 1}, {0, 3}, "kkJ"},
      // composition fMotionThreshold
      {"comp_find_threshold", {findLine}, {findEdited}, {0, 0}, {0, 23}, "fqlllry"},
      // composition effortWeight/distanceWeight
      {"comp_rename", {"int cnt = 0;", "cnt = cnt + 1;", "return cnt;"},
       {"int tot = 0;", "tot = tot + 1;", "return tot;"}, {0, 4}, {2, 9},
       "cwtot<Esc>j0cwtot<Esc>wwcwtot<Esc>jwcwtot<Esc>"},
      // composition useDirectionalPruning
      {"comp_two_edits_middle", {"first old line", "keep this untouched", "second old line"},
       {"abc line here", "keep this untouched", "def line here"}, {0, 0}, {2, 12},
       "c2wabc<Esc>A here<Esc>jj0c2wdef<Esc>A here<Esc>", true},
      // VimDiff moveDeleteScale
      {"comp_main",
       {"int main() {", "  int n = 0;", "  for(int i = 0; i < n; i++) {",
        "    cout << i << endl;", "  }", "}"},
       {"int main() {", "  int m = 1;", "  for(int i = 0; i < m; i++) {",
        "    cout << i+1 << endl;", "  }", "}"},
       {1, 6}, {3, 14}, "rmf0r1jfnrmjfia+1<Esc>", true},

      // composition exploreFactor: best just over the cap of a paste
      {"outcome_unbeaten", {"ab cd"}, {"ab zcd"}, {0, 3}, {0, 3}, "p"},
      // composition exploreFactor: best just under the cap of a paste
      {"comp_effort_cap", {"ab cd"}, {"ab xcd"}, {0, 3}, {0, 3}, "p"},
      // nav exploreFactor: best just over the cap of an unmodeled `*`
      {"outcome_no_result", rows, rows, {0, 0}, {6, 3}, "*"},
  };
}

WindowState state(const Lines& lines, pair<int, int> at) {
  return WindowState{
      .lines = make_shared<const Lines>(lines),
      .row = at.first,
      .col = at.second,
      .windowHeight = 40,
      .scrollAmount = 20,
  };
}

struct Scored {
  string outcome;
  double bestCost = 0;
  SearchStopReason stopReason = SearchStopReason::Unknown;
};

// `Journal::evaluate` with deadlineMs 0, keeping the stop reason it drops.
Scored score(const Case& c, const Config& config) {
  const Window window{.start = state(c.before, c.from),
                      .end = state(c.after, c.to),
                      .keys = c.keys,
                      .typed = c.keys};
  const Journal::WindowSlice sliced = Journal::sliceWindow(window, PARAMS);
  if (sliced.outcome != Outcome::Scored) return {.outcome = "unscorable"};
  const AnalyzeOutput analyzed =
      analyze(sliced.inputs, PARAMS.resultsCalculated, config, SearchControl::NEVER_STOP);
  Scored out{.stopReason = analyzed.stopReason};
  if (analyzed.results.empty()) {
    out.outcome = analyzed.unbeaten ? "unbeaten" : "no_result";
  } else {
    out.outcome = "scored";
    out.bestCost = analyzed.results.front().getCost();
  }
  return out;
}

string searchCanonical() {
  const Config qwerty = Config::qwerty();
  const Config uniform = Config::uniform();
  string out;
  for (const Case& c : referenceCases()) {
    vector<pair<string_view, const Config*>> evaluated = {{"qwerty", &qwerty}};
    if (c.uniformToo) evaluated.emplace_back("uniform", &uniform);
    for (const auto& [configName, config] : evaluated) {
      const Scored scored = score(c, *config);
      EXPECT_TRUE(scored.stopReason != SearchStopReason::MaxPopsReached)
          << c.name << " under " << configName << " hit the node cap; shrink it";
      out += format("{} {} {} {:.6f} {}\n", c.name, configName, scored.outcome, scored.bestCost,
                    to_string(scored.stopReason));
    }
  }
  return out;
}

uint64_t fnv1a(string_view text) {
  uint64_t hash = 0xcbf29ce484222325;
  for (const unsigned char byte : text) {
    hash ^= byte;
    hash *= 0x100000001b3;
  }
  return hash;
}

}  // namespace

// Pins the effort model and the best costs of a reference set, so a change that
// moves scoring has to bump SCORING_VERSION (dev/tests/testing.md § Scoring signature).
TEST(ScoringSignature, MatchesTheRecordedScoringVersion) {
  ASSERT_EQ(VimOptions::shiftwidth(), 8);
  ASSERT_FALSE(globalRuntimeOptions().useCountPenaltyOverrides);

  const string effort = effortCanonical();
  const string search = searchCanonical();

  const string signature = format("{:016x}", fnv1a(effort + search));
  EXPECT_EQ(signature, SCORING_SIGNATURE)
      << "Effort model:\n"
      << effort << "Reference costs (name config outcome bestCost stop):\n"
      << search
      << "If this scoring change is intended, bump SCORING_VERSION in src/Versions.cppm and set "
         "SCORING_SIGNATURE to \""
      << signature << "\"; otherwise the change moved costs unexpectedly.";
}
