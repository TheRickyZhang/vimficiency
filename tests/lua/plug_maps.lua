-- `<Plug>Vimfy*` registration. There is no `vimfy.keymap` API anymore —
-- CmdAtom makes a plain `<Cmd>Vimfy ...<CR>` map correct on its own, so
-- `setup()` registers the convenience `<Plug>` maps as ordinary silent
-- `<Cmd>` mappings.

local vimfy = require("vimficiency")

--- Look up a normal-mode keymap registration for a given LHS.
local function find_map(lhs)
  for _, m in ipairs(vim.api.nvim_get_keymap("n")) do
    if m.lhs == lhs then return m end
  end
  return nil
end

test("plug maps: setup registers <Plug>VimfyStartA as a silent <Cmd> map", function()
  vimfy.setup({})
  local found = assert(find_map("<Plug>VimfyStartA"), "<Plug>VimfyStartA registered")
  assert_eq(found.silent, 1, "silent defaults to true")
  assert_match(found.rhs or "", "Vimfy start a", "RHS runs the :Vimfy command")
  assert_eq(found.desc, "Vimfy start a")
end)

test("plug maps: argument-carrying actions are registered too", function()
  vimfy.setup({})
  assert(find_map("<Plug>VimfySuggestToggle"), "<Plug>VimfySuggestToggle registered")
  assert(find_map("<Plug>VimfyList"), "<Plug>VimfyList registered")
end)
