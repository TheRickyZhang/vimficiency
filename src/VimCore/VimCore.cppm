module;
#include <cassert>

export module vf.VimCore.VimCore;

import std.compat;
export import vf.Types.CursorPos;
export import vf.Types.Lines;
export import vf.Types.EdgeType;

export {

// VimCore: Character classification, string helpers, position stepping,
// and core word motion logic. Used by VimMotionUtils, VimEndpointUtils,
// and BufferIndex.
//
// Edit operations (deleteRange, insertText, etc.) are in VimEditUtils.h

namespace VimCore {

// =============================================================================
// 1. String/Line Helpers
// =============================================================================

// Check if line is blank (empty or whitespace-only)
bool isBlankLine(std::string_view s);

// Vim paragraph motions use empty lines as separators; whitespace-only lines
// still belong to a paragraph.
bool isParagraphSeparatorLine(std::string_view s);

// Return column of first non-blank character (or 0 if all blank)
int firstNonBlankColInLine(std::string_view s);

// Target for `^`: first non-blank if one exists; otherwise Vim keeps the
// cursor where it is on a whitespace-only line.
int caretTargetColInLine(std::string_view s, int currentCol);

// =============================================================================
// 2. CursorPos Stepping
// =============================================================================

template<bool Forward>
inline CursorPos step(const Lines& lines, CursorPos pos) {
  if constexpr (Forward) return lines.getNextPos(pos);
  else return lines.getPrevPos(pos);
}

template<bool Forward>
inline CursorPos stepBack(const Lines& lines, CursorPos pos) {
  if constexpr (Forward) return lines.getPrevPos(pos);
  else return lines.getNextPos(pos);
}

// Ports of Neovim's position steppers (inc/incl/dec/decl) and gchar_pos. Vim's
// line model has a NUL slot at col == size, which these treat as a visitable
// position: `inc` returns 2 when landing on it, `incl`/`decl` step over it on
// non-empty lines, and `gchar` reads it as 0. They mutate `col` directly, so
// callers that establish a cursor position must `setCol` afterwards.
//
// Return codes: -1 at end/start of buffer (pos unchanged), 0 moved within the
// line, 1 crossed a line boundary, 2 (`inc` only) moved onto the NUL slot.
inline int gchar(const Lines& lines, CursorPos pos) {
  assert(pos.line >= 0 && pos.line <= lines.lastLine() && pos.col >= 0);
  const Line& line = lines[pos.line];
  if (pos.col >= static_cast<int>(line.size())) return 0;
  return static_cast<unsigned char>(line[pos.col]);
}

inline int inc(const Lines& lines, CursorPos& pos) {
  int len = static_cast<int>(lines[pos.line].size());
  if (pos.col < len) {
    pos.col++;
    return pos.col < len ? 0 : 2;
  }
  if (pos.line < lines.lastLine()) {
    pos.line++;
    pos.col = 0;
    return 1;
  }
  return -1;
}

inline int incl(const Lines& lines, CursorPos& pos) {
  int r = inc(lines, pos);
  if (r >= 1 && pos.col != 0) r = inc(lines, pos);
  return r;
}

inline int dec(const Lines& lines, CursorPos& pos) {
  if (pos.col > 0) {
    pos.col--;
    return 0;
  }
  if (pos.line > 0) {
    pos.line--;
    pos.col = static_cast<int>(lines[pos.line].size());
    return 1;
  }
  return -1;
}

inline int decl(const Lines& lines, CursorPos& pos) {
  int r = dec(lines, pos);
  if (r == 1 && pos.col != 0) r = dec(lines, pos);
  return r;
}

// =============================================================================
// 3. Word Motion Core (depends on stepping + char classification)
// =============================================================================

// Core motion that returns raw result - POSITION_OUTSIDE_BOUNDARY if motion
// would go past buffer boundary. Used by VimEndpointUtils for boundary detection.
//
// lineBounded: When true, motion stops at line boundaries instead of crossing.
//   - Backward from whitespace: returns CursorPos(line, 0) instead of crossing to prev line
//   - Forward through whitespace: returns CursorPos(line, lastCol) instead of crossing to next line
//   Used by daw text object which doesn't cross lines from indentation.
template<bool Forward, EdgeType Edge>
CursorPos motionWordCore(CursorPos pos,
                        const Lines& lines,
                        bool big,
                        bool skipCurrent,
                        bool lineBounded = false);

// =============================================================================
// 4. Paragraph Helpers (depends on line classification)
// =============================================================================

// Returns the first line index of the paragraph containing lineIdx.
int paragraphStartLine(const Lines& lines, int lineIdx);

// Returns the last line index of the paragraph containing lineIdx.
int paragraphEndLine(const Lines& lines, int lineIdx);

} // namespace VimCore

}  // export
