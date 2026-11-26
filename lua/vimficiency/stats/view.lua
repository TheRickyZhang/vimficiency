-- The `:Vimfy stats` tab. <CR> toggles a command list in place or opens a chart
-- or its runs in a float.

---@class (exact) VF.StatsView
---@field open fun()
local M = {}

local v = vim.api
local buf_window = require("vimficiency.ui.buf_window")
local chart_view = require("vimficiency.stats.chart_view")
local config = require("vimficiency.config")
local highlights = require("vimficiency.ui.highlights")
local item_nav = require("vimficiency.ui.item_nav")
local run_list = require("vimficiency.stats.run_list")
local sampling = require("vimficiency.stats.sampling")
local stats_query = require("vimficiency.stats.query")
local render = require("vimficiency.stats.render")
local ui_windows = require("vimficiency.ui.windows")
local util = require("vimficiency.util")

local ns = v.nvim_create_namespace("vimfy_stats")

---@class VF.Stats.View
---@field query VF.Stats.Scope
---@field summary VF.Stats.Summary
---@field expanded_commands VF.Stats.ExpandedCommands
---@field chart? VF.Stats.ChartView   # the last chart opened
---@field page VF.Stats.Page   # as last drawn
---@field nav VF.Ui.ItemNav
---@field tab integer
---@field win integer
---@field buf integer
---@field runs? VF.Stats.RunListView   # the last run list opened
---@field saved_tab integer
---@field saved_win integer
---@field progress_timer? integer

---@param buf integer
---@param lines string[]
local function set_lines(buf, lines)
  vim.bo[buf].modifiable = true
  v.nvim_buf_set_lines(buf, 0, -1, false, lines)
  vim.bo[buf].modifiable = false
end

local watch_scoring

---@param view VF.Stats.View
local function draw(view)
  local buf = view.buf
  local page = render.build_page(view.summary, view.query, { scoring = sampling.running(), err = sampling.last_error() },
    v.nvim_win_get_width(view.win), view.expanded_commands)
  view.page = page
  set_lines(buf, page.lines)
  v.nvim_buf_clear_namespace(buf, ns, 0, -1)
  for _, mark in ipairs(page.marks) do
    v.nvim_buf_set_extmark(buf, ns, mark.row, mark.col, {
      end_col = mark.end_col, hl_group = mark.hl, priority = mark.priority,
    })
  end
  view.nav.sync()
  watch_scoring(view)
end

---@param view VF.Stats.View
local function refresh(view)
  view.summary = stats_query.summary(view.query)
  draw(view)
  if view.chart and v.nvim_win_is_valid(view.chart.win) then view.chart.refresh() end
end

---@param view VF.Stats.View
local function stop_progress(view)
  if view.progress_timer then vim.fn.timer_stop(view.progress_timer) end
  view.progress_timer = nil
end

---@param view VF.Stats.View
function watch_scoring(view)
  if view.progress_timer or not sampling.running() then return end
  view.progress_timer = vim.fn.timer_start(1000, util.entry(function()
    if view.tab ~= v.nvim_get_current_tabpage() then return end
    local coverage = stats_query.journal_coverage()
    local journal = view.summary.journal
    if render.journal_percent(journal) == render.journal_percent(coverage) then return end
    journal.scored_bytes = coverage.scored_bytes
    journal.journal_bytes = coverage.journal_bytes
    draw(view)
    if view.chart and v.nvim_win_is_valid(view.chart.win) then view.chart.update_coverage(coverage) end
  end), { ["repeat"] = -1 })
  sampling.when_done(util.entry(function()
    stop_progress(view)
    if not v.nvim_win_is_valid(view.win) then return end
    refresh(view)
    if view.runs and v.nvim_win_is_valid(view.runs.list_win) then view.runs.refresh() end
  end))
end

--- Every drill, on the stats or a chart's page, opens its runs here.
---@param view VF.Stats.View
---@param item VF.Stats.Item
local function list_runs(view, item)
  view.runs = run_list.open(vim.tbl_extend("force", view.query, { filter = item.drill }), item.label, item.order)
end

--- What <CR> does to the current item.
---@param view VF.Stats.View
---@param item VF.Stats.Item
local function enter(view, item)
  local section = item.command_section
  if section then
    local expanded = view.expanded_commands[item.source]
    expanded[section] = not expanded[section]
    draw(view)
  elseif item.drill then
    list_runs(view, item)
  elseif item.kind == "chart" then
    view.chart = chart_view.open(item.chart, view.summary,
      function(scale)
        if not scale or (scale.overview and scale.unit == view.summary.chart_unit) then return view.summary end
        return stats_query.summary(view.query, scale)
      end,
      ---@param listed VF.Stats.Item
      function(listed) list_runs(view, listed) end)
  end
end

