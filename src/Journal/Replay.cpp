module;
#include "proto/journal.pb.h"

module vf.Journal.Replay;

import std.compat;
import vf.Types.Lines;

using namespace std;

namespace Journal {

namespace {

// Field 1 (`File.records`), length-delimited.
constexpr char RECORDS_TAG = 0x0A;

bool readVarint(string_view& in, uint64_t& out) {
  out = 0;
  for (int shift = 0; shift < 64 && !in.empty(); shift += 7) {
    const uint8_t byte = in.front();
    in.remove_prefix(1);
    out |= uint64_t(byte & 0x7F) << shift;
    if (!(byte & 0x80)) return true;
  }
  return false;
}

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

bool nextRecord(string_view& in, string_view& record) {
  string_view at = in;
  if (at.empty() || at.front() != RECORDS_TAG) return false;
  at.remove_prefix(1);
  uint64_t size = 0;
  if (!readVarint(at, size) || size > at.size()) return false;
  record = at.substr(0, size);
  in = at.substr(size);
  return true;
}

bool Replay::next(Event& out) {
  string_view bytes;
  vf::journal::Record record;
  while (nextRecord(rest, bytes) && record.ParseFromArray(bytes.data(), bytes.size())) {
    switch (record.body_case()) {
      case vf::journal::Record::kHeader:
        if (record.header().version() != FORMAT_VERSION) {
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
        out.typed = atom.typed();
        out.decision = decisionOf(atom.decision());
        out.lossy = atom.lossy();
        out.normalMode = atom.mode() == "n";
        return true;
      }

      case vf::journal::Record::BODY_NOT_SET:
        break;
    }
  }
  return false;
}

}  // namespace Journal
