// Safety property: arbitrary bytes cross the FFI payload layer intact or are
// rejected cleanly, and the sequence parser returns stable, bounded results.

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>
#include <msgpack.hpp>

#include "LuaExports/Api.h"

import std.compat;
import vf.Interpreter.SequenceParser;
import vf.LuaExports.Common;
import vf.LuaExports.Payload;

using namespace std;

namespace payload = VF::LuaExports::payload;

namespace {

void PackedStringsRoundTrip(vector<string> values) {
  const VF::LuaExports::Packed packed = VF::LuaExports::packStrings(values);

  size_t offset = 0;
  const msgpack::object_handle handle =
      msgpack::unpack(packed.bytes.data(), packed.bytes.size(), offset);
  ASSERT_EQ(offset, packed.bytes.size());

  const msgpack::object& object = handle.get();
  ASSERT_EQ(object.type, msgpack::type::ARRAY);
  ASSERT_EQ(object.via.array.size, values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    const msgpack::object& item = object.via.array.ptr[i];
    ASSERT_EQ(item.type, msgpack::type::STR);
    EXPECT_EQ(string_view(item.via.str.ptr, item.via.str.size), values[i]);
  }
}

void LineArraysCopyOrReject(vector<string> lines) {
  vector<VFByteSlice> slices;
  bool valid = !lines.empty();
  for (const string& line : lines) {
    slices.push_back(VFByteSlice{.data = line.data(), .len = line.size()});
    if (line.contains('\n') || line.contains('\0')) valid = false;
  }

  auto copied = payload::linesFromSlices(slices.data(), slices.size());
  ASSERT_EQ(copied.has_value(), valid);
  if (!copied) return;

  ASSERT_EQ(copied->size(), lines.size());
  for (size_t i = 0; i < lines.size(); ++i) {
    EXPECT_EQ((*copied)[i], lines[i]);
  }
}

void SequenceTokensAreStable(string sequence) {
  auto parsed = parseSequence(sequence);
  if (!parsed) {
    EXPECT_LE(parsed.error().offset, sequence.size());
    EXPECT_FALSE(formatSequenceParseError(parsed.error()).empty());
    return;
  }

  auto parsedStrings = parseSequenceStrings(sequence);
  ASSERT_TRUE(parsedStrings) << formatSequenceParseError(parsedStrings.error());
  ASSERT_EQ(parsedStrings->size(), parsed->size());
  for (size_t i = 0; i < parsed->size(); i++) {
    const auto& token = (*parsed)[i];
    EXPECT_EQ((*parsedStrings)[i], string(token.token));
  }
}

}  // namespace

FUZZ_TEST(PayloadSafetyTest, PackedStringsRoundTrip)
    .WithDomains(fuzztest::VectorOf(fuzztest::String().WithMaxSize(300)).WithMaxSize(20))
    .WithSeeds({
        vector<string>{},
        vector<string>{""},
        vector<string>{"a", "", "b c"},
        vector<string>{string("a\0b", 3), "\xff\x1e\x1f", "a\nb\tc"},
        // Each string length header: fixstr, str8, str16.
        vector<string>{string(31, 'x'), string(32, 'x'), string(255, 'x'), string(256, 'x')},
        // array16 header.
        vector<string>(16, "x"),
    });

FUZZ_TEST(PayloadSafetyTest, LineArraysCopyOrReject)
    .WithDomains(fuzztest::VectorOf(fuzztest::String().WithMaxSize(64)).WithMaxSize(20))
    .WithSeeds({
        vector<string>{},
        vector<string>{""},
        vector<string>{"a", ""},
        vector<string>{"", "", ""},
        vector<string>{"a\nb"},
        vector<string>{"ok", "\n"},
        vector<string>{string("a\0b", 3)},
        vector<string>{"\xff\x1e\x1f"},
    });

FUZZ_TEST(
    SequenceParserSafetyTest,
    SequenceTokensAreStable)
    .WithDomains(fuzztest::String().WithMaxSize(256))
    .WithSeeds({
        "",
        "w",
        "3dw",
        "ciwhello<Esc>",
        "<C-d>",
        "\xffw",

        // Partial and malformed tokens.
        "<",
        "<C-",
        "<Bad>",
        "f,;",
        "d",
        "r",
        "ra",
        "iabc<Esc>",
        "vwd",
        "999999999999dw",
    });
