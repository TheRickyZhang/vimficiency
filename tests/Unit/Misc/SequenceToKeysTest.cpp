// Run: ./build/tests/vimfy_unit_tests --gtest_filter="*SequenceToKeys*"

#include <gtest/gtest.h>

import std.compat;
import vf.Keyboard.ToKeys.MovementToKeys;
import vf.Types.KeyNotation;
import vf.Utils.Debug;

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
  // keytrans() names `|`, `\` and Ctrl-J.
  EXPECT_EQ(tok.tokenize("<Bar>"), CHAR_TO_KEYS.at('|'));
  EXPECT_EQ(tok.tokenize("<Bslash>"), CHAR_TO_KEYS.at('\\'));
  EXPECT_EQ(tok.tokenize("<NL>"), PhysicalKeys({Key::Key_Ctrl, Key::Key_J}));
}

TEST(SequenceToKeysTest, EveryNamedCharacterIsPriced) {
  const auto& tok = globalSequenceToKeys();
  for (const CharName& named : CHAR_NAMES) {
    EXPECT_FALSE(tok.tokenize(named.name).empty()) << named.name;
  }
}

TEST(SequenceToKeysTest, ModifiersArePricedByStructure) {
  const auto& tok = globalSequenceToKeys();
  EXPECT_EQ(tok.tokenize("<C-^>"), PhysicalKeys({Key::Key_Ctrl, Key::Key_Shift, Key::Key_6}));
  EXPECT_EQ(tok.tokenize("<C-]>"), PhysicalKeys({Key::Key_Ctrl, Key::Key_RBracket}));
  EXPECT_EQ(tok.tokenize("<S-Esc>"), PhysicalKeys({Key::Key_Shift, Key::Key_Esc}));
  EXPECT_EQ(tok.tokenize("<M-x>"), PhysicalKeys({Key::Key_Alt, Key::Key_X}));
  EXPECT_EQ(tok.tokenize("<A-x>"), PhysicalKeys({Key::Key_Alt, Key::Key_X}));
  EXPECT_EQ(tok.tokenize("<M-Bar>"), PhysicalKeys({Key::Key_Alt, Key::Key_Shift, Key::Key_Backslash}));
  EXPECT_EQ(tok.tokenize("<M-C-Left>"), PhysicalKeys({Key::Key_Alt, Key::Key_Ctrl, Key::Key_Left}));
  // keytrans() writes Ctrl-A as `<C-A>`, and a held Shift as its own modifier.
  EXPECT_EQ(tok.tokenize("<C-A>"), PhysicalKeys({Key::Key_Ctrl, Key::Key_A}));
  EXPECT_EQ(tok.tokenize("<C-S-W>"), PhysicalKeys({Key::Key_Ctrl, Key::Key_Shift, Key::Key_W}));
  EXPECT_EQ(tok.tokenize("<M-X>"), PhysicalKeys({Key::Key_Alt, Key::Key_Shift, Key::Key_X}));
}

TEST(SequenceToKeysTest, NamedKeysBeyondTheMainBlock) {
  const auto& tok = globalSequenceToKeys();
  EXPECT_EQ(tok.tokenize("<F5>"), PhysicalKeys({Key::Key_F5}));
  EXPECT_EQ(tok.tokenize("<C-F5>"), PhysicalKeys({Key::Key_Ctrl, Key::Key_F5}));
  EXPECT_EQ(tok.tokenize("<PageDown>"), PhysicalKeys({Key::Key_PageDown}));
  EXPECT_EQ(tok.tokenize("<D-x>"), PhysicalKeys({Key::Key_Cmd, Key::Key_X}));
  EXPECT_EQ(tok.tokenize("<kEnter>"), PhysicalKeys({Key::Key_Enter}));
  EXPECT_EQ(tok.tokenize("<xHome>"), PhysicalKeys({Key::Key_Home}));
}

// A name with no key warns and adds nothing; it is never spelled out letter by letter.
TEST(SequenceToKeysTest, NamesWithoutAKeyWarn) {
  const auto& tok = globalSequenceToKeys();
  consume_warning_output();
  for (const char* name : {"<F13>", "<Help>", "<T-x>", "<NoSuchKey>"}) {
    EXPECT_TRUE(tok.tokenize(name).empty()) << name;
    EXPECT_NE(consume_warning_output().find(name), string::npos) << name;
  }
  EXPECT_EQ(tok.tokenize("a<F13>b"), charwise("ab"));
  consume_warning_output();
}

TEST(SequenceToKeysTest, NamesThatAreNotKeyPressesAreSilent) {
  const auto& tok = globalSequenceToKeys();
  consume_warning_output();
  for (const char* name : {"<t_PS>", "<t_PE>", "<Plug>", "<Cmd>"}) {
    EXPECT_TRUE(tok.tokenize(name).empty()) << name;
  }
  EXPECT_EQ(consume_warning_output(), "");
}

// The fixture is every name Neovim writes (`tests/lua/key_names.lua` holds it
// to that), so this is the full list of what is and is not priced.
TEST(SequenceToKeysTest, EveryNeovimKeyNameIsClassified) {
  const auto& tok = globalSequenceToKeys();
  ifstream fixture(filesystem::path(__FILE__).parent_path() / "fixtures" / "neovim_key_names.tsv");
  ASSERT_TRUE(fixture.is_open());
  string line;
  int names = 0;
  while (getline(fixture, line)) {
    const size_t tab = line.find('\t');
    const string name = line.substr(0, tab);
    EXPECT_EQ(tok.tokenize(name).empty(), line.substr(tab + 1) == "unpriced") << name;
    names++;
  }
  EXPECT_GT(names, 0);
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
