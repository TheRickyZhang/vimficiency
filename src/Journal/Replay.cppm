export module vf.Journal.Replay;

import std.compat;
import vf.Types.Lines;

namespace Journal {

struct BufferText {
  std::shared_ptr<Lines> lines;
  int version = 0;
  bool discontinuous = false;
};

}  // namespace Journal

export namespace Journal {

inline constexpr int FORMAT_VERSION = 3;

enum class EventKind : uint8_t {
  Start,   // a journal header: nothing before it carries over
  Resync,
  Atom,
};

enum class Decision : uint8_t { Record, Skip, Abort };

// A journal record that says where a window stands, with its buffer's text at
// that point. Rows and columns are 0-indexed.
struct Event {
  EventKind kind = EventKind::Start;
  int64_t ms = 0;
  int tick = 0;

  // Start only.
  int64_t wallEpoch = 0;

  int buf = 0;
  int win = 0;
  // Null when the journal holds no text for `buf`.
  SharedLines lines;
  // Differs between two events of a journal exactly when `lines` may.
  int textVersion = 0;
  // A snapshot replaced text the deltas did not lead to, so what changed it
  // is unknown.
  bool discontinuous = false;
  int row = 0;
  int col = 0;
  int windowHeight = 0;
  int scrollAmount = 0;
  // Only then is the state one a command can start from.
  bool normalMode = false;

  // Atom only.
  int atomIndex = 0;  // among the journal's atom records
  std::string keys;
  std::string typed;
  Decision decision = Decision::Record;
  bool lossy = false;
};

// The payload of the next entry of a file of appended single-record messages
// (`vf.journal.File`, `vf.journal.SampleFile`), advancing `in` past it. False
// at the end, which includes a truncated entry: a file still being written
// ends mid-record.
bool nextRecord(std::string_view& in, std::string_view& record);

// Rebuilds buffer text from a journal's snapshots and deltas and yields its
// other records in order. `bytes` must outlive the replay.
class Replay {
public:
  inline explicit Replay(std::string_view bytes) : rest(bytes) {}

  // False at the end of the journal.
  bool next(Event& out);

private:
  std::string_view rest;
  std::unordered_map<int, BufferText> buffers;
  int versions = 0;
  int atoms = 0;
};

}  // namespace Journal
