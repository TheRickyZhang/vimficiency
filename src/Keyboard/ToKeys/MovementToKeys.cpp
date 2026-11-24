module vf.Keyboard.ToKeys.MovementToKeys;

import std.compat;

using namespace std;

namespace {

// <...> notations whose keys are not the concatenation of their chars'
// CHAR_TO_KEYS entries. See :h key-notation, :h keytrans()
// The `<...>` names without modifiers that the keyboard model has a key for.
const CommandToKeys KEY_NAMES = {
  {"<lt>", {Key::Key_Shift, Key::Key_Comma}},
  {"<LT>", {Key::Key_Shift, Key::Key_Comma}},
  {"<Bslash>", {Key::Key_Backslash}},
  {"<Bar>", {Key::Key_Shift, Key::Key_Backslash}},

  {"<Space>", {Key::Key_Space}},
  {"<Tab>", {Key::Key_Tab}},
  {"<CR>", {Key::Key_Enter}},
  {"<Enter>", {Key::Key_Enter}},
  {"<Return>", {Key::Key_Enter}},
  {"<NL>", {Key::Key_Ctrl, Key::Key_J}},
  {"<Nul>", {Key::Key_Ctrl, Key::Key_Space}},
  {"<Esc>", {Key::Key_Esc}},
  {"<BS>", {Key::Key_Backspace}},
  {"<Del>", {Key::Key_Delete}},

  {"<Up>", {Key::Key_Up}},
  {"<Down>", {Key::Key_Down}},
  {"<Left>", {Key::Key_Left}},
  {"<Right>", {Key::Key_Right}},

  {"<Home>", {Key::Key_Home}},
  {"<End>", {Key::Key_End}},
};

}  // namespace

const MotionSet ALL_MOTIONS = {
  "h", "j", "k", "l",
  "0", "^", "$",
  "w", "b", "e", "ge", "W", "B", "E", "gE",
  "gg", "G",
  "{", "}", "(", ")",
  "<C-f>", "<C-b>", "<C-d>", "<C-u>",
  // Need a target char (f/F/t/T) or prior find context (;/,)
  "f", "F", "t", "T", ";", ",",
};

const SequenceToKeys& globalSequenceToKeys() {
  static SequenceToKeys tok(KEY_NAMES);
  return tok;
}
