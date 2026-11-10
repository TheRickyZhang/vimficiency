// Run: ./build/tests/vimfy_unit_tests --gtest_filter="*SequenceToKeys*"

#include <gtest/gtest.h>

import std.compat;
import vf.Keyboard.ToKeys.MovementToKeys;

using namespace std;

namespace {

PhysicalKeys charwise(string_view s) {
  PhysicalKeys keys;
  for (char c : s) keys.append(CHAR_TO_KEYS.at(c));
  return keys;
}

}  // namespace

// The tokenizer's only non-charwise tokens are the <...> key notations; every
// other byte must price exactly as CHAR_TO_KEYS does.
TEST(SequenceToKeysTest, PrintableAsciiMatchesCharToKeys) {
  const auto& tok = globalSequenceToKeys();
  string chars = "\t\n\r";
  for (char c = 0x20; c <= 0x7e; ++c) chars += c;
  for (char c : chars) {
    ASSERT_TRUE(CHAR_TO_KEYS.contains(c)) << "byte " << int(c);
    EXPECT_EQ(tok.tokenize(string(1, c)), CHAR_TO_KEYS.at(c)) << "byte " << int(c);
  }
}

TEST(SequenceToKeysTest, MotionTokensAreCharwise) {
  const auto& tok = globalSequenceToKeys();
  for (const string& motion : ALL_MOTIONS) {
    if (motion.front() == '<') continue;
    EXPECT_EQ(tok.tokenize(motion), charwise(motion)) << motion;
  }
}

TEST(SequenceToKeysTest, KeyNotations) {
  const auto& tok = globalSequenceToKeys();
  EXPECT_EQ(tok.tokenize("<lt>"), CHAR_TO_KEYS.at('<'));
  EXPECT_EQ(tok.tokenize("<LT>"), CHAR_TO_KEYS.at('<'));
  EXPECT_EQ(tok.tokenize("<"), CHAR_TO_KEYS.at('<'));
  EXPECT_EQ(tok.tokenize("<C-d>"), PhysicalKeys({Key::Key_Ctrl, Key::Key_D}));
  EXPECT_EQ(tok.tokenize("<C-Space>"), PhysicalKeys({Key::Key_Ctrl, Key::Key_Space}));
  EXPECT_EQ(tok.tokenize("<Esc>"), PhysicalKeys({Key::Key_Esc}));
  EXPECT_EQ(tok.tokenize("<Space>"), PhysicalKeys({Key::Key_Space}));
  EXPECT_EQ(tok.tokenize("<BS>"), PhysicalKeys({Key::Key_Backspace}));
}

TEST(SequenceToKeysTest, MixedSequences) {
  const auto& tok = globalSequenceToKeys();
  EXPECT_EQ(tok.tokenize("gg3w<C-d>fA;"),
            PhysicalKeys({Key::Key_G, Key::Key_G, Key::Key_3, Key::Key_W,
                          Key::Key_Ctrl, Key::Key_D, Key::Key_F,
                          Key::Key_Shift, Key::Key_A, Key::Key_Semicolon}));
  EXPECT_EQ(tok.tokenize("ci(a<lt>b<Esc>"),
            PhysicalKeys({Key::Key_C, Key::Key_I, Key::Key_Shift, Key::Key_9,
                          Key::Key_A, Key::Key_Shift, Key::Key_Comma, Key::Key_B,
                          Key::Key_Esc}));
  // A '<' that opens no known notation is literal text.
  EXPECT_EQ(tok.tokenize("i<hi"),
            PhysicalKeys({Key::Key_I, Key::Key_Shift, Key::Key_Comma,
                          Key::Key_H, Key::Key_I}));
}
