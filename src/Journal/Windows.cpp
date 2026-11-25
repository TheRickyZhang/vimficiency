module vf.Journal.Windows;

import std.compat;
import vf.Journal.Replay;
import vf.Optimizer.Analyze;
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
      .filetype = event.filetype,
      .row = event.row,
      .col = event.col,
      .windowHeight = event.windowHeight,
      .scrollAmount = event.scrollAmount,
      .indent = event.indent,
      .ms = event.ms,
  };
}

}  // namespace

WindowCutter::WindowCutter(span<const int> sizes, int64_t idleGapMs, bool keepSteps)
    : idleGapMs(idleGapMs), keepSteps(keepSteps) {
  for (const int size : sizes) open.push_back(Window{.size = size});
}

void WindowCutter::push(const Event& event, vector<Window>& out) {
  const bool boundary = event.kind == EventKind::Start || event.kind == EventKind::TickEnd;
  if (!group.empty() && (boundary || event.tick != group.back().tick)) closeGroup(out);
  if (event.kind == EventKind::TickEnd) return;
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
  // A cursor past the end of the text means the journal lost track of it.
  const bool clean = last.lines && last.normalMode && last.row >= 0 && last.row < ssize(*last.lines);

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
    UntouchedRows untouched;
    int minHeight = ssize(*anchor.lines);
    for (const Event& event : group) {
      untouched.include(event.untouched.above, event.untouched.below);
      minHeight = min(minHeight, event.minHeight);
    }
    for (const Event& event : group) {
      for (const int row : event.visitedRows) {
        if (row >= 0) untouched.include(row, std::max(0, minHeight - row - 1));
      }
    }
    untouched.include(last.row, last.lines->size() - last.row - 1);
    // Only an atom alone in its tick has a state of its own to judge by.
    const bool withinLine = group.size() == 1 && last.row == anchor.row &&
                            last.col != anchor.col && isScreenLineMotion(last.keys);
    for (const Event& event : group) {
      if (event.decision != Decision::Record) continue;
      for (Window& window : open) {
        if (window.atomCount == 0) {
          window.start = anchor;
          window.firstAtom = event.atomIndex;
        }
        window.lastAtom = event.atomIndex;
        window.atomCount++;
        window.commands.push_back({event.keys, event.atomKind, withinLine});
        window.keys += event.keys;
        window.typed += event.typed;
        window.typedInsert += event.typedInsert;
        window.typedInsertKnown &= event.typedInsertKnown;
        window.lossy |= event.lossy;
      }
    }
    for (Window& window : open) {
      if (window.atomCount > 0) window.untouched.include(untouched.above, untouched.below);
      if (keepSteps && recorded) window.steps.push_back({window.commands.size(), stateAfter(last)});
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
