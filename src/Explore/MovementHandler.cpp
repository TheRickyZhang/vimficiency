module vf.Explore.MovementHandler;

import std.compat;
import vf.Interpreter.MovementInterpreter;

using namespace std;

namespace Explore::MovementHandler {

namespace {

expected<MotionSuccess, Rejected> finishMove(
    const Lines& lines,
    CursorPos newCursor,
    const NavBoundary& boundary) {
  if (!lines.contains(newCursor)) {
    return unexpected(Rejected{"motion landed outside the current buffer"});
  }
  if (!boundary.isPositionInBounds(newCursor, lines.lastLine(),
                                   lines.back().size())) {
    return unexpected(Rejected{"motion landed outside the allowed boundary"});
  }
  return MotionSuccess{.newCursor = newCursor};
}

}  // namespace

std::expected<MotionSuccess, Rejected> applyMovement(
    const Lines& lines, CursorPos cursor, std::string_view text,
    const NavBoundary& boundary,
    const NavContext& navContext) {
  if (text.empty()) {
    return std::unexpected(Rejected{"motion text must be non-empty"});
  }
  auto parsed = parseMovements(text);
  if (!parsed) {
    return std::unexpected(Rejected{
        "motion text failed to parse: " +
        formatMovementParseError(parsed.error())});
  }
  return finishMove(
      lines, simulateMovements(cursor, text, lines, navContext), boundary);
}

std::expected<MotionSuccess, Rejected> acceptCursorMove(
    const Lines& lines,
    CursorPos newCursor,
    const NavBoundary& boundary) {
  return finishMove(lines, newCursor, boundary);
}

} // namespace Explore::MovementHandler
