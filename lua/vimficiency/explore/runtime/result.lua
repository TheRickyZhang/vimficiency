local M = {}
local v = vim.api

---@param result VF.Session.Result
---@param source_win integer
---@param optimizer_overrides string
---@return VF.SearchRequest
function M.search_request(result, source_win, optimizer_overrides)
  return {
    initial_lines = result.lines,
    goal_lines = result.goal_lines,
    start_row = result.start_row,
    start_col = result.start_col,
    end_row = result.end_row,
    end_col = result.end_col,
    has_lines_above = result.has_lines_above,
    has_lines_below = result.has_lines_below,
    window_height = v.nvim_win_get_height(source_win),
    scroll_amount = v.nvim_get_option_value("scroll", { win = source_win }),
    user_seq = result.user_seq,
    optimizer_overrides = optimizer_overrides,
  }
end

return M
