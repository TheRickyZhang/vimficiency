module;
#include "LuaExports/Api.h"

export module vf.LuaExports.Common;

import std.compat;
import vf.LuaExports.AsyncJobRegistry;
import vf.Keyboard.Config;

export {

// Shared by every LuaExports/*.cpp: error/result types, the config global
// (defined in ConfigState.cpp), and request/result marshaling. Generated
// protobuf headers cannot be included here, so the helpers that touch a
// message are templates instantiated by the including .cpp.
namespace VF::LuaExports {

inline constexpr int MANUAL_EVICT_NONE = 0;
inline constexpr int MANUAL_EVICT_DRIFT = 1;
inline constexpr int MANUAL_EVICT_IDLE = 2;

struct ExportError {
  std::string message;
};

template <typename T>
using Expected = std::expected<T, ExportError>;

enum DEFAULT_KEYBOARD {
  NONE,
  UNIFORM,
  QWERTY,
  COLEMAK_DH,
};

extern Config g_config_internal;

// Merges a serialized `vf.config.Config` (proto/config.proto) into what is
// configured so far, then rebuilds `g_config_internal` and the other
// process-global state every search reads.
Expected<void> applyConfig(std::string_view serialized);
void resetConfig();
std::string describeConfig();
std::span<const char* const> countClassNames();

inline std::unexpected<ExportError> unexpectedError(std::string message) {
  return std::unexpected<ExportError>{ExportError{std::move(message)}};
}

inline VFByteSlice byteSlice(const std::string& storage) {
  return VFByteSlice{
      .data = storage.data(),
      .len = storage.size(),
  };
}

// One serialized protobuf message from src/LuaExports/proto/. A distinct type
// so raw text cannot be returned across the FFI unserialized.
struct Proto {
  std::string bytes;
};

using ProtoResult = Expected<Proto>;

template <typename Message>
Proto proto(const Message& message) {
  return Proto{message.SerializeAsString()};
}

template <typename Message>
Expected<Message> parseRequest(const char* data, size_t len) {
  Message message;
  if (!message.ParseFromArray(data, len)) {
    return unexpectedError("malformed " + std::string(message.GetTypeName()) + " request");
  }
  return message;
}

// Every VFByteSlice return is a `vf.Envelope` (proto/common.proto) holding the
// result message, an error, or pending. Lua decoder: `decode` in ffi/core.lua.
VFByteSlice storeResult(std::string& storage, ProtoResult result);
VFByteSlice storePending(std::string& storage);

// An exception unwinding out of an export into LuaJIT's C frames is undefined
// behaviour, so every export body runs inside one of the wrappers below.
// Anything Lua passes, ids included, fails as an `unexpectedError`; `CHECK` is
// for states C++ itself guarantees.

// For exports returning VFByteSlice: an escaping exception becomes an error
// envelope, which Lua raises like any other C++ error.
template <typename Body>
VFByteSlice guarded(std::string& storage, Body body) noexcept {
  try {
    return storeResult(storage, body());
  } catch (const std::exception& e) {
    return storeResult(storage, unexpectedError(std::string("C++ exception: ") + e.what()));
  } catch (...) {
    return storeResult(storage, unexpectedError("unknown C++ exception"));
  }
}

// `guarded` for exports taking a serialized `Request`; `body` receives it parsed.
template <typename Request, typename Body>
VFByteSlice guardedRequest(
    std::string& storage, const char* data, size_t len, Body body) noexcept {
  return guarded(storage, [&]() -> ProtoResult {
    auto request = parseRequest<Request>(data, len);
    if (!request) return std::unexpected(request.error());
    return body(*request);
  });
}

// `guarded` for a `*_poll` export: pending while the worker runs, then frees
// the job and returns its failure or `finish(state)`.
template <typename JobT, typename Finish>
VFByteSlice guardedPoll(
    std::string& storage, JobRegistry<JobT>& jobs, int id, Finish finish) noexcept {
  if (jobs.running(id)) return storePending(storage);
  return guarded(storage, [&]() -> ProtoResult {
    const std::unique_ptr<JobT> job = jobs.take(id);
    if (!job) return unexpectedError("unknown job_id " + std::to_string(id));
    if (!job->error().empty()) return unexpectedError(job->error());
    return finish(job->state());
  });
}

// For exports with no error channel: `noexcept` turns an escaping exception
// into std::terminate, which reports it and aborts.
template <typename Body>
auto noUnwind(Body body) noexcept {
  return body();
}

}  // namespace VF::LuaExports

}  // export
