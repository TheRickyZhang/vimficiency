module;
#include <cstdio>

module vf.VimCore.VimEndpointUtils;

import std.compat;
import vf.VimCore.CharMask;
import vf.VimCore.VimCore;
import vf.VimCore.VimMotionUtils;
import vf.Types.CursorPos;

using namespace std;

namespace VimCore {

// =============================================================================
// Word endpoint/range computation
// =============================================================================

static bool hasWordBoundary(WordBoundaryContext boundary) {
  return boundary.leftColOffset > 0 || boundary.rightColOffset > 0 ||
         boundary.hasLinesAbove || boundary.hasLinesBelow;
}

static bool usesWordEndpointBoundarySemantics(WordBoundaryContext boundary) {
  return hasWordBoundary(boundary) || !boundary.clampOutside;
}

static bool inWordBoundaryRegion(const CursorPos& pos, const Lines& lines,
                                 WordBoundaryContext boundary) {
  if (pos.line < 0 || pos.line > lines.lastLine()) return true;
  if (pos.line == 0 && pos.col < boundary.leftColOffset) return true;
  return boundary.inSuffixRegion(pos, lines);
}

template<bool Forward, EdgeType Edge>
struct WordScan {
  static constexpr bool forward = Forward;
  static constexpr EdgeType edge = Edge;
  bool skipCurrent = false;
  bool lineBounded = false;
};

static constexpr WordScan<true, EdgeType::NextEdge> SCAN_NEXT_BEGIN{
    false, false};
static constexpr WordScan<true, EdgeType::WordEdge> SCAN_NEXT_END{
    true, false};
static constexpr WordScan<true, EdgeType::GapEdge> SCAN_NEXT_GAP{
    false, false};
static constexpr WordScan<false, EdgeType::WordEdge> SCAN_PREVIOUS_BEGIN{
    true, false};
static constexpr WordScan<false, EdgeType::NextEdge> SCAN_PREVIOUS_END{
    true, false};
static constexpr WordScan<false, EdgeType::GapEdge> SCAN_PREVIOUS_GAP{
    false, false};

template<typename Scan>
static constexpr Scan lineBounded(Scan scan) {
  scan.lineBounded = true;
  return scan;
}

template<typename Scan>
static constexpr Scan noSkip(Scan scan) {
  scan.skipCurrent = false;
  return scan;
}

template<typename Scan>
static int wordBoundaryOffset(Scan, WordBoundaryContext boundary) {
  return Scan::forward ? boundary.rightColOffset : boundary.leftColOffset;
}

template<typename Scan>
static bool wordHasLinesOutside(Scan, WordBoundaryContext boundary) {
  return Scan::forward ? boundary.hasLinesBelow : boundary.hasLinesAbove;
}

template<typename Scan>
static CursorPos rawWordEndpoint(CursorPos cursor, const Lines& lines,
                                 bool big, Scan scan) {
  return motionWordCore<Scan::forward, Scan::edge>(
      cursor, lines, big, scan.skipCurrent, scan.lineBounded);
}

template<typename Scan>
static CursorPos boundedWordEndpoint(CursorPos cursor,
                                     const Lines& lines,
                                     bool big,
                                     Scan scan,
                                     WordBoundaryContext boundary) {
  CursorPos result = rawWordEndpoint(cursor, lines, big, scan);

  int boundaryOffset = wordBoundaryOffset(scan, boundary);
  bool hasLinesOutside = wordHasLinesOutside(scan, boundary);

  if (result == POSITION_OUTSIDE_BOUNDARY) {
    if (hasLinesOutside || boundaryOffset > 0 || !boundary.clampOutside) {
      return POSITION_OUTSIDE_BOUNDARY;
    }
    return cursor;
  }

  if (boundaryOffset > 0) {
    int lastLine = lines.lastLine();
    if constexpr (Scan::forward) {
      if (result.line == lastLine &&
          result.col >= lines.getSize(lastLine) - boundaryOffset) {
        return POSITION_OUTSIDE_BOUNDARY;
      }
    } else {
      if (result.line == 0 && result.col < boundaryOffset) {
        return POSITION_OUTSIDE_BOUNDARY;
      }
    }
  }

  return result;
}

template<typename Scan>
static CursorPos wordTextObjectEndpoint(CursorPos cursor,
                                        const Lines& lines,
                                        bool big,
                                        Scan scan,
                                        WordBoundaryContext boundary) {
  if (!usesWordEndpointBoundarySemantics(boundary)) {
    return rawWordEndpoint(cursor, lines, big, scan);
  }
  return boundedWordEndpoint(cursor, lines, big, scan, boundary);
}

static CursorPos onePastInclusiveWordEndpoint(CursorPos endpoint,
                                              const Lines& lines) {
  if (endpoint == POSITION_OUTSIDE_BOUNDARY) return endpoint;
  return VimCore::onePastOnSameLine(lines, endpoint);
}

static CharRange computeWhitespaceRun(CursorPos cursor, const Lines& lines) {
  const string& line = lines[cursor.line];
  if (line.empty()) return CharRange(cursor, cursor);

  CursorPos start = cursor;
  while (start.col > 0 &&
         CharMask::isBlank(line[start.col - 1])) {
    start.col--;
  }

  CursorPos end = cursor;
  while (end.col + 1 < static_cast<int>(line.size()) &&
         CharMask::isBlank(line[end.col + 1])) {
    end.col++;
  }
  return CharRange(start, VimCore::onePastOnSameLine(lines, end));
}

static CharRange applyWhitespaceRunBoundary(CharRange range,
                                            const Lines& lines,
                                            WordBoundaryContext boundary) {
  if (boundary.leftColOffset > 0 && range.begin.line == 0 &&
      range.begin.col < boundary.leftColOffset) {
    range.begin = POSITION_OUTSIDE_BOUNDARY;
  }

  if (boundary.rightColOffset > 0) {
    CursorPos tail = lines.getPrevPos(range.end);
    if (tail == POSITION_OUTSIDE_BOUNDARY) {
      range.end = POSITION_OUTSIDE_BOUNDARY;
    } else if (tail.line == lines.lastLine()) {
      int lineLen = static_cast<int>(lines[lines.lastLine()].size());
      if (tail.col >= lineLen - boundary.rightColOffset) {
        range.end = POSITION_OUTSIDE_BOUNDARY;
      }
    }
  }
  return range;
}

static CharRange finalizeWordTextObjectRange(CharRange range,
                                             const Lines& lines,
                                             WordBoundaryContext boundary) {
  if (!usesWordEndpointBoundarySemantics(boundary)) {
    if (range.begin == POSITION_OUTSIDE_BOUNDARY) range.begin = CursorPos(0, 0);
    if (range.end == POSITION_OUTSIDE_BOUNDARY) range.end = lines.endPos();
  }
  return range;
}

static CursorPos includeFollowingEmptyLineAfterTrailingGap(
    CursorPos end, const Lines& lines) {
  if (end == POSITION_OUTSIDE_BOUNDARY) return end;
  if (end.line >= lines.lastLine()) return end;
  if (end.col != static_cast<int>(lines[end.line].size())) return end;
  if (!lines[end.line + 1].empty()) return end;
  return CursorPos(end.line + 1, 0);
}

CursorPos wordMotionEndpoint(CursorPos cursor,
                             const Lines& lines,
                             WordMotionTarget target,
                             bool isBigWord,
                             WordBoundaryContext boundary) {
  switch (target) {
    case WordMotionTarget::NextBegin:
      return boundedWordEndpoint(
          cursor, lines, isBigWord, SCAN_NEXT_BEGIN, boundary);
    case WordMotionTarget::NextEnd:
      return boundedWordEndpoint(
          cursor, lines, isBigWord, SCAN_NEXT_END, boundary);
    case WordMotionTarget::PreviousBegin:
      return boundedWordEndpoint(
          cursor, lines, isBigWord, SCAN_PREVIOUS_BEGIN, boundary);
    case WordMotionTarget::PreviousEnd:
      return boundedWordEndpoint(
          cursor, lines, isBigWord, SCAN_PREVIOUS_END, boundary);
  }
  __builtin_unreachable();
}

CharRange wordOperatorRange(CursorPos cursor,
                            const Lines& lines,
                            WordOperatorTarget target,
                            bool isBigWord,
                            WordBoundaryContext boundary) {
  CursorPos endpoint = POSITION_OUTSIDE_BOUNDARY;

  switch (target) {
    case WordOperatorTarget::DeleteToNextWord:
      endpoint = boundedWordEndpoint(
          cursor, lines, isBigWord, SCAN_NEXT_GAP, boundary);
      if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == cursor) {
        return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
      }
      if (endpoint.line > cursor.line) {
        return CharRange(
            cursor,
            CursorPos(cursor.line, static_cast<int>(lines[cursor.line].size())));
      }
      return CharRange(cursor, onePastInclusiveWordEndpoint(endpoint, lines));

    case WordOperatorTarget::DeleteToWordEnd:
      endpoint = boundedWordEndpoint(
          cursor, lines, isBigWord, SCAN_NEXT_END, boundary);
      if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == cursor) {
        return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
      }
      return CharRange(cursor, onePastInclusiveWordEndpoint(endpoint, lines));

