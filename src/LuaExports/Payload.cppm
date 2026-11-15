module;
#include "LuaExports/Api.h"

export module vf.LuaExports.Payload;

import std.compat;
import vf.LuaExports.Common;
import vf.Optimizer.DiffPlanner.DiffState;
import vf.Types.Lines;

export namespace VF::LuaExports::payload {

// Copies a borrowed line array. Rejects an empty array and any line holding a
// newline or NUL byte.
Expected<Lines> linesFromSlices(const VFByteSlice* slices, size_t count);

// Array of `VF.Diff.Region` (lua/vimficiency/types.lua): 0-indexed, half-open
// `init` and `goal` spans. An empty init span is a pure insertion, an empty
// goal span a pure deletion. Both spans share begin (the common prefix is
// identical in both buffers) but are written explicitly so each pane is
// consumed independently.
void writeDiffRegions(MsgpackWriter& w, const std::vector<DiffState>& diffs);

}  // namespace VF::LuaExports::payload
