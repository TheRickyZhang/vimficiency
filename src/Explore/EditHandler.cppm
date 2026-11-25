export module vf.Explore.EditHandler;

import std.compat;
export import vf.Explore.Rejected;
export import vf.Types.CursorPos;
export import vf.Types.Lines;
export import vf.VimCore.VimOptions;

export {

namespace Explore::EditHandler {

// Buffer-state validation under the strict (b) policy.
//   true  : newLines matches nextFencepost (advance phase).
//   false : newLines matches currentFencepost (no-op — native undo or re-sync).
// Rejected: drift outside the planned edit scope.
std::expected<bool, Rejected> validateBufferState(
    const Lines& newLines,
    const Lines& currentFencepost,
    const Lines& nextFencepost);

struct EditSuccess {
  Lines postLines;
  CursorPos postCursor{0, 0};
  bool reachesPostFencepost = false;
  bool entersInsert = false;
};

std::expected<EditSuccess, Rejected> applyEdit(
    const Lines& postFencepost, const Lines& currentLines, CursorPos cursor,
    std::string_view text, const IndentOptions& options = {});

}  // namespace Explore::EditHandler

}  // export
