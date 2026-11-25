// How often a recorded gj/gk stayed on its buffer line. Run with
//   VIMFY_JOURNAL_DIR=~/.local/share/nvim/vimficiency/journal ./build/tests/vimfy_debug
//     --gtest_also_run_disabled_tests --gtest_filter='ScreenMotionCount.*'

#include <gtest/gtest.h>
#include <cstdio>

import std.compat;
import vf.Journal.Replay;

using namespace std;
namespace fs = std::filesystem;

namespace {

struct Tally {
  int crossed = 0;
  int sameRowMoved = 0;
  int sameRowStill = 0;
  int batched = 0;
  int noPrior = 0;
};

// 0 unless `keys` is exactly [count]gj or [count]gk.
char screenMotion(string_view keys) {
  size_t i = 0;
  while (i < keys.size() && keys[i] >= '0' && keys[i] <= '9') i++;
  const string_view rest = keys.substr(i);
  if (rest == "gj") return 'j';
  if (rest == "gk") return 'k';
  return 0;
}

struct State {
  int buf = 0;
  int row = 0;
  int col = 0;
};

}  // namespace

TEST(ScreenMotionCount, DISABLED_Journals) {
  const char* dir = getenv("VIMFY_JOURNAL_DIR");
  ASSERT_NE(dir, nullptr);
  vector<fs::path> files;
  for (const auto& e : fs::directory_iterator(dir)) {
    if (e.path().extension() == ".vfj") files.push_back(e.path());
  }
  ranges::sort(files);

  map<char, Tally> tallies;
  map<string, int> embedded;
  for (const fs::path& file : files) {
    ifstream in(file, ios::binary);
    const string bytes{istreambuf_iterator<char>(in), istreambuf_iterator<char>()};
    Journal::Replay replay(bytes);
    // Per window, the last state that is real: a resync, or a tick's last atom.
    map<int, State> real;
    bool pending = false;
    Journal::Event held;
    auto settle = [&](bool lastOfTick) {
      if (!pending) return;
      pending = false;
      const char motion = screenMotion(held.keys);
      if (motion) {
        Tally& t = tallies[motion];
        const auto it = real.find(held.win);
        if (!lastOfTick) {
          t.batched++;
        } else if (it == real.end() || it->second.buf != held.buf) {
          t.noPrior++;
        } else if (it->second.row != held.row) {
          t.crossed++;
        } else if (it->second.col != held.col) {
          t.sameRowMoved++;
        } else {
          t.sameRowStill++;
        }
      } else if (held.keys.contains("gj") || held.keys.contains("gk")) {
        embedded[held.keys]++;
      }
      if (lastOfTick) {
        real[held.win] = {held.buf, held.row, held.col};
      } else {
        real.erase(held.win);
      }
    };

    Journal::Event event;
    while (replay.next(event)) {
      switch (event.kind) {
        case Journal::EventKind::Start:
          settle(true);
          real.clear();
          break;
        case Journal::EventKind::Resync:
          settle(true);
          real[event.win] = {event.buf, event.row, event.col};
          break;
        case Journal::EventKind::Atom:
          settle(!pending || held.tick != event.tick);
          held = event;
          pending = true;
          break;
        case Journal::EventKind::TickEnd:
          settle(true);
          break;
      }
    }
    settle(true);
  }

  for (const auto& [motion, t] : tallies) {
    printf("g%c: crossed %d · same row, col moved %d · same row, still %d · batched %d · no prior %d\n",
           motion, t.crossed, t.sameRowMoved, t.sameRowStill, t.batched, t.noPrior);
  }
  printf("other atoms containing gj/gk:\n");
  for (const auto& [keys, n] : embedded) printf("  %-20s %d\n", keys.c_str(), n);
}
