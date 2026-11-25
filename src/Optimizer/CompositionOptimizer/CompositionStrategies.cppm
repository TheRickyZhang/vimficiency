export module vf.Optimizer.CompositionOptimizer.CompositionStrategies;

import std.compat;
export import vf.Optimizer.BuildTypedCommands;
export import vf.Optimizer.CompositionOptimizer.CompositionSearchContext;
export import vf.Optimizer.DiffPlanner.DiffState;
export import vf.Types.BracketFlags;
export import vf.Types.Lines;
export import vf.Types.QuoteFlags;
export import vf.VimCore.VimCore;
export import vf.VimCore.VimEditUtils;
export import vf.VimCore.VimOptions;

export {

// =============================================================================
// Composition strategies — single source of truth
// =============================================================================
// What sequences telescope Navigate + Transform (and possibly Insert) for a
// given diff. Both consumers hand callbacks here:
//
//   - CompositionOptimizer (full A*) enqueues each strategy as a candidate
//     edit transition (with motion prefix found by an inner NavOptimizer).
//   - CompositionFrontier emits only the single structural action when the
//     current cursor is already in the strategy's activation region.
//
// Line-break replacements are batch-only because their Insert continuation
// includes newline removal before typing.



namespace CompositionStrategies {

// One Insert-mode entry strategy. Multiple strategies may apply to the same diff
// (e.g. `I` line-scope + `i` point-scope for FNB insertions); each is
// reported as a separate callback invocation.
//
// Activation region: cursor lands inside the half-open range
// [beginCol, endCol) on `targetLine`.
//
// `structural` is the single Vim action that enters Insert at the right
// position (one of `o O i a I A`). `insertCmd` includes any newline
// removal, the typed payload and the trailing `<Esc>`.
// CompositionFrontier emits `structural` only; CompositionOptimizer (batch
// A*) uses `insertCmd`.
struct Insertion {
  int targetLine;
  int beginCol;
  int endCol;
  std::string structural;
  std::string insertCmd;
  CursorPos goalPos;
  CursorPos entryPos;
};

// One bracket/quote shortcut. Many strategies per diff — one per
// (column, pair) seen by validQuoteMask / validBracketMask.
//
// Activation region: cursor at the enumerated `(line, col)`.
//
// Explore emits the structural action. The batch body also includes any
// Insert-mode keys needed to complete a replacement.
struct BracketQuote {
  int line;
  int col;
  std::string structural;
  std::string body;
  CursorPos goalPos;
};

// Typed payload after the structural entry (trailing `<Esc>` included) and
// the Normal-mode cursor it leaves.
struct TypedInsertion {
  KeyedSequence typed;
  CursorPos exitCursor;
};

inline TypedInsertion typedInsertionOnOpenedLine(int newLine, std::string_view body,
                                                std::string_view indent, const IndentOptions& options) {
  Lines insertLines = Lines::unflatten(std::string(body));
  return {buildTypedCommands(insertLines, indent, "", "", options),
          typedCommandsExitCursor(CursorPos(newLine, 0), insertLines)};
}

// `i a I A` at `insertPos`: the structural only picks the entry, the payload
// is the same for all of them.
inline TypedInsertion typedInsertionAt(const Lines& lines, CursorPos insertPos,
                                       const Lines& insertLines, const IndentOptions& options) {
  std::string_view lineText = lines[insertPos.line];
  std::string_view prefix = lineText.substr(0, insertPos.col);
  std::string_view suffix = lineText.substr(insertPos.col);
  return {buildTypedCommands(insertLines, "", prefix, suffix, options),
          typedCommandsExitCursor(insertPos, insertLines, suffix, options)};
}

// Trailing `\n` at col 0 of a later line: the line is wholly new content, so
// opening a line (`o`/`O`) is the only entry.
inline bool opensLineOnly(const DiffState& diff) {
  return diff.isNewLineInsertion() && diff.beginPos.col == 0 &&
         diff.beginPos.line > 0;
}

// The single payload for a pure insertion once Insert has been entered: the
// `o` form when opening a line is the only entry, else the column form (a
// leading-`\n` append is typed as `<CR>...` from the column).
inline TypedInsertion typedInsertion(const DiffState& diff, const Lines& lines, const IndentOptions& options) {
  if (opensLineOnly(diff)) {
    return typedInsertionOnOpenedLine(diff.beginPos.line, diff.insertedTextBody(),
                                      options.openedIndent(lines[diff.beginPos.line - 1]), options);
  }
  return typedInsertionAt(lines, diff.beginPos, diff.insertedLines(), options);
}

// Enumerate every Insertion strategy for `diff` against `lines`. No-op
// when `diff` is not a pure insertion.
template <class OnStrategy>
void enumerateInsertions(const DiffState& diff, const Lines& lines, const IndentOptions& options, OnStrategy&& cb) {
  if (!diff.isPureInsertion()) return;
  const CursorPos insertPos = diff.beginPos;
  const std::string& ins = diff.insertedText;

  // Opening a line types the body on a new line: `o` from the line above it or
  // `O` from the line below it. A new-line insertion reaches them in two
  // equivalent anchorings: a *trailing* `\n` at col 0 of a line (insert "X\n"
  // before it), or a *leading* `\n` at the end of a line (insert "\nX" after
  // it) — the latter is also the only form for an end-of-buffer append. The
  // body is the insert minus that one boundary newline. Autoindent copies the
  // line the command runs from, so `o` and `O` can differ in payload.
  const bool trailingNl = diff.isNewLineInsertion() && insertPos.col == 0;
  const bool leadingNl = !ins.empty() && ins.front() == '\n' &&
                         insertPos.col == lines[insertPos.line].effectiveSize();
  if (trailingNl || leadingNl) {
    const int newLine = trailingNl ? insertPos.line : insertPos.line + 1;
    std::string_view body = trailingNl ? diff.insertedTextBody()
                                       : std::string_view(ins).substr(1);
    auto openFrom = [&](int fromLine, std::string_view command) {
      const auto indent = options.openedIndent(lines[fromLine]);
      TypedInsertion opened = typedInsertionOnOpenedLine(newLine, body, indent, options);
      cb(Insertion{fromLine, 0, lines[fromLine].effectiveSize(), std::string(command),
                   std::string(command) + opened.typed.seq.str(), opened.exitCursor,
                   CursorPos(newLine, indent.size())});
    };
    if (newLine > 0) openFrom(newLine - 1, "o");
    if (newLine <= lines.lastLine()) openFrom(newLine, "O");
    if (opensLineOnly(diff)) return;
  }

  // i/a/I/A family; lowercase first so it wins exact-cost ties.
  const int line = insertPos.line;
  const int fnb = VimCore::firstNonBlankColInLine(lines[line]);
  const int lineLen = lines.getSize(line);
  const int lineEnd = lines[line].effectiveSize();
  TypedInsertion at = typedInsertionAt(lines, insertPos, diff.insertedLines(), options);
  const std::string escaped = at.typed.seq.str();

  // `i` needs the cursor on the insert column, which is unreachable one past
  // a non-empty line.
  if (insertPos.col < lineEnd) {
    cb(Insertion{line, insertPos.col, insertPos.col + 1,
                 "i", "i" + escaped, at.exitCursor, insertPos});
  }
  // `a` from the character before the insert point (col 0 on an empty line).
  if (insertPos.col > 0 || lineLen == 0) {
    const int aCol = std::max(insertPos.col - 1, 0);
    cb(Insertion{line, aCol, aCol + 1, "a", "a" + escaped, at.exitCursor, insertPos});
  }
  // `I` lands at the first non-blank, or past the indent (col == size) on an
  // all-whitespace line.
  const int iCol = VimCore::isBlankLine(lines[line]) ? lineLen : fnb;
  if (insertPos.col == iCol) {
    cb(Insertion{line, 0, lineEnd, "I", "I" + escaped, at.exitCursor, insertPos});
  }
  if (insertPos.col == lineLen) {
    cb(Insertion{line, 0, lineEnd, "A", "A" + escaped, at.exitCursor, insertPos});
  }
}

template <class OnStrategy>
void enumerateLineBreakReplacements(const DiffState& diff, const Lines& lines, const IndentOptions& options, OnStrategy&& cb) {
  if (!diff.isLineBreakReplacement()) return;
  const Lines inserted = diff.insertedLines();
  const auto typed = buildTypedCommands(inserted, "", diff.boundary.prefix(), diff.boundary.suffix(), options);
  const auto goal = typedCommandsExitCursor(diff.beginPos, inserted, diff.boundary.suffix(), options);
  const int breaks = diff.endPos.line - diff.beginPos.line;
  auto emit = [&](int lineOffset, int endCol, std::string entry) {
    auto collapse = buildCollapseSequence(breaks + 1, lineOffset);
    const int line = diff.beginPos.line + lineOffset;
    cb(Insertion{line, 0, endCol, entry, entry + collapse.seq.str() + typed.seq.str(),
                 goal, {line, lineOffset == 0 ? diff.beginPos.col : 0}});
  };
  emit(0, lines[diff.beginPos.line].effectiveSize(), "A");
  emit(breaks, 1, "i");
}

inline CursorPos textObjectDeleteLanding(const DiffState& diff, const Lines& lines) {
  Lines line{lines[diff.beginPos.line]};
  CursorPos landing(0, diff.beginPos.col);
  VimCore::deleteRangeAndUpdatePos(line, CharRange(landing, CursorPos(0, diff.endPos.col)), landing);
  landing.line = diff.beginPos.line;
  return landing;
}

// Enumerate every BracketQuote strategy for `diff` against `bqContext`.
// No-op when `diff` is a pure insertion or `bqContext` was not computed
// for this diff's line (line < 0).
template <class OnStrategy>
void enumerateBracketQuotes(const DiffState& diff,
                            const Lines& lines,
                            const BracketQuoteContext& bqContext, const IndentOptions& options,
                            OnStrategy&& cb) {
  if (diff.isPureInsertion()) return;
  if (bqContext.line < 0 || !bqContext.hasAnyValid()) return;

  const bool pureDeletion = diff.isPureDeletion();
  const char textObjOp = pureDeletion ? 'd' : 'c';
  Sequence typed;
  CursorPos goalPos;
  if (pureDeletion) {
    goalPos = textObjectDeleteLanding(diff, lines);
  } else {
    const Lines inserted = diff.insertedLines();
    const auto& suffix = diff.boundary.suffix();
    typed = buildTypedCommands(inserted, "", diff.boundary.prefix(), suffix, options).seq;
    goalPos = typedCommandsExitCursor(diff.beginPos, inserted, suffix, options);
  }

  auto emit = [&](int col, char modifier, char delimiter) {
    std::string structural{textObjOp, modifier, delimiter};
    cb(BracketQuote{bqContext.line, col, structural, structural + typed.str(), goalPos});
  };

  for (int col = 0; col < static_cast<int>(bqContext.validQuoteMask.size()); col++) {
    for (char q : QuoteFlags::ALL_QUOTES) {
      if (!bqContext.validQuoteMask[col].seen(q)) continue;
      emit(col, bqContext.quoteModifier(q), q);
    }
  }
  for (int col = 0; col < static_cast<int>(bqContext.validBracketMask.size()); col++) {
    for (char b : BracketFlags::ALL_BRACKETS) {
      if (!bqContext.validBracketMask[col].seen(b)) continue;
      emit(col, bqContext.bracketModifier(b), b);
    }
  }
}

}  // namespace CompositionStrategies

}  // export
