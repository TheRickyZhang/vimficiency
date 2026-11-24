#include <gtest/gtest.h>

import std.compat;
import vf.Stats.Commands;

using namespace std;

namespace {

Stats::CommandCounts countsOf(initializer_list<string_view> commands) {
  Stats::CommandCounts counts;
  for (const string_view keys : commands) Stats::countKeys(counts, keys, true);
  return counts;
}

Stats::CommandCounts repeated(string_view keys, int times) {
  Stats::CommandCounts counts;
  for (int i = 0; i < times; i++) Stats::countKeys(counts, keys, true);
  return counts;
}

}  // namespace

TEST(StatsCommands, TargetCharactersCollapse) {
  const auto counts = countsOf({"fa", "Fb", "2tc", "re", "f<Space>", "dw", "3w"});
  EXPECT_EQ(counts.commands, (map<string, int>{
                                 {"f_", 2}, {"F_", 1}, {"2t_", 1}, {"r_", 1}, {"dw", 1}, {"3w", 1}}));
}

TEST(StatsCommands, ATargetMayBeAnyNamedCharacter) {
  const auto counts = countsOf({"f<Bar>", "t<Bslash>", "r<lt>"});
  EXPECT_EQ(counts.commands, (map<string, int>{{"f_", 1}, {"t_", 1}, {"r_", 1}}));
}

TEST(StatsCommands, CommandsTheParserDoesNotKnowCountAsThemselves) {
  const auto counts = countsOf({"yiw", "p", "/foo<CR>"});
  EXPECT_EQ(counts.commands, (map<string, int>{{"yiw", 1}, {"p", 1}, {"/foo<CR>", 1}}));
  EXPECT_EQ(counts.typedChars, 0);
}

TEST(StatsCommands, InsertSplitsIntoEntryKeysAndTypedCharacters) {
  const auto counts = countsOf({"ciwab<BS>c<C-w>é<Space>d<Esc>"});
  EXPECT_EQ(counts.commands, (map<string, int>{{"ciw", 1}, {"<BS>", 1}, {"<C-w>", 1}}));
  // a, b, c, é, <Space>, d.
  EXPECT_EQ(counts.typedChars, 6);
}

TEST(StatsCommands, InsertTextCountsOnlyWhenAsked) {
  Stats::CommandCounts counts;
  Stats::countKeys(counts, "ciwab<BS>c<Esc>w", false);
  EXPECT_EQ(counts.commands, (map<string, int>{{"ciw", 1}, {"w", 1}}));
  EXPECT_EQ(counts.typedChars, 0);
}

TEST(StatsCommands, PressedInsertKeysCountLikeInsertText) {
  Stats::CommandCounts counts;
  Stats::countInsertKeys(counts, "ab<BS>c<C-w>é<Space>d");
  EXPECT_EQ(counts.commands, (map<string, int>{{"<BS>", 1}, {"<C-w>", 1}}));
  EXPECT_EQ(counts.typedChars, 6);
}

TEST(StatsCommands, DisplayCollapsesTypedTextAndKeepsEveryKey) {
  // é, <Space> and d are one stretch.
  EXPECT_EQ(Stats::displayKeys("ciwab<BS>c<C-w>é<Space>d<Esc>"), "ciw⟨2⟩<BS>⟨1⟩<C-w>⟨3⟩<Esc>");
  EXPECT_EQ(Stats::displayKeys("o<CR><BS>So_we<CR>x<Esc>2j"), "o↵<BS>⟨5⟩↵⟨1⟩<Esc>2j");
  EXPECT_EQ(Stats::displayKeys("f<Space>dw"), "f␣dw");
  EXPECT_EQ(Stats::displayKeys("/foo<CR>"), "/foo↵");
  EXPECT_EQ(Stats::displayKeys(""), "");
}

TEST(StatsCommands, ASequenceCountsEachCommand) {
  const auto counts = countsOf({"2jciwfoo<Esc>w"});
  EXPECT_EQ(counts.commands, (map<string, int>{{"2j", 1}, {"ciw", 1}, {"w", 1}}));
  EXPECT_EQ(counts.typedChars, 3);
}

TEST(StatsCommands, RarelyUsedCommandsAreLeftOut) {
  auto user = repeated("w", 5);
  Stats::countKeys(user, "b", true);
  const auto optimizer = repeated("b", 4);
  const auto compared = Stats::compareCommands(user, optimizer);
  // `w` 5 + 0 and `b` 1 + 4 both fall short of ten uses.
  EXPECT_TRUE(compared.more.empty());
  EXPECT_TRUE(compared.less.empty());
}

