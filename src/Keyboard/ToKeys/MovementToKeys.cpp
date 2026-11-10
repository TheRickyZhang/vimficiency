module vf.Keyboard.ToKeys.MovementToKeys;

import std.compat;

using namespace std;

namespace {

// <...> notations whose keys are not the concatenation of their chars'
// CHAR_TO_KEYS entries. See :h key-notation, :h keytrans()
const CommandToKeys specialWithBracket = {
  {"<lt>", {Key::Key_Shift, Key::Key_Comma}},
  {"<LT>", {Key::Key_Shift, Key::Key_Comma}},

  {"<Space>", {Key::Key_Space}},
  {"<Tab>", {Key::Key_Tab}},
  {"<CR>", {Key::Key_Enter}},
  {"<Enter>", {Key::Key_Enter}},
  {"<Return>", {Key::Key_Enter}},
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

const CommandToKeys ctrlCombinations = {
  {"<C-a>", {Key::Key_Ctrl, Key::Key_A}},
  {"<C-b>", {Key::Key_Ctrl, Key::Key_B}},
  {"<C-c>", {Key::Key_Ctrl, Key::Key_C}},
  {"<C-d>", {Key::Key_Ctrl, Key::Key_D}},
  {"<C-e>", {Key::Key_Ctrl, Key::Key_E}},
  {"<C-f>", {Key::Key_Ctrl, Key::Key_F}},
  {"<C-g>", {Key::Key_Ctrl, Key::Key_G}},
  {"<C-h>", {Key::Key_Ctrl, Key::Key_H}},
  {"<C-i>", {Key::Key_Ctrl, Key::Key_I}},
  {"<C-j>", {Key::Key_Ctrl, Key::Key_J}},
  {"<C-k>", {Key::Key_Ctrl, Key::Key_K}},
  {"<C-l>", {Key::Key_Ctrl, Key::Key_L}},
  {"<C-m>", {Key::Key_Ctrl, Key::Key_M}},
  {"<C-n>", {Key::Key_Ctrl, Key::Key_N}},
  {"<C-o>", {Key::Key_Ctrl, Key::Key_O}},
  {"<C-p>", {Key::Key_Ctrl, Key::Key_P}},
  {"<C-q>", {Key::Key_Ctrl, Key::Key_Q}},
  {"<C-r>", {Key::Key_Ctrl, Key::Key_R}},
  {"<C-s>", {Key::Key_Ctrl, Key::Key_S}},
  {"<C-t>", {Key::Key_Ctrl, Key::Key_T}},
  {"<C-u>", {Key::Key_Ctrl, Key::Key_U}},
  {"<C-v>", {Key::Key_Ctrl, Key::Key_V}},
  {"<C-w>", {Key::Key_Ctrl, Key::Key_W}},
  {"<C-x>", {Key::Key_Ctrl, Key::Key_X}},
  {"<C-y>", {Key::Key_Ctrl, Key::Key_Y}},
  {"<C-z>", {Key::Key_Ctrl, Key::Key_Z}},

  {"<C-Space>", {Key::Key_Ctrl, Key::Key_Space}},
  {"<C-BS>", {Key::Key_Ctrl, Key::Key_Backspace}},
  {"<C-CR>", {Key::Key_Ctrl, Key::Key_Enter}},
  {"<C-Tab>", {Key::Key_Ctrl, Key::Key_Tab}},
};

const CommandToKeys KEY_NOTATIONS = combineAll({
  cref(specialWithBracket),
  cref(ctrlCombinations),
});

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
  static SequenceToKeys tok(KEY_NOTATIONS);
  return tok;
}
