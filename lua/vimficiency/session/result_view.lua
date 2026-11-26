-- VF.Session.Result display adapters. Stable report text is formatted in C++.

local sequence_display = require("vimficiency.ui.sequence_display")

local M = {}

---@param title string
---@param result VF.Session.Result
---@return string
function M.format_message(title, result)
  return sequence_display.result_message(title, result)
end

return M
