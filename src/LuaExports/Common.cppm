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

// Boundary for a sliced buffer: `lastCol` is inclusive, so the exclusive end
// is one past it on the last line.
inline NavBoundary sliceBoundary(const Lines& lines, int firstCol, int lastCol,
                                 bool hasLinesAbove, bool hasLinesBelow) {
  return NavBoundary(lines, CursorPos(0, firstCol),
                     CursorPos(lines.lastLine(), lastCol + 1),
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

// Every VFByteSlice return is a one-entry msgpack map: {value}, {error}, or
// {pending}. Lua decoder: `decode` in ffi/core.lua.
inline VFByteSlice storeBytes(std::string& storage, Expected<Packed> result) {
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

}  // namespace VF::LuaExports

}  // export
