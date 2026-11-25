-- One chart over the whole editor, with the rest of its section under
-- it: a float over the stats tab, each bucket and each row that lists runs an
-- item (ui/item_nav.lua).

---@class (exact) VF.Stats.ChartViewModule
---@field open fun(name: VF.Stats.ChartName, summary: VF.Stats.Summary, summary_of: (fun(scale?: VF.Stats.ChartScale): VF.Stats.Summary), list_runs: fun(item: VF.Stats.Item)): VF.Stats.ChartView
local M = {}

local v = vim.api
local buf_window = require("vimficiency.ui.buf_window")
local item_nav = require("vimficiency.ui.item_nav")
local render = require("vimficiency.stats.render")
local ui_windows = require("vimficiency.ui.windows")

local ns = v.nvim_create_namespace("vimfy_stats_chart")

local titles = {
  sessions = "Sessions", journal = "Journal", nav_timing = "Movement search", edit_timing = "Composition search",
}

---@type VF.Stats.ChartScale[]
local SCALES = {
  { unit = "minutes", bucket_size = 1, buckets = 15, tick_step = 3 },
  { unit = "minutes", bucket_size = 1, buckets = 30, tick_step = 5, overview = true },
  { unit = "minutes", bucket_size = 2, buckets = 30, tick_step = 5 },
  { unit = "minutes", bucket_size = 10, buckets = 18, tick_step = 6 },
  { unit = "minutes", bucket_size = 15, buckets = 24, tick_step = 4 },
  { unit = "minutes", bucket_size = 30, buckets = 24, tick_step = 4 },
  { unit = "hours", bucket_size = 1, buckets = 30, tick_step = 5, overview = true },
  { unit = "hours", bucket_size = 3, buckets = 24, tick_step = 8 },
  { unit = "days", bucket_size = 1, buckets = 7, tick_step = 1 },
  { unit = "days", bucket_size = 1, buckets = 14, tick_step = 2 },
  { unit = "days", bucket_size = 1, buckets = 30, tick_step = 5, overview = true },
  { unit = "days", bucket_size = 3, buckets = 30, tick_step = 5 },
  { unit = "weeks", bucket_size = 1, buckets = 30, tick_step = 5, overview = true },
}

---@class VF.Stats.ChartView
---@field win integer
---@field buf integer
---@field refresh fun()   # as after a scoring run
---@field close fun()

---@param name VF.Stats.ChartName
---@param summary VF.Stats.Summary
---@param summary_of fun(scale?: VF.Stats.ChartScale): VF.Stats.Summary
---@param list_runs fun(item: VF.Stats.Item)
---@return VF.Stats.ChartView
function M.open(name, summary, summary_of, list_runs)
  local history = name == "sessions" or name == "journal"
  local index
  if history then
    for i, scale in ipairs(SCALES) do
      if scale.overview and scale.unit == summary.chart_unit then
        index = i
        break
      end
    end
    assert(index)
  end
  local label = titles[name]
  ---@param detail? string
  local function title(detail)
    local text = " Stats › " .. label
    if name == "journal" then text = text .. " · " .. render.journal_efficiency(summary.journal) end
    if detail then text = text .. " · " .. detail end
    return text .. " "
  end
  local origin = v.nvim_get_current_win()
  local buf = buf_window.create_scratch_buffer(
    string.format("vimficiency://stats/%d/chart", v.nvim_get_current_tabpage()), { filetype = "vimficiency_stats" })
  -- The border adds a row above and below; the tabline row stays clear.
  local win = v.nvim_open_win(buf, true, {
    relative = "editor", row = 1, col = 0,
    width = vim.o.columns - 2, height = math.max(3, vim.o.lines - vim.o.cmdheight - 4),
    border = "rounded", style = "minimal", title = title(), title_pos = "left",
  })
  ui_windows.configure_scratch_window(win, { scrolloff = 0 })

  ---@type VF.Stats.Page
  local page = { lines = {}, marks = {}, items = {} }
  ---@type VF.Stats.ChartLines
  local shown = { primary = true, samples = true }
  local nav = item_nav.attach(buf, {
    win = function() return win end,
    items = function() return page.items end,
    enter = list_runs,
    ---@param item VF.Stats.Item
    focused = function(item)
      if item.kind == "day" or item.kind == "timing_bucket" then
        v.nvim_win_call(win, function() vim.fn.winrestview({ topline = 1 }) end)
      end
      v.nvim_win_set_config(win, { title = title(item.detail or item.label) })
    end,
  })

  local function redraw()
    page = render.chart_page(summary, name, v.nvim_win_get_width(win), v.nvim_win_get_height(win), shown,
      index and SCALES[index])
    vim.bo[buf].modifiable = true
    v.nvim_buf_set_lines(buf, 0, -1, false, page.lines)
    vim.bo[buf].modifiable = false
    v.nvim_buf_clear_namespace(buf, ns, 0, -1)
    for _, mark in ipairs(page.marks) do
      v.nvim_buf_set_extmark(buf, ns, mark.row, mark.col, { end_col = mark.end_col, hl_group = mark.hl })
    end
    if #page.items == 0 then v.nvim_win_set_config(win, { title = title() }) end
    nav.sync()
  end

  ---@param offset? integer
  local function refresh(offset)
    local next_index = index
    if index and offset then
      next_index = math.max(1, math.min(#SCALES, index + offset))
      if next_index == index then return end
    end
    summary = summary_of(next_index and SCALES[next_index])
    index = next_index
    redraw()
  end

  ---@type VF.Stats.ChartView
  local view = {
    win = win,
    buf = buf,
    refresh = refresh,
    close = function()
      if v.nvim_win_is_valid(win) then v.nvim_win_close(win, true) end
      -- Neovim returns to the previous window, which a run list opened from
      -- here and closed since no longer is.
      if v.nvim_win_is_valid(origin) then v.nvim_set_current_win(origin) end
    end,
  }
  ---@type VF.Keymap[]
  local keys = {
    { lhs = history and "e" or "t", desc = history and "Toggle the efficiency line" or "Toggle the median time line",
      handler = function()
      shown.primary = not shown.primary
      redraw()
    end },
    { lhs = "s", desc = "Toggle the samples line", handler = function()
      shown.samples = not shown.samples
      redraw()
    end },
    { lhs = "<Esc>", desc = "Back to the stats", summary_group = "close", summary_desc = "Back to the stats",
      nowait = true, handler = view.close },
    { lhs = "q", desc = "Back to the stats", summary_group = "close", nowait = true, handler = view.close },
  }
  if history then
    keys[#keys + 1] = { lhs = "+", desc = "Show more history", handler = function() refresh(vim.v.count1) end }
    keys[#keys + 1] = { lhs = "-", desc = "Show less history", handler = function() refresh(-vim.v.count1) end }
  end
  ui_windows.set_buffer_keymaps(buf, ui_windows.with_standard_ui_keymaps(
    vim.list_extend(vim.deepcopy(nav.keymaps), keys), { title = "Vimfy Stats Chart Keys",
      settings = { handler = "<Cmd>Vimfy appearance<CR>", desc = "Open Appearance" } }))
  redraw()
  return view
end

return M
