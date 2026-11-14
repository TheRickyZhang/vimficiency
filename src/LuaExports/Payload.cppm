export module vf.LuaExports.Payload;

import std.compat;
import vf.LuaExports.Common;
import vf.Optimizer.DiffPlanner.DiffState;
import vf.Types.Lines;

export namespace VF::LuaExports::payload {

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

Expected<std::vector<std::string>> decodeLengthPrefixedStrings(std::string_view encoded);
Expected<Lines> decodeLineArray(std::string_view encoded);
Expected<std::vector<KeyTrackingEvent>> decodeKeyTrackingEvents(std::string_view encoded);

// One record per diff, EVENT_FIELD_SEP between fields, '\n' after each record:
//   init_begin_row, init_begin_col, init_end_row, init_end_col,
//   goal_begin_row, goal_begin_col, goal_end_row, goal_end_col
// 0-indexed, half-open [begin, end); an empty init span is a pure insertion,
// an empty goal span a pure deletion. Both spans share begin (the common
// prefix is identical in both buffers) but are serialized explicitly so each
// pane is consumed independently. Lua decoder: `parse_diff_region` in
// ffi/optimizer.lua.
std::string encodeDiffRegions(const std::vector<DiffState>& diffs);

}  // namespace VF::LuaExports::payload
