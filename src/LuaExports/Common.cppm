module;
#include "LuaExports/Api.h"

export module vf.LuaExports.Common;

import std.compat;
import vf.Boundary.NavBoundary;
import vf.Keyboard.Config;
import vf.Types.Lines;

export {

// Shared by every LuaExports/*.cpp: error/result types, wire constants, the
// FFI config globals (defined in lua_exports.cpp), and argument/result
// marshaling. Wire encoders/decoders live in vf.LuaExports.Payload and
// optimizer-facing logic in vf.LuaExports.Logic.
namespace VF::LuaExports {

inline constexpr char EVENT_FIELD_SEP = '\x1f';
inline constexpr char EVENT_RECORD_SEP = '\x1e';
inline constexpr int MANUAL_EVICT_NONE = 0;
inline constexpr int MANUAL_EVICT_DRIFT = 1;
inline constexpr int MANUAL_EVICT_IDLE = 2;

struct ExportError {
  std::string message;
};

template <typename T>
using Result = std::expected<T, ExportError>;

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

inline Result<std::string_view> requiredBytes(
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

inline VFByteSlice storeBytes(std::string& storage, Result<std::string> result) {
  if (!result) {
    storage = "ERROR: " + result.error().message;
    return byteSlice(storage);
  }
  storage = std::move(*result);
  return byteSlice(storage);
}

inline std::string packInts(int first, int second) {
  return std::to_string(first) + std::string(1, EVENT_FIELD_SEP) + std::to_string(second);
}

inline std::string joinWithTrailingNewline(const std::vector<std::string>& items) {
  std::ostringstream oss;
  for (const auto& item : items) {
    oss << item << "\n";
  }
  return oss.str();
}

}  // namespace VF::LuaExports

}  // export
