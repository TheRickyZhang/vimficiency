local M = {}

---@class VF.IndentOptions
---@field autoindent boolean
---@field expandtab boolean
---@field copyindent boolean
---@field tabstop integer
---@field shiftwidth integer

local fields = { "autoindent", "expandtab", "copyindent", "tabstop", "shiftwidth" }

---@return VF.IndentOptions
function M.capture(buf)
  local bo = vim.bo[buf]
  local indent = {}
  for _, name in ipairs(fields) do indent[name] = bo[name] end
  local override = require("vimficiency.ffi.config").applied_globals().shiftwidth
  if override and override >= 0 then indent.shiftwidth = override end
  return indent
end

---@param indent VF.IndentOptions
function M.apply(buf, indent)
  local bo = vim.bo[buf]
  for _, name in ipairs(fields) do bo[name] = indent[name] end
end

return M
