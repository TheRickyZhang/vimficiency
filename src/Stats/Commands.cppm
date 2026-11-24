export module vf.Stats.Commands;

import std.compat;

export namespace Stats {

// One side's commands, by the class each counts under:
// - a token of the sequence parser, with an arbitrary target character
//   collapsed (`fa` → `f_`);
// - a command the parser does not know (`yiw`, `p`, `/foo<CR>`), as itself;
// - insert mode split into its entry command, the keys that edit typed text
//   (`<BS>`, `<C-w>`, …), and a count of the characters typed.
struct RunCommands;

struct CommandCounts {
  std::map<std::string, int> commands;
  int64_t typedChars = 0;

  void merge(const CommandCounts& other);
  void merge(const RunCommands& other);
};

// Counts one command, or a whole sequence of them. What they type in insert
// mode counts only with `insertText`.
void countKeys(CommandCounts& counts, std::string_view keys, bool insertText);

// Counts a gj/gk that moved the cursor along its buffer line, apart from the
// ones that share a row with j/k.
void countWithinLine(CommandCounts& counts, std::string_view keys);

// Counts keys pressed in insert mode. The user's insert text is counted from
// these, never from the keys that ran, which also hold what mappings typed.
void countInsertKeys(CommandCounts& counts, std::string_view pressed);

// `keys` as the run list shows them: each stretch of characters typed into a
// line as `⟨n⟩`, every other key as the sequence display renders it.
std::string displayKeys(std::string_view keys);

// CommandCounts with each command interned: what every cached run keeps.
struct RunCommands {
  // (command id, count), by id.
  std::vector<std::pair<int, int>> commands;
  int64_t typedChars = 0;

  bool uses(int command) const;
};

RunCommands intern(const CommandCounts& counts);

// The id `command` is interned under; -1 when no run has used it.
int internedId(std::string_view command);

struct CommandRow {
  std::string text;
  int user = 0;
  int optimizer = 0;
  // The fraction of each side's commands the row makes up.
  double userShare = 0;
  double optimizerShare = 0;
  // `userShare` over `optimizerShare`; infinity when the optimizer never uses it.
  double ratio = 0;
  // A merged row's user counts per variant (`ce 5 · cw 7`); empty otherwise.
  std::string split;
  // Every command either side counted under the row.
  std::vector<std::string> variants;
};

struct CommandComparison {
  // Commands the user leans on more than the optimizer, furthest first.
  std::vector<CommandRow> more;
  // And less, furthest first.
  std::vector<CommandRow> less;
  // Commands only the user uses, most used first; kept out of `more`, where
  // their infinite ratios would crowd out every other row.
  std::vector<CommandRow> never;
  // How many more of those were cut.
  int neverCut = 0;
};

CommandComparison compareCommands(const CommandCounts& user, const CommandCounts& optimizer);

}  // namespace Stats
