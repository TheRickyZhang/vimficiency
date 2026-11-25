#include "LuaExports/Api.h"
#include "proto/common.pb.h"

import std.compat;
import vf.Journal.Sampler;
import vf.LuaExports.Common;
import vf.Stats.SessionLog;
import vf.Stats.Summary;

using namespace std;
using namespace VF::LuaExports;

extern "C" {

VFByteSlice vf_append_session(const char* request, size_t request_len) {
  static string storage;
  return guarded(storage, [&]() -> ProtoResult {
    const auto appended = Stats::appendSession(string_view(request, request_len));
    if (!appended) return unexpectedError(appended.error());
    return proto(vf::Empty{});
  });
}

VFByteSlice vf_stats_summary(const char* request, size_t request_len) {
  static string storage;
  return guarded(storage, [&]() -> ProtoResult {
    const int64_t now = chrono::duration_cast<chrono::seconds>(
        chrono::system_clock::now().time_since_epoch()).count();
    auto summary = Stats::summarize(string_view(request, request_len), now);
    if (!summary) return unexpectedError(summary.error());
    return Proto{std::move(*summary)};
  });
}

VFByteSlice vf_stats_runs(const char* request, size_t request_len) {
  static string storage;
  return guarded(storage, [&]() -> ProtoResult {
    auto runs = Stats::listRuns(string_view(request, request_len));
    if (!runs) return unexpectedError(runs.error());
    return Proto{std::move(*runs)};
  });
}

VFByteSlice vf_stats_run(const char* request, size_t request_len) {
  static string storage;
  return guarded(storage, [&]() -> ProtoResult {
    auto detail = Journal::runDetail(string_view(request, request_len));
    if (!detail) return unexpectedError(detail.error());
    return Proto{std::move(*detail)};
  });
}

}
