---@class (exact) VF.Capture.State
---@field capture_view               fun(buf: integer, win: integer): VF.ViewState
---@field capture_state              fun(buf: integer, win: integer): VF.BufferState
---@field check_state_inconsistencies fun(start_state: VF.ViewState, end_state: VF.ViewState)
---@field is_insert_mode             fun(mode: string): boolean
local M = {}

local v = vim.api

---@class VF.ViewState
---@field bufname string       # original buffer name (full path)
---@field filetype string      # buffer filetype
---@field mode string
---@field row integer          # 0-indexed cursor row
---@field col integer          # 0-indexed cursor column
---@field top_row integer      # top visible row
---@field bottom_row integer   # bottom visible row
---@field window_height integer # Ctrl-F, B distance
---@field indent VF.IndentOptions
---@field scroll_amount integer # Ctrl-D, U distance (may differ from window_height/2)

---@class VF.BufferState : VF.ViewState
---@field lines string[]       # buffer lines at capture time

---@return VF.ViewState
function M.capture_view(buf, win)
  local cursor = v.nvim_win_get_cursor(win)
  ---@type VF.ViewState
  return {
    bufname = v.nvim_buf_get_name(buf),
    filetype = vim.bo[buf].filetype,
    indent = require("vimficiency.indent").capture(buf),
    mode = v.nvim_get_mode().mode,
    row = cursor[1] - 1,
    col = cursor[2],
    top_row = vim.fn.line("w0"),
    bottom_row = vim.fn.line("w$"),
    window_height = v.nvim_win_get_height(win),
    scroll_amount = v.nvim_get_option_value("scroll", { win = win }),
  }
end

---@return VF.BufferState
function M.capture_state(buf, win)
  local state = M.capture_view(buf, win) --[[@as VF.BufferState]]
  state.lines = v.nvim_buf_get_lines(buf, 0, -1, false)
  return state
end

function M.check_state_inconsistencies(start_state, end_state)
  if end_state.scroll_amount ~= start_state.scroll_amount then
    vim.notify("scroll amount changed during session")
  end
  if end_state.window_height ~= start_state.window_height then
    vim.notify("window height changed during session")
  end
end

--- Insert or Replace, including their completion submodes.
---@param mode string  `nvim_get_mode().mode`
---@return boolean
function M.is_insert_mode(mode)
  local lead = mode:sub(1, 1)
  return lead == "i" or lead == "R"
end

return M