    case WordOperatorTarget::DeleteBackToWordBegin: {
      endpoint = boundedWordEndpoint(
          cursor, lines, isBigWord, SCAN_PREVIOUS_BEGIN, boundary);
      if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == cursor) {
        return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
      }
      int contentStartCol = boundary.contentStartCol(cursor.line);
      if (cursor.col == contentStartCol && endpoint.line == cursor.line) {
        return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
      }
      return buildBackwardExclusiveCharRange(
          endpoint, cursor, lines, contentStartCol);
    }

    case WordOperatorTarget::DeleteBackToWordEnd:
      if (inWordBoundaryRegion(cursor, lines, boundary)) {
        return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
      }
      endpoint = boundedWordEndpoint(
          cursor, lines, isBigWord, SCAN_PREVIOUS_END, boundary);
      if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == cursor) {
        return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
      }
      return CharRange(endpoint, VimCore::onePastOnSameLine(lines, cursor));
  }

  __builtin_unreachable();
}

static CharRange wordTextObjectRangeImpl(CursorPos cursor,
                                         const Lines& lines,
                                         WordTextObjectKind kind,
                                         bool isBigWord,
                                         WordBoundaryContext boundary,
                                         bool includeTrailingEmptyLine) {
  bool isInner = kind == WordTextObjectKind::Inner;

  // When no boundary context constraints apply, route to the faithful
  // current_word port. This gives correct Vim semantics for `aw`/`iw`/`aW`/
  // `iW` in the common case. Boundary-aware callers (TransformExplorer) fall
  // through to the existing implementation which handles boundary clipping.
  if (!usesWordEndpointBoundarySemantics(boundary)) {
    bool include = !isInner;
    auto cw = currentWord(cursor, lines, include, isBigWord);
    if (!cw.ok) {
      return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
    }
    // Vim's current_word can return start > end when the algorithm scanned
    // backward (e.g. cursor on trailing empty line with fwd_word + decl).
    // The operator treats the range as the span [min, max] inclusive.
    CursorPos lo = cw.start <= cw.end ? cw.start : cw.end;
    CursorPos hi = cw.start <= cw.end ? cw.end : cw.start;
    CharRange range(lo, VimCore::onePastOnSameLine(lines, hi));

    // Vim operator-level quirk: `daw` (around, delete-shaped) on a buffer
    // where the range starts on a blank line and the end-line's trailing
    // whitespace reaches EOL extends to include the following empty line.
    // Not in current_word per se — surfaces only at operator application.
    bool startsOnBlankLine =
        lo.line >= 0 && lo.line < static_cast<int>(lines.size()) &&
        isBlankLine(lines[lo.line]);
    bool endsAtLineTrailingWhitespace = false;
    if (range.end.line >= 0 && range.end.line < static_cast<int>(lines.size())) {
      const string& endLine = lines[range.end.line];
      int endCol = range.end.col;
      // The exclusive end is at or before EOL of its line, and everything
      // from endCol to EOL is whitespace. (If endCol >= size, the range
      // doesn't end in trailing whitespace — it's already past EOL.)
      endsAtLineTrailingWhitespace = endCol < static_cast<int>(endLine.size()) &&
                                     hasOnlyBlankSuffix(endLine, endCol);
    }
    if (!isInner && includeTrailingEmptyLine &&
        startsOnBlankLine && endsAtLineTrailingWhitespace) {
      // Push range.end to one-past-EOL first so includeFollowing... sees the
      // "at EOL" precondition it expects.
      if (range.end.line >= 0 && range.end.line < static_cast<int>(lines.size())) {
        range.end.col = static_cast<int>(lines[range.end.line].size());
      }
      range.end = includeFollowingEmptyLineAfterTrailingGap(range.end, lines);
    }

    // Vim op_delete extension: when the inclusive end of a text-object range
    // is on the NUL of an empty line, the delete operator treats the range
    // as consuming the empty line. Combined with blank-prefix begin, this
    // satisfies the exclusive-linewise rule. Implement via snapping begin.col
    // to 0 so applyCharDeletionToBuffer's auto-empty-line-removal does the
    // linewise effect. Only fires for delete-shaped (includeTrailingEmptyLine);
    // change-shaped operator (c<text-obj>) keeps the original characterwise
    // range — Vim preserves leading whitespace in that case.
    if (includeTrailingEmptyLine &&
        range.end.line > range.begin.line &&
        range.end.line < static_cast<int>(lines.size()) &&
        lines[range.end.line].empty() &&
        range.end.col == 0 &&
        range.begin.col > 0 &&
        hasOnlyBlankPrefix(lines[range.begin.line], range.begin.col)) {
      range.begin.col = 0;
      if (range.end.line + 1 < static_cast<int>(lines.size())) {
        range.end = CursorPos(range.end.line + 1, 0);
      }
    }

    return finalizeWordTextObjectRange(range, lines, boundary);
  }

  bool cursorOnWhitespace = CharMask::isBlank(lines.get(cursor));
  CursorPos start;
  CursorPos end;

  if (lines[cursor.line].empty()) {
    // `iw`/`iW` on trailing empty line folds in the previous line (matches
    // Vim for the common cases). `aw`/`aW` is always a no-op on empty line.
    // Note: Vim's full `current_word` has more nuanced conditional folding
    // we don't model — particularly for multi-line buffers with intermediate
    // content; primitive test narrows to avoid those cases.
    if (isInner &&
        cursor.line == lines.lastLine() &&
        cursor.line > 0 &&
        !boundary.hasLinesBelow) {
      return finalizeWordTextObjectRange(
          CharRange(CursorPos(cursor.line - 1, 0), cursor), lines, boundary);
    }
    return CharRange(cursor, cursor);
  }

  if (isInner && cursorOnWhitespace) {
    return finalizeWordTextObjectRange(
        applyWhitespaceRunBoundary(computeWhitespaceRun(cursor, lines),
                                   lines, boundary),
        lines, boundary);
  }

  if (isInner) {
    start = wordTextObjectEndpoint(
        cursor, lines, isBigWord, noSkip(SCAN_PREVIOUS_BEGIN), boundary);
    end = onePastInclusiveWordEndpoint(
        wordTextObjectEndpoint(
            cursor, lines, isBigWord, noSkip(SCAN_NEXT_END), boundary),
        lines);
    return finalizeWordTextObjectRange(CharRange(start, end), lines, boundary);
  }

  if (cursorOnWhitespace) {
    CharRange whitespace = applyWhitespaceRunBoundary(
        computeWhitespaceRun(cursor, lines), lines, boundary);
    if (!whitespace.isValid()) {
      return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
    }

    if (whitespace.end.col < static_cast<int>(lines[whitespace.end.line].size())) {
      CursorPos wordEnd = wordTextObjectEndpoint(
          whitespace.end, lines, isBigWord, noSkip(SCAN_NEXT_END), boundary);
      end = onePastInclusiveWordEndpoint(wordEnd, lines);
    } else if (whitespace.end.line < lines.lastLine()) {
      int nextLine = whitespace.end.line + 1;
      if (isParagraphSeparatorLine(lines[nextLine])) {
        end = CursorPos(nextLine, 0);
      } else if (isBlankLine(lines[nextLine])) {
        end = POSITION_OUTSIDE_BOUNDARY;
      } else {
        CursorPos wordStart(
            nextLine, firstNonBlankColInLine(lines[nextLine]));
        CursorPos rawWordEnd = rawWordEndpoint(
            wordStart, lines, isBigWord, noSkip(SCAN_NEXT_END));
        bool hasTrailingWhitespace = false;
        bool trailingWhitespaceReachesLineEnd = false;
        if (rawWordEnd != POSITION_OUTSIDE_BOUNDARY) {
          int nextCol = rawWordEnd.col + 1;
          if (nextCol < static_cast<int>(lines[rawWordEnd.line].size())) {
            hasTrailingWhitespace =
                CharMask::isWhitespace(lines[rawWordEnd.line][nextCol]);
            trailingWhitespaceReachesLineEnd =
                hasTrailingWhitespace &&
                hasOnlyBlankSuffix(lines[rawWordEnd.line], nextCol + 1);
          }
        }
        if (hasTrailingWhitespace &&
            trailingWhitespaceReachesLineEnd &&
            includeTrailingEmptyLine &&
            isBlankLine(lines[whitespace.begin.line])) {
          end = onePastInclusiveWordEndpoint(
              wordTextObjectEndpoint(
                  wordStart, lines, isBigWord, lineBounded(SCAN_NEXT_GAP),
                  boundary),
              lines);
          if (includeTrailingEmptyLine) {
            end = includeFollowingEmptyLineAfterTrailingGap(end, lines);
          }
        } else {
          CursorPos wordEnd = wordTextObjectEndpoint(
              wordStart, lines, isBigWord, noSkip(SCAN_NEXT_END), boundary);
          end = onePastInclusiveWordEndpoint(wordEnd, lines);
        }
      }
    } else {
      end = POSITION_OUTSIDE_BOUNDARY;
    }
    if (end == POSITION_OUTSIDE_BOUNDARY) {
      return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
    }
    return finalizeWordTextObjectRange(
        CharRange(whitespace.begin, end), lines, boundary);
  }

  CursorPos rawWordEnd = rawWordEndpoint(
      cursor, lines, isBigWord, noSkip(SCAN_NEXT_END));
  bool hasTrailingWhitespace = false;
  if (rawWordEnd != POSITION_OUTSIDE_BOUNDARY) {
    int nextCol = rawWordEnd.col + 1;
    if (nextCol < static_cast<int>(lines[rawWordEnd.line].size())) {
      hasTrailingWhitespace =
          CharMask::isWhitespace(lines[rawWordEnd.line][nextCol]);
    }
  }

  if (hasTrailingWhitespace) {
    start = wordTextObjectEndpoint(
        cursor, lines, isBigWord, noSkip(SCAN_PREVIOUS_BEGIN), boundary);
    end = onePastInclusiveWordEndpoint(
        wordTextObjectEndpoint(
            cursor, lines, isBigWord, lineBounded(SCAN_NEXT_GAP), boundary),
        lines);
  } else {
    CursorPos gapStart = wordTextObjectEndpoint(
        cursor, lines, isBigWord, lineBounded(SCAN_PREVIOUS_GAP),
        boundary);
    if (gapStart != POSITION_OUTSIDE_BOUNDARY &&
        gapStart.line == cursor.line && gapStart.col > 0) {
      start = gapStart;
    } else {
      CursorPos wordStart = wordTextObjectEndpoint(
          cursor, lines, isBigWord, noSkip(SCAN_PREVIOUS_BEGIN), boundary);
      if (wordStart != POSITION_OUTSIDE_BOUNDARY &&
          wordStart.line == cursor.line && wordStart.col > 0 &&
          CharMask::isBlank(lines[wordStart.line][wordStart.col - 1])) {
        start = POSITION_OUTSIDE_BOUNDARY;
      } else {
        start = wordStart;
      }
    }
    end = onePastInclusiveWordEndpoint(
        wordTextObjectEndpoint(
            cursor, lines, isBigWord, noSkip(SCAN_NEXT_END), boundary),
        lines);
  }

  return finalizeWordTextObjectRange(CharRange(start, end), lines, boundary);
}

