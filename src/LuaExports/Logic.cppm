module;
#include "LuaExports/Api.h"

export module vf.LuaExports.Logic;

import std.compat;
import vf.LuaExports.Common;
import vf.Boundary.NavBoundary;
import vf.Optimizer.OptimizerParamOverrides;
import vf.Types.CursorPos;
import vf.Types.Lines;
import vf.Types.NavContext;

export namespace VF::LuaExports::logic {

Result<OptimizerParamOverrides> parseOverrides(std::string_view text);

// Owning decode of a VFSearchRequest. Lines and the user sequence are copied so
// the value outlives the Lua-owned FFI buffers on an async worker. `overrides`
// stays unresolved because analyze applies it to Nav or Composition params
// depending on whether initial == goal.
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

Result<SearchInputs> decodeSearchRequest(const VFSearchRequest* request);

Result<std::string> buildKeySequence(std::string_view encoded);
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
