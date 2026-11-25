module vf.Keyboard.ToKeys.MovementToKeys;

import std.compat;

using namespace std;

namespace {

// <...> notations whose keys are not the concatenation of their chars'
// CHAR_TO_KEYS entries. See :h key-notation, :h keytrans()
// The `<...>` names without modifiers that have a price, including none.
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
  {"<PageUp>", {Key::Key_PageUp}},
  {"<PageDown>", {Key::Key_PageDown}},
  {"<Insert>", {Key::Key_Insert}},

  {"<F1>", {Key::Key_F1}},
  {"<F2>", {Key::Key_F2}},
  {"<F3>", {Key::Key_F3}},
  {"<F4>", {Key::Key_F4}},
  {"<F5>", {Key::Key_F5}},
  {"<F6>", {Key::Key_F6}},
  {"<F7>", {Key::Key_F7}},
  {"<F8>", {Key::Key_F8}},
  {"<F9>", {Key::Key_F9}},
  {"<F10>", {Key::Key_F10}},
  {"<F11>", {Key::Key_F11}},
  {"<F12>", {Key::Key_F12}},

  // Terminals' other codes for the same keys.
  {"<xHome>", {Key::Key_Home}},
  {"<zHome>", {Key::Key_Home}},
  {"<xEnd>", {Key::Key_End}},
  {"<zEnd>", {Key::Key_End}},
  {"<xUp>", {Key::Key_Up}},
  {"<xDown>", {Key::Key_Down}},
  {"<xLeft>", {Key::Key_Left}},
  {"<xRight>", {Key::Key_Right}},
  {"<xF1>", {Key::Key_F1}},
  {"<xF2>", {Key::Key_F2}},
  {"<xF3>", {Key::Key_F3}},
  {"<xF4>", {Key::Key_F4}},

  // The keypad, priced as the main keys with the same legends.
  {"<k0>", {Key::Key_0}},
  {"<k1>", {Key::Key_1}},
  {"<k2>", {Key::Key_2}},
  {"<k3>", {Key::Key_3}},
  {"<k4>", {Key::Key_4}},
  {"<k5>", {Key::Key_5}},
  {"<k6>", {Key::Key_6}},
  {"<k7>", {Key::Key_7}},
  {"<k8>", {Key::Key_8}},
  {"<k9>", {Key::Key_9}},
  {"<kPlus>", {Key::Key_Shift, Key::Key_Equal}},
  {"<kMinus>", {Key::Key_Minus}},
  {"<kMultiply>", {Key::Key_Shift, Key::Key_8}},
  {"<kDivide>", {Key::Key_Slash}},
  {"<kPoint>", {Key::Key_Period}},
  {"<kComma>", {Key::Key_Comma}},
  {"<kEqual>", {Key::Key_Equal}},
  {"<kEnter>", {Key::Key_Enter}},
  {"<kHome>", {Key::Key_Home}},
  {"<kEnd>", {Key::Key_End}},
  {"<kPageUp>", {Key::Key_PageUp}},
  {"<kPageDown>", {Key::Key_PageDown}},
  {"<kInsert>", {Key::Key_Insert}},
  {"<kDel>", {Key::Key_Delete}},
  {"<kUp>", {Key::Key_Up}},
  {"<kDown>", {Key::Key_Down}},
  {"<kLeft>", {Key::Key_Left}},
  {"<kRight>", {Key::Key_Right}},

  // Not key presses: bracketed-paste markers and names internal to mappings.
  {"<t_PS>", {}},
  {"<t_PE>", {}},
  {"<Ignore>", {}},
  {"<SNR>", {}},
  {"<Plug>", {}},
  {"<Cmd>", {}},
  {"<CSI>", {}},
  {"<Drop>", {}},
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