CharRange wordTextObjectRange(CursorPos cursor,
                              const Lines& lines,
                              WordTextObjectKind kind,
                              bool isBigWord,
                              WordBoundaryContext boundary) {
  return wordTextObjectRangeImpl(
      cursor, lines, kind, isBigWord, boundary,
      /*includeTrailingEmptyLine=*/true);
}

CharRange wordTextObjectChangeRange(CursorPos cursor,
                                    const Lines& lines,
                                    WordTextObjectKind kind,
                                    bool isBigWord,
                                    WordBoundaryContext boundary) {
  return wordTextObjectRangeImpl(
      cursor, lines, kind, isBigWord, boundary,
      /*includeTrailingEmptyLine=*/false);
}

static bool crossesColumnBoundary(CharRange range, const Lines& lines,
                                  int leftColOffset, int rightColOffset) {
  if (!range.isValid()) return false;
  range.normalize();

  if (leftColOffset > 0 && range.begin.line == 0 &&
      range.begin.col < leftColOffset) {
    return true;
  }

  if (rightColOffset > 0 && range.end.line == lines.lastLine()) {
    int firstSuffixCol = static_cast<int>(lines[range.end.line].size()) - rightColOffset;
    if (range.end.col > firstSuffixCol) return true;
  }

  return false;
}

