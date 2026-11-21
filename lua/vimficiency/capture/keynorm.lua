-- Convert raw input bytes into the C++ tokenizer's printable form. Fed by the
-- CmdAtom capture (`capture/atom_stream.lua`, the keys/lhs fields) and by Explore's
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

-- K_SPECIAL KS_EXTRA KE_LUA; it has no key notation.
local K_LUA = "\128\253g"
local K_COMMAND = vim.keycode("<Cmd>")

--- True when resolved keys call into this Neovim instead of naming keys: a
--- Lua-callback mapping resolves to a function ref (`d<K_LUA>538<NL>e` for
--- flash.nvim's `dfe`), an expr mapping to a `<Cmd>` (mini.ai's `ci(`).
---@param raw string  raw CmdAtom `keys`
---@return boolean
function M.has_callback(raw)
  return raw:find(K_LUA, 1, true) ~= nil or raw:find(K_COMMAND, 1, true) ~= nil
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

--- Drops mouse event tokens, which are not keys.
---@param normalized string  output of `M.normalize`
---@return string
function M.without_mouse(normalized)
  return (normalized:gsub("<[^<>]+>", function(token)
    if M.is_mouse(token) then return "" end
  end))
end

return M
