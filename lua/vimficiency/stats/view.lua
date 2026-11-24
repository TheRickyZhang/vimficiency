-- The `:Vimfy stats` tab: a header over two panes of stats
-- (render.build_pages), Sessions and Journal on the left, Commands and Dev on
-- the right. <CR> on a stats row or chart day lists its runs in a float
-- (stats/run_list.lua) under the filter C++ attached to it; nothing here builds
-- or edits a filter.

---@class (exact) VF.StatsView
---@field open fun()
local M = {}

local v = vim.api
local buf_window = require("vimficiency.ui.buf_window")
local config = require("vimficiency.config")
local highlights = require("vimficiency.ui.highlights")
local run_list = require("vimficiency.stats.run_list")
local sampling = require("vimficiency.stats.sampling")
local stats_query = require("vimficiency.stats.query")
local render = require("vimficiency.stats.render")
local ui_windows = require("vimficiency.ui.windows")
local util = require("vimficiency.util")

-- Any narrower beside the stats and the right pane goes below them instead.
local MIN_SIDE_WIDTH = 60
local ns = v.nvim_create_namespace("vimfy_stats")

---@class VF.Stats.ViewQuery
---@field current_only boolean
---@field weight VF.Stats.Weight
---@field charted_size integer   # 0: the middle window size

---@class VF.Stats.ViewData
---@field summary VF.Stats.Summary
---@field pages VF.Stats.Page[]   # the left pane's, then the right pane's

---@class VF.Stats.Pane
---@field buf integer
---@field win integer

---@class VF.Stats.ViewPanes
---@field tab integer
---@field header VF.Stats.Pane
---@field overview VF.Stats.Pane   # left
---@field commands VF.Stats.Pane   # right
---@field side_by_side boolean

---@class VF.Stats.View
---@field query VF.Stats.ViewQuery
---@field data VF.Stats.ViewData
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
    charted_size = query.charted_size,
  }
end

---@param query VF.Stats.ViewQuery
---@return VF.Stats.ViewData
local function fetch(query)
  local summary = stats_query.summary(scope(query, {}))
  return {
    summary = summary,
    pages = render.build_pages(summary, { scoring = sampling.running(), err = sampling.last_error() }, query.weight),
  }
end

---@param view VF.Stats.View
---@return string
local function header_line(view)
  local query, data = view.query, view.data
  local scoring = query.current_only and data.summary.scoring_label or "all scorings"
  local settings = { scoring, "weight: " .. query.weight, "runs of " .. data.summary.journal.charted_size }
  return string.format(" Stats    %s", table.concat(settings, " · "))
end

---@param lines string[]
---@return integer
local function page_width(lines)
  local width = 0
  for _, line in ipairs(lines) do width = math.max(width, vim.fn.strdisplaywidth(line)) end
  return width + 1
end

---@param buf integer
---@param lines string[]
local function set_lines(buf, lines)
  vim.bo[buf].modifiable = true
  v.nvim_buf_set_lines(buf, 0, -1, false, lines)
  vim.bo[buf].modifiable = false
end

