export module vf.Boundary.TransformBoundary;

import std.compat;
export import vf.Boundary.BoundaryContext;
export import vf.Types.CursorPos;
export import vf.Types.BracketFlags;
export import vf.Types.Lines;
export import vf.Types.NoChar;
export import vf.Types.QuoteFlags;

export {

// =============================================================================
// TransformBoundary: Pre-computed boundary info for constrained transform operations
// =============================================================================
// Historical name retained. Stores the characters immediately outside the
// transform region, plus context
// flags for quote/bracket text objects and line-level operations.
//
// Workflow:
// 1. Compute TransformBoundary from the buffer that contains the transform region.
// 2. Pass to VimEndpointUtils which uses raw chars for boundary decisions.
//
// Related: NavBoundary stores only offsets (no strings) for lighter-weight
// motion constraint checking. Use context() to convert TransformBoundary to
// BoundaryContext for interop with NavBoundary.
// =============================================================================

struct TransformBoundary {
  // Default constructor: empty prefix/suffix, no lines above/below
  TransformBoundary() = default;
  static const TransformBoundary& noParent();
  // Narrow factory: a boundary whose only meaningful fields are
  // hasLinesAbove/hasLinesBelow. Empty prefix/suffix/brackets/quotes are
  // correct only for synthesized single-line residual buffers (today: the
  // join-residual transform in CompositionOptimizer/PlannedEditArtifacts).
  // Do not extend to other use sites without revisiting those defaults.
  static TransformBoundary joinResidualBoundary(bool hasLinesAbove, bool hasLinesBelow);

  // The default constructor has NO PARENT, so basically any motions are possible
  // To specify restricted motions for lines, call with hasLinesBelow = false, hasLinesAbove = false
  // Construct from buffer context, optionally inheriting from parent
  // endPos is exclusive: one past the last valid cursor position on the end line
  TransformBoundary(const Lines& lines, CursorPos beginPos, CursorPos endPos,
               const TransformBoundary& parent = noParent());

  // Getters for boundary content
  inline const std::string& prefix() const { return prefix_; }
  inline const std::string& suffix() const { return suffix_; }
  inline bool hasLinesAbove() const { return hasLinesAbove_; }
  inline bool hasLinesBelow() const { return hasLinesBelow_; }

  // Column offsets (derived from prefix/suffix lengths)
  inline int leftColOffset() const { return static_cast<int>(prefix_.size()); }
  inline int rightColOffset() const { return static_cast<int>(suffix_.size()); }

  // Wrap `editRegionLines` with this boundary's prefix/suffix into the
  // "effective lines" view consumed by TransformExplorer / TransformOptimizer.
  // Both the optimizer's main loop setup and the depth-1 frontier
  // (TransformFrontier::enumerateDepth1DeletionStructurals) call this so the
  // construction stays in lockstep — direct mutation in one consumer was
  // a frequent source of mismatched cursor / boundary semantics.
  inline Lines withBoundary(Lines editRegionLines) const {
    if (!prefix_.empty()) editRegionLines.front().insert(0, prefix_);
    if (!suffix_.empty()) editRegionLines.back() += suffix_;
    return editRegionLines;
  }

  // Convert to BoundaryContext for interop with NavBoundary
  inline BoundaryContext context() const {
    BoundaryContext ctx;
    ctx.hasLinesAbove = hasLinesAbove_;
    ctx.hasLinesBelow = hasLinesBelow_;
    ctx.leftColOffset = leftColOffset();
    ctx.rightColOffset = rightColOffset();
    return ctx;
  }

  // Quote/bracket context for text object operations (read-only)
  inline const QuoteFlags& beginLineQuotes() const { return beginLineQuotes_; }
  inline const QuoteFlags& endLineQuotes() const { return endLineQuotes_; }
  inline const BracketFlags& beginLineBrackets() const { return beginLineBrackets_; }
  inline const BracketFlags& endLineBrackets() const { return endLineBrackets_; }

  // Helper accessors for boundary char (last char of prefix, first char of suffix)
  inline char leftChar() const { return prefix_.empty() ? (hasLinesAbove_ ? '\n' : NO_CHAR) : prefix_.back(); }
  inline char rightChar() const { return suffix_.empty() ? (hasLinesBelow_ ? '\n' : NO_CHAR) : suffix_.front(); }

  inline bool hasPrefix() const { return !prefix_.empty(); }
  inline bool hasSuffix() const { return !suffix_.empty(); }

private:
  // Full content before/after the transform region on the same line.
  // - prefix_: characters before transform region on begin line (empty if at line start)
  // - suffix_: characters after transform region on end line (empty if at line end)
  // These enable correct cursor behavior after multi-line deletions merge lines.
  std::string prefix_;
  std::string suffix_;

  // Whether there are lines above/below the transform region in the buffer.
  // Used for vertical boundary detection (gg, G, k, j escaping).
  bool hasLinesAbove_ = false;
  bool hasLinesBelow_ = false;

  // Quote/bracket context for text object operations
  QuoteFlags beginLineQuotes_;
  QuoteFlags endLineQuotes_;
  BracketFlags beginLineBrackets_;
  BracketFlags endLineBrackets_;

  void computeBoundaryChars(const Lines& lines, CursorPos beginPos, CursorPos endPos);
  void scanQuotesAndBrackets(const Lines& lines, CursorPos beginPos, CursorPos endPos);
};

}  // export
