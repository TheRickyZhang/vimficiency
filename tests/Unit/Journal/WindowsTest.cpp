#include <gtest/gtest.h>

#include "Unit/Journal/JournalBuilder.h"

import std.compat;
import vf.Journal.Replay;
import vf.Journal.Windows;
import vf.Types.Lines;

using namespace std;
using Journal::Window;

namespace {

constexpr int64_t IDLE_GAP_MS = 30000;

vector<Window> cut(const string& bytes, vector<int> sizes, int64_t idleGapMs = IDLE_GAP_MS) {
  Journal::Replay replay(bytes);
  Journal::WindowCutter cutter(sizes, idleGapMs);
  vector<Window> windows;
  Journal::Event event;
  while (replay.next(event)) cutter.push(event, windows);
  return windows;
}

// A header, one line of text and the state before the first command.
JournalBuilder opened() {
  JournalBuilder j;
  j.header(5000).snapshot({"abcdefghijklmnopqrstuvwxyz"}).resync(0, 0);
  return j;
}

// `count` single `l` atoms, one tick each, starting from column `col`.
void moveRight(JournalBuilder& j, int count, int col = 0) {
  for (int i = 1; i <= count; i++) j.next().atom("l", 0, col + i);
}

}  // namespace

TEST(JournalWindowsTest, CutsBackToBackWindows) {
  JournalBuilder j = opened();
  moveRight(j, 5);

  const vector<Window> windows = cut(j.bytes(), {2});
  ASSERT_EQ(windows.size(), 2u);

  EXPECT_EQ(windows[0].size, 2);
  EXPECT_EQ(windows[0].atomCount, 2);
  EXPECT_EQ(windows[0].firstAtom, 0);
  EXPECT_EQ(windows[0].lastAtom, 1);
  EXPECT_EQ(windows[0].keys, "ll");
  EXPECT_EQ(windows[0].start.col, 0);
  EXPECT_EQ(windows[0].end.col, 2);
  EXPECT_EQ(windows[0].wallEpoch, 5000);
  EXPECT_EQ(windows[0].start.windowHeight, 40);
  EXPECT_EQ(windows[0].start.scrollAmount, 20);

  EXPECT_EQ(windows[1].firstAtom, 2);
  EXPECT_EQ(windows[1].lastAtom, 3);
  EXPECT_EQ(windows[1].start.col, 2);
  EXPECT_EQ(windows[1].end.col, 4);
}

TEST(JournalWindowsTest, CutsOneSeriesPerSize) {
  JournalBuilder j = opened();
  moveRight(j, 9);

  const vector<Window> windows = cut(j.bytes(), {2, 4});
  int twos = 0;
  int fours = 0;
  for (const Window& window : windows) (window.size == 2 ? twos : fours)++;
  EXPECT_EQ(twos, 4);
  EXPECT_EQ(fours, 2);
}

TEST(JournalWindowsTest, WindowsCarryTheTextAtBothEnds) {
  JournalBuilder j = opened();
  j.next().delta(0, 1, {"bcdefghijklmnopqrstuvwxyz"}).atom("x", 0, 0);
  j.next().atom("l", 0, 1);
  j.next().atom("l", 0, 2);

  const vector<Window> windows = cut(j.bytes(), {2});
  ASSERT_EQ(windows.size(), 1u);
  EXPECT_EQ(*windows[0].start.lines, (Lines{"abcdefghijklmnopqrstuvwxyz"}));
  EXPECT_EQ(*windows[0].end.lines, (Lines{"bcdefghijklmnopqrstuvwxyz"}));
  EXPECT_NE(windows[0].start.textVersion, windows[0].end.textVersion);
}

TEST(JournalWindowsTest, TheLastGroupIsNotCutUntilALaterTickArrives) {
  JournalBuilder j = opened();
  moveRight(j, 2);
  EXPECT_TRUE(cut(j.bytes(), {2}).empty());

  j.next().atom("l", 0, 3);
  EXPECT_EQ(cut(j.bytes(), {2}).size(), 1u);
}

TEST(JournalWindowsTest, AGrowingJournalOnlyAddsWindows) {
  JournalBuilder j = opened();
  moveRight(j, 3);
  j.next().atom("l", 0, 4).atom("l", 0, 5);
  j.next().resync(0, 9);
  moveRight(j, 4, 9);
  const vector<Window> whole = cut(j.bytes(), {2, 3});

  for (size_t size = 0; size <= j.bytes().size(); size++) {
    const vector<Window> part = cut(j.bytes().substr(0, size), {2, 3});
    ASSERT_LE(part.size(), whole.size()) << "cut at " << size;
    for (size_t i = 0; i < part.size(); i++) {
      EXPECT_EQ(part[i].size, whole[i].size) << "cut at " << size;
      EXPECT_EQ(part[i].firstAtom, whole[i].firstAtom) << "cut at " << size;
      EXPECT_EQ(part[i].lastAtom, whole[i].lastAtom) << "cut at " << size;
    }
  }
}

