#include <gtest/gtest.h>

#include "Unit/Journal/JournalBuilder.h"

import std.compat;
import vf.Journal.Replay;
import vf.Types.Lines;

using namespace std;
using Journal::Event;
using Journal::EventKind;

namespace {

vector<Event> replay(const string& bytes) {
  Journal::Replay replay(bytes);
  vector<Event> events;
  Event event;
  while (replay.next(event)) events.push_back(event);
  return events;
}

}  // namespace

TEST(JournalReplayTest, EventsCarryTheTextBuiltSoFar) {
  JournalBuilder j;
  j.header(1234).snapshot({"one", "two", "three"}).resync(0, 0);
  j.next().atom("j", 1, 0);
  j.next().delta(1, 1, {"TWO", "extra"}).atom("cc", 1, 2);
  j.next().delta(0, 2, {}).atom("dk", 0, 0);

  const vector<Event> events = replay(j.bytes());
  ASSERT_EQ(events.size(), 5u);

  EXPECT_EQ(events[0].kind, EventKind::Start);
  EXPECT_EQ(events[0].wallEpoch, 1234);

  EXPECT_EQ(events[1].kind, EventKind::Resync);
  EXPECT_TRUE(events[1].normalMode);
  EXPECT_EQ(*events[1].lines, (Lines{"one", "two", "three"}));

  EXPECT_EQ(events[2].kind, EventKind::Atom);
  EXPECT_EQ(events[2].keys, "j");
  EXPECT_EQ(events[2].atomIndex, 0);
  EXPECT_EQ(events[2].row, 1);
  EXPECT_EQ(events[2].textVersion, events[1].textVersion);

  EXPECT_EQ(*events[3].lines, (Lines{"one", "TWO", "extra", "three"}));
  EXPECT_NE(events[3].textVersion, events[2].textVersion);
  EXPECT_EQ(events[3].atomIndex, 1);
  EXPECT_EQ(events[3].col, 2);

  EXPECT_EQ(*events[4].lines, (Lines{"extra", "three"}));
}

TEST(JournalReplayTest, AnEventKeepsItsTextWhenLaterDeltasArrive) {
  JournalBuilder j;
  j.header().snapshot({"a", "b"}).resync(0, 0);
  j.next().delta(0, 1, {"changed"}).atom("cc", 0, 6);

  const vector<Event> events = replay(j.bytes());
  ASSERT_EQ(events.size(), 3u);
  EXPECT_EQ(*events[1].lines, (Lines{"a", "b"}));
  EXPECT_EQ(*events[2].lines, (Lines{"changed", "b"}));
}

TEST(JournalReplayTest, RepeatedSnapshotOfTheSameTextChangesNothing) {
  JournalBuilder j;
  j.header().snapshot({"a", "b"}).resync(0, 0);
  j.next().snapshot({"a", "b"}).atom("j", 1, 0);

  const vector<Event> events = replay(j.bytes());
  ASSERT_EQ(events.size(), 3u);
  EXPECT_FALSE(events[2].discontinuous);
  EXPECT_EQ(events[2].textVersion, events[1].textVersion);
}

TEST(JournalReplayTest, SnapshotOfOtherTextIsADiscontinuityForOneEvent) {
  JournalBuilder j;
  j.header().snapshot({"a", "b"}).resync(0, 0);
  j.next().snapshot({"reloaded"}).atom("j", 0, 0);
  j.next().atom("l", 0, 1);

  const vector<Event> events = replay(j.bytes());
  ASSERT_EQ(events.size(), 4u);
  EXPECT_TRUE(events[2].discontinuous);
  EXPECT_EQ(*events[2].lines, (Lines{"reloaded"}));
  EXPECT_FALSE(events[3].discontinuous);
}

TEST(JournalReplayTest, DeltaOutsideTheTextDropsItUntilTheNextSnapshot) {
  JournalBuilder j;
  j.header().snapshot({"a"}).resync(0, 0);
  j.next().delta(3, 1, {"x"}).atom("x", 0, 0);
  j.next().snapshot({"b"}).atom("x", 0, 0);

  const vector<Event> events = replay(j.bytes());
  ASSERT_EQ(events.size(), 4u);
  EXPECT_EQ(events[2].lines, nullptr);
  EXPECT_TRUE(events[2].discontinuous);
  EXPECT_EQ(*events[3].lines, (Lines{"b"}));
}

TEST(JournalReplayTest, DeletingEveryLineLeavesOneEmptyLine) {
  JournalBuilder j;
  j.header().snapshot({"a", "b"}).resync(0, 0);
  j.next().delta(0, 2, {}).atom("dG", 0, 0);
  j.next().delta(0, 1, {"typed"}).atom("ityped<Esc>", 0, 4);

  const vector<Event> events = replay(j.bytes());
  ASSERT_EQ(events.size(), 4u);
  EXPECT_EQ(*events[2].lines, (Lines{""}));
  EXPECT_EQ(*events[3].lines, (Lines{"typed"}));
}

TEST(JournalReplayTest, AHeaderForgetsEveryBuffer) {
  JournalBuilder j;
  j.header().snapshot({"old"}).resync(0, 0);
  j.header(2000);
  j.next().atom("j", 0, 0);

  const vector<Event> events = replay(j.bytes());
  ASSERT_EQ(events.size(), 4u);
  EXPECT_EQ(events[2].kind, EventKind::Start);
  EXPECT_EQ(events[2].wallEpoch, 2000);
  EXPECT_EQ(events[3].lines, nullptr);
}

TEST(JournalReplayTest, ATruncatedRecordEndsTheJournal) {
  JournalBuilder j;
  j.header().snapshot({"a"}).resync(0, 0);
  const size_t whole = j.bytes().size();
  j.next().atom("j", 0, 0);

  for (size_t size = whole; size < j.bytes().size(); size++) {
    EXPECT_EQ(replay(j.bytes().substr(0, size)).size(), 2u) << "cut at " << size;
  }
  EXPECT_EQ(replay(j.bytes()).size(), 3u);
}

TEST(JournalReplayTest, AnotherFormatVersionYieldsNothing) {
  JournalBuilder j;
  j.header(1000, 1).snapshot({"a"}).resync(0, 0);

  EXPECT_TRUE(replay(j.bytes()).empty());
}
