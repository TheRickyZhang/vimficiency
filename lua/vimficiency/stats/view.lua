-- The `:Vimfy stats` tab: a header over the stats (render.build_page). <CR>
-- opens a float over it: a chart on its own (stats/chart_view.lua), or the runs
-- of a row or of a chart's day (stats/run_list.lua) under the filter C++
-- attached to it; nothing here builds or edits a filter.

---@class (exact) VF.StatsView
---@field open fun()
local M = {}

local v = vim.api
local buf_window = require("vimficiency.ui.buf_window")
local chart_view = require("vimficiency.stats.chart_view")
local config = require("vimficiency.config")
local highlights = require("vimficiency.ui.highlights")
local item_nav = require("vimficiency.ui.item_nav")
local log = require("vimficiency.stats.log")
local run_list = require("vimficiency.stats.run_list")
local sampling = require("vimficiency.stats.sampling")
local stats_query = require("vimficiency.stats.query")
local render = require("vimficiency.stats.render")
local ui_windows = require("vimficiency.ui.windows")
local util = require("vimficiency.util")

local ns = v.nvim_create_namespace("vimfy_stats")

---@class VF.Stats.ViewQuery
---@field current_only boolean
---@field weight VF.Stats.Weight
---@field chart_unit? string
---@field charted_size integer   # 0: the middle window size

---@class VF.Stats.Pane
---@field buf integer
---@field win integer

---@class VF.Stats.ViewPanes
---@field tab integer
---@field header VF.Stats.Pane
---@field overview VF.Stats.Pane

---@class VF.Stats.View
---@field query VF.Stats.ViewQuery
---@field summary VF.Stats.Summary
---@field chart? VF.Stats.ChartView   # the last chart opened
---@field page VF.Stats.Page   # as last drawn
---@field nav VF.Ui.ItemNav
---@field panes VF.Stats.ViewPanes
---@field runs? VF.Stats.RunListView   # the last run list opened
---@field saved_tab integer
---@field saved_win integer
---@field waiting boolean   # a redraw is queued on the scoring run in flight

---@param query VF.Stats.ViewQuery
---@param filter VF.Stats.Filter
---@return VF.Stats.Scope
local function scope(query, filter)
  return {
    current_only = query.current_only,
    weight = query.weight,
    filter = filter,
    chart_unit = query.chart_unit,
    charted_size = query.charted_size,
  }
end

---@param view VF.Stats.View
---@return string[]
local function header_lines(view)
  local query, summary = view.query, view.summary
  local scoring = query.current_only and summary.scoring_label or "all scorings"
  local settings = { scoring, "weight: " .. query.weight, "runs of " .. summary.journal.charted_size }
  return {
    string.format(" Stats    %s", table.concat(settings, " · ")),
    string.format(" Log      %s", vim.fn.fnamemodify(log.path(), ":~")),
  }
end

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
  local panes = view.panes
  local win, buf = panes.overview.win, panes.overview.buf
  local page = render.build_page(view.summary, { scoring = sampling.running(), err = sampling.last_error() },
    v.nvim_win_get_width(win))
  local header = header_lines(view)
  local rows = #header
  page.lines = vim.list_extend(header, page.lines)
  for _, item in ipairs(page.items) do
    item.row, item.last_row, item.cursor[1] = item.row + rows, item.last_row + rows, item.cursor[1] + rows
    for _, span in ipairs(item.spans or {}) do span[1] = span[1] + rows end
  end
  for _, mark in ipairs(page.marks) do mark.row = mark.row + rows end
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
  view.summary = stats_query.summary(scope(view.query, {}))
  draw(view)
  if view.chart and v.nvim_win_is_valid(view.chart.win) then view.chart.redraw() end
end

--- Redraws once the scoring run in flight finishes, if the view is still open.
---@param view VF.Stats.View
function watch_scoring(view)
  if view.waiting then return end
  view.waiting = sampling.when_done(util.entry(function()
    view.waiting = false
    if not v.nvim_tabpage_is_valid(view.panes.tab) then return end
    refresh(view)
    if view.runs and v.nvim_win_is_valid(view.runs.list_win) then view.runs.refresh() end
  end))
end

--- What <CR> does to the current item.
---@param view VF.Stats.View
---@param item VF.Stats.Item
local function enter(view, item)
  if item.drill then
    view.runs = run_list.open(scope(view.query, item.drill), item.label)
  elseif item.kind == "chart" then
    view.chart = chart_view.open(item.chart == "journal" and "Journal" or "Sessions",
      function(width, height) return render.chart_page(view.summary, item.chart, width, height) end,
      ---@param listed VF.Stats.Item
      function(listed) view.runs = run_list.open(scope(view.query, listed.drill), listed.label) end)
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