bool isBackslashEscaped(string_view line, int col) {
  int begin = col;
  while (begin > 0 && line[begin - 1] == '\\') --begin;
  return (col - begin) % 2 != 0;
}

size_t nextUnescapedQuote(string_view line, size_t from, char quote) {
  size_t col = line.find(quote, from);
  while (col != string_view::npos && isBackslashEscaped(line, col))
    col = line.find(quote, col + 1);
  return col;
}

CharRange quotePairRange(string_view line, Pos open, int closeCol, bool isInner) {
  if (isInner) return {CursorPos(open.line, open.col + 1), CursorPos(open.line, closeCol)};

  int end = closeCol + 1;
  while (end < ssize(line) && CharMask::isWhitespace(line[end])) ++end;
  if (end == closeCol + 1) {
    while (open.col > 0 && CharMask::isWhitespace(line[open.col - 1])) --open.col;
  }
  return {CursorPos(open.line, open.col), CursorPos(open.line, end)};
}

// Port of Neovim's current_quote (textobject.c:1542), non-Visual operator path,
// count == 1. Returns the deletion range as a half-open CharRange.
CharRange quoteTextObjectRange(CursorPos cursor, const Lines& lines, bool isInner,
                               char quote, int leftColOffset, int rightColOffset) {
  int lineIdx = std::clamp(cursor.line, 0, lines.lastLine());
  const string& ln = lines[lineIdx];
  int len = static_cast<int>(ln.size());
  if (len == 0) return CHAR_RANGE_OUTSIDE_BOUNDARY;

  const size_t pivot = std::clamp(cursor.col, 0, len - 1);
  size_t open, close;
  if (ln[pivot] == quote) {
    size_t from = 0;
    do {
      open = ln.find(quote, from);
      if (open > pivot) return CHAR_RANGE_OUTSIDE_BOUNDARY;
      close = nextUnescapedQuote(ln, open + 1, quote);
      if (close == string::npos) return CHAR_RANGE_OUTSIDE_BOUNDARY;
      from = close + 1;
    } while (close < pivot);
  } else {
    open = pivot ? ln.rfind(quote, pivot - 1) : string::npos;
    while (open != string::npos && isBackslashEscaped(ln, open))
      open = ln.rfind(quote, open - 1);
    if (open == string::npos) open = ln.find(quote);
    if (open == string::npos) return CHAR_RANGE_OUTSIDE_BOUNDARY;
    close = nextUnescapedQuote(ln, open + 1, quote);
    if (close == string::npos) return CHAR_RANGE_OUTSIDE_BOUNDARY;
  }

  CharRange range = quotePairRange(ln, {lineIdx, static_cast<int>(open)}, close, isInner);
  if (crossesColumnBoundary(range, lines, leftColOffset, rightColOffset)) {
    return CHAR_RANGE_OUTSIDE_BOUNDARY;
  }
  return range;
}

