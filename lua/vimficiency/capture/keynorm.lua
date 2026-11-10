-- Convert raw input bytes into the C++ tokenizer's printable form. Fed by the
-- CmdAtom capture (`capture/atoms.lua`, the keys/lhs fields) and by Explore's
-- own `vim.on_key` listener. See dev/lua/key-normalization.md for the contract.

local M = {}

--- Input MUST be raw input bytes, not printable notation.
---@param input string|nil  raw input bytes (CmdAtom keys/lhs, or vim.on_key)
---@return string           printable form, e.g. `<C-u>`, `<BS>`, `a`
function M.normalize(input)
  if not input or input == "" then return "" end
  return (vim.fn.keytrans(input):gsub("<C%-([A-Z])>", function(c)
    return "<C-" .. c:lower() .. ">"
  end))
end

--- True for any mouse event token (clicks, drags, releases, wheel, move).
--- These arrive as `<...>` names that the C++ tokenizer does not recognize,
--- so they would otherwise be costed character-by-character.
---@param normalized string  output of `M.normalize`
---@return boolean
function M.is_mouse(normalized)
  if not normalized or normalized == "" then return false end
  return normalized:find("Mouse") ~= nil
    or normalized:find("ScrollWheel") ~= nil
    or normalized:find("Drag") ~= nil
    or normalized:find("Release") ~= nil
end

return M
