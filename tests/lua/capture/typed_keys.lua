-- ffi/session.typed_keys: the keys pressed for an atom, from what Neovim read
-- as typed and what plugins fed as typed. Each case is a shape measured on
-- Neovim 0.13-dev with which-key.nvim, flash.nvim and mini.ai.

local ffi_session = require("vimficiency.ffi.session")

--- `parts` alternate between reads (plain strings) and feeds ({ fed = "keys" }).
---@param parts (string|{ fed: string })[]
---@param expected string
local function case(parts, expected)
  local inputs = {}
  for i, part in ipairs(parts) do
    inputs[i] = type(part) == "table" and { keys = part.fed, fed = true } or { keys = part, fed = false }
  end
  assert_eq(ffi_session.typed_keys(inputs).typed, expected, vim.inspect(parts))
end

test("typed_keys: reads alone are the pressed keys", function()
  case({ "d", "2", "w" }, "d2w")
  case({ "c", "t", "g", "x", "<Esc>" }, "ctgx<Esc>")
end)

test("typed_keys: fed keys read back are not pressed again", function()
  -- which-key: `d` triggers it, it reads `f` with getchar() and feeds it back.
  case({ "d", "f", { fed = "f" }, "f", "g" }, "dfg")
  case({ "3", "g", "g", { fed = "3gg" }, "3", "g", "g" }, "3gg")
end)

test("typed_keys: one read can cover fed keys and a new press", function()
  case({ "g", "c", { fed = "gc" }, "gcc" }, "gcc")
end)

test("typed_keys: fed keys consumed unreported are dropped at the next other read", function()
  -- mini.ai's `i` consumes the fed `i` without on_key reporting it.
  case({ '"', "a", { fed = '"a' }, '"a', "y", "i", { fed = "i" }, "w" }, '"ayiw')
end)

test("typed_keys: keys fed but never read are not pressed", function()
  case({ "x", { fed = "abc" } }, "x")
end)

test("typed_keys: fed keys match whole notation tokens", function()
  case({ "c", { fed = "<Esc>" }, "<Esc>", "é", { fed = "é" }, "é" }, "cé")
end)

test("typed_keys: the insert text is told apart", function()
  -- `c` triggers which-key, which feeds back the `w` it read; `x` is typed into
  -- the insert session that opens and `<Esc>` leaves it.
  local pressed = ffi_session.typed_keys({
    { keys = "c", fed = false, insert = false },
    { keys = "w", fed = false, insert = false },
    { keys = "w", fed = true, insert = false },
    { keys = "w", fed = false, insert = false },
    { keys = "x", fed = false, insert = true },
    { keys = "<Esc>", fed = false, insert = false },
  })
  assert_eq(pressed, { typed = "cwx<Esc>", typed_insert = "x" })
end)
