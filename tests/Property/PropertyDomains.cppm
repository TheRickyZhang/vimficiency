module;
#include <fuzztest/fuzztest.h>

export module vf.tests.Property.PropertyDomains;

import std.compat;

export {

namespace PropertyDomains {

inline auto LineTextDomain(int minLen, int maxLen) {
  return fuzztest::PrintableAsciiString()
      .WithMinSize(minLen)
      .WithMaxSize(maxLen);
}

inline auto LineVecDomain(
    int minLines, int maxLines, int minLineLen, int maxLineLen) {
  return fuzztest::VectorOf(LineTextDomain(minLineLen, maxLineLen))
      .WithMinSize(minLines)
      .WithMaxSize(maxLines);
}

}  // namespace PropertyDomains

}  // export