---@param pane VF.Stats.Pane
---@param page VF.Stats.Page
local function draw_page(pane, page)
  local cursor = v.nvim_win_get_cursor(pane.win)
  set_lines(pane.buf, page.lines)
  v.nvim_buf_clear_namespace(pane.buf, ns, 0, -1)
  for _, row in ipairs(page.title_rows) do
    v.nvim_buf_set_extmark(pane.buf, ns, row, 0, {
      end_row = row + 1, end_col = 0, hl_group = highlights.STATS_SECTION_TITLE, hl_eol = true,
    })
  end
  for _, target in ipairs(page.targets) do
    if not target.cols then
      local line = page.lines[target.row + 1]
      v.nvim_buf_set_extmark(pane.buf, ns, target.row, line:find("%S") - 1, {
        end_col = #line, hl_group = highlights.STATS_DRILL,
      })
    end
  end
  v.nvim_win_set_cursor(pane.win, { math.min(cursor[1], #page.lines), cursor[2] })
end

local watch_scoring

---@param view VF.Stats.View
local function draw(view)
  local panes, data = view.panes, view.data
  set_lines(panes.header.buf, { header_line(view) })
  draw_page(panes.overview, data.pages[1])
  if panes.side_by_side then
    v.nvim_win_resize(panes.overview.win, page_width(data.pages[1].lines), -1)
  end
  draw_page(panes.commands, data.pages[2])
  watch_scoring(view)
end

---@param view VF.Stats.View
local function refresh(view)
  view.data = fetch(view.query)
  draw(view)
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

---@param page VF.Stats.Page
---@param win integer
---@return VF.Stats.DrillTarget|nil
local function target_at_cursor(page, win)
  local cursor = v.nvim_win_get_cursor(win)
  local row, col = cursor[1] - 1, cursor[2]
  for _, target in ipairs(page.targets) do
    if target.row == row and (not target.cols or (col >= target.cols[1] and col < target.cols[2])) then
      return target
    end
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
  vim.cmd.tabclose(v.nvim_tabpage_get_number(view.panes.tab))
  if v.nvim_win_is_valid(view.saved_win) then v.nvim_set_current_win(view.saved_win) end
end

--- The panes only work together, so closing one closes the view.
---@param view VF.Stats.View
local function close_with_any_pane(view)
  local panes = view.panes
  local group = v.nvim_create_augroup("vimfy_stats_" .. panes.tab, {})
  for _, win in ipairs({ panes.header.win, panes.overview.win, panes.commands.win }) do
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
    { lhs = "w", desc = "Toggle the score weight: effort, uniform", handler = function()
      query.weight = query.weight == "effort" and "uniform" or "effort"
      refresh(view)
    end },
    { lhs = "l", desc = "Cycle the journal run length the stats chart", handler = function()
      local journal = view.data.summary.journal
      for i, size in ipairs(journal.sizes) do
        if size.size == journal.charted_size then
          query.charted_size = journal.sizes[i % #journal.sizes + 1].size
          break
        end
      end
      refresh(view)
    end },
    { lhs = "v", desc = "Toggle the scorings counted: all, current only", handler = function()
      query.current_only = not query.current_only
      refresh(view)
    end },
    { lhs = "q", desc = "Close stats", handler = function() close(view) end },
  }
  ---@param page_index integer
  ---@param pane VF.Stats.Pane
  ---@return VF.Keymap
  local function drill_key(page_index, pane)
    return { lhs = "<CR>", desc = "List the runs of the row or chart day under the cursor", handler = function()
      local target = target_at_cursor(view.data.pages[page_index], pane.win)
      if not target then return end
      view.runs = run_list.open(scope(query, target.drill), target.label)
    end }
  end
  local opts = { title = "Vimfy Stats Keys" }
  local panes = view.panes
  ui_windows.set_buffer_keymaps(panes.overview.buf, ui_windows.with_standard_ui_keymaps(
    vim.list_extend({ drill_key(1, panes.overview) }, view_keys), opts))
  ui_windows.set_buffer_keymaps(panes.commands.buf, ui_windows.with_standard_ui_keymaps(
    vim.list_extend({ drill_key(2, panes.commands) }, view_keys), opts))
  ui_windows.set_buffer_keymaps(panes.header.buf, ui_windows.with_standard_ui_keymaps(view_keys, opts))
end

---@param tab integer
---@param name string
---@return integer buf
local function pane_buffer(tab, name)
  return buf_window.create_scratch_buffer(string.format("vimficiency://stats/%d/%s", tab, name),
    { filetype = "vimficiency_stats" })
end

--- A new tab: the header across the top, the left page `width` wide and the
--- right one beside it, or below it when the screen is too narrow.
---@param width integer
---@return VF.Stats.ViewPanes
local function open_panes(width)
  vim.cmd.tabnew()
  local tab = v.nvim_get_current_tabpage()
  local placeholder = v.nvim_get_current_buf()
  local overview = { buf = pane_buffer(tab, "overview"), win = v.nvim_get_current_win() }
  v.nvim_win_set_buf(overview.win, overview.buf)
  v.nvim_buf_delete(placeholder, { force = true })

  local header_buf = pane_buffer(tab, "header")
  local header = { buf = header_buf,
    win = v.nvim_open_win(header_buf, false, { split = "above", win = overview.win, height = 1 }) }
  ui_windows.configure_scratch_window(header.win, { winfixheight = true })

  local side_by_side = vim.o.columns >= width + MIN_SIDE_WIDTH
  local commands_buf = pane_buffer(tab, "commands")
  local commands = { buf = commands_buf,
    win = v.nvim_open_win(commands_buf, false, { split = side_by_side and "right" or "below", win = overview.win }) }
  ui_windows.configure_scratch_window(overview.win, { cursorline = true, winfixwidth = side_by_side })
  ui_windows.configure_scratch_window(commands.win, { cursorline = true })
  v.nvim_win_resize(header.win, -1, 1)
  return { tab = tab, header = header, overview = overview, commands = commands, side_by_side = side_by_side }
end

function M.open()
  highlights.refresh()
  local saved_tab, saved_win = v.nvim_get_current_tabpage(), v.nvim_get_current_win()
  ---@type VF.Stats.ViewQuery
  local query = { current_only = false, weight = config.stats.weight, charted_size = 0 }
  local data = fetch(query)
  ---@type VF.Stats.View
  local view = {
    query = query,
    data = data,
    panes = open_panes(page_width(data.pages[1].lines)),
    saved_tab = saved_tab,
    saved_win = saved_win,
    waiting = false,
  }
  set_keymaps(view)
  close_with_any_pane(view)
  draw(view)
end

return M