char textObjectBracketAt(string_view line, int col, char open, char close) {
  const char c = line[col];
  return (c == open || c == close) && !isBackslashEscaped(line, col) ? c : 0;
}

static CursorPos precedingBracketOpen(const Lines& lines, Pos before, char open, char close) {
  int depth = 0;
  for (int row = before.line; row >= 0; --row) {
    const int end = row == before.line ? before.col : lines.getSize(row);
    const string_view prefix(lines[row].data(), end);
    if (prefix.find(open) == string_view::npos && prefix.find(close) == string_view::npos) continue;
    for (int col = end - 1; col >= 0; --col) {
      const char bracket = textObjectBracketAt(lines[row], col, open, close);
      if (bracket == close) ++depth;
      else if (bracket == open) {
        if (depth == 0) return {row, col};
        --depth;
      }
    }
  }

  return POSITION_OUTSIDE_BOUNDARY;
}

static int bracketSearchOrigin(const Line& line, int col, char open) {
  col = std::clamp(col, 0, static_cast<int>(line.size()));
  if (col == ssize(line)) return col;
  if (open == '{' && CharMask::isWhitespace(line[col]))
    col = max(col, firstNonBlankColInLine(line));
  return col + (line[col] == open);
}

pair<int, int> bracketForwardEntryColumns(const Lines& lines, Pos opener, char close,
                                       bool hasHiddenLinesAbove) {
  const Line& line = lines[opener.line];
  const int left = opener.col;
  const char open = line[left];
  if (left == 0 || (hasHiddenLinesAbove && open != '{')) return {left, left};

  int first = left;
  while (first > 0 && !textObjectBracketAt(line, first - 1, open, close)) --first;
  if (first == left) return {left, left};

  const int origin = bracketSearchOrigin(line, left - 1, open);
  if (origin > left) return {first, left};
  if (hasHiddenLinesAbove || precedingBracketOpen(lines, {opener.line, first}, open, close).line >= 0)
    return {left, left};
  return {first, origin == left ? left - 1 : left};
}

static CursorPos findBracketOpen(const Lines& lines, CursorPos pos, char open, char close) {
  if (pos.line < 0 || pos.line > lines.lastLine()) return POSITION_OUTSIDE_BOUNDARY;
  const int start = bracketSearchOrigin(lines[pos.line], pos.col, open);
  const CursorPos preceding = precedingBracketOpen(lines, {pos.line, start}, open, close);
  if (preceding.line >= 0) return preceding;

  int depth = 0;
  for (int row = pos.line; row <= lines.lastLine(); ++row) {
    const int first = row == pos.line ? max(0, start + 1) : 0;
    for (int col = first; col < lines.getSize(row); ++col) {
      const char bracket = textObjectBracketAt(lines[row], col, open, close);
      if (bracket == close) ++depth;
      else if (bracket == open) {
        if (depth == 0) return {row, col};
        --depth;
      }
    }
  }
  return POSITION_OUTSIDE_BOUNDARY;
}

static bool hasOddQuoteCount(string_view line, int end) {
  bool odd = false;
  const auto prefix = line.substr(0, end);
  for (size_t col = prefix.find('"'); col != string_view::npos; col = prefix.find('"', col + 1)) {
    const bool charLiteral = col > 0 && col + 1 < line.size() &&
                             line[col - 1] == '\'' && line[col + 1] == '\'';
    if (!charLiteral && !isBackslashEscaped(line, col)) odd = !odd;
  }
  return odd;
}

CursorPos matchingBracketClose(const Lines& lines, CursorPos opener, char close, int lastLine) {
  const char open = lines[opener.line][opener.col];
  int depth = 1;
  bool inQuote = false;
  bool startedInQuote = false;
  for (int row = opener.line; row <= lastLine; ++row) {
    const Line& line = lines[row];
    const int end = lines.getSize(row);
    const int first = row == opener.line ? opener.col + 1 : 0;
    const bool fromPrevious = row > 0 && lines[row - 1].ends_with('\\');
    const bool continues = line.ends_with('\\');
    optional<bool> matchQuotes;
    if (fromPrevious || continues) {
      matchQuotes = true;
      if (hasOddQuoteCount(line, end)) {
        if (row == opener.line) {
          startedInQuote = first == end || !hasOddQuoteCount(line, first);
          inQuote = startedInQuote;
        } else {
          inQuote = fromPrevious;
        }
      }
    }
    for (int col = first; col < end; ++col) {
      const char c = line[col];
      if (c == '"') {
        if (!matchQuotes.has_value()) matchQuotes = !hasOddQuoteCount(line, end);
        if (*matchQuotes && !isBackslashEscaped(line, col)) {
          inQuote = !inQuote;
          startedInQuote = false;
        }
      } else if (c == '\'') {
        if (col + 3 < end && line[col + 1] == '\\' && line[col + 3] == '\'') {
          col += 3;
        } else if (col + 2 < end && line[col + 2] == '\'') {
          col += 2;
        }
      } else if ((!inQuote || startedInQuote) && (c == open || c == close) &&
                 !isBackslashEscaped(line, col)) {
        if (c == open) ++depth;
        else if (--depth == 0) return {row, col};
      }
    }
    if (!continues) {
      inQuote = false;
      startedInQuote = false;
    }
  }
  return POSITION_OUTSIDE_BOUNDARY;
}

// Port of Neovim's inindent(extra=1): true iff cols [0, col] are all whitespace.
static bool inIndent(const Lines& lines, CursorPos pos) {
  const string& ln = lines[pos.line];
  int len = static_cast<int>(ln.size());
  for (int i = 0; i <= pos.col; i++) {
    if (i >= len) return false;
    if (!CharMask::isWhitespace(ln[i])) return false;
  }
  return true;
}

