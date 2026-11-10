module;
#include <cstdio>

module vf.VimCore.VimCore;

import std.compat;
import vf.VimCore.CharMask;

using namespace std;

namespace VimCore {

// =============================================================================
// 1. String/Line Helpers
// =============================================================================

bool isBlankLine(std::string_view s) {
  for (char c : s) {
    if (!CharMask::isWhitespace(c))
      return false;
  }
  return true; // empty or whitespace-only
}

bool isParagraphSeparatorLine(std::string_view s) {
  return s.empty();
}

int firstNonBlankColInLine(std::string_view s) {
  for (int i = 0; i < (int)s.size(); ++i) {
    char c = s[i];
    if (!CharMask::isWhitespace(c))
      return i;
  }
  return 0;
}

int caretTargetColInLine(std::string_view s, int currentCol) {
  for (int i = 0; i < static_cast<int>(s.size()); ++i) {
    char c = s[i];
    if (!CharMask::isWhitespace(c)) return i;
  }
  if (s.empty()) return 0;
  return static_cast<int>(s.size()) - 1;
}

// =============================================================================
// 2. Word Motion Core
// =============================================================================

namespace {
// Cold path helper for lineBounded returns
template<bool Forward>
[[gnu::noinline, gnu::cold]]
CursorPos handleLineBounded(CursorPos prev, const Lines& lines) {
  if constexpr (Forward) {
    return CursorPos(prev.line, lines[prev.line].lastCol());
  } else {
    return CursorPos(prev.line, 0);
  }
}
} // anonymous namespace

template<bool Forward, EdgeType Edge>
CursorPos motionWordCore(CursorPos pos,
                        const Lines& lines,
                        bool big,
                        bool skipCurrent,
                        bool lineBounded) {
  // ---------------------------------------------------------------------------
  // skipCurrent: Move off current position before searching
  // ---------------------------------------------------------------------------
  if (skipCurrent) {
    CursorPos prevPos = pos;
    CharMask prevClass(lines.get(pos));

    pos = step<Forward>(lines, pos);
    if (pos == prevPos) {
      return POSITION_OUTSIDE_BOUNDARY;
    }

    CharMask currClass(lines.get(pos));

    // b/B (backward WordEdge): Empty line is a word, stop here
    if constexpr (!Forward && Edge == EdgeType::WordEdge) {
      if (currClass.newLine()) {
        return pos;
      }
    }

    // ge/gE (backward NextEdge): Line/word boundaries are stopping points
    if constexpr (!Forward && Edge == EdgeType::NextEdge) {
      bool crossedLine = (pos.line != prevPos.line);
      if (currClass.newLine()) {
        return pos;
      }
      if ((crossedLine || !sameSpan(prevClass, currClass, big)) &&
          !currClass.blank()) {
        return pos;
      }
    }
    // e/E (forward WordEdge): No early termination
  }

  CharMask c(lines.get(pos));

  // Phase 1: If starting on blank, skip to first non-blank
  if (c.blank()) {
    do {
      CursorPos prev = pos;
      pos = step<Forward>(lines, pos);
      if (pos == prev) return POSITION_OUTSIDE_BOUNDARY;

      if (lineBounded && pos.line != prev.line) {
        return handleLineBounded<Forward>(prev, lines);
      }

      c = CharMask(lines.get(pos));
      // An empty line is its own word in Vim — `w` stops at the first empty
      // line crossed, regardless of where the scan started.
      if constexpr (Forward && Edge == EdgeType::NextEdge) {
        if (c.newLine()) return pos;
      }
      if constexpr (!Forward && Edge == EdgeType::WordEdge) {
        if (c.newLine()) return pos;
      }
      if constexpr (!Forward && Edge == EdgeType::NextEdge) {
        if (c.newLine()) return pos;
      }
    } while (c.blank());

    if constexpr (Edge == EdgeType::NextEdge) return pos;
    if constexpr (Edge == EdgeType::GapEdge) return stepBack<Forward>(lines, pos);
  }

  // Phase 2: Skip current word span
  bool crossedLine = false;
  while (true) {
    CursorPos prev = pos;
    CharMask prevClass = c;
    pos = step<Forward>(lines, pos);
    if (pos == prev) {
      if constexpr (Edge == EdgeType::WordEdge) return prev;
      return POSITION_OUTSIDE_BOUNDARY;
    }
    c = CharMask(lines.get(pos));
    if (pos.line != prev.line) {
      crossedLine = true;
      break;
    }
    if (!continuesSpan(prevClass, c, big)) break;
  }

  if constexpr (Edge == EdgeType::WordEdge) return stepBack<Forward>(lines, pos);

  // Empty line is a word
  if (crossedLine && c.newLine()) {
    if constexpr (Edge == EdgeType::NextEdge) return pos;
    if constexpr (Edge == EdgeType::GapEdge) return stepBack<Forward>(lines, pos);
  }

  // Phase 3: If at non-blank different type (word only, not bigWord)
  if (!c.blank()) {
    if constexpr (Edge == EdgeType::NextEdge) return pos;
    if constexpr (Edge == EdgeType::GapEdge) return stepBack<Forward>(lines, pos);
  }

  // Phase 4: Skip whitespace to reach next word (but stop at empty lines)
  while (c.whitespace()) {
    CursorPos prev = pos;
    pos = step<Forward>(lines, pos);
    if (pos == prev) return POSITION_OUTSIDE_BOUNDARY;

    if (lineBounded && pos.line != prev.line) {
      return handleLineBounded<Forward>(prev, lines);
    }

    c = CharMask(lines.get(pos));
    if (c.newLine()) {
      if constexpr (Edge == EdgeType::NextEdge) return pos;
      if constexpr (Edge == EdgeType::GapEdge) return stepBack<Forward>(lines, pos);
    }
  }

  if constexpr (Edge == EdgeType::GapEdge) return stepBack<Forward>(lines, pos);
  return pos;
}

// Explicit instantiations (6 combinations used by word motions)
template CursorPos motionWordCore<true, EdgeType::WordEdge>(CursorPos, const Lines&, bool, bool, bool);
template CursorPos motionWordCore<true, EdgeType::GapEdge>(CursorPos, const Lines&, bool, bool, bool);
template CursorPos motionWordCore<true, EdgeType::NextEdge>(CursorPos, const Lines&, bool, bool, bool);
template CursorPos motionWordCore<false, EdgeType::WordEdge>(CursorPos, const Lines&, bool, bool, bool);
template CursorPos motionWordCore<false, EdgeType::GapEdge>(CursorPos, const Lines&, bool, bool, bool);
template CursorPos motionWordCore<false, EdgeType::NextEdge>(CursorPos, const Lines&, bool, bool, bool);

// =============================================================================
// 3. Paragraph Helpers
// =============================================================================

// Returns the first line index of the paragraph containing lineIdx.
// If lineIdx is on separator lines, the "paragraph" is that empty-line run.
int paragraphStartLine(const Lines& lines, int lineIdx) {
  bool blank = isParagraphSeparatorLine(lines[lineIdx]);
  int i = lineIdx;
  while (i > 0 && isParagraphSeparatorLine(lines[i - 1]) == blank)
    --i;
  return i;
}

// Returns the last line index of the paragraph containing lineIdx.
// If lineIdx is on separator lines, the "paragraph" is that empty-line run.
int paragraphEndLine(const Lines& lines, int lineIdx) {
  bool blank = isParagraphSeparatorLine(lines[lineIdx]);
  int i = lineIdx;
  while (i < lines.lastLine() && isParagraphSeparatorLine(lines[i + 1]) == blank)
    ++i;
  return i;
}

} // namespace VimCore
