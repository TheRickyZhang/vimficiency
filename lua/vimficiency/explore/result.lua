local ffi_explore = require("vimficiency.ffi.explore")

local M = {}
local v = vim.api

---@param result VF.Session.Result
---@return integer
function M.boundary_last_col(result)
  local initial_last = result.lines[#result.lines]
  local goal_last = result.goal_lines[#result.goal_lines]
  return math.max(0, math.max(#initial_last, #goal_last) - 1)
end

---@param result VF.Session.Result
---@param source_win integer
---@param optimizer_overrides string
---@param deadline_ms integer
---Returns a job id; the composition search runs on a worker thread. Poll with
---`ffi_explore.explore_poll` until it yields a view id.
---@return integer
function M.start_view_async(result, source_win, optimizer_overrides, deadline_ms)
  ---@type VF.SearchRequest
  local request = {
    initial_lines = result.lines,
    goal_lines = result.goal_lines,
    start_row = result.start_row,
    start_col = result.start_col,
    end_row = result.end_row,
    end_col = result.end_col,
    boundary_first_col = 0,
    boundary_last_col = M.boundary_last_col(result),
    has_lines_above = result.has_lines_above,
    has_lines_below = result.has_lines_below,
    window_height = v.nvim_win_get_height(source_win),
    scroll_amount = v.nvim_get_option_value("scroll", { win = source_win }),
    user_seq = result.user_seq,
    optimizer_overrides = optimizer_overrides,
  }
  return ffi_explore.explore_start_async(request, deadline_ms)
end

return M