TEST(JournalWindowsTest, AtomsDeliveredTogetherAreNeverSplit) {
  JournalBuilder j = opened();
  j.next().atom("l", 0, 1);
  j.next().atom("l", 0, 4).atom("l", 0, 4).atom("l", 0, 4);
  j.next().atom("l", 0, 5);

  const vector<Window> windows = cut(j.bytes(), {2});
  ASSERT_EQ(windows.size(), 1u);
  EXPECT_EQ(windows[0].atomCount, 4);
  EXPECT_EQ(windows[0].keys, "llll");
  EXPECT_EQ(windows[0].end.col, 4);
}

TEST(JournalWindowsTest, NothingIsCutBeforeAStateIsKnown) {
  JournalBuilder j;
  j.header().snapshot({"abcdef"});
  moveRight(j, 4);

  const vector<Window> windows = cut(j.bytes(), {2});
  ASSERT_EQ(windows.size(), 1u);
  EXPECT_EQ(windows[0].firstAtom, 1);
  EXPECT_EQ(windows[0].start.col, 1);
}

TEST(JournalWindowsTest, AResyncEndsOpenWindowsAndAnchorsTheNext) {
  JournalBuilder j = opened();
  moveRight(j, 1);
  j.next().resync(0, 10);
  moveRight(j, 3, 10);

  const vector<Window> windows = cut(j.bytes(), {2});
  ASSERT_EQ(windows.size(), 1u);
  EXPECT_EQ(windows[0].firstAtom, 1);
  EXPECT_EQ(windows[0].start.col, 10);
  EXPECT_EQ(windows[0].end.col, 12);
}

TEST(JournalWindowsTest, AtomsSharingATickWithAResyncOnlyAnchor) {
  JournalBuilder j = opened();
  j.next().resync(0, 10).atom("l", 0, 10).atom("l", 0, 10);
  moveRight(j, 3, 10);

  const vector<Window> windows = cut(j.bytes(), {2});
  ASSERT_EQ(windows.size(), 1u);
  EXPECT_EQ(windows[0].firstAtom, 2);
  EXPECT_EQ(windows[0].start.col, 10);
}

TEST(JournalWindowsTest, AnAbortedAtomEndsOpenWindows) {
  JournalBuilder j = opened();
  moveRight(j, 1);
  j.next().atom("@q", 0, 7, {.decision = vf::journal::Atom::abort});
  moveRight(j, 3, 7);

  const vector<Window> windows = cut(j.bytes(), {2});
  ASSERT_EQ(windows.size(), 1u);
  EXPECT_EQ(windows[0].firstAtom, 2);
  EXPECT_EQ(windows[0].start.col, 7);
  EXPECT_EQ(windows[0].keys, "ll");
}

TEST(JournalWindowsTest, AnAbortThatLeftNoTraceIsLeftOut) {
  JournalBuilder j = opened();
  moveRight(j, 1);
  j.next().atom(":w<CR>", 0, 1, {.decision = vf::journal::Atom::abort});
  moveRight(j, 2, 1);

  const vector<Window> windows = cut(j.bytes(), {2});
  ASSERT_EQ(windows.size(), 1u);
  EXPECT_EQ(windows[0].firstAtom, 0);
  EXPECT_EQ(windows[0].lastAtom, 2);
  EXPECT_EQ(windows[0].keys, "ll");
}

TEST(JournalWindowsTest, AnAbortThatOnlyChangedTextEndsOpenWindows) {
  JournalBuilder j = opened();
  moveRight(j, 1);
  j.next().delta(0, 1, {"ABCDEF"}).atom(":s/.*/ABCDEF/<CR>", 0, 1, {.decision = vf::journal::Atom::abort});
  moveRight(j, 3, 1);

  const vector<Window> windows = cut(j.bytes(), {2});
  ASSERT_EQ(windows.size(), 1u);
  EXPECT_EQ(windows[0].firstAtom, 2);
  EXPECT_EQ(*windows[0].start.lines, (Lines{"ABCDEF"}));
}

TEST(JournalWindowsTest, AnAbortSharingATickWithARecordedAtomEndsOpenWindows) {
  JournalBuilder j = opened();
  moveRight(j, 1);
  j.next().atom("l", 0, 2).atom(":w<CR>", 0, 2, {.decision = vf::journal::Atom::abort});
  moveRight(j, 3, 2);

  const vector<Window> windows = cut(j.bytes(), {2});
  ASSERT_EQ(windows.size(), 1u);
  EXPECT_EQ(windows[0].firstAtom, 3);
  EXPECT_EQ(windows[0].start.col, 2);
}

