module;
#include "LuaExports/Api.h"

export module vf.LuaExports.Common;

import std.compat;
import vf.Boundary.NavBoundary;
import vf.Keyboard.Config;
import vf.Types.Lines;

export {

// Shared by every LuaExports/*.cpp: error/result types, the config global
// (defined in lua_exports.cpp), and request/result marshaling. Generated
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

inline std::unexpected<ExportError> unexpectedError(std::string message) {
  return std::unexpected<ExportError>{ExportError{std::move(message)}};
}

// Boundary for a sliced buffer. The slice is the whole edit region, so there is
// no horizontal prefix or suffix; only the lines cut off above and below vary.
inline NavBoundary sliceBoundary(const Lines& lines, bool hasLinesAbove, bool hasLinesBelow) {
  const int lastLine = lines.lastLine();
  return NavBoundary(lines, CursorPos(0, 0),
                     CursorPos(lastLine, lines.getSize(lastLine)),
                     hasLinesAbove, hasLinesBelow);
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

// For exports with no error channel: `noexcept` turns an escaping exception
// into std::terminate, which reports it and aborts.
template <typename Body>
auto noUnwind(Body body) noexcept {
  return body();
}

}  // namespace VF::LuaExports

}  // export
