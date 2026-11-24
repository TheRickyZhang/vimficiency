-- The `:Vimfy stats` tab: a header over two panes. At the top level the stats
-- (render.build_pages) fill both: Sessions and Journal on the left, Commands and
-- Dev on the right. Once narrowed, the left holds all of them and the right the
-- runs of the current filter. <CR> on a stats row or chart day pushes the
-- filter C++ attached to it; nothing here builds or edits a filter. <CR> on a
-- journal run replays it in `:Vimfy play`'s view.

---@class (exact) VF.StatsView
---@field open fun()
local M = {}

local v = vim.api
local buf_window = require("vimficiency.ui.buf_window")
local config = require("vimficiency.config")
local highlights = require("vimficiency.ui.highlights")
local sampling = require("vimficiency.stats.sampling")
local simulate = require("vimficiency.simulate")
local stats_query = require("vimficiency.stats.query")
local render = require("vimficiency.stats.render")
local ui_windows = require("vimficiency.ui.windows")
local util = require("vimficiency.util")

local PAGE_SIZE = 50
-- Any narrower beside the stats and the right pane goes below them instead.
local MIN_SIDE_WIDTH = 60
---@type table<VF.Stats.Order, VF.Stats.Order>
local NEXT_ORDER = { worst = "newest", newest = "slowest", slowest = "worst" }
local ns = v.nvim_create_namespace("vimfy_stats")

---@class VF.Stats.ViewQuery
---@field stack { filter: VF.Stats.Filter, label: string }[]   # top: the current filter
---@field current_only boolean
---@field weight VF.Stats.Weight
---@field charted_size integer   # 0: the middle window size
---@field order VF.Stats.Order
---@field offset integer

---@class VF.Stats.ViewData
---@field summary VF.Stats.Summary
---@field list? VF.Stats.RunList   # narrowed only
---@field pages VF.Stats.Page[]   # the left pane's, then at the top level the right pane's

---@class VF.Stats.Pane
---@field buf integer
---@field win integer

---@class VF.Stats.ViewPanes
---@field tab integer
---@field header VF.Stats.Pane
---@field overview VF.Stats.Pane   # left
---@field side integer           # the right window: `commands_buf` at the top level, else `runs_buf`
---@field commands_buf integer
---@field runs_buf integer
---@field side_by_side boolean

---@class VF.Stats.View
---@field query VF.Stats.ViewQuery
---@field data VF.Stats.ViewData
---@field panes VF.Stats.ViewPanes
---@field saved_tab integer
---@field saved_win integer
---@field waiting boolean   # a redraw is queued on the scoring run in flight

---@param query VF.Stats.ViewQuery
---@return boolean
local function narrowed(query)
  return #query.stack > 0
end

