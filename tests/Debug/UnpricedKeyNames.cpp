// Tallies the `<...>` names in every journal's pressed keys that the effort
// model has no key for.
//   VIMFY_JOURNAL_DIR=<dir> ./build/tests/vimfy_debug --gtest_filter='UnpricedKeyNames.*'

#include <gtest/gtest.h>

import std.compat;
import vf.Journal.Replay;
import vf.Keyboard.ToKeys.MovementToKeys;
import vf.Types.KeyNotation;

using namespace std;
namespace fs = std::filesystem;

// Rewrites the fixture behind `SequenceToKeysTest.EveryNeovimKeyNameIsClassified`
// from a file of names, one per line (`tests/lua/key_names.lua` lists them).
//   VIMFY_KEY_NAMES=<names> VIMFY_KEY_NAMES_OUT=<fixture> ./build/tests/vimfy_debug \
//     --gtest_filter='UnpricedKeyNames.WriteFixture'
TEST(UnpricedKeyNames, WriteFixture) {
  const char* in = getenv("VIMFY_KEY_NAMES");
  const char* out = getenv("VIMFY_KEY_NAMES_OUT");
  if (!in || !out) GTEST_SKIP() << "set VIMFY_KEY_NAMES and VIMFY_KEY_NAMES_OUT";
  ifstream names(in);
  ofstream fixture(out);
  string name;
  while (getline(names, name)) {
    fixture << name << "\t" << (globalSequenceToKeys().tokenize(name).empty() ? "unpriced" : "priced") << "\n";
  }
}

TEST(UnpricedKeyNames, InJournals) {
  const char* dir = getenv("VIMFY_JOURNAL_DIR");
  if (!dir) GTEST_SKIP() << "set VIMFY_JOURNAL_DIR";
  const auto& tok = globalSequenceToKeys();

  map<string, int> unpriced;
  int keys = 0, names = 0, files = 0;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() != ".vfj") continue;
    files++;
    ifstream in(entry.path(), ios::binary);
    const string bytes(istreambuf_iterator<char>(in), {});
    Journal::Replay replay(bytes);
    Journal::Event event;
    while (replay.next(event)) {
      if (event.kind != Journal::EventKind::Atom) continue;
      for (const string_view key : splitKeys(event.typed)) {
        keys++;
        if (!isKeyName(key)) continue;
        names++;
        if (tok.tokenize(key).empty()) unpriced[string(key)]++;
      }
    }
  }

  vector<pair<string, int>> rows(unpriced.begin(), unpriced.end());
  ranges::sort(rows, [](const auto& a, const auto& b) { return a.second > b.second; });
  int total = 0;
  for (const auto& [name, count] : rows) {
    total += count;
    cout << "  " << count << "\t" << name << "\n";
  }
  cout << "files=" << files << " keys=" << keys << " names=" << names << " unpriced=" << total << "\n";
}
