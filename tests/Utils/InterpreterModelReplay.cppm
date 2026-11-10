export module vf.tests.Utils.InterpreterModelReplay;

import std.compat;
export import vf.Types.CursorPos;
export import vf.Types.Lines;
export import vf.Types.Mode;

export {

struct InterpreterReplayResult {
  Lines lines;
  CursorPos cursor;
  Mode mode;
};

InterpreterReplayResult applyUserSequence(
    Lines lines, CursorPos cursor, std::string_view sequence);

}  // export