CharRange bracketTextObjectRange(CursorPos cursor, const Lines& lines, bool isInner,
                                 char open, char close, int leftColOffset,
                                 int rightColOffset) {
  const CursorPos openPos = findBracketOpen(lines, cursor, open, close);
  if (openPos.line < 0) return CHAR_RANGE_OUTSIDE_BOUNDARY;
  const CursorPos closePos = matchingBracketClose(lines, openPos, close, lines.lastLine());
  if (closePos.line < 0) return CHAR_RANGE_OUTSIDE_BOUNDARY;

  if (!isInner) {
    // Around: simple inclusive range [open, close+1).
    CharRange range(openPos, VimCore::onePastOnSameLine(lines, closePos));
    if (crossesColumnBoundary(range, lines, leftColOffset, rightColOffset)) {
      return CHAR_RANGE_OUTSIDE_BOUNDARY;
    }
    return range;
  }

  // Inner: port of current_block (textobject.c:955). Start is incl past open;
  // cursor is decl from close, skipping leading indent on the close's line.
  CursorPos start = openPos;
  if (incl(lines, start) == -1) return CHAR_RANGE_OUTSIDE_BOUNDARY;

  CursorPos last = closePos;
  bool sol = (last.col == 0);
  if (decl(lines, last) == -1) return CHAR_RANGE_OUTSIDE_BOUNDARY;
  while (inIndent(lines, last)) {
    sol = true;
    if (decl(lines, last) == -1) break;
  }

  CharRange range;
  if (sol) {
    // Motion exclusive; advance cursor past indent line boundary.
    CursorPos end = last;
    if (incl(lines, end) == -1) {
      return CHAR_RANGE_OUTSIDE_BOUNDARY;
    }
    range = CharRange(start, end);
  } else if (start <= last) {
    // Inclusive end: one past cursor.
    CursorPos end = last;
    inc(lines, end);
    range = CharRange(start, end);
  } else {
    // Empty: no text between brackets; cursor lands at start_pos.
    range = CharRange(start, start);
  }

  if (crossesColumnBoundary(range, lines, leftColOffset, rightColOffset)) {
    return CHAR_RANGE_OUTSIDE_BOUNDARY;
  }
  return range;
}

// =============================================================================
// Paragraph endpoint/range computation
// =============================================================================

// Core implementation - computes endpoint without boundary checking
template<bool Forward, LineEdgeType Edge>
static int motionParagraphEndpointCore(int cursorLine, const Lines& lines) {
  int n = static_cast<int>(lines.size());
  cursorLine = std::clamp(cursorLine, 0, n - 1);
  bool cursorOnBlank = isParagraphSeparatorLine(lines[cursorLine]);

  int result = cursorLine;

  if constexpr (Edge == LineEdgeType::BlockEdge) {
    // Edge of current same-type block
    if constexpr (Forward) {
      result = paragraphEndLine(lines, cursorLine);
    } else {
      result = paragraphStartLine(lines, cursorLine);
    }
  } else if constexpr (Edge == LineEdgeType::GapEdge) {
    // Edge of blank line run adjacent to current paragraph
    if constexpr (Forward) {
      // Find end of trailing blank lines (after current paragraph)
      int blockEnd = paragraphEndLine(lines, cursorLine);
      if (cursorOnBlank) {
        // Already on blanks - return end of blank run
        result = blockEnd;
      } else if (blockEnd + 1 < n &&
                 isParagraphSeparatorLine(lines[blockEnd + 1])) {
        // Skip past non-blank paragraph, find end of following blank run
        result = paragraphEndLine(lines, blockEnd + 1);
      } else {
        // No trailing blanks
        result = blockEnd;
      }
    } else {
      // Find start of leading blank lines (before current paragraph)
      int blockStart = paragraphStartLine(lines, cursorLine);
      if (cursorOnBlank) {
        // Already on blanks - return start of blank run
        result = blockStart;
      } else if (blockStart > 0 &&
                 isParagraphSeparatorLine(lines[blockStart - 1])) {
        // Skip past non-blank paragraph, find start of preceding blank run
        result = paragraphStartLine(lines, blockStart - 1);
      } else {
        // No leading blanks
        result = blockStart;
      }
    }
  } else if constexpr (Edge == LineEdgeType::NextEdge) {
    // Edge of next block (}/{ motions go to blank line separator)
    if constexpr (Forward) {
      // Skip current blank lines
      int i = cursorLine;
      while (i < n && isParagraphSeparatorLine(lines[i])) {
        i++;
      }
      if (i >= n) {
        // All blanks to end - return last line
        result = n - 1;
      } else {
        // Scan forward for next blank line
        i++;
        while (i < n && !isParagraphSeparatorLine(lines[i])) {
          i++;
        }
        // Return blank line, or last line if not found
        result = (i < n) ? i : n - 1;
      }
    } else {
      // Skip current blank lines
      int i = cursorLine;
      while (i > 0 && isParagraphSeparatorLine(lines[i])) {
        i--;
      }
      // Scan backward for previous blank line
      i--;
      while (i >= 0 && !isParagraphSeparatorLine(lines[i])) {
        i--;
      }
      // Return blank line, or line 0 if not found
      result = max(i, 0);
    }
  }

  return result;
}

template<bool Forward, LineEdgeType Edge>
int motionParagraphEndpoint(int cursorLine, const Lines& lines, bool hasLinesOutside) {
  int result = motionParagraphEndpointCore<Forward, Edge>(cursorLine, lines);

  // Boundary check
  if (hasLinesOutside) {
    int lastLine = lines.lastLine();
    if constexpr (Forward) {
      if (result >= lastLine) {
        return LINE_OUTSIDE_BOUNDARY;
      }
    } else {
      if (result <= 0) {
        return LINE_OUTSIDE_BOUNDARY;
      }
    }
  }

  return result;
}

// Explicit instantiations for templated version
template int motionParagraphEndpoint<true, LineEdgeType::BlockEdge>(int, const Lines&, bool);
template int motionParagraphEndpoint<true, LineEdgeType::GapEdge>(int, const Lines&, bool);
template int motionParagraphEndpoint<true, LineEdgeType::NextEdge>(int, const Lines&, bool);
template int motionParagraphEndpoint<false, LineEdgeType::BlockEdge>(int, const Lines&, bool);
template int motionParagraphEndpoint<false, LineEdgeType::GapEdge>(int, const Lines&, bool);
template int motionParagraphEndpoint<false, LineEdgeType::NextEdge>(int, const Lines&, bool);

