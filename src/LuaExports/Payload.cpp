module;
#include "LuaExports/Api.h"

module vf.LuaExports.Payload;

import std.compat;
import vf.LuaExports.Common;
import vf.Optimizer.DiffPlanner.DiffState;
import vf.Types.Lines;

using namespace std;

namespace VF::LuaExports::payload {

Expected<Lines> linesFromSlices(const VFByteSlice* slices, size_t count) {
  if (count == 0) {
    return unexpectedError("line array must contain at least one line");
  }
  Lines lines;
  lines.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    const string_view line = sliceView(slices[i]);
    if (line.contains('\n')) {
      return unexpectedError(
          "line array contains newline byte at line " + to_string(i));
    }
    if (line.contains('\0')) {
      return unexpectedError(
          "line array contains NUL byte at line " + to_string(i));
    }
    lines.push_back(string(line));
  }
  return lines;
}

namespace {

void writeSpan(MsgpackWriter& w, const CursorPos& begin, const CursorPos& end) {
  w.map(4)
      .key("begin_row").integer(begin.line)
      .key("begin_col").integer(begin.col)
      .key("end_row").integer(end.line)
      .key("end_col").integer(end.col);
}

}  // namespace

void writeDiffRegions(MsgpackWriter& w, const vector<DiffState>& diffs) {
  w.array(diffs.size());
  for (const DiffState& d : diffs) {
    const CursorPos goalEnd = DiffText::advancePositionByText(d.beginPos, d.insertedText);
    w.map(2);
    w.key("init");
    writeSpan(w, d.beginPos, d.endPos);
    w.key("goal");
    writeSpan(w, d.beginPos, goalEnd);
  }
}

}  // namespace VF::LuaExports::payload
