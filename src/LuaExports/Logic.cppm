export module vf.LuaExports.Logic;

import std.compat;
import vf.LuaExports.Common;
import vf.LuaExports.Payload;
import vf.Boundary.NavBoundary;
import vf.Optimizer.OptimizerParamOverrides;
import vf.Types.CursorPos;
import vf.Types.Lines;
import vf.Types.NavContext;

export namespace VF::LuaExports::logic {

Expected<OptimizerParamOverrides> parseOverrides(std::string_view text);

// Owning decode of a search request, so the value outlives the request bytes
// on an async worker. `overrides` stays unresolved because analyze applies it
// to Nav or Composition params depending on whether initial == goal.
struct SearchInputs {
  Lines initialLines;
  Lines goalLines;
  CursorPos initialPos{0, 0};
  CursorPos goalPos{0, 0};
  NavBoundary boundary;
  NavContext navContext;
  std::string userSeq;
  OptimizerParamOverrides overrides;
};

// `Request` is `vf.search.SearchRequest` (proto/search.proto).
template <typename Request>
Expected<SearchInputs> decodeSearchRequest(const Request& r) {
  auto initialLines = payload::linesFrom(r.initial_lines());
  if (!initialLines) return std::unexpected(initialLines.error());
  auto goalLines = payload::linesFrom(r.goal_lines());
  if (!goalLines) return std::unexpected(goalLines.error());
  auto overrides = parseOverrides(r.optimizer_overrides());
  if (!overrides) return std::unexpected(overrides.error());

  SearchInputs in;
  in.initialLines = std::move(*initialLines);
  in.goalLines = std::move(*goalLines);
  in.userSeq = r.user_seq();
  in.overrides = std::move(*overrides);
  // Neovim can capture an insert-mode / 'virtualedit' cursor one past
  // end-of-line, or (for recall windows) a row outside the sliced buffer. The
  // optimizer requires normal-mode-valid endpoints, so clamp at the boundary
  // rather than aborting on the downstream CHECK.
  in.initialPos = in.initialLines.clamp(CursorPos(r.start_row(), r.start_col()));
  in.goalPos = in.goalLines.clamp(CursorPos(r.end_row(), r.end_col()));
  in.boundary = sliceBoundary(in.initialLines, r.has_lines_above(), r.has_lines_below());
  in.navContext = NavContext(r.window_height(), r.scroll_amount());
  return in;
}

struct KeyEvent {
  std::string mode;
  std::string keyTyped;
};

std::string buildKeySequence(std::span<const KeyEvent> events);
std::pair<int, int> computeSearchRegion(
    int startRow,
    int endRow,
    const Lines& startLines,
    const Lines& endLines,
    int padding);
int manualEvictReason(
    int startRow,
    int cursorRow,
    int64_t lastKeyTimeNs,
    bool hasLastKey,
    int64_t nowNs,
    int maxSearchLines,
    int manualIdleTimeoutSeconds);

}  // namespace VF::LuaExports::logic