---@param view VF.Stats.View
---@param item VF.Stats.Item
---@param offset integer
---@return boolean
local function adjust(view, item, offset)
  local selector = item.selector
  if not selector then return false end
  local index = math.max(1, math.min(#selector.values, selector.index + offset))
  if index == selector.index then return true end
  local value = selector.values[index]
  if type(value) == "string" then
    view.query.chart_unit = value
  else
    view.query.charted_size = value
  end
  refresh(view)
  return true
end

--- Where a move off the edge of one side of a band lands on its other: `l` on
--- the band's first Commands row, `h` on its first item, the chart.
---@param page VF.Stats.Page
---@param item VF.Stats.Item
---@param key "h"|"j"|"k"|"l"
---@return string|nil
local function across(page, item, key)
  local side = item.group == "left" and key == "l" and "right" or item.group == "right" and key == "h" and "left"
  if not side then return nil end
  for _, other in ipairs(page.items) do
    if other.group == side and other.source == item.source then return other.id end
  end
end

---@param view VF.Stats.View
local function close(view)
  if v.nvim_tabpage_is_valid(view.saved_tab) then
    v.nvim_set_current_tabpage(view.saved_tab)
  elseif #v.nvim_list_tabpages() == 1 then
    -- The tab it was opened from is gone, and the last tab can't be closed.
    vim.cmd.tabnew()
  end
  vim.cmd.tabclose(v.nvim_tabpage_get_number(view.tab))
  if v.nvim_win_is_valid(view.saved_win) then v.nvim_set_current_win(view.saved_win) end
end

---@param view VF.Stats.View
local function watch_view(view)
  local group = v.nvim_create_augroup("vimfy_stats_" .. view.tab, {})
  v.nvim_create_autocmd("User", {
    group = group,
    pattern = "VimficiencyJournalScoringStarted",
    callback = util.entry(function() refresh(view) end),
  })
  v.nvim_create_autocmd("WinClosed", {
    group = group,
    pattern = tostring(view.win),
    callback = function()
      stop_progress(view)
      v.nvim_del_augroup_by_id(group)
      -- `q` closes the tab itself; by the time this runs, it is gone.
      vim.schedule(function()
        if v.nvim_tabpage_is_valid(view.tab) then close(view) end
      end)
    end,
  })
end

---@param view VF.Stats.View
local function set_keymaps(view)
  local query = view.query
  ---@type VF.Keymap[]
  local view_keys = {
    { lhs = "w", desc = "Toggle the efficiency weight: effort, uniform", handler = function()
      query.weight = query.weight == "effort" and "uniform" or "effort"
      refresh(view)
    end },
    { lhs = "v", desc = "Toggle the scorings counted: all, current only", handler = function()
      query.current_only = not query.current_only
      refresh(view)
    end },
    { lhs = "q", desc = "Close stats", handler = function() close(view) end },
  }
  local opts = { title = "Vimfy Stats Keys",
    settings = { handler = "<Cmd>Vimfy appearance<CR>", desc = "Open Appearance" } }
  ui_windows.set_buffer_keymaps(view.buf, ui_windows.with_standard_ui_keymaps(
    vim.list_extend(vim.deepcopy(view.nav.keymaps), view_keys), opts))
end

function M.open()
  highlights.refresh()
  local saved_tab, saved_win = v.nvim_get_current_tabpage(), v.nvim_get_current_win()
  ---@type VF.Stats.Scope
  local query = { current_only = false, weight = config.stats.weight, charted_size = 0, filter = {} }
  local summary = stats_query.summary(query)
  vim.cmd.tabnew()
  local tab, win = v.nvim_get_current_tabpage(), v.nvim_get_current_win()
  local placeholder = v.nvim_get_current_buf()
  local buf = buf_window.create_scratch_buffer(string.format("vimficiency://stats/%d/overview", tab),
    { filetype = "vimficiency_stats" })
  v.nvim_win_set_buf(win, buf)
  v.nvim_buf_delete(placeholder, { force = true })
  ui_windows.configure_scratch_window(win)
  ---@type VF.Stats.View
  local view
  view = {
    query = query,
    summary = summary,
    expanded_commands = { sessions = {}, journal = {} },
    page = { lines = {}, marks = {}, items = {} },
    nav = item_nav.attach(buf, {
      win = function() return win end,
      items = function() return view.page.items end,
      ---@param item VF.Stats.Item
      enter = function(item) enter(view, item) end,
      ---@param item VF.Stats.Item
      adjust = function(item, offset) return adjust(view, item, offset) end,
      ---@param item VF.Stats.Item
      leave = function(item, key) return across(view.page, item, key) end,
    }),
    tab = tab, win = win, buf = buf,
    saved_tab = saved_tab,
    saved_win = saved_win,
  }
  set_keymaps(view)
  watch_view(view)
  draw(view)
end

return M
