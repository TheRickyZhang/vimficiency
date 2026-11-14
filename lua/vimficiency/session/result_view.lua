-- VF.Session.Result display adapters. Stable report text is formatted in C++.

local ffi_display = require("vimficiency.ffi.display")
local sequence_display = require("vimficiency.sequence_display")

local M = {}

---@param result VF.Session.Result
---@return string  e.g. "(1,2) → (3,4)"
function M.format_position(result)
  return ffi_display.format_result_position(result)
end

---@param result VF.Session.Result
---@return string  e.g. " [mark]", or ""
function M.format_kind_suffix(result)
  return ffi_display.format_result_kind_suffix(result)
end

---@param title string
---@param result VF.Session.Result
---@return string
function M.format_message(title, result)
  return sequence_display.result_message(title, result)
end

return M