LineRange paragraphTextObjectRange(int cursorLine, const Lines& lines,
                                   bool isInner, int topBoundary,
                                   int bottomBoundary) {
  // Faithful port of Neovim's `current_par` (textobject.c) for count=1.
  // Differs from `}`/`{` motion: text object uses `linewhite` (whitespace-only
  // OR empty), motion uses strict empty-line check.
  int n = static_cast<int>(lines.size());

  int startLine = std::clamp(cursorLine, 0, n - 1);
  bool whiteInFront = isBlankLine(lines[startLine]);

  // Move start back through same-blankness lines.
  while (startLine > 0 && isBlankLine(lines[startLine - 1]) == whiteInFront) {
    startLine--;
  }

  // Move end forward through same-blankness lines.
  int endLine = startLine;
  while (endLine < n && isBlankLine(lines[endLine]) == whiteInFront) {
    endLine++;
  }
  endLine--;

  bool prevStartIsWhite = whiteInFront;
  bool doWhite = false;
  if (isInner) {
    // `ip`: just the current same-blankness block. No extension.
  } else {
    // At EOF: if current block is white, FAIL (no following non-white para to
    // wrap with). If non-white, skip forward extension and absorb all leading
    // blanks (any kind) backward — Vim's observable behavior for `ap` on a
    // paragraph that runs to buffer end.
    if (endLine >= n - 1) {
      if (whiteInFront) return LINE_RANGE_OUTSIDE_BOUNDARY;
      while (startLine > 0 && isBlankLine(lines[startLine - 1])) {
        startLine--;
      }
    } else {
      doWhite = true;
      int t = endLine + 1;
      while (t < n) {
        if (isBlankLine(lines[t]) != prevStartIsWhite) {
          // The next block's blankness drives where the second forward-extend
          // loop runs. Cursor-on-white → entering non-white → doWhite=false →
          // extend through the non-white para. Cursor-on-non-white → entering
          // white → doWhite=true → extend through trailing blanks AND include
          // one extra line for the gap before the next non-white block.
          doWhite = isBlankLine(lines[t]);
          if (doWhite) endLine++;
          break;
        }
        t++;
      }
      endLine++;
      while (endLine < n && isBlankLine(lines[endLine]) == doWhite) endLine++;
      endLine--;
      prevStartIsWhite = doWhite;

      // If we didn't wrap a blank gap (forward extension landed on non-white),
      // look for blanks preceding the start.
      if (!whiteInFront && startLine > 0) {
        bool startIsWhite = !prevStartIsWhite;
        while (startLine > 0 &&
               isBlankLine(lines[startLine - 1]) == startIsWhite) {
          startLine--;
          startIsWhite = !startIsWhite;
        }
      }
    }
  }

  int finalEndLine = endLine + 1;
  if (topBoundary >= 0 && startLine <= topBoundary) {
    return LINE_RANGE_OUTSIDE_BOUNDARY;
  }
  if (bottomBoundary >= 0 && finalEndLine > bottomBoundary) {
    return LINE_RANGE_OUTSIDE_BOUNDARY;
  }
  return LineRange(startLine, finalEndLine);
}

// =============================================================================
// Sentence endpoint/range computation
// =============================================================================

static bool sentenceEndpointOutsideBoundary(CursorPos result, const Lines& lines,
                                            bool forward, int boundaryOffset,
                                            bool hasLinesOutside) {
  int lastLine = lines.lastLine();
  if (forward) {
    if (hasLinesOutside && result.line >= lastLine) {
      return true;
    }
    if (boundaryOffset > 0 && result.line == lastLine &&
        result.col > static_cast<int>(lines[lastLine].size()) - boundaryOffset) {
      return true;
    }
  } else {
    if (hasLinesOutside && result.line <= 0) {
      return true;
    }
    if (boundaryOffset > 0 && result.line == 0 && result.col < boundaryOffset) {
      return true;
    }
  }
  return false;
}

static bool sentenceOperatorEndpointOutsideBoundary(
    CursorPos result, const Lines& lines, bool forward, int boundaryOffset,
    bool hasLinesOutside) {
  int lastLine = lines.lastLine();
  if (forward) {
    if (hasLinesOutside && result.line == lastLine &&
        result.col >= static_cast<int>(lines[lastLine].size())) {
      // Endpoint at NUL of the slice's last line. For non-empty last lines,
      // this is one-past-EOL. For empty last lines (size=0, lastCol=0), col=0
      // is the NUL position. With linesOutside, the real `findsent` would have
      // continued past — the slice-local stop is artificial.
      return true;
    }
    if (hasLinesOutside && result.line == lastLine &&
        result.col < static_cast<int>(lines[lastLine].size()) &&
        hasOnlyBlankSuffix(lines[lastLine], result.col)) {
      return true;
    }
    if (result.line > lastLine) {
      return hasLinesOutside || boundaryOffset > 0;
    }
    if (boundaryOffset > 0 && result.line == lastLine &&
        result.col >= static_cast<int>(lines[lastLine].size()) - boundaryOffset) {
      return true;
    }
  } else {
    if (hasLinesOutside && result.line == 0 &&
        result.col <= firstNonBlankColInLine(lines[0])) {
      return true;
    }
    if (boundaryOffset > 0 && result.line == 0 && result.col < boundaryOffset) {
      return true;
    }
  }
  return false;
}

CursorPos sentenceMotionEndpoint(CursorPos cursor, const Lines& lines, bool forward,
                                 int boundaryOffset, bool hasLinesOutside) {
  CursorPos result = cursor;
  if (forward) {
    motionSentenceNext(result, lines);
  } else {
    motionSentencePrev(result, lines);
  }

  if (sentenceEndpointOutsideBoundary(
          result, lines, forward, boundaryOffset, hasLinesOutside)) {
    return POSITION_OUTSIDE_BOUNDARY;
  }
  return result;
}

static CursorPos sentenceOperatorEndpointCore(CursorPos cursor, const Lines& lines,
                                              bool forward) {
  // Port of Neovim's `nv_brace` (normal.c) operator path: run findsent with
  // the raw past-EOL semantics, return the resulting position. If findsent
  // signals FAIL, return cursor unchanged so the interpreter applies no edit
  // (matches Vim's `clearopbeep`).
  CursorPos result = lines.clamp(cursor);
  bool ok = findSentenceForOperator(result, lines, forward, 1);
  if (!ok) return cursor;
  return result;
}

