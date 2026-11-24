export module vf.Stats.SessionLog;

import std.compat;
import vf.Stats.Run;

export namespace Stats {

// The counts are of every stamp's sessions.
struct SessionHistory {
  // In log order.
  std::vector<Run> runs;
  // The current stamp's, `scoring <N>·<id>`.
  std::string label;
  // Left out by `currentOnly`.
  int hidden = 0;
  int sessions = 0;
  int mark = 0;
  int watch = 0;
  int recall = 0;
  int suggest = 0;
  int64_t commands = 0;
};

std::expected<SessionHistory, std::string> readSessionLog(const std::string& path,
                                                        std::string_view stampBytes,
                                                        bool currentOnly);

// Appends a session to the log, writing its stamp first when the log lacks
// it. `request` is a serialized `vf.stats.AppendSessionRequest`
// (proto/stats.proto).
std::expected<void, std::string> appendSession(std::string_view request);

}  // namespace Stats
