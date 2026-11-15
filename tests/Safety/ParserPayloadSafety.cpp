// Safety property: arbitrary line arrays cross the FFI payload layer intact or
// are rejected cleanly, and the sequence parser returns stable, bounded results.

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

import std.compat;
import vf.Interpreter.SequenceParser;
import vf.LuaExports.Common;
import vf.LuaExports.Payload;

using namespace std;

namespace payload = VF::LuaExports::payload;

namespace {

void LineArraysCopyOrReject(vector<string> lines) {
  bool valid = !lines.empty();
  for (const string& line : lines) {
    if (line.contains('\n') || line.contains('\0')) valid = false;
  }

  auto copied = payload::linesFrom(lines);
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
