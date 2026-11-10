export module vf.tests.Unit.TransformOptimizer.EmbeddedRegionTestUtils;

import std.compat;
export import vf.Boundary.TransformBoundary;
export import vf.Types.CursorPos;
export import vf.Types.Lines;

export {

// Edit slice plus surrounding full-buffer context used to verify boundaries.
struct EmbeddedEditRegion {
  Lines fullBuffer;
  Lines editRegion;
  std::string prefix;
  std::string suffix;

  int startLine;
  int startCol;
  int endLine;
  int endCol;

  TransformBoundary makeBoundary() const;
  CursorPos toFullBufferPos(const CursorPos& editPos) const;
  std::string expectedAfterDeletion() const;
};

EmbeddedEditRegion generateSingleLineEmbedded(
    int prefixLen,
    int contentLen,
    int suffixLen);

EmbeddedEditRegion generateMultiLineEmbedded(
    int numLines,
    int minLineLen,
    int maxLineLen,
    int prefixLen,
    int suffixLen);

EmbeddedEditRegion generateRandomSingleLineEmbedded();
EmbeddedEditRegion generateRandomMultiLineEmbedded();

}  // export
