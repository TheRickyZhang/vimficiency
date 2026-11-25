module vf.Explore.EditHandler;

import std.compat;
import vf.Interpreter.EditInterpreter;
import vf.Types.Mode;

using namespace std;

namespace Explore::EditHandler {

std::expected<bool, Rejected> validateBufferState(
    const Lines& newLines,
    const Lines& currentFencepost,
    const Lines& nextFencepost) {
  if (newLines == nextFencepost) return true;
  if (newLines == currentFencepost) return false;
  return std::unexpected(
      Rejected{"buffer state drifted outside the planned edit scope"});
}

std::expected<EditSuccess, Rejected> applyEdit(
    const Lines& postFencepost, const Lines& currentLines, CursorPos cursor,
    std::string_view text, const IndentOptions& options) {
  Lines simLines = currentLines;
  CursorPos simPos = cursor;
  Mode simMode = Mode::Normal;
  DotRepeat lastEdit;
  auto parsed = Edit::parseEdits(text);
  if (!parsed) {
    return std::unexpected(Rejected{"applyEdit: edit text failed to parse"});
  }
  for (const ParsedEdit& e : *parsed) {
    Edit::applyEdit(simLines, simPos, simMode, e, &lastEdit, false, 0, 0, false, nullptr, options);
  }

  const bool reachesPostFencepost = simLines == postFencepost;
  return EditSuccess{
      .postLines = std::move(simLines),
      .postCursor = simPos,
      .reachesPostFencepost = reachesPostFencepost,
      .entersInsert = simMode == Mode::Insert,
  };
}

}  // namespace Explore::EditHandler
