module;
#include <cassert>
#include <cstdio>

module vf.VimCore.VimMotionUtils;

import std.compat;
import vf.VimCore.CharMask;
import vf.VimCore.VimCore;
import vf.VimCore.VimEndpointUtils;
import vf.Types.EdgeType;
import vf.Types.LineEdgeType;
import vf.Types.CursorPos;

using namespace std;

namespace VimCore {

// =============================================================================
// Word motions
// =============================================================================
//
// Named motions clamp the shared word scan to Vim cursor landings.
// Edit/text-object ranges use VimEndpointUtils.
//
// =============================================================================

template<bool Forward, EdgeType Edge>
static void motionWord(CursorPos &pos, const Lines &lines, bool big,
                       bool skipCurrent) {
  CursorPos result = motionWordCore<Forward, Edge>(pos, lines, big, skipCurrent);

  if (result == POSITION_OUTSIDE_BOUNDARY) {
    // Vim clamps to buffer edge based on direction
    if constexpr (Forward) {
      pos = lines.lastPos();
    } else {
      pos = CursorPos(0, 0);
    }
  } else {
    pos = result;
  }
}

// =============================================================================
// Direct port of Neovim's `fwd_word` (textobject.c) for operator-pending `w`.
// =============================================================================
//
// The non-operator path (motionWordCore + motionW) handles cursor navigation
// where multi-line scans are allowed and the result is a valid normal-mode
// cursor position. The operator path needs different semantics:
//   - When motion would cross a line boundary in eol mode (operator pending),
//     stop on the first line crossing — this implements Vim's "for dw on last
//     word of line, the newline is not included" rule.
//   - When scanning forward through whitespace, STOP at an empty line so the
//     operator only consumes through that empty line (not multiple).
//   - When motion can't advance (cursor on last char of last line), leave
//     cursor at the NUL/past-EOL position so the operator still consumes the
//     buffer tail.
//
// Char classes (matching Neovim's `cls()`):
//   0 - whitespace, NUL, blank chars
//   1 - punctuation (any non-word non-whitespace)
//   2 - word characters (alnum + '_'); collapsed to 1 for bigword `W`/`E`/`B`

namespace {

int wordCharClassAt(const Lines& lines, CursorPos pos, bool big) {
  int c = gchar(lines, pos);
  if (c == 0) return 0;
  CharMask m(static_cast<char>(c));
  if (m.whitespace()) return 0;
  if (big) return 1;  // Bigword: only whitespace vs non-whitespace
  return m.smallWord() ? 2 : 1;
}

bool atEmptyLine(const Lines& lines, CursorPos pos) {
  return pos.col == 0 && gchar(lines, pos) == 0;
}

// Skip a row of characters of the same class in the given direction.
// Returns true if hit buffer edge (cursor stays at edge).
bool wordSkipChars(int cclass, bool forward, const Lines& lines, CursorPos& pos, bool big) {
  while (wordCharClassAt(lines, pos, big) == cclass) {
    int i = forward ? inc(lines, pos) : dec(lines, pos);
    if (i == -1) return true;
  }
  return false;
}

}  // namespace (close anonymous so the count-aware motion ports are reachable
   //              from the VimCore::* scope declared in VimMotionUtils.h)

// Port of fwd_word(count, bigword, eol=true) — operator-pending forward `w`.
//
// Deliberate divergence: Neovim's eol checks are `i >= 1`, so landing on the
// NUL slot (inc == 2) also stops the last word there, and nv_wordcmd's
// adjust_cursor then backs onto the last char inclusive. We test `i == 1` so
// the scan keeps going and reports the line crossing instead; the interpreter
// (shouldStopAtEndOfLine) relies on seeing goalPos on the next line to apply
// the "dw on last word of line doesn't take the newline" rule.
bool fwdWordOperator(CursorPos& pos, const Lines& lines, bool big, int count) {
  while (--count >= 0) {
    int sclass = wordCharClassAt(lines, pos, big);
    bool lastLine = (pos.line == lines.lastLine());
    int i = inc(lines, pos);
    if (i == -1 || (i == 1 && lastLine)) return false;
    if (i == 1 && count == 0) return true;

    if (sclass != 0) {
      while (wordCharClassAt(lines, pos, big) == sclass) {
        i = inc(lines, pos);
        if (i == -1 || (i == 1 && count == 0)) return true;
      }
    }

    while (wordCharClassAt(lines, pos, big) == 0) {
      if (atEmptyLine(lines, pos)) break;
      i = inc(lines, pos);
      if (i == -1 || (i == 1 && count == 0)) return true;
    }
  }
  return true;
}

// Port of bck_word(count, bigword, stop=false) — operator-pending backward `b`.
// Vim calls bck_word with stop=false from nv_bck_word for both motion and
// operator-pending use.
bool bckWordOperator(CursorPos& pos, const Lines& lines, bool big, int count) {
  bool stop = false;  // matches nv_bck_word(cap->count1, cap->arg, false)
  while (--count >= 0) {
    int sclass = wordCharClassAt(lines, pos, big);
    if (dec(lines, pos) == -1) return false;

    if (!stop || sclass == wordCharClassAt(lines, pos, big) || sclass == 0) {
      // Skip whitespace before the word. Stop on empty line.
      while (wordCharClassAt(lines, pos, big) == 0) {
        if (atEmptyLine(lines, pos)) goto finished;
        if (dec(lines, pos) == -1) return true;
      }
      // Move backward to start of this word.
      if (wordSkipChars(wordCharClassAt(lines, pos, big), false, lines, pos, big)) return true;
    }
    inc(lines, pos);  // overshot — forward one
finished:
    stop = false;
  }
  return true;
}

// Port of bckend_word(count, bigword, eol=false) — `ge`/`gE`. Vim's nv_g_cmd
// calls bckend_word with eol=false always (the early i==1 line-cross return
// inside the function does not fire).
bool bckEndWordOperator(CursorPos& pos, const Lines& lines, bool big, int count) {
  while (--count >= 0) {
    int sclass = wordCharClassAt(lines, pos, big);
    if (dec(lines, pos) == -1) return false;

    if (sclass != 0) {
      while (wordCharClassAt(lines, pos, big) == sclass) {
        if (dec(lines, pos) == -1) return true;
      }
    }

    while (wordCharClassAt(lines, pos, big) == 0) {
      if (atEmptyLine(lines, pos)) break;
      if (dec(lines, pos) == -1) return true;
    }
  }
  return true;
}

namespace {

// Port of `back_in_line` from textobject.c: move backward within the current
// line to the start of the current character class run. Does NOT cross lines.
void backInLine(CursorPos& pos, const Lines& lines, bool big) {
  int sclass = wordCharClassAt(lines, pos, big);
  while (pos.col > 0) {
    pos.col--;
    if (wordCharClassAt(lines, pos, big) != sclass) {
      pos.col++;  // undo: we went one past
      return;
    }
  }
}

// Port of `oneleft` from edit.c (non-virtual-edit branch only).
// Returns true if cursor moved (col was > 0), false otherwise.
bool oneLeftCursor(CursorPos& pos) {
  if (pos.col == 0) return false;
  pos.col--;
  return true;
}

// General port of end_word from textobject.c. Parameterized over `stop` and
// `empty`. current_word uses (stop=true, empty=true); plain `e`/`E`/`de`/`dE`
// use (stop=false, empty=false). nv_wordcmd sets stop=true only for `cw`→`ce`.
bool endWord(CursorPos& pos, const Lines& lines, bool big, int count,
             bool stop, bool empty) {
  while (--count >= 0) {
    int sclass = wordCharClassAt(lines, pos, big);
    if (inc(lines, pos) == -1) return false;

    bool finished = false;
    if (wordCharClassAt(lines, pos, big) == sclass && sclass != 0) {
      if (wordSkipChars(sclass, true, lines, pos, big)) return false;
    } else if (!stop || sclass == 0) {
      while (wordCharClassAt(lines, pos, big) == 0) {
        if (empty && atEmptyLine(lines, pos)) {
          finished = true;
          break;
        }
        if (inc(lines, pos) == -1) return false;
      }
      if (!finished) {
        if (wordSkipChars(wordCharClassAt(lines, pos, big), true, lines, pos, big)) return false;
      }
    }
    if (!finished) dec(lines, pos);
    stop = false;
  }
  return true;
}

}  // namespace

// Per Neovim's nv_wordcmd, `stop` is only true for the `cw`/`cW` → `ce`/`cE`
// mapping. Plain `e`/`E` and operator-pending `de`/`dE` use stop=false.
bool endWordOperator(CursorPos& pos, const Lines& lines, bool big, int count) {
  return endWord(pos, lines, big, count, /*stop=*/false, /*empty=*/false);
}

// =============================================================================
// Named word motion forwarders
// =============================================================================
//
// Pure motion semantics:
//   w:  Forward  + NextEdge
//   e:  Forward  + WordEdge, skip current first
//   b:  Backward + WordEdge, skip current first
//   ge: Backward + NextEdge, skip current first
//

void motionW(CursorPos &pos, const Lines &lines, bool big) {
  motionWord<true, EdgeType::NextEdge>(pos, lines, big, false);
}

void motionB(CursorPos &pos, const Lines &lines, bool big) {
  // For backward direction, End gives edge opposite to travel = leftmost =
  // START skipCurrent needed so b from word start goes to PREVIOUS word start
  motionWord<false, EdgeType::WordEdge>(pos, lines, big, true);
}

void motionE(CursorPos &pos, const Lines &lines, bool big) {
  // e needs to skip current position first, otherwise we'd stay at current word
  // end
  motionWord<true, EdgeType::WordEdge>(pos, lines, big, true);
}

void motionGe(CursorPos &pos, const Lines &lines, bool big) {
  // For backward direction, Next gives edge in travel direction = rightmost =
  // END skipCurrent needed so ge from word end goes to PREVIOUS word end
  motionWord<false, EdgeType::NextEdge>(pos, lines, big, true);
}

void motionWForOperator(CursorPos& pos, const Lines& lines, bool big) {
  // FAIL: cursor stays where fwd_word left it (typically past-EOL on last
  // line). The interpreter's isPastEndPosition / shouldStopAtEndOfLine
  // checks see this and apply the operator through the buffer tail.
  fwdWordOperator(pos, lines, big, 1);
}

void motionEForOperator(CursorPos& pos, const Lines& lines, bool big) {
  endWordOperator(pos, lines, big, 1);
}

CurrentWordResult currentWord(CursorPos cursor, const Lines& lines,
                              bool include, bool big) {
  // Port of Vim's `current_word` (textobject.c). Returns the text object
  // span as (start, cursor-at-end, inclusive). The caller converts that
  // inclusive [start, cursor] range to a half-open CharRange.
  //
  // Empty cursor line: Vim's current_word doesn't have an explicit early
  // return for this — it goes through the same path. `back_in_line` is a
  // no-op (col is already 0), startPos = cursor. `(cls() == 0) == include`:
  // empty line has cls=0 (NUL is class 0). For `iw` (include=false), this
  // is `true == false` → false → take the `fwd_word` branch. For `aw`
  // (include=true), it's `true == true` → end_word. We follow Vim's flow
  // and let the algorithm produce whatever Vim's algorithm produces.

  CurrentWordResult result;
  result.ok = true;
  result.inclusive = true;

  // Step 1: Go to start of current word or white space.
  backInLine(cursor, lines, big);
  CursorPos startPos = cursor;

  // Step 2: Find the end of the text object.
  bool curOnWhite = wordCharClassAt(lines, cursor, big) == 0;
  bool includeWhite = false;
  if (curOnWhite == include) {
    // (on white && include white) OR (on word && exclude white): find end of word
    if (!endWord(cursor, lines, big, 1, /*stop=*/true, /*empty=*/true)) {
      // Match Vim's behavior on FAIL: cursor stays where endWord left it.
      result.ok = false;
      result.start = startPos;
      result.end = cursor;
      return result;
    }
  } else {
    // (on word && include white) OR (on white && exclude white): find next word start
    fwdWordOperator(cursor, lines, big, 1);
    // Vim: "If we end up in the first column of the next line (single char
    // word), back up to end of the line."
    if (cursor.col == 0) {
      decl(lines, cursor);
    } else {
      oneLeftCursor(cursor);
    }
    if (include) {
      includeWhite = true;
    }
  }

  // Step 3: include_white extension — for "daw" on last word, extend start
  // backward to include leading whitespace, but NOT indent at line start.
  if (includeWhite &&
      (wordCharClassAt(lines, cursor, big) != 0 ||
       (cursor.col == 0 && !result.inclusive))) {
    CursorPos savedEnd = cursor;
    cursor = startPos;
    if (oneLeftCursor(cursor)) {
      backInLine(cursor, lines, big);
      if (wordCharClassAt(lines, cursor, big) == 0 && cursor.col > 0) {
        startPos = cursor;
      }
    }
    cursor = savedEnd;
  }

  result.start = startPos;
  result.end = cursor;
  return result;
}

// =============================================================================
// Paragraph motion forwarders
// =============================================================================

void motionParagraphPrev(CursorPos &pos, const Lines &lines) {
  pos.line = motionParagraphEndpoint<false, LineEdgeType::NextEdge>(pos.line, lines);
  pos.setCol(0);
}

void motionParagraphNext(CursorPos &pos, const Lines &lines) {
  int resultLine = motionParagraphEndpoint<true, LineEdgeType::NextEdge>(pos.line, lines);
  pos.line = resultLine;

  // Special case: if at last line and it's not blank, go to last char
  // (This matches vim's behavior at EOF)
  if (resultLine == lines.lastLine() && !isParagraphSeparatorLine(lines[resultLine])) {
    pos.setCol(lines[resultLine].lastCol());
  } else {
    pos.setCol(0);
  }
}

// =============================================================================
// CursorPos helpers
// =============================================================================

int clampCol(const Lines &lines, int col, int lineIdx) {
  return lines.clamp(CursorPos(lineIdx, col)).col;
}

void moveCol(CursorPos &pos, const Lines &lines, int dx) {
  int nextCol = clampCol(lines, pos.col + dx, pos.line);
  if (nextCol == pos.col) return;
  pos.setCol(nextCol);
}

void moveLine(CursorPos &pos, const Lines &lines, int dy) {
  int n = static_cast<int>(lines.size());
  int nextLine = std::clamp(pos.line + dy, 0, n - 1);
  if (nextLine == pos.line) return;
  pos.line = nextLine;
  // Vertical movement: clamp col to line length but preserve targetCol
  pos.clampColPreservingTarget(clampCol(lines, pos.targetCol, pos.line));
}

// =============================================================================
// Sentence motions
// =============================================================================

namespace {

// The scan works on `gchar` ints (0 = NUL slot), which CharMask never flags.
inline bool sentenceIsWhite(int c) {
  return CharMask::isWhitespace(static_cast<char>(c));
}

inline bool sentenceIsEndPunct(int c) {
  return CharMask::isSentenceEnd(static_cast<char>(c));
}

inline bool sentenceIsPunctOrCloser(int c) {
  CharMask cls(static_cast<char>(c));
  return cls.sentenceEnd() || cls.sentenceCloser();
}

bool sentenceInMacro(string_view opt, string_view s) {
  for (size_t i = 0; i < opt.size(); i += 2) {
    char first = opt[i];
    char second = (i + 1 < opt.size()) ? opt[i + 1] : '\0';
    char s0 = s.empty() ? '\0' : s[0];
    char s1 = s.size() < 2 ? '\0' : s[1];

    bool firstMatches = first == s0 || (first == ' ' && (s0 == '\0' || s0 == ' '));
    bool secondMatches = second == s1 ||
        ((second == '\0' || second == ' ') && (s0 == '\0' || s1 == '\0' || s1 == ' '));
    if (firstMatches && secondMatches) return true;
  }
  return false;
}

bool sentenceStartPS(const Lines& lines, int line, int para = 0, bool both = false) {
  if (line < 0 || line >= static_cast<int>(lines.size())) return false;
  const string& s = lines[line];
  char first = s.empty() ? '\0' : s[0];
  if (static_cast<unsigned char>(first) == para || first == '\f' ||
      (both && first == '}')) {
    return true;
  }
  static constexpr string_view SECTIONS = "SHNHH HUnhsh";
  static constexpr string_view PARAGRAPHS = "IPLPPPQPP TPHPLIPpLpItpplpipbp";
  return first == '.' &&
      (sentenceInMacro(SECTIONS, string_view(s).substr(1)) ||
       (para == 0 && sentenceInMacro(PARAGRAPHS, string_view(s).substr(1))));
}

// Preserves the past-EOL position (col == size) that Neovim's findsent leaves
// on the NUL terminator; operator endpoints need it to build the exclusive
// range, and motion callers clamp afterwards.
CursorPos sentenceScanToCursorRaw(const Lines& lines, CursorPos pos) {
  pos.line = clamp(pos.line, 0, lines.lastLine());
  int len = static_cast<int>(lines[pos.line].size());
  if (len == 0) return {pos.line, 0};
  return {pos.line, clamp(pos.col, 0, len)};
}

}  // namespace

// Port of Neovim's `findsent()` from textobject.c.
bool findSentenceForOperator(CursorPos& cursor, const Lines& lines, bool forward, int count) {
  if (count <= 0) return false;

  CursorPos pos(clamp(cursor.line, 0, lines.lastLine()), cursor.col);
  int len = static_cast<int>(lines[pos.line].size());
  // Allow col up to len (NUL position) — Vim's findsent operates on NUL too,
  // and the sentence text object port (current_sent in VimEndpointUtils) calls
  // findsent with post-incl cursors that legitimately sit at NUL. Out-of-range
  // values are still clamped.
  pos.col = clamp(pos.col, 0, len);

  bool noskip = false;
  while (count > 0) {
    count--;
    CursorPos prevPos = pos;
    int c = gchar(lines, pos);

    if (c == 0) {
      do {
        if ((forward ? incl(lines, pos) : decl(lines, pos)) == -1) {
          break;
        }
      } while (gchar(lines, pos) == 0);
      if (forward) {
        goto found;
      }
    } else if (forward && pos.col == 0 && sentenceStartPS(lines, pos.line)) {
      if (pos.line == lines.lastLine()) {
        return false;
      }
      pos.line++;
      pos.col = 0;
      goto found;
    } else if (!forward) {
      decl(lines, pos);
    }

    {
      bool foundDot = false;
      while (true) {
        c = gchar(lines, pos);
        if (!sentenceIsWhite(c) && !sentenceIsPunctOrCloser(c)) {
          break;
        }

        CursorPos tpos = pos;
        if (decl(lines, tpos) == -1 ||
            (lines[tpos.line].empty() && forward)) {
          break;
        }
        if (foundDot) {
          break;
        }
        if (sentenceIsEndPunct(c)) {
          foundDot = true;
        }
        if ((c == ')' || c == ']' || c == '"' || c == '\'') &&
            !sentenceIsPunctOrCloser(gchar(lines, tpos))) {
          break;
        }
        decl(lines, pos);
      }
    }

    {
      int startLine = pos.line;
      while (true) {
        c = gchar(lines, pos);
        if (c == 0 || (pos.col == 0 && sentenceStartPS(lines, pos.line))) {
          if (!forward && pos.line != startLine) {
            pos.line++;
            pos.col = 0;
          }
          break;
        }

        if (sentenceIsEndPunct(c)) {
          CursorPos tpos = pos;
          int next = 0;
          do {
            next = inc(lines, tpos);
            if (next == -1) break;
          } while (gchar(lines, tpos) == ')' ||
                   gchar(lines, tpos) == ']' ||
                   gchar(lines, tpos) == '"' ||
                   gchar(lines, tpos) == '\'');

          int after = gchar(lines, tpos);
          if (next == -1 || sentenceIsWhite(after) || after == 0) {
            pos = tpos;
            if (gchar(lines, pos) == 0) {
              incl(lines, pos);
            }
            break;
          }
        }

        if ((forward ? incl(lines, pos) : decl(lines, pos)) == -1) {
          if (count > 0) {
            return false;
          }
          noskip = true;
          break;
        }
      }
    }

found:
    while (!noskip && sentenceIsWhite(gchar(lines, pos))) {
      if (incl(lines, pos) == -1) {
        break;
      }
    }

    if (prevPos == pos) {
      if ((forward ? incl(lines, pos) : decl(lines, pos)) == -1) {
        if (count > 0) {
          return false;
        }
        break;
      }
      count++;
    }
  }

  cursor = sentenceScanToCursorRaw(lines, pos);
  return true;
}

void motionSentenceNext(CursorPos &pos, const Lines &lines) {
  findSentenceForOperator(pos, lines, true, 1);
  pos = lines.clamp(pos);
}

void motionSentencePrev(CursorPos &pos, const Lines &lines) {
  findSentenceForOperator(pos, lines, false, 1);
  pos = lines.clamp(pos);
}

// =============================================================================
// Character Find (f/F/t/T)
// =============================================================================

int findCharInLine(char target, string_view line, int startCol,
                   bool forward, bool till, int count, bool repeat) {
  if (count <= 0) return -1;

  const int n = static_cast<int>(line.size());
  const int step = forward ? 1 : -1;
  int i = startCol + step;

  // Repeating t/T skips the adjacent target the prior till stopped beside.
  if (repeat && till && i >= 0 && i < n && line[i] == target) {
    i += step;
    if (count > 1) count--;
  }

  int seen = 0;
  for (; i >= 0 && i < n; i += step) {
    if (line[i] == target) {
      seen++;
      if (seen == count) {
        return till ? i - step : i;
      }
    }
  }
  return -1; // Not found
}

// =============================================================================
// Template instantiations
// =============================================================================

// Return char since f motions are guaranteed to just be one character. Will be
// converted to string further up.
template <bool Forward>
vector<tuple<char, int, int>>
generateFMotions(int currCol, int targetCol,
                 string_view line, int threshold) {
  vector<tuple<char, int, int>> res;
  const int n = static_cast<int>(line.size());

  threshold = min(threshold, abs(currCol - targetCol));
  int l = max(0, targetCol - threshold);
  int r = min(n - 1, targetCol + threshold);

  if constexpr (Forward) {
    l = max(l, currCol + 1);
  } else {
    r = min(r, currCol - 1);
  }
  // Reachable: a boundary column can exceed the line after a deletion shrinks it.
  if (l > r) return res;

  res.reserve(r - l + 1);
  // Might be possible to optimize with static instance and resetting only
  // characters touched, but 256 is small so not necessary.
  array<int, 256> cnt{};

  // Count occurrences between cursor and window
  if constexpr (Forward) {
    for (int i = currCol + 1; i < l; i++) {
      cnt[static_cast<unsigned char>(line[i])]++;
    }
    for (int i = l; i <= r; i++) {
      char c = line[i];
      res.emplace_back(c, i, cnt[static_cast<unsigned char>(c)]++);
    }
  } else {
    for (int i = currCol - 1; i > r; i--) {
      cnt[static_cast<unsigned char>(line[i])]++;
    }
    for (int i = r; i >= l; i--) {
      char c = line[i];
      res.emplace_back(c, i, cnt[static_cast<unsigned char>(c)]++);
    }
  }
  return res;
}

template std::vector<std::tuple<char, int, int>>
generateFMotions<true>(int, int, std::string_view, int);

template std::vector<std::tuple<char, int, int>>
generateFMotions<false>(int, int, std::string_view, int);

} // namespace VimCore
