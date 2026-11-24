#pragma once

#include <string>
#include <vector>

#include "proto/journal.pb.h"

struct JournalAtomOptions {
  vf::journal::Atom::Decision decision = vf::journal::Atom::record;
  std::string mode = "n";
  int buf = 1;
  int win = 1000;
  bool lossy = false;
  // Empty: the keys were typed as they ran.
  std::string typed;
  std::string kind = "motion";
  std::string typedInsert;
  // False: an atom journaled before `typed_insert` existed.
  bool insertTagged = true;
};

// Journal bytes as capture/journal.lua writes them: one single-record
// `vf.journal.File` appended per record.
class JournalBuilder {
public:
  static constexpr int BUF = 1;
  static constexpr int WIN = 1000;

  JournalBuilder& header(long long wallEpoch = 1000, int version = 3) {
    vf::journal::Record record;
    record.mutable_header()->set_version(version);
    record.mutable_header()->set_started_wall_epoch(wallEpoch);
    now = 0;
    return write(record);
  }

  // Later records share a new tick, `ms` after the previous one.
  JournalBuilder& next(long long ms = 100) {
    tick++;
    now += ms;
    return *this;
  }

  JournalBuilder& snapshot(const std::vector<std::string>& lines, int buf = BUF,
                           const std::string& filetype = "") {
    vf::journal::Record record;
    record.mutable_snapshot()->set_buf(buf);
    record.mutable_snapshot()->set_filetype(filetype);
    for (const std::string& line : lines) record.mutable_snapshot()->add_lines(line);
    return write(record);
  }

  JournalBuilder& delta(int first, int oldCount, const std::vector<std::string>& lines,
                        int buf = BUF) {
    vf::journal::Record record;
    record.mutable_delta()->set_buf(buf);
    record.mutable_delta()->set_first(first);
    record.mutable_delta()->set_old_count(oldCount);
    for (const std::string& line : lines) record.mutable_delta()->add_lines(line);
    return write(record);
  }

  JournalBuilder& resync(int row, int col, int buf = BUF, int win = WIN) {
    vf::journal::Record record;
    record.mutable_resync()->set_buf(buf);
    record.mutable_resync()->set_win(win);
    setView(record.mutable_resync()->mutable_view(), row, col);
    return write(record);
  }

  JournalBuilder& atom(const std::string& keys, int row, int col,
                       const JournalAtomOptions& options = {}) {
    vf::journal::Record record;
    vf::journal::Atom* atom = record.mutable_atom();
    atom->set_buf(options.buf);
    atom->set_win(options.win);
    atom->set_keys(keys);
    atom->set_kind(options.kind);
    atom->set_typed(options.typed.empty() ? keys : options.typed);
    if (options.insertTagged) atom->set_typed_insert(options.typedInsert);
    atom->set_decision(options.decision);
    atom->set_lossy(options.lossy);
    atom->set_mode(options.mode);
    setView(atom->mutable_view(), row, col);
    return write(record);
  }

  JournalBuilder& tickEnd() {
    vf::journal::File file;
    file.add_records()->mutable_tick_end();
    out += file.SerializeAsString();
    return *this;
  }

  const std::string& bytes() const { return out; }

private:
  static void setView(vf::journal::ViewState* view, int row, int col) {
    view->set_row(row);
    view->set_col(col);
    view->set_window_height(40);
    view->set_scroll_amount(20);
  }

  JournalBuilder& write(vf::journal::Record& record) {
    record.set_ms(now);
    record.set_tick(tick);
    vf::journal::File file;
    *file.add_records() = record;
    out += file.SerializeAsString();
    return *this;
  }

  std::string out;
  long long now = 0;
  int tick = 1;
};
