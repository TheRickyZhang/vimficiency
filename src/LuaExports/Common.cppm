module;
#include "LuaExports/Api.h"

export module vf.LuaExports.Common;

import std.compat;
export import vf.LuaExports.Msgpack;
import vf.Boundary.NavBoundary;
import vf.Keyboard.Config;
import vf.Types.Lines;

export {

// Shared by every LuaExports/*.cpp: error/result types, the FFI config globals
// (defined in lua_exports.cpp), and argument/result marshaling. The msgpack
// encoder lives in vf.LuaExports.Msgpack, shared payload shapes in
// vf.LuaExports.Payload, and optimizer-facing logic in vf.LuaExports.Logic.
namespace VF::LuaExports {

inline constexpr int MANUAL_EVICT_NONE = 0;
inline constexpr int MANUAL_EVICT_DRIFT = 1;
inline constexpr int MANUAL_EVICT_IDLE = 2;

struct ExportError {
  std::string message;
};

template <typename T>
using Expected = std::expected<T, ExportError>;

using PackedResult = Expected<Packed>;

enum DEFAULT_KEYBOARD {
  NONE,
  UNIFORM,
  QWERTY,
  COLEMAK_DH,
};

extern VFConfig g_config_ffi;
extern Config g_config_internal;

inline std::unexpected<ExportError> unexpectedError(std::string message) {
  return std::unexpected<ExportError>{ExportError{std::move(message)}};
}

inline Expected<std::string_view> requiredBytes(
    const char* data,
    size_t len,
    std::string_view name) {
  if (!data && len > 0) {
    return unexpectedError("missing " + std::string(name));
  }
  return data ? std::string_view(data, len) : std::string_view{};
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

inline std::string_view sliceView(VFByteSlice slice) {
  return std::string_view(slice.data, slice.len);
}

// Msgpack exports return a one-entry map: {value}, {error}, or {pending}. Lua
// decoder: `decode` in ffi/core.lua.
inline VFByteSlice storeBytes(std::string& storage, PackedResult result) {
  MsgpackWriter w;
  w.map(1);
  if (result) {
    w.key("value").value(*result);
  } else {
    w.key("error").str(result.error().message);
  }
  storage = w.take().bytes;
  return byteSlice(storage);
}

inline VFByteSlice storePending(std::string& storage) {
  storage = MsgpackWriter().map(1).key("pending").boolean(true).take().bytes;
  return byteSlice(storage);
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

// Protobuf exports return a `vf.Envelope` (proto/common.proto) holding the
// message, an error, or pending. Lua decoder: `unwrap_proto` in ffi/core.lua.
VFByteSlice storeProto(std::string& storage, ProtoResult result);
VFByteSlice storeProtoPending(std::string& storage);

// An exception unwinding out of an export into LuaJIT's C frames is undefined
// behaviour, so every export body runs inside one of the wrappers below.

// For exports returning VFByteSlice: an escaping exception becomes an {error}
// envelope, which Lua raises like any other C++ error.
template <typename Body>
VFByteSlice guarded(std::string& storage, Body body) noexcept {
  try {
    return storeBytes(storage, body());
  } catch (const std::exception& e) {
    return storeBytes(storage, unexpectedError(std::string("C++ exception: ") + e.what()));
  } catch (...) {
    return storeBytes(storage, unexpectedError("unknown C++ exception"));
  }
}

// `guarded` for exports whose result is a protobuf message.
template <typename Body>
VFByteSlice guardedProto(std::string& storage, Body body) noexcept {
  try {
    return storeProto(storage, body());
  } catch (const std::exception& e) {
    return storeProto(storage, unexpectedError(std::string("C++ exception: ") + e.what()));
  } catch (...) {
    return storeProto(storage, unexpectedError("unknown C++ exception"));
  }
}

// For exports with no error channel: `noexcept` turns an escaping exception
// into std::terminate, which reports it and aborts.
template <typename Body>
auto noUnwind(Body body) noexcept {
  return body();
}

}  // namespace VF::LuaExports

}  // export
