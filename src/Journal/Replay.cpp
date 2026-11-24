module;
#include "proto/journal.pb.h"

module vf.Journal.Replay;

import std.compat;
import vf.Storage.RecordFile;
import vf.Types.Lines;
import vf.Versions;

using namespace std;

namespace Journal {

namespace {

// Neovim's empty buffer is one empty line.
void keepOneLine(Lines& lines) {
  if (lines.empty()) lines.emplace_back();
}

void applySnapshot(unordered_map<int, BufferText>& buffers, int& versions,
                   const vf::journal::Snapshot& snapshot) {
  Lines lines;
  lines.reserve(snapshot.lines_size());
  for (const string& line : snapshot.lines()) lines.emplace_back(line);
  keepOneLine(lines);

  BufferText& text = buffers[snapshot.buf()];
  text.filetype = snapshot.filetype();
  if (text.lines && *text.lines == lines) return;
  if (text.lines) text.discontinuous = true;
  text.lines = make_shared<Lines>(std::move(lines));
  text.version = ++versions;
}

void applyDelta(unordered_map<int, BufferText>& buffers, int& versions,
                const vf::journal::Delta& delta) {
  const auto it = buffers.find(delta.buf());
  if (it == buffers.end() || !it->second.lines) return;
  BufferText& text = it->second;

  const int first = delta.first();
  const int oldCount = delta.old_count();
  if (first < 0 || oldCount < 0 || first + oldCount > ssize(*text.lines)) {
    text.lines = nullptr;
    text.discontinuous = true;
    return;
  }

  // An event may still hold the text as it was.
  if (text.lines.use_count() > 1) text.lines = make_shared<Lines>(*text.lines);
  Lines& lines = *text.lines;
  lines.erase(lines.begin() + first, lines.begin() + first + oldCount);
  lines.insert(lines.begin() + first, delta.lines().begin(), delta.lines().end());
  keepOneLine(lines);
  text.version = ++versions;
}

Event stateEvent(unordered_map<int, BufferText>& buffers, EventKind kind,
                 const vf::journal::Record& record, int buf, int win,
                 const vf::journal::ViewState& view) {
  Event event;
  event.kind = kind;
  event.ms = record.ms();
  event.tick = record.tick();
  event.buf = buf;
  event.win = win;
  event.row = view.row();
  event.col = view.col();
  event.windowHeight = view.window_height();
  event.scrollAmount = view.scroll_amount();

  const auto it = buffers.find(buf);
  if (it != buffers.end()) {
    BufferText& text = it->second;
    event.lines = text.lines;
    event.textVersion = text.version;
    event.discontinuous = text.discontinuous;
    event.filetype = text.filetype;
    text.discontinuous = false;
  }
  return event;
}

Decision decisionOf(vf::journal::Atom::Decision decision) {
  switch (decision) {
    case vf::journal::Atom::skip:  return Decision::Skip;
    case vf::journal::Atom::abort: return Decision::Abort;
    default:                       return Decision::Record;
  }
}

}  // namespace

bool Replay::next(Event& out) {
  string_view bytes;
  vf::journal::Record record;
  while (nextRecord(rest, bytes) && record.ParseFromArray(bytes.data(), bytes.size())) {
    switch (record.body_case()) {
      case vf::journal::Record::kHeader:
        if (record.header().version() != JOURNAL_FORMAT) {
          rest = {};
          return false;
        }
        buffers.clear();
        out = Event{};
        out.ms = record.ms();
        out.tick = record.tick();
        out.wallEpoch = record.header().started_wall_epoch();
        return true;

      case vf::journal::Record::kSnapshot:
        applySnapshot(buffers, versions, record.snapshot());
        break;

      case vf::journal::Record::kDelta:
        applyDelta(buffers, versions, record.delta());
        break;

      case vf::journal::Record::kResync: {
        const vf::journal::Resync& resync = record.resync();
        out = stateEvent(buffers, EventKind::Resync, record, resync.buf(), resync.win(),
                         resync.view());
        out.normalMode = true;
        return true;
      }

      case vf::journal::Record::kAtom: {
        const vf::journal::Atom& atom = record.atom();
        out = stateEvent(buffers, EventKind::Atom, record, atom.buf(), atom.win(), atom.view());
        out.atomIndex = atoms++;
        out.keys = atom.keys();
        out.atomKind = atom.kind();
        out.typed = atom.typed();
        out.typedInsert = atom.typed_insert();
        out.typedInsertKnown = atom.has_typed_insert();
        out.decision = decisionOf(atom.decision());
        out.lossy = atom.lossy();
        out.normalMode = atom.mode() == "n";
        return true;
      }

      case vf::journal::Record::kTickEnd:
        out = Event{};
        out.kind = EventKind::TickEnd;
        return true;

      case vf::journal::Record::BODY_NOT_SET:
        break;
    }
  }
  return false;
}

}  // namespace Journal
