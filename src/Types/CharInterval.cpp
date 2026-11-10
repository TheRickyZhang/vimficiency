module;
#include <cassert>

module vf.Types.Lines;

import std.compat;

CharInterval::CharInterval(const CharRange& range, const Lines& lines)
    : first(range.begin),
      last(lines.getPrevPos(range.end)) {
  assert(range.isValid() && !range.isEmpty());
  if (last < first) first = last;
}