--- Where a move off the edge of one side of the page lands on the other: `l`
--- on the first Commands row, `h` on the journal's chart, else the sessions'.
---@param page VF.Stats.Page
---@param item VF.Stats.Item
---@param key "h"|"j"|"k"|"l"
---@return string|nil
local function across(page, item, key)
  if item.group == "left" and key == "l" then
    for _, other in ipairs(page.items) do
      if other.group == "right" then return other.id end
    end
  elseif item.group == "right" and key == "h" then
    local chart
    for _, other in ipairs(page.items) do
      if other.kind == "chart" and (not chart or chart.chart ~= "journal") then chart = other end
    end
    return chart and chart.id
  end
end

--- `view_of` returns the view being built around the result.
---@param panes VF.Stats.ViewPanes
---@param view_of fun(): VF.Stats.View
---@return VF.Ui.ItemNav
local function attach_nav(panes, view_of)
  return item_nav.attach(panes.overview.buf, {
    win = function() return panes.overview.win end,
    items = function() return view_of().page.items end,
    ---@param item VF.Stats.Item
    enter = function(item) enter(view_of(), item) end,
    ---@param item VF.Stats.Item
    adjust = function(item, offset) return adjust(view_of(), item, offset) end,
    ---@param item VF.Stats.Item
    leave = function(item, key) return across(view_of().page, item, key) end,
  })
end

---@param view VF.Stats.View
local function close(view)
  if v.nvim_tabpage_is_valid(view.saved_tab) then
    v.nvim_set_current_tabpage(view.saved_tab)
  elseif #v.nvim_list_tabpages() == 1 then
    -- The tab it was opened from is gone, and the last tab can't be closed.
    vim.cmd.tabnew()
  end
  vim.cmd.tabclose(v.nvim_tabpage_get_number(view.panes.tab))
  if v.nvim_win_is_valid(view.saved_win) then v.nvim_set_current_win(view.saved_win) end
end

---@param view VF.Stats.View
local function watch_view(view)
  local panes = view.panes
  local group = v.nvim_create_augroup("vimfy_stats_" .. panes.tab, {})
  v.nvim_create_autocmd("User", {
    group = group,
    pattern = "VimficiencyJournalScoringStarted",
    callback = util.entry(function() draw(view) end),
  })
  for _, win in ipairs({ panes.overview.win }) do
    v.nvim_create_autocmd("WinClosed", {
      group = group,
      pattern = tostring(win),
      callback = function()
        v.nvim_del_augroup_by_id(group)
        -- `q` closes the tab itself; by the time this runs, it is gone.
        vim.schedule(function()
          if v.nvim_tabpage_is_valid(panes.tab) then close(view) end
        end)
      end,
    })
  end
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
  local panes = view.panes
  local opts = { title = "Vimfy Stats Keys" }
  ui_windows.set_buffer_keymaps(panes.overview.buf, ui_windows.with_standard_ui_keymaps(
    vim.list_extend(vim.deepcopy(view.nav.keymaps), view_keys), opts))
end

---@param tab integer
---@param name string
---@return integer buf
local function pane_buffer(tab, name)
  return buf_window.create_scratch_buffer(string.format("vimficiency://stats/%d/%s", tab, name),
    { filetype = "vimficiency_stats" })
end

--- A new tab: the header over the stats.
---@return VF.Stats.ViewPanes
local function open_panes()
  vim.cmd.tabnew()
  local tab = v.nvim_get_current_tabpage()
  local placeholder = v.nvim_get_current_buf()
  local overview = { buf = pane_buffer(tab, "overview"), win = v.nvim_get_current_win() }
  v.nvim_win_set_buf(overview.win, overview.buf)
  v.nvim_buf_delete(placeholder, { force = true })

  ui_windows.configure_scratch_window(overview.win)
  return { tab = tab, header = overview, overview = overview }
end

function M.open()
  highlights.refresh()
  local saved_tab, saved_win = v.nvim_get_current_tabpage(), v.nvim_get_current_win()
  ---@type VF.Stats.ViewQuery
  local query = { current_only = false, weight = config.stats.weight, charted_size = 0 }
  local panes = open_panes()
  ---@type VF.Stats.View
  local view
  view = {
    query = query,
    summary = stats_query.summary(scope(query, {})),
    page = { lines = {}, marks = {}, items = {} },
    nav = attach_nav(panes, function() return view end),
    panes = panes,
    saved_tab = saved_tab,
    saved_win = saved_win,
    waiting = false,
  }
  set_keymaps(view)
  watch_view(view)
  draw(view)
end

return M
