module vf.Journal.Windows;

import std.compat;
import vf.Journal.Replay;
import vf.Types.Lines;

using namespace std;

namespace Journal {

namespace {

WindowState stateAfter(const Event& event) {
  return WindowState{
      .buf = event.buf,
      .win = event.win,
      .lines = event.lines,
      .textVersion = event.textVersion,
      .row = event.row,
      .col = event.col,
      .windowHeight = event.windowHeight,
      .scrollAmount = event.scrollAmount,
      .ms = event.ms,
  };
}

}  // namespace

WindowCutter::WindowCutter(span<const int> sizes, int64_t idleGapMs) : idleGapMs(idleGapMs) {
  for (const int size : sizes) open.push_back(Window{.size = size});
}

void WindowCutter::push(const Event& event, vector<Window>& out) {
  if (!group.empty() && (event.kind == EventKind::Start || event.tick != group.back().tick)) {
    closeGroup(out);
  }
  if (event.kind == EventKind::Start) {
    wallEpoch = event.wallEpoch;
    anchored = false;
    for (Window& window : open) window = Window{.size = window.size};
    return;
  }
  group.push_back(event);
}

void WindowCutter::closeGroup(vector<Window>& out) {
  const Event& last = group.back();
  const bool clean = last.lines && last.normalMode;

  // Whether the anchor, this group's recorded atoms and its last state are the
  // whole story of what happened.
  bool vouched = anchored && clean;
  bool recorded = false;
  bool aborted = false;
  for (const Event& event : group) {
    if (event.kind == EventKind::Resync || event.discontinuous || event.buf != anchor.buf ||
        event.win != anchor.win) {
      vouched = false;
    }
    recorded |= event.kind == EventKind::Atom && event.decision == Decision::Record;
    aborted |= event.decision == Decision::Abort;
  }
  // Saving a modified buffer aborts, since it bumps changedtick, yet leaves the
  // text and cursor alone. Only an abort that shows in the state is unexplained.
  const bool traceless = !recorded && last.textVersion == anchor.textVersion &&
                         last.row == anchor.row && last.col == anchor.col;
  if (aborted && !traceless) vouched = false;

  const bool idle = anchored && group.front().ms - anchor.ms > idleGapMs;
  if (!vouched || idle) {
    for (Window& window : open) window = Window{.size = window.size};
  }

  if (vouched) {
    for (const Event& event : group) {
      if (event.decision != Decision::Record) continue;
      for (Window& window : open) {
        if (window.atomCount == 0) {
          window.start = anchor;
          window.firstAtom = event.atomIndex;
        }
        window.lastAtom = event.atomIndex;
        window.atomCount++;
        window.commands.push_back({event.keys, event.atomKind});
        window.keys += event.keys;
        window.typed += event.typed;
        window.lossy |= event.lossy;
      }
    }
    for (Window& window : open) {
      if (window.atomCount < window.size) continue;
      window.end = stateAfter(last);
      window.wallEpoch = wallEpoch + last.ms / 1000;
      out.push_back(std::move(window));
      window = Window{.size = out.back().size};
    }
  }

  anchored = clean;
  if (clean) anchor = stateAfter(last);
  group.clear();
}

}  // namespace Journal
