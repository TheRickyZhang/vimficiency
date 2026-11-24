// Prints one buffer's journal records: snapshots, deltas, and the atoms and
// resyncs whose cursor row lies outside the text the journal holds.
//   VIMFY_JOURNAL_FILE=<file.vfj> VIMFY_JOURNAL_BUF=<buf> \
//     ./build/tests/vimfy_debug --gtest_filter='JournalBufferTrace.*'

#include <gtest/gtest.h>

#include "proto/journal.pb.h"

import std.compat;
import vf.Storage.RecordFile;

using namespace std;

namespace {

TEST(JournalBufferTrace, Records) {
  const char* file = getenv("VIMFY_JOURNAL_FILE");
  const char* bufText = getenv("VIMFY_JOURNAL_BUF");
  if (!file || !bufText) GTEST_SKIP() << "set VIMFY_JOURNAL_FILE and VIMFY_JOURNAL_BUF";
  const int buf = stoi(bufText);

  ifstream in(file, ios::binary);
  const string all(istreambuf_iterator<char>(in), {});
  string_view rest = all;
  string_view bytes;
  vf::journal::Record record;
  int lineCount = -1;
  int index = 0;
  int outside = 0;
  while (nextRecord(rest, bytes) && record.ParseFromArray(bytes.data(), bytes.size())) {
    index++;
    const string at = format("#{} ms={} tick={}", index, record.ms(), record.tick());
    switch (record.body_case()) {
      case vf::journal::Record::kSnapshot:
        if (record.snapshot().buf() != buf) break;
        lineCount = max(1, record.snapshot().lines_size());
        cout << at << " snapshot lines=" << record.snapshot().lines_size()
             << " name=" << record.snapshot().bufname()
             << " ft=" << record.snapshot().filetype() << "\n";
        break;
      case vf::journal::Record::kDelta:
        if (record.delta().buf() != buf) break;
        if (lineCount >= 0) {
          lineCount = max(1, lineCount - record.delta().old_count() + record.delta().lines_size());
        }
        cout << at << " delta first=" << record.delta().first()
             << " old=" << record.delta().old_count() << " new=" << record.delta().lines_size()
             << " -> lines=" << lineCount << "\n";
        break;
      case vf::journal::Record::kResync:
      case vf::journal::Record::kAtom: {
        const bool isAtom = record.has_atom();
        const int recordBuf = isAtom ? record.atom().buf() : record.resync().buf();
        if (recordBuf != buf) break;
        const vf::journal::ViewState& view = isAtom ? record.atom().view() : record.resync().view();
        if (view.row() < lineCount) break;
        if (outside++ < 20) {
          cout << at << (isAtom ? " atom keys=" + record.atom().keys() : string(" resync"))
               << " row=" << view.row() << " col=" << view.col()
               << " lines=" << lineCount << "\n";
        }
        break;
      }
      default:
        break;
    }
  }
  cout << "records " << index << " cursor outside text " << outside << "\n";
}

}  // namespace
