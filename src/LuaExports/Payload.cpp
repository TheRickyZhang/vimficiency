module vf.LuaExports.Payload;

import std.compat;
import vf.LuaExports.Common;
import vf.Optimizer.DiffPlanner.DiffState;
import vf.Types.Lines;

using namespace std;

namespace VF::LuaExports::payload {

Result<vector<string>> decodeLengthPrefixedStrings(string_view encoded) {
  vector<string> parts;
  size_t pos = 0;
  while (pos < encoded.size()) {
    const size_t colon = encoded.find(':', pos);
    if (colon == string_view::npos) {
      return unexpectedError("invalid length-prefixed payload");
    }
    size_t len = 0;
    const char* lenEnd = encoded.data() + colon;
    const auto [ptr, ec] = from_chars(encoded.data() + pos, lenEnd, len);
    if (ec != errc{} || ptr != lenEnd) {
      return unexpectedError("invalid length-prefixed payload");
    }
    pos = colon + 1;
    if (len > encoded.size() - pos) {
      return unexpectedError("truncated length-prefixed payload");
    }
    parts.emplace_back(encoded.substr(pos, len));
    pos += len;
  }
  return parts;
}

Result<Lines> decodeLineArray(string_view encoded) {
  return decodeLengthPrefixedStrings(encoded).and_then([](const vector<string>& parts)
      -> Result<Lines> {
    if (parts.empty()) {
      return unexpectedError(
          "line array payload must contain at least one line");
    }
    Lines lines;
    lines.reserve(parts.size());
    for (size_t i = 0; i < parts.size(); ++i) {
      const auto& part = parts[i];
      if (part.find('\n') != string::npos) {
        return unexpectedError(
            "line array payload contains newline byte at line " + to_string(i));
      }
      if (part.find('\0') != string::npos) {
        return unexpectedError(
            "line array payload contains NUL byte at line " + to_string(i));
      }
      lines.push_back(part);
    }
    return lines;
  });
}

Result<vector<KeyTrackingEvent>> decodeKeyTrackingEvents(string_view encoded) {
  vector<KeyTrackingEvent> events;
  size_t start = 0;
  while (start < encoded.size()) {
    size_t end = encoded.find(EVENT_RECORD_SEP, start);
    if (end == string_view::npos) {
      end = encoded.size();
    }
    const string_view record = encoded.substr(start, end - start);
    if (!record.empty()) {
      const size_t sep = record.find(EVENT_FIELD_SEP);
      if (sep == string_view::npos) {
        return unexpectedError("invalid key event payload");
      }
      events.push_back({
          string(record.substr(0, sep)),
          string(record.substr(sep + 1)),
      });
    }
    start = end + 1;
  }
  return events;
}

string encodeDiffRegions(const vector<DiffState>& diffs) {
  ostringstream oss;
  for (const DiffState& d : diffs) {
    const CursorPos goalEnd = DiffText::advancePositionByText(d.beginPos, d.insertedText);
    oss << d.beginPos.line << EVENT_FIELD_SEP << d.beginPos.col << EVENT_FIELD_SEP
        << d.endPos.line << EVENT_FIELD_SEP << d.endPos.col << EVENT_FIELD_SEP
        << d.beginPos.line << EVENT_FIELD_SEP << d.beginPos.col << EVENT_FIELD_SEP
        << goalEnd.line << EVENT_FIELD_SEP << goalEnd.col << '\n';
  }
  return oss.str();
}

}  // namespace VF::LuaExports::payload
