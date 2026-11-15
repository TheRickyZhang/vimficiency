#include <gtest/gtest.h>

#include "LuaExports/Api.h"
#include "proto/common.pb.h"

import std.compat;
import vf.LuaExports.Common;
import vf.Types.Lines;

// `linesFrom` is covered by PayloadSafetyTest.LineArraysCopyOrReject.

TEST(LuaExportsPayloadTest, GuardedTurnsAnExceptionIntoAnErrorEnvelope) {
  std::string storage;
  const VFByteSlice slice = VF::LuaExports::guarded(
      storage, []() -> VF::LuaExports::ProtoResult { throw std::out_of_range("boom"); });

  vf::Envelope envelope;
  ASSERT_TRUE(envelope.ParseFromArray(slice.data, slice.len));
  EXPECT_EQ(envelope.error(), "C++ exception: boom");
}

TEST(LuaExportsPayloadTest, GuardedRequestRejectsMalformedBytes) {
  std::string storage;
  const std::string garbage = "\xff\xff\xff";
  const VFByteSlice slice = VF::LuaExports::guardedRequest<vf::Position>(
      storage, garbage.data(), garbage.size(),
      [](const vf::Position& p) { return VF::LuaExports::proto(p); });

  vf::Envelope envelope;
  ASSERT_TRUE(envelope.ParseFromArray(slice.data, slice.len));
  EXPECT_EQ(envelope.error(), "malformed vf.Position request");
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