TEST(StatsCommands, RatiosCompareSharesAndOrderByDistance) {
  Stats::CommandCounts user = repeated("w", 10);
  for (int i = 0; i < 10; i++) Stats::countKeys(user, "e", true);
  Stats::CommandCounts optimizer = repeated("e", 20);
  for (int i = 0; i < 10; i++) Stats::countKeys(optimizer, "b", true);
  const auto compared = Stats::compareCommands(user, optimizer);

  EXPECT_TRUE(compared.more.empty());
  ASSERT_EQ(compared.never.size(), 1u);
  EXPECT_EQ(compared.never[0].text, "w");
  EXPECT_EQ(compared.never[0].ratio, numeric_limits<double>::infinity());

  // `e`: half of the user's commands, two thirds of the optimizer's.
  ASSERT_EQ(compared.less.size(), 2u);
  EXPECT_EQ(compared.less[0].text, "b");
  EXPECT_EQ(compared.less[0].ratio, 0);
  EXPECT_EQ(compared.less[1].text, "e");
  EXPECT_DOUBLE_EQ(compared.less[1].userShare, 0.5);
  EXPECT_DOUBLE_EQ(compared.less[1].optimizerShare, 2.0 / 3);
  EXPECT_DOUBLE_EQ(compared.less[1].ratio, 0.75);
}

TEST(StatsCommands, SharesNearEqualAreNeitherMoreNorLess) {
  const auto compared = Stats::compareCommands(repeated("w", 10), repeated("w", 30));
  EXPECT_TRUE(compared.more.empty());
  EXPECT_TRUE(compared.less.empty());
}

TEST(StatsCommands, ChangeWordAndChangeToWordEndShareARow) {
  Stats::CommandCounts user = repeated("cefoo<Esc>", 5);
  for (int i = 0; i < 7; i++) Stats::countKeys(user, "cwfoo<Esc>", true);
  for (int i = 0; i < 12; i++) Stats::countKeys(user, "j", true);
  Stats::CommandCounts optimizer = repeated("j", 12);
  const auto compared = Stats::compareCommands(user, optimizer);

  ASSERT_EQ(compared.never.size(), 1u);
  EXPECT_EQ(compared.never[0].text, "ce|cw");
  EXPECT_EQ(compared.never[0].user, 12);
  EXPECT_EQ(compared.never[0].split, "ce 5 · cw 7");
  ASSERT_EQ(compared.less.size(), 1u);
  EXPECT_EQ(compared.less[0].text, "j");
  EXPECT_EQ(compared.less[0].split, "");
}

TEST(StatsCommands, AScreenLineMotionSharesARowWithItsLineMotionUnlessItStayedOnItsLine) {
  Stats::CommandCounts user = repeated("gj", 9);
  for (int i = 0; i < 3; i++) Stats::countKeys(user, "j", true);
  for (int i = 0; i < 10; i++) Stats::countWithinLine(user, "gj");
  for (int i = 0; i < 2; i++) Stats::countKeys(user, "w", true);
  Stats::CommandCounts optimizer = repeated("j", 8);
  for (int i = 0; i < 8; i++) Stats::countKeys(optimizer, "w", true);
  const auto compared = Stats::compareCommands(user, optimizer);

  // 12 of the user's 24 commands against 8 of the optimizer's 16.
  EXPECT_TRUE(compared.more.empty());
  ASSERT_EQ(compared.never.size(), 1u);
  EXPECT_EQ(compared.never[0].text, "gj (same line)");
  EXPECT_EQ(compared.never[0].user, 10);
  ASSERT_EQ(compared.less.size(), 1u);
  EXPECT_EQ(compared.less[0].text, "w");
}

TEST(StatsCommands, CommandsOnlyTheUserUsesOrderByUseAndAreCut) {
  Stats::CommandCounts user;
  for (int n = 10; n < 17; n++) {
    for (int i = 0; i < n; i++) Stats::countKeys(user, format("{}w", n), true);
  }
  const auto compared = Stats::compareCommands(user, repeated("b", 10));
  ASSERT_EQ(compared.never.size(), 5u);
  EXPECT_EQ(compared.never[0].text, "16w");
  EXPECT_EQ(compared.never[4].text, "12w");
  EXPECT_EQ(compared.neverCut, 2);
}