CursorPos sentenceOperatorEndpoint(CursorPos cursor, const Lines& lines, bool forward,
                                   int boundaryOffset, bool hasLinesOutside) {
  CursorPos result = sentenceOperatorEndpointCore(cursor, lines, forward);
  if (sentenceOperatorEndpointOutsideBoundary(
          result, lines, forward, boundaryOffset, hasLinesOutside)) {
    return POSITION_OUTSIDE_BOUNDARY;
  }
  return result;
}

static bool isAsciiWhite(int c) { return CharMask::isWhitespace(static_cast<char>(c)); }

// Port of Neovim's find_first_blank (textobject.c:553).
static void findFirstBlank(const Lines& lines, CursorPos& pos) {
  while (true) {
    CursorPos prev = pos;
    if (decl(lines, prev) == -1) return;
    if (!isAsciiWhite(gchar(lines, prev))) return;  // non-blank: stay put
    pos = prev;
  }
}

// Port of Neovim's findsent_forward (textobject.c:567).
static void findSentForward(const Lines& lines, CursorPos& pos, int count, bool atStart) {
  while (count--) {
    findSentenceForOperator(pos, lines, true, 1);
    if (atStart) {
      findFirstBlank(lines, pos);
    }
    if (count == 0 || atStart) {
      decl(lines, pos);
    }
    atStart = !atStart;
  }
}

// Port of Neovim's current_sent (textobject.c:794), non-Visual operator path,
// count == 1. Returns the half-open deletion range matching what Vim's
// op_delete would produce from the (start_pos, cursor, inclusive) triple
// current_sent leaves behind.
//
// The translation to half-open CharRange:
//   - inclusive=false → end = post-incl cursor (exclusive).
//   - inclusive=true (incl failed at buffer end) → end = onePast(cursor's
//     original position), which is NUL position if cursor was on a real char
//     at end of line. op_delete then deletes [start.col, end.col) on the
//     same line, which extends through the line's last char.
CharRange sentenceTextObjectRange(CursorPos cursor, const Lines& lines, bool isInner,
                                  CursorPos leftBoundary, CursorPos rightBoundary) {
  bool include = !isInner;
  CursorPos start = cursor;  // start_pos
  CursorPos pos = cursor;    // whitespace scan helper

  // findsent(FORWARD, 1) — move to start of next sentence (or EOF).
  findSentenceForOperator(cursor, lines, true, 1);

  // Scan pos forward through whitespace. If we land where findsent did,
  // the original cursor was inside a whitespace gap before the next sentence.
  while (isAsciiWhite(gchar(lines, pos))) {
    if (incl(lines, pos) == -1) break;
  }
  bool startBlank;
  if (pos == cursor) {
    startBlank = true;
    findFirstBlank(lines, start);
  } else {
    startBlank = false;
    findSentenceForOperator(cursor, lines, false, 1);
    start = cursor;
  }

  int ncount = include ? 2 : 1;
  if (!include && startBlank) ncount--;

  if (ncount > 0) {
    findSentForward(lines, cursor, ncount, true);
  } else {
    decl(lines, cursor);
  }

  if (include) {
    if (startBlank) {
      findFirstBlank(lines, cursor);
      if (isAsciiWhite(gchar(lines, cursor))) {
        decl(lines, cursor);
      }
    } else if (!isAsciiWhite(gchar(lines, cursor))) {
      findFirstBlank(lines, start);
    }
  }

  // current_sent's final block: incl(cursor); if it returns -1, inclusive=true,
  // else inclusive=false. The incl call modifies cursor regardless of return.
  bool vimInclusive = incl(lines, cursor) == -1;

  // Vim's nv_object → adjust_cursor_col: if cursor is at NUL of a non-empty
  // line and not in Visual mode, col--. This shifts the cursor back onto a
  // real char, which then lets op_delete compute the right deletion count.
  if (cursor.col > 0 && gchar(lines, cursor) == 0) {
    cursor.col--;
  }

  // Vim's do_pending_operator normalizes (oap->start, curwin->w_cursor):
  // whichever is smaller becomes oap->start, the other becomes oap->end. Then
  // the deletion range is:
  //   inclusive=false → [start, end) half-open (chars [start.col, end.col))
  //   inclusive=true  → [start, end+1) half-open (chars [start.col, end.col])
  CursorPos rangeStart = start;
  CursorPos rangeEnd = cursor;
  if (rangeStart > rangeEnd) {
    std::swap(rangeStart, rangeEnd);
  }
  CursorPos resultStart = rangeStart;
  CursorPos resultEnd;
  if (vimInclusive) {
    int lineSize = static_cast<int>(lines[rangeEnd.line].size());
    int onePastC = rangeEnd.col < lineSize ? rangeEnd.col + 1 : lineSize;
    resultEnd = CursorPos(rangeEnd.line, onePastC);
  } else {
    resultEnd = rangeEnd;
  }
  if (leftBoundary.isValid() && resultStart <= leftBoundary) {
    return CHAR_RANGE_OUTSIDE_BOUNDARY;
  }
  if (rightBoundary.isValid() && resultEnd > rightBoundary) {
    return CHAR_RANGE_OUTSIDE_BOUNDARY;
  }
  return CharRange(resultStart, resultEnd);
}

// =============================================================================
// Scroll endpoint computation
// =============================================================================

int scrollEndpoint(int cursorLine, int numLines, int shift, bool hasLinesAbove,
                   bool hasLinesBelow) {
  if (numLines == 0)
    return LINE_OUTSIDE_BOUNDARY;

  int lastLine = numLines - 1;
  int targetLine = cursorLine + shift;

  // Clamp to buffer bounds
  targetLine = std::clamp(targetLine, 0, lastLine);

  // Check if motion would escape bounds
  if (shift > 0) {
    // Down scroll: suspicious if landing on or past last line when there are
    // lines below
    if (hasLinesBelow && targetLine >= lastLine) {
      return LINE_OUTSIDE_BOUNDARY;
    }
  } else if (shift < 0) {
    // Up scroll: suspicious if landing on or before first line when there are
    // lines above
    if (hasLinesAbove && targetLine <= 0) {
      return LINE_OUTSIDE_BOUNDARY;
    }
  }

  return targetLine;
}

} // namespace VimCore
