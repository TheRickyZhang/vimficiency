-- keynorm.is_mouse classifies mouse/scroll event tokens so the capture layer
-- can strip them (they would otherwise be costed character-by-character).

local keynorm = require("vimficiency.capture.keynorm")

test("is_mouse: true for clicks, drags, releases, wheel, move", function()
  for _, tok in ipairs({
    "<LeftMouse>", "<LeftRelease>", "<LeftDrag>",
    "<RightMouse>", "<MiddleMouse>",
    "<ScrollWheelUp>", "<ScrollWheelDown>", "<ScrollWheelLeft>",
    "<2-LeftMouse>", "<C-ScrollWheelDown>", "<MouseMove>",
  }) do
    assert_true(keynorm.is_mouse(tok), "expected mouse: " .. tok)
  end
end)

test("has_callback: true for a Lua function ref or a <Cmd> inside the keys", function()
  assert_true(keynorm.has_callback("d\128\253g538\ne"), "Lua callback motion")
  assert_true(keynorm.has_callback("c" .. vim.keycode("<Cmd>") .. "lua f()\nzz\27"), "<Cmd> text object")
end)

test("has_callback: false for plain and special keys", function()
  for _, raw in ipairs({ "dw", "ciwfoo\27", vim.keycode("i<Right><BS><Esc>"), vim.keycode("<C-d>"), "" }) do
    assert_true(not keynorm.has_callback(raw), "expected plain keys: " .. vim.fn.keytrans(raw))
  end
end)

test("is_mouse: false for keyboard tokens", function()
  for _, tok in ipairs({ "a", "M", "<BS>", "<Esc>", "<C-d>", "<C-u>", "w", "" }) do
    assert_true(not keynorm.is_mouse(tok), "expected non-mouse: " .. tok)
  end
end)
