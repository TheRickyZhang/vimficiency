// Does the interpreter model match Neovim when a linewise (or
// linewise-promoted) delete is entered from a clamped cursor (col < curswant)?
// For each command it also reports how the landing depends on the entry
// curswant: never, as min(curswant, lastCol), or some other way.
//   ./build/tests/vimfy_debug --gtest_filter='ClampedLinewiseDelete.*'

#include <gtest/gtest.h>

import std.compat;
import vf.Types.CursorPos;
import vf.Types.Lines;
import vf.Types.Mode;
import vf.Utils.PrettyText;
import vf.tests.Utils.InterpreterModelReplay;
import vf.tests.Utils.NeovimOracle;
import vf.tests.Utils.OracleReplay;

using namespace std;

namespace {

struct Tally {
  int runs = 0, noop = 0, mismatches = 0;
  int linewise = 0, independent = 0, followsClamp = 0, other = 0;
};

string randomLine(mt19937& rng) {
  auto pick = [&](int lo, int hi) { return uniform_int_distribution<int>(lo, hi)(rng); };
  const int kind = pick(0, 9);
  if (kind == 0) return "";
  if (kind == 1) return string(pick(1, 4), ' ');
  string s(pick(0, 3), ' ');
  const int words = pick(1, 4);
  for (int w = 0; w < words; w++) {
    if (w > 0) s += ' ';
    for (int i = pick(1, 4); i > 0; i--) s += static_cast<char>('a' + pick(0, 5));
    if (pick(0, 4) == 0) s += pick(0, 1) ? "." : ",";
  }
  if (pick(0, 5) == 0) s += string(pick(1, 2), ' ');
  return s;
}

TEST(ClampedLinewiseDelete, ModelMatchesOracle) {
  NeovimOracle oracle;
  mt19937 rng(20261124);
  auto pick = [&](int lo, int hi) { return uniform_int_distribution<int>(lo, hi)(rng); };
  const vector<string> commands = {"dd", "2dd", "dj", "dk", "d}", "d{", "d)", "d(",
                                   "dw", "dW", "db", "dB", "de", "dE", "dge", "dgE"};
  map<string, Tally> tallies;
  int printed = 0;
  const int cases = 400;

  for (int i = 0; i < cases; i++) {
    vector<string> text;
    text.push_back("0123456789abcdefghij");
    for (int n = pick(2, 5); n > 0; n--) text.push_back(randomLine(rng));
    const Lines initial(text);
    const int entryLine = pick(1, initial.lastLine());
    const string down(entryLine, 'j');
    const int entryCol = initial[entryLine].lastCol();
    if (entryCol >= 8) continue;

    for (const string& cmd : commands) {
      Tally& t = tallies[cmd];
      // The same visible cursor with curswant at the column, at 8, and at end of line.
      struct Entry { CursorPos start; string prefix; };
      const array<Entry, 3> entries = {{{CursorPos(entryLine, entryCol), ""},
                                        {CursorPos(0, 8), down},
                                        {CursorPos(0, 0), "$" + down}}};
      vector<InterpreterReplayResult> ours;

      bool allMatch = true;
      for (const Entry& entry : entries) {
        const string seq = entry.prefix + cmd;
        InterpreterReplayResult r = applyUserSequence(initial, entry.start, seq);
        auto ok = OracleReplay::matches(oracle, initial, entry.start, seq, r.lines, r.cursor,
                                        r.mode, cmd);
        if (!ok) {
          allMatch = false;
          if (printed < 12) {
            printed++;
            cout << "MISMATCH " << VF::prettify(seq) << " on " << initial << "\n"
                 << ok.message() << "\n";
          }
        }
        // A vertical move afterwards exposes the curswant the delete left behind.
        // Skipped for a failed command: Neovim drops the rest of the typed keys.
        for (const char* after : {"k", "j"}) {
          if (r.lines == initial) break;
          const string moved = seq + after;
          const InterpreterReplayResult m = applyUserSequence(initial, entry.start, moved);
          auto movedOk = OracleReplay::matches(oracle, initial, entry.start, moved, m.lines,
                                               m.cursor, m.mode, cmd);
          if (!movedOk) {
            allMatch = false;
            if (printed < 12) {
              printed++;
              cout << "MISMATCH " << VF::prettify(moved) << " on " << initial << "\n"
                   << movedOk.message() << "\n";
            }
          }
        }
        ours.push_back(std::move(r));
      }
      t.runs++;
      if (!allMatch) {
        t.mismatches++;
        continue;
      }
      if (ours[0].lines == initial) {
        t.noop++;
        continue;
      }
      if (ours[0].lines.size() == initial.size()) continue;
      t.linewise++;
      const CursorPos a = ours[0].cursor, b = ours[1].cursor, c = ours[2].cursor;
      const int lastCol = ours[0].lines[a.line].lastCol();
      if (a.pos() == b.pos() && b.pos() == c.pos()) {
        t.independent++;
      } else if (a.line == b.line && b.line == c.line &&
                 a.col == min(entryCol, lastCol) && b.col == min(8, lastCol) &&
                 c.col == lastCol) {
        t.followsClamp++;
      } else {
        t.other++;
        if (printed < 24) {
          printed++;
          cout << "OTHER " << cmd << " on " << initial << " entry line " << entryLine
               << " lands " << a << " / " << b << " / " << c << "\n";
        }
      }
    }
  }
  cout << "cmd    runs  mismatch  noop  removedLines  independent  min(curswant,lastCol)  other\n";
  for (const string& cmd : commands) {
    const Tally& t = tallies[cmd];
    cout << left << setw(6) << cmd << right << setw(5) << t.runs << setw(10) << t.mismatches
         << setw(6) << t.noop << setw(14) << t.linewise << setw(13) << t.independent
         << setw(23) << t.followsClamp << setw(7) << t.other << "\n";
  }
}

}  // namespace
