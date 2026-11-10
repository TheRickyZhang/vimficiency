module;
#include "LuaExports/Api.h"

export module vf.LuaExports.Common;

import std.compat;
export import vf.Boundary.NavBoundary;
export import vf.Keyboard.Config;
export import vf.Keyboard.Finger;
export import vf.Keyboard.Hand;
export import vf.Keyboard.Key;
export import vf.Optimizer.CountPenalty;
export import vf.Optimizer.DiffPlanner.DiffState;
export import vf.Optimizer.OptimizerParamOverrides;
export import vf.Types.Lines;

export {

// Cross-cutting declarations and inline helpers shared by every LuaExports/*.cpp.
// Out-of-line definitions for `payload::*` live in Payload.cpp; for `logic::*`
// in Logic.cpp. FFI export bodies live in *Exports.cpp.
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

namespace helpers {

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

Result<OptimizerParamOverrides> parseOverrides(std::string_view text);

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

}  // namespace helpers

namespace payload {

// Wire format: a sequence of "<len>:<bytes>" chunks. Encoders and decoders
// are paired so adding a new field type requires touching one place.

inline std::string encodeField(std::string_view field) {
  return std::to_string(field.size()) + ":" + std::string(field);
}

template <std::integral T>
std::string encodeField(T v) {
  return encodeField(std::string_view(std::to_string(v)));
}

inline std::string doubleField(double v) {
  std::ostringstream oss;
  oss.setf(std::ios::fixed);
  oss.precision(6);
  oss << v;
  return oss.str();
}

inline std::string encodeField(double v) {
  return encodeField(std::string_view(doubleField(v)));
}

template <typename... Fields>
std::string encodeFields(const Fields&... fields) {
  std::string out;
  ((out += encodeField(fields)), ...);
  return out;
}

template <typename Strings>
std::string encodeStringList(const Strings& values) {
  std::string out;
  for (std::string_view value : values) out += encodeField(value);
  return out;
}

struct KeyTrackingEvent {
  std::string mode;
  std::string keyTyped;
};

Result<std::vector<std::string>> decodeLengthPrefixedStrings(std::string_view encoded);
Result<Lines> decodeLineArray(std::string_view encoded);
Result<std::vector<KeyTrackingEvent>> decodeKeyTrackingEvents(std::string_view encoded);

// One record per diff, EVENT_FIELD_SEP between fields, '\n' after each record:
//   init_begin_row, init_begin_col, init_end_row, init_end_col,
//   goal_begin_row, goal_begin_col, goal_end_row, goal_end_col
// 0-indexed, half-open [begin, end); an empty init span is a pure insertion,
// an empty goal span a pure deletion. Both spans share begin (the common
// prefix is identical in both buffers) but are serialized explicitly so each
// pane is consumed independently. Lua decoder: `parse_diff_region` in
// ffi/optimizer.lua.
std::string encodeDiffRegions(const std::vector<DiffState>& diffs);

}  // namespace payload

namespace logic {

Result<std::string> buildKeySequence(std::string_view encoded);
std::pair<int, int> computeSearchRegion(
    int startRow,
    int endRow,
    const Lines& startLines,
    const Lines& endLines,
    int padding);
int manualEvictReason(
    int startRow,
    int cursorRow,
    int64_t lastKeyTimeNs,
    bool hasLastKey,
    int64_t nowNs,
    int maxSearchLines,
    int manualIdleTimeoutSeconds);

}  // namespace logic

}  // namespace VF::LuaExports

}  // export
