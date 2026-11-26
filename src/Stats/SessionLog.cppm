export module vf.Stats.SessionLog;

import std.compat;
import vf.Stats.Run;

export namespace Stats {

struct LoggedSession : Run {
  std::string kind;
  int keyCount;
};

struct SessionHistory {
  // In log order.
  std::vector<LoggedSession> runs;
  // The current stamp's, `scoring <N>·<id>`.
  std::string label;
  // Left out by `currentOnly`.
  int hidden = 0;
  int sessions = 0;
};

std::expected<SessionHistory, std::string> readSessionLog(const std::string& path,
                                                        std::string_view stampBytes,
                                                        bool currentOnly);

// Appends a session to the log, writing its stamp first when the log lacks
// it. `request` is a serialized `vf.stats.AppendSessionRequest`
// (proto/stats.proto).
std::expected<void, std::string> appendSession(std::string_view request);

}  // namespace Stats
