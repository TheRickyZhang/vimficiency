#include <gtest/gtest.h>

#include "LuaExports/Api.h"

import std.compat;
import vf.LuaExports.Common;
import vf.LuaExports.Payload;
import vf.Types.Lines;

namespace payload = VF::LuaExports::payload;

namespace {

VF::LuaExports::Expected<Lines> linesFrom(const std::vector<std::string>& lines) {
  std::vector<VFByteSlice> slices;
  for (const std::string& line : lines) {
    slices.push_back(VFByteSlice{.data = line.data(), .len = line.size()});
  }
  return payload::linesFromSlices(slices.data(), slices.size());
}

}  // namespace

TEST(LuaExportsPayloadTest, LinesFromSlicesPreservesTrailingEmptyLine) {
  auto result = linesFrom({"a", ""});

  ASSERT_TRUE(result.has_value()) << result.error().message;
  ASSERT_EQ(result->size(), 2u);
  EXPECT_EQ((*result)[0], "a");
  EXPECT_EQ((*result)[1], "");
}

TEST(LuaExportsPayloadTest, LinesFromSlicesRejectsEmptyArray) {
  EXPECT_FALSE(linesFrom({}).has_value());
}

TEST(LuaExportsPayloadTest, LinesFromSlicesRejectsNewlineInsideLine) {
  EXPECT_FALSE(linesFrom({"ok", "a\nb"}).has_value());
}

TEST(LuaExportsPayloadTest, LinesFromSlicesRejectsNulInsideLine) {
  EXPECT_FALSE(linesFrom({std::string("a\0b", 3)}).has_value());
}

// Regression: the analyze FFI boundary clamps captured cursors into a valid
// normal-mode target. Neovim can report an insert-mode / 'virtualedit' cursor
// one past end-of-line, which the optimizer's contains() invariant rejects.
TEST(LuaExportsPayloadTest, ClampMapsCursorIntoNormalModeTarget) {
  Lines lines = {"abc", "", "de"};

  auto pastEol = lines.clamp(CursorPos(0, 3));   // insert pos at EOL
  EXPECT_EQ(pastEol.line, 0);
  EXPECT_EQ(pastEol.col, 2);

  auto onEmpty = lines.clamp(CursorPos(1, 5));   // empty line -> col 0
  EXPECT_EQ(onEmpty.line, 1);
  EXPECT_EQ(onEmpty.col, 0);

  auto pastEnd = lines.clamp(CursorPos(9, 9));   // row past last -> last line/char
  EXPECT_EQ(pastEnd.line, 2);
  EXPECT_EQ(pastEnd.col, 1);

  EXPECT_TRUE(lines.contains(lines.clamp(CursorPos(0, 1))));   // already valid
  EXPECT_TRUE(lines.contains(lines.clamp(CursorPos(-1, -1)))); // negative
}