TEST(JournalWindowsTest, SkippedAtomsAreLeftOut) {
  JournalBuilder j = opened();
  moveRight(j, 1);
  j.next().atom(":w<CR>", 0, 1, {.decision = vf::journal::Atom::skip});
  moveRight(j, 2, 1);

  const vector<Window> windows = cut(j.bytes(), {2});
  ASSERT_EQ(windows.size(), 1u);
  EXPECT_EQ(windows[0].atomCount, 2);
  EXPECT_EQ(windows[0].firstAtom, 0);
  EXPECT_EQ(windows[0].lastAtom, 2);
  EXPECT_EQ(windows[0].keys, "ll");
}

TEST(JournalWindowsTest, AStateOutsideNormalModeIsNoAnchor) {
  JournalBuilder j = opened();
  moveRight(j, 1);
  j.next().atom("l", 0, 2, {.mode = "i"});
  j.next().atom("ix<Esc>", 0, 2);
  moveRight(j, 3, 2);

  const vector<Window> windows = cut(j.bytes(), {2});
  ASSERT_EQ(windows.size(), 1u);
  EXPECT_EQ(windows[0].firstAtom, 3);
  EXPECT_EQ(windows[0].start.col, 2);
}

TEST(JournalWindowsTest, AnAtomOfAnotherWindowEndsOpenWindows) {
  JournalBuilder j = opened();
  moveRight(j, 1);
  j.next().atom("l", 0, 1, {.win = 1001});
  j.next().atom("l", 0, 2, {.win = 1001});
  j.next().atom("l", 0, 3, {.win = 1001});
  j.next().atom("l", 0, 4, {.win = 1001});

  const vector<Window> windows = cut(j.bytes(), {2});
  ASSERT_EQ(windows.size(), 1u);
  EXPECT_EQ(windows[0].firstAtom, 2);
  EXPECT_EQ(windows[0].start.win, 1001);
}

TEST(JournalWindowsTest, ADiscontinuityEndsOpenWindows) {
  JournalBuilder j = opened();
  moveRight(j, 1);
  j.next().snapshot({"reloaded text"}).atom("l", 0, 2);
  moveRight(j, 3, 2);

  const vector<Window> windows = cut(j.bytes(), {2});
  ASSERT_EQ(windows.size(), 1u);
  EXPECT_EQ(windows[0].firstAtom, 2);
  EXPECT_EQ(*windows[0].start.lines, (Lines{"reloaded text"}));
}

TEST(JournalWindowsTest, ACursorPastTheTextIsNoAnchor) {
  JournalBuilder j = opened();
  moveRight(j, 1);
  j.next().snapshot({}).atom("l", 0, 2);
  j.next().atom("j", 5, 2);
  j.next().atom("j", 6, 2);
  j.next().atom("j", 7, 2);

  EXPECT_TRUE(cut(j.bytes(), {2}).empty());
}

TEST(JournalWindowsTest, AnIdleGapRestartsWindowsFromTheSameState) {
  JournalBuilder j = opened();
  moveRight(j, 1);
  j.next(IDLE_GAP_MS + 1).atom("l", 0, 2);
  moveRight(j, 2, 2);

  const vector<Window> windows = cut(j.bytes(), {2});
  ASSERT_EQ(windows.size(), 1u);
  EXPECT_EQ(windows[0].firstAtom, 1);
  EXPECT_EQ(windows[0].start.col, 1);
  EXPECT_EQ(windows[0].end.col, 3);
}

TEST(JournalWindowsTest, AHeaderEndsOpenWindows) {
  JournalBuilder j = opened();
  moveRight(j, 1);
  j.next().header(9000).snapshot({"abcdef"}).resync(0, 0);
  moveRight(j, 3);

  const vector<Window> windows = cut(j.bytes(), {2});
  ASSERT_EQ(windows.size(), 1u);
  EXPECT_EQ(windows[0].firstAtom, 1);
  EXPECT_EQ(windows[0].wallEpoch, 9000);
}

TEST(JournalWindowsTest, ALossyAtomMarksItsWindow) {
  JournalBuilder j = opened();
  j.next().atom("<Space>f", 0, 1, {.lossy = true});
  moveRight(j, 4, 1);

  const vector<Window> windows = cut(j.bytes(), {2});
  ASSERT_EQ(windows.size(), 2u);
  EXPECT_TRUE(windows[0].lossy);
  EXPECT_FALSE(windows[1].lossy);
}

TEST(JournalWindowsTest, CarriesTheKeysPressedBesideTheKeysThatRan) {
  JournalBuilder j = opened();
  j.next().atom("3l", 0, 3, {.typed = "Q"});
  moveRight(j, 2, 3);

  const vector<Window> windows = cut(j.bytes(), {2});
  ASSERT_EQ(windows.size(), 1u);
  EXPECT_EQ(windows[0].keys, "3ll");
  EXPECT_EQ(windows[0].typed, "Ql");
}
