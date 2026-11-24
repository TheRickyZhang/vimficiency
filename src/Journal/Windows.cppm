export module vf.Journal.Windows;

import std.compat;
import vf.Journal.Replay;
import vf.Types.Lines;

export namespace Journal {

// Where a window stood between two commands.
struct WindowState {
  int buf = 0;
  int win = 0;
  SharedLines lines;
  int textVersion = 0;
  std::string filetype;
  int row = 0;
  int col = 0;
  int windowHeight = 0;
  int scrollAmount = 0;
  int64_t ms = 0;
};

struct Command {
  std::string keys;
  std::string kind;
  // A gj/gk that moved the cursor along its buffer line. Every other one is
  // taken for the line motion it equals on unwrapped text.
  bool withinLine = false;
};

// Consecutive recorded atoms of one window and the states on either side.
struct Window {
  // The atom count it was cut for. `atomCount` exceeds it when the atoms that
  // completed it were delivered together and cannot be told apart.
  int size = 0;
  int firstAtom = 0;
  int lastAtom = 0;
  int atomCount = 0;
  int64_t wallEpoch = 0;
  WindowState start;
  WindowState end;
  // `keys` split back into its atoms.
  std::vector<Command> commands;
  std::string keys;
  std::string typed;
  // Of `typed`, the keys read in Insert mode; unknown once an atom has none.
  std::string typedInsert;
  bool typedInsertKnown = true;
  bool lossy = false;
};

// Cuts a journal into back-to-back windows, one series per size.
//
// Events sharing a tick form a group, and only a group's last state is real,
// so a group is never split. A window grows only across groups the journal can
// vouch for from end to end: any other group ends every open window, and the
// next ones start from the state after it.
//
// A group is complete at its `TickEnd`, or once an event of a later tick
// arrives; a last group with neither is never cut. That keeps the windows of a
// growing journal a prefix of the windows it yields later.
class WindowCutter {
public:
  WindowCutter(std::span<const int> sizes, int64_t idleGapMs);

  // Appends the windows that `event` completes.
  void push(const Event& event, std::vector<Window>& out);

private:
  void closeGroup(std::vector<Window>& out);

  int64_t idleGapMs;
  int64_t wallEpoch = 0;
  std::vector<Event> group;
  // One per size; `atomCount == 0` while unopened.
  std::vector<Window> open;
  bool anchored = false;
  WindowState anchor;
};

}  // namespace Journal