---@param query VF.Stats.ViewQuery
---@return VF.Stats.Scope
local function scope(query)
  local top = query.stack[#query.stack]
  return {
    current_only = query.current_only,
    weight = query.weight,
    filter = top and top.filter or {},
    charted_size = query.charted_size,
  }
end

---@param query VF.Stats.ViewQuery
---@return VF.Stats.ViewData
local function fetch(query)
  local summary = stats_query.summary(scope(query))
  local is_narrowed = narrowed(query)
  return {
    summary = summary,
    list = is_narrowed and stats_query.runs(scope(query), query.order, query.offset, PAGE_SIZE) or nil,
    pages = render.build_pages(summary, { scoring = sampling.running(), err = sampling.last_error() },
      query.weight, not is_narrowed),
  }
end

---@param list VF.Stats.RunList
---@return integer
local function listable(list)
  return list.total - list.rolled_up
end

---@param view VF.Stats.View
---@return string
local function header_line(view)
  local query, data = view.query, view.data
  local crumbs = { "Stats" }
  for _, entry in ipairs(query.stack) do crumbs[#crumbs + 1] = entry.label end
  local scoring = query.current_only and data.summary.scoring_label or "all scorings"
  local settings = { scoring, "weight: " .. query.weight, "runs of " .. data.summary.journal.charted_size }
  if data.list then
    settings[#settings + 1] = "sort: " .. query.order
    settings[#settings + 1] = string.format("page %d/%d", math.floor(query.offset / PAGE_SIZE) + 1,
      math.max(1, math.ceil(listable(data.list) / PAGE_SIZE)))
  end
  return string.format(" %s    %s", table.concat(crumbs, " › "), table.concat(settings, " · "))
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

---@param buf integer
---@param page VF.Stats.Page
local function draw_page(buf, page)
  set_lines(buf, page.lines)
  v.nvim_buf_clear_namespace(buf, ns, 0, -1)
  for _, row in ipairs(page.title_rows) do
    v.nvim_buf_set_extmark(buf, ns, row, 0, {
      end_row = row + 1, end_col = 0, hl_group = highlights.STATS_SECTION_TITLE, hl_eol = true,
    })
  end
  for _, target in ipairs(page.targets) do
    if not target.cols then
      local line = page.lines[target.row + 1]
      v.nvim_buf_set_extmark(buf, ns, target.row, line:find("%S") - 1, {
        end_col = #line, hl_group = highlights.STATS_DRILL,
      })
    end
  end
end

---@param win integer
---@param cursor integer[]
local function restore_cursor(win, cursor)
  v.nvim_win_set_cursor(win, { math.min(cursor[1], v.nvim_buf_line_count(v.nvim_win_get_buf(win))), cursor[2] })
end

local watch_scoring

---@param view VF.Stats.View
local function draw(view)
  local panes, data = view.panes, view.data
  local side_buf = data.list and panes.runs_buf or panes.commands_buf
  if v.nvim_win_get_buf(panes.side) ~= side_buf then
    v.nvim_win_set_buf(panes.side, side_buf)
    ui_windows.configure_scratch_window(panes.side, { cursorline = true })
  end
  local overview_cursor = v.nvim_win_get_cursor(panes.overview.win)
  local side_cursor = v.nvim_win_get_cursor(panes.side)
  set_lines(panes.header.buf, { header_line(view) })
  draw_page(panes.overview.buf, data.pages[1])
  if panes.side_by_side then
    v.nvim_win_resize(panes.overview.win, page_width(data.pages[1].lines), -1)
  end
  if data.list then
    set_lines(panes.runs_buf, render.run_lines(data.list, view.query.offset, v.nvim_win_get_width(panes.side)))
  else
    draw_page(panes.commands_buf, data.pages[2])
  end
  restore_cursor(panes.overview.win, overview_cursor)
  restore_cursor(panes.side, side_cursor)
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
    if v.nvim_tabpage_is_valid(view.panes.tab) then refresh(view) end
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
---@param offset integer
local function show_page(view, offset)
  if offset == view.query.offset then return end
  view.query.offset = offset
  refresh(view)
  v.nvim_win_set_cursor(view.panes.side, { 1, 0 })
end

---@param cost number
---@return string
local function format_cost(cost)
  return string.format("%.2f", cost)
end

--- Replays the journal run under the cursor in the run list against the best
--- sequence. A session keeps no text, so its keys and costs are echoed instead.
---@param view VF.Stats.View
local function open_run(view)
  local row = assert(view.data.list).runs[v.nvim_win_get_cursor(view.panes.side)[1] - 1]
  if not row then return end
  if row.id.run == "session" then
    local best = row.best_seq == "" and "—" or string.format("%s (%s)", row.best_seq, format_cost(row.best_cost))
    v.nvim_echo({ { string.format("you: %s (%s)  best: %s", row.user_keys, format_cost(row.user_cost), best) } },
      false, {})
    return
  end
  local detail, err = stats_query.run(row.id)
  if err then
    vim.notify("run not available: " .. err, vim.log.levels.WARN)
    return
  end
  ---@cast detail -nil
  simulate.simulate_compare(detail.lines, detail.start_row, detail.start_col, {
    user = { seq = detail.keys, cost = format_cost(row.user_cost) },
    suggestions = row.best_seq == "" and {} or { { seq = row.best_seq, cost = format_cost(row.best_cost) } },
  }, {
    label = string.format("%s · %d commands", os.date("%b %d %H:%M", row.wall_epoch), row.window_size),
    end_row = detail.end_row,
    end_col = detail.end_col,
    filetype = detail.filetype,
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

--- The panes only work together, so closing one closes the view.
---@param view VF.Stats.View
local function close_with_any_pane(view)
  local panes = view.panes
  local group = v.nvim_create_augroup("vimfy_stats_" .. panes.tab, {})
  for _, win in ipairs({ panes.header.win, panes.overview.win, panes.side }) do
    v.nvim_create_autocmd("WinClosed", {
      group = group,
      pattern = tostring(win),
      callback = function()
        v.nvim_del_augroup_by_id(group)
        -- `q` closes the tab itself; by the time this runs, it is gone.
        vim.schedule(function()
          if v.nvim_tabpage_is_valid(panes.tab) then close(view) end
          -- The right pane's buffers hide rather than wipe, so they can swap.
          for _, buf in ipairs({ panes.commands_buf, panes.runs_buf }) do
            if v.nvim_buf_is_valid(buf) then v.nvim_buf_delete(buf, { force = true }) end
          end
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
    { lhs = "<BS>", desc = "Back to the previous filter", handler = function()
      if not narrowed(query) then return end
      query.stack[#query.stack] = nil
      query.offset = 0
      refresh(view)
    end },
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
      query.offset = 0
      refresh(view)
    end },
    { lhs = "v", desc = "Toggle the scorings counted: all, current only", handler = function()
      query.current_only = not query.current_only
      query.offset = 0
      refresh(view)
    end },
    { lhs = "q", desc = "Close stats", handler = function() close(view) end },
  }
  ---@type VF.Keymap[]
  local list_keys = {
    { lhs = "s", desc = "Cycle the run order: worst, newest, slowest (once narrowed)", handler = function()
      if not narrowed(query) then return end
      query.order = NEXT_ORDER[query.order]
      query.offset = 0
      refresh(view)
    end },
    { lhs = "]", desc = "Next page of runs (once narrowed)", summary_group = "page",
      summary_desc = "Next/previous page of runs (once narrowed)",
      handler = function()
        if not view.data.list then return end
        local following = query.offset + PAGE_SIZE
        if following < listable(view.data.list) then show_page(view, following) end
      end },
    { lhs = "[", desc = "Previous page of runs (once narrowed)", summary_group = "page",
      handler = function()
        if not view.data.list then return end
        show_page(view, math.max(0, query.offset - PAGE_SIZE))
      end },
  }
  ---@param page_index integer
  ---@param win fun(): integer
  ---@return VF.Keymap
  local function drill_key(page_index, win)
    return { lhs = "<CR>", desc = "List the runs of the row or chart day under the cursor", handler = function()
      local target = target_at_cursor(view.data.pages[page_index], win())
      if not target then return end
      query.stack[#query.stack + 1] = { filter = target.drill, label = target.label }
      query.offset = 0
      refresh(view)
    end }
  end
  local opts = { title = "Vimfy Stats Keys" }
  local panes = view.panes
  local all = vim.list_extend(vim.deepcopy(view_keys), list_keys)
  ui_windows.set_buffer_keymaps(panes.overview.buf, ui_windows.with_standard_ui_keymaps(
    vim.list_extend({ drill_key(1, function() return panes.overview.win end) }, all), opts))
  ui_windows.set_buffer_keymaps(panes.commands_buf, ui_windows.with_standard_ui_keymaps(
    vim.list_extend({ drill_key(2, function() return panes.side end) }, view_keys), opts))
  ui_windows.set_buffer_keymaps(panes.header.buf, ui_windows.with_standard_ui_keymaps(all, opts))
  ui_windows.set_buffer_keymaps(panes.runs_buf, ui_windows.with_standard_ui_keymaps(vim.list_extend({
    { lhs = "<CR>", desc = "Replay the journal run under the cursor; echo a session's keys",
      handler = function() open_run(view) end },
  }, all), opts))
end

---@param tab integer
---@param name string
---@param options? table
---@return integer buf
local function pane_buffer(tab, name, options)
  return buf_window.create_scratch_buffer(string.format("vimficiency://stats/%d/%s", tab, name),
    vim.tbl_extend("force", { filetype = "vimficiency_stats" }, options or {}))
end

--- A new tab: the header across the top, the stats `width` wide on the left
--- and the right pane beside them, or below them when the screen is too narrow.
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
  local commands_buf = pane_buffer(tab, "commands", { bufhidden = "hide" })
  local runs_buf = pane_buffer(tab, "runs", { bufhidden = "hide" })
  local side = v.nvim_open_win(commands_buf, false, { split = side_by_side and "right" or "below", win = overview.win })
  ui_windows.configure_scratch_window(overview.win, { cursorline = true, winfixwidth = side_by_side })
  ui_windows.configure_scratch_window(side, { cursorline = true })
  v.nvim_win_resize(header.win, -1, 1)
  return {
    tab = tab, header = header, overview = overview, side = side,
    commands_buf = commands_buf, runs_buf = runs_buf, side_by_side = side_by_side,
  }
end

function M.open()
  highlights.refresh()
  local saved_tab, saved_win = v.nvim_get_current_tabpage(), v.nvim_get_current_win()
  ---@type VF.Stats.ViewQuery
  local query = { stack = {}, current_only = false, weight = config.stats.weight, charted_size = 0, order = "worst", offset = 0 }
  local data = fetch(query)
  ---@type VF.Stats.View
  local view = {
    query = query,
    data = data,
    -- Once narrowed, the left pane holds both pages' lines.
    panes = open_panes(math.max(page_width(data.pages[1].lines), page_width(data.pages[2].lines))),
    saved_tab = saved_tab,
    saved_win = saved_win,
    waiting = false,
  }
  set_keymaps(view)
  close_with_any_pane(view)
  draw(view)
end

return M
