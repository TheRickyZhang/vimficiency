export module vf.LuaExports.Logic;

import std.compat;
import vf.LuaExports.Common;
import vf.LuaExports.Payload;
import vf.Optimizer.Analyze;
import vf.Optimizer.OptimizerParamOverrides;
import vf.Types.Lines;

export namespace VF::LuaExports::logic {

Expected<OptimizerParamOverrides> parseOverrides(std::string_view text);

// Owning decode of a search request, so the value outlives the request bytes
// on an async worker. `Request` is `vf.search.SearchRequest` (proto/search.proto).
template <typename Request>
Expected<SearchInputs> decodeSearchRequest(const Request& r) {
  auto initialLines = payload::linesFrom(r.initial_lines());
  if (!initialLines) return std::unexpected(initialLines.error());
  auto goalLines = payload::linesFrom(r.goal_lines());
  if (!goalLines) return std::unexpected(goalLines.error());
  auto overrides = parseOverrides(r.optimizer_overrides());
  if (!overrides) return std::unexpected(overrides.error());

  SearchSlice slice;
  slice.initialLines = std::move(*initialLines);
  slice.goalLines = std::move(*goalLines);
  slice.ends = SessionEnds{
      .startRow = r.start_row(),
      .startCol = r.start_col(),
      .endRow = r.end_row(),
      .endCol = r.end_col(),
      .windowHeight = r.window_height(),
      .scrollAmount = r.scroll_amount(),
  };
  slice.hasLinesAbove = r.has_lines_above();
  slice.hasLinesBelow = r.has_lines_below();
  slice.userSeq = r.user_seq();
  slice.userTyped = r.user_typed();
  return searchInputs(std::move(slice), std::move(*overrides));
}

struct KeyEvent {
  std::string mode;
  std::string keyTyped;
};

std::string buildKeySequence(std::span<const KeyEvent> events);

// Keys in notation that Neovim read as typed (`fed` false), or that a plugin
// fed as if typed (`fed` true).
struct KeyInput {
  std::string keys;
  bool fed;
};

// The keys pressed for one CmdAtom, from what Neovim read as typed, in order.
// A plugin that feeds keys as typed (which-key re-feeding what it read with
// getchar()) makes Neovim read them again; those reads are not presses.
std::string typedKeys(std::span<const KeyInput> inputs);
int manualEvictReason(
    int startRow,
    int cursorRow,
    int64_t lastKeyTimeNs,
    bool hasLastKey,
    int64_t nowNs,
    int maxSearchLines,
    int manualIdleTimeoutSeconds);

}  // namespace VF::LuaExports::logic
