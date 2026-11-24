export module vf.Stats.SessionLog;

import std.compat;
import vf.Stats.Run;

export namespace Stats {

// One stamp's logged sessions.
struct Group {
  std::string label;
  bool current = false;
  int64_t latest = 0;
  std::vector<Run> sessions;
};

struct SessionHistory {
  std::vector<Group> groups;
  int sessions = 0;
  int mark = 0;
  int watch = 0;
  int recall = 0;
  int suggest = 0;
  int64_t commands = 0;
};

std::expected<SessionHistory, std::string> readSessionLog(const std::string& path,
                                                        std::string_view stampBytes);

// Appends a session to the log, writing its stamp first when the log lacks
// it. `request` is a serialized `vf.stats.AppendSessionRequest`
// (proto/stats.proto).
std::expected<void, std::string> appendSession(std::string_view request);

}  // namespace Stats
