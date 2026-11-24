-- One daily chart over the whole editor, with the rest of its section under
-- it: a float over the stats tab, each day and each row that lists runs an
-- item (ui/item_nav.lua).

---@class (exact) VF.Stats.ChartViewModule
---@field open fun(label: string, page_of: (fun(width: integer, height: integer): VF.Stats.Page), list_runs: fun(item: VF.Stats.Item)): VF.Stats.ChartView
local M = {}

local v = vim.api
local buf_window = require("vimficiency.ui.buf_window")
local item_nav = require("vimficiency.ui.item_nav")
local ui_windows = require("vimficiency.ui.windows")

local ns = v.nvim_create_namespace("vimfy_stats_chart")

---@class VF.Stats.ChartView
---@field win integer
---@field buf integer
---@field redraw fun()   # as after a scoring run
---@field close fun()

---@param label string                                               # what the stats call the chart
---@param page_of fun(width: integer, height: integer): VF.Stats.Page   # the page, as it stands now
---@param list_runs fun(item: VF.Stats.Item)                         # <CR> on a day or a row
---@return VF.Stats.ChartView
function M.open(label, page_of, list_runs)
  local origin = v.nvim_get_current_win()
  local buf = buf_window.create_scratch_buffer(
    string.format("vimficiency://stats/%d/chart", v.nvim_get_current_tabpage()), { filetype = "vimficiency_stats" })
  -- The border adds a row above and below; the tabline row stays clear.
  local win = v.nvim_open_win(buf, true, {
    relative = "editor", row = 1, col = 0,
    width = vim.o.columns - 2, height = math.max(3, vim.o.lines - vim.o.cmdheight - 4),
    border = "rounded", style = "minimal", title = string.format(" Stats › %s ", label), title_pos = "left",
  })
  ui_windows.configure_scratch_window(win, {})

  ---@type VF.Stats.Page
  local page = { lines = {}, marks = {}, items = {} }
  local nav = item_nav.attach(buf, {
    win = function() return win end,
    items = function() return page.items end,
    enter = list_runs,
    ---@param item VF.Stats.Item
    focused = function(item)
      local current = item.kind == "day" and string.format("%s: %.1f%% · %d samples", item.label, item.score, item.samples) or item.label
      v.nvim_win_set_config(win, { title = string.format(" Stats › %s · %s ", label, current) })
    end,
  })

  ---@type VF.Stats.ChartView
  local view = {
    win = win,
    buf = buf,
    redraw = function()
      page = page_of(v.nvim_win_get_width(win), v.nvim_win_get_height(win))
      vim.bo[buf].modifiable = true
      v.nvim_buf_set_lines(buf, 0, -1, false, page.lines)
      vim.bo[buf].modifiable = false
      v.nvim_buf_clear_namespace(buf, ns, 0, -1)
      for _, mark in ipairs(page.marks) do
        v.nvim_buf_set_extmark(buf, ns, mark.row, mark.col, { end_col = mark.end_col, hl_group = mark.hl })
      end
      nav.sync()
    end,
    close = function()
      if v.nvim_win_is_valid(win) then v.nvim_win_close(win, true) end
      -- Neovim returns to the previous window, which a run list opened from
      -- here and closed since no longer is.
      if v.nvim_win_is_valid(origin) then v.nvim_set_current_win(origin) end
    end,
  }
  ---@type VF.Keymap[]
  local keys = {
    { lhs = "<Esc>", desc = "Back to the stats", summary_group = "close", summary_desc = "Back to the stats",
      nowait = true, handler = view.close },
    { lhs = "q", desc = "Back to the stats", summary_group = "close", nowait = true, handler = view.close },
  }
  ui_windows.set_buffer_keymaps(buf, ui_windows.with_standard_ui_keymaps(
    vim.list_extend(vim.deepcopy(nav.keymaps), keys), { title = "Vimfy Stats Chart Keys" }))
  view.redraw()
  return view
end

return M
