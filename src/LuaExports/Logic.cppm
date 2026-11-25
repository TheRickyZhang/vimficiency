export module vf.LuaExports.Logic;

import std.compat;
import vf.LuaExports.Common;
import vf.LuaExports.Payload;
import vf.Optimizer.Analyze;
import vf.Optimizer.OptimizerParamOverrides;
import vf.Types.Lines;

export namespace VF::LuaExports::logic {

Expected<OptimizerParamOverrides> parseOverrides(std::string_view text);

template <typename Request>
Expected<IndentOptions> decodeIndentOptions(const Request& request) {
  if (!request.has_indent()) return IndentOptions{.shiftwidth = VimOptions::shiftwidth()};
  const auto& in = request.indent();
  if (in.tabstop() < 1 || in.tabstop() > 9999 || in.shiftwidth() < 0)
    return unexpectedError("Invalid buffer indentation widths");
  return IndentOptions{in.autoindent(), in.expandtab(), in.copyindent(), in.tabstop(), in.shiftwidth()};
}

template <typename Message>
void encodeIndentOptions(Message& out, const IndentOptions& options) {
  out.set_autoindent(options.autoindent);
  out.set_expandtab(options.expandtab);
  out.set_copyindent(options.copyindent);
  out.set_tabstop(options.tabstop);
  out.set_shiftwidth(options.shiftwidth);
}

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

  auto indent = decodeIndentOptions(r);
  if (!indent) return std::unexpected(indent.error());
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
      .indent = *indent,
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
  // Insert text: pressed in an insert without ending it.
  bool insert;
};

struct TypedKeys {
  std::string typed;
  // Of `typed`, the insert text.
  std::string typedInsert;
};

// The keys pressed for one CmdAtom, from what Neovim read as typed, in order.
// A plugin that feeds keys as typed (which-key re-feeding what it read with
// getchar()) makes Neovim read them again; those reads are not presses.
TypedKeys typedKeys(std::span<const KeyInput> inputs);
int manualEvictReason(
    int startRow,
    int cursorRow,
    int64_t lastKeyTimeNs,
    bool hasLastKey,
    int64_t nowNs,
    int maxSearchLines,
    int manualIdleTimeoutSeconds);

}  // namespace VF::LuaExports::logic
