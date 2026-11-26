-- The runs behind one stats row: a floating list over the stats tab, each
-- run's cells items (ui/item_nav.lua), with a preview of the current run in a
-- second float below it.

---@class (exact) VF.Stats.RunListModule
---@field PAGE_SIZE integer
---@field open fun(scope: VF.Stats.Scope, label: string, order?: VF.Stats.Order): VF.Stats.RunListView
local M = {}

local v = vim.api
local archive = require("vimficiency.session.archive")
local buf_window = require("vimficiency.ui.buf_window")
local item_nav = require("vimficiency.ui.item_nav")
local playback = require("vimficiency.session.playback")
local render = require("vimficiency.stats.render")
local stats_query = require("vimficiency.stats.query")
local ui_windows = require("vimficiency.ui.windows")
local util = require("vimficiency.util")

M.PAGE_SIZE = 50
local ns = v.nvim_create_namespace("vimfy_stats_runs")

---@class VF.Stats.RunListView
---@field scope VF.Stats.Scope
---@field label string
---@field sort? VF.Stats.RunSort
---@field offset integer
---@field list VF.Stats.RunList
---@field items VF.Stats.RunCell[]   # the listed runs' cells
---@field nav VF.Ui.ItemNav
---@field list_buf integer
---@field list_win integer
---@field preview_buf integer
---@field preview_win integer
---@field preview_row? VF.Stats.RunRow
---@field preview_width? integer
---@field details table<VF.Stats.RunRow, { detail?: VF.Session.Viewable, err?: string }>   # the listed runs looked up so far
---@field refresh fun()   # re-queries, as after a scoring run
---@field close fun()

---@param view VF.Stats.RunListView
---@return integer
local function listable(view)
  return view.list.total - view.list.rolled_up
end

---@param view VF.Stats.RunListView
---@return string
local function title(view)
  local sort = view.sort
  local parts = {
    "Stats › " .. view.label,
    "sort: " .. (sort and string.format("%s %s", sort.order, sort.ascending and "▲" or "▼") or "worst"),
    string.format("page %d/%d", math.floor(view.offset / M.PAGE_SIZE) + 1,
      math.max(1, math.ceil(listable(view) / M.PAGE_SIZE))),
  }
  return " " .. table.concat(parts, " · ") .. " "
end

---@param buf integer
---@param lines string[]
local function set_lines(buf, lines)
  vim.bo[buf].modifiable = true
  v.nvim_buf_set_lines(buf, 0, -1, false, lines)
  vim.bo[buf].modifiable = false
end

--- The run whose cell is current.
---@param view VF.Stats.RunListView
---@return VF.Stats.RunRow|nil
local function current_row(view)
  local cell = view.nav.current() --[[@as VF.Stats.RunCell|nil]]
  return cell and view.list.runs[cell.index]
end

--- A listed run's text, looked up once.
---@param view VF.Stats.RunListView
---@param row VF.Stats.RunRow
---@return VF.Session.Viewable|nil detail
---@return string|nil err
local function detail_of(view, row)
  if not view.details[row] then
    local detail, err = stats_query.run(row)
    view.details[row] = { detail = detail, err = err }
  end
  return view.details[row].detail, view.details[row].err
end

---@param view VF.Stats.RunListView
local function draw_preview(view)
  local row = current_row(view)
  local width = v.nvim_win_get_width(view.preview_win)
  if view.preview_row == row and view.preview_width == width then return end
  v.nvim_buf_clear_namespace(view.preview_buf, ns, 0, -1)
  if not row then
    set_lines(view.preview_buf, {})
    view.preview_row, view.preview_width = nil, width
    return
  end
  local detail, err = detail_of(view, row)
  local preview = render.run_preview(row, detail, err, width)
  set_lines(view.preview_buf, preview.lines)
  for _, mark in ipairs(preview.marks) do
    v.nvim_buf_set_extmark(view.preview_buf, ns, mark.row, mark.col,
      { end_col = mark.end_col, hl_group = mark.hl, priority = mark.priority })
  end
  view.preview_row, view.preview_width = row, width
end

---@param view VF.Stats.RunListView
local function draw(view)
  local lines, items = render.run_table(view.list, view.offset, v.nvim_win_get_width(view.list_win), view.sort)
  set_lines(view.list_buf, lines)
  view.items = items
  v.nvim_win_set_config(view.list_win, { title = title(view) })
  -- The current cell draws the preview; with no runs there is none.
  view.nav.sync()
  if #view.items == 0 then draw_preview(view) end
end

---@param view VF.Stats.RunListView
local function refresh(view)
  local sort = view.sort
  view.list = stats_query.runs(view.scope, sort and sort.order or "worst", sort ~= nil and sort.ascending,
    view.offset, M.PAGE_SIZE)
  view.details = {}
  draw(view)
end

--- Sorts by the current cell's column: largest first, then smallest first,
--- then back to the worst first.
---@param view VF.Stats.RunListView
local function cycle_sort(view)
  local cell = view.nav.current() --[[@as VF.Stats.RunCell|nil]]
  if not cell then return end
  local order, sort = cell.order, view.sort
  if not sort or sort.order ~= order then
    view.sort = { order = order, ascending = false }
  elseif not sort.ascending then
    sort.ascending = true
  else
    view.sort = nil
  end
  view.offset = 0
  refresh(view)
end

---@param view VF.Stats.RunListView
---@param offset integer
local function show_page(view, offset)
  if offset == view.offset then return end
  local cell = view.nav.current() --[[@as VF.Stats.RunCell]]
  view.offset = offset
  refresh(view)
  view.nav.focus("1:" .. cell.order)
end

--- Replays the current run beside what was found for it.
---@param view VF.Stats.RunListView
local function open_run(view)
  local row = current_row(view)
  if not row then return end
  local detail, err = detail_of(view, row)
  if not detail then
    vim.notify("run not available: " .. err, vim.log.levels.WARN)
    return
  end
  playback.view(detail, render.run_name(row))
end

--- Pins the current run's saved session under a filename asked for.
---@param view VF.Stats.RunListView
local function pin_run(view)
  local row = current_row(view)
  if not row then return end
  if row.saved_filename == "" then
    vim.notify("only a saved session can be pinned", vim.log.levels.WARN)
    return
  end
  vim.ui.input({ prompt = "Pin as: ", default = row.saved_filename }, util.entry(function(input)
    if not input or input == "" then return end
    archive.pin(row.saved_filename, input)
  end))
end

---@param view VF.Stats.RunListView
local function set_keymaps(view)
  local function return_to_list()
    v.nvim_set_current_win(view.list_win)
  end
  ui_windows.set_buffer_keymaps(view.preview_buf, {
    { lhs = "<C-s>", desc = "Return to the selected run", handler = return_to_list },
    { lhs = "q", desc = "Return to the selected run", handler = return_to_list },
    { lhs = "<Esc>", desc = "Return to the selected run", handler = return_to_list },
  })
  ---@type VF.Keymap[]
  local keys = {
    { lhs = "<C-s>", desc = "Focus the run preview", handler = function() v.nvim_set_current_win(view.preview_win) end },
    { lhs = "s", desc = "Sort by the current column: descending, ascending, off",
      handler = function() cycle_sort(view) end },
    { lhs = "p", desc = "Pin the current session", handler = function() pin_run(view) end },
    { lhs = "]", desc = "Next page of runs", summary_group = "page", summary_desc = "Next/previous page of runs",
      handler = function()
        local following = view.offset + M.PAGE_SIZE
        if following < listable(view) then show_page(view, following) end
      end },
    { lhs = "[", desc = "Previous page of runs", summary_group = "page",
      handler = function() show_page(view, math.max(0, view.offset - M.PAGE_SIZE)) end },
    { lhs = "<Esc>", desc = "Back to the stats", summary_group = "close", summary_desc = "Back to the stats",
      nowait = true, handler = view.close },
    { lhs = "q", desc = "Back to the stats", summary_group = "close", nowait = true, handler = view.close },
  }
  ui_windows.set_buffer_keymaps(view.list_buf, ui_windows.with_standard_ui_keymaps(
    vim.list_extend(vim.deepcopy(view.nav.keymaps), keys), { title = "Vimfy Stats Run Keys" }))
end

-- The share of the two floats' rows the list takes; the preview's text is
-- what explains a run.
local LIST_SHARE = 0.4

--- The list over the top of the editor and the preview under it, both its
--- full width.
---@param view VF.Stats.RunListView
local function open_windows(view)
  local tab = v.nvim_get_current_tabpage()
  -- Each float adds a border row above and below; the tabline row stays clear.
  local rows = vim.o.lines - vim.o.cmdheight - 2
  local list_height = math.max(3, math.floor((rows - 4) * LIST_SHARE))
  local width = vim.o.columns - 2

  ---@param name string
  ---@return integer
  local function buffer(name)
    return buf_window.create_scratch_buffer(string.format("vimficiency://stats/%d/%s", tab, name),
      { filetype = "vimficiency_stats" })
  end
  ---@param buf integer
  ---@param enter boolean
  ---@param row integer
  ---@param height integer
  ---@param window_options table
  ---@return integer
  local function float(buf, enter, row, height, window_options)
    local win = v.nvim_open_win(buf, enter, {
      relative = "editor", row = row, col = 0, width = width, height = height, zindex = 49,
      border = "rounded", style = "minimal", title = "", title_pos = "left",
    })
    ui_windows.configure_scratch_window(win, window_options)
    return win
  end
  view.preview_buf = buffer("preview")
  view.preview_win = float(view.preview_buf, false, 1 + list_height + 2, math.max(1, rows - 4 - list_height), {})
  view.list_buf = buffer("runs")
  view.list_win = float(view.list_buf, true, 1, list_height, {})
end

---@param scope VF.Stats.Scope   # the runs to list
---@param label string           # what the stats row called them
---@param order? VF.Stats.Order
---@return VF.Stats.RunListView
function M.open(scope, label, order)
  local origin = v.nvim_get_current_win()
  ---@type VF.Stats.RunListView
  ---@diagnostic disable-next-line: missing-fields
  local view = { scope = scope, label = label, sort = order and { order = order, ascending = false },
    offset = 0, details = {}, items = {} }
  view.refresh = function() refresh(view) end
  view.close = function()
    if v.nvim_win_is_valid(view.list_win) then v.nvim_win_close(view.list_win, true) end
    if v.nvim_win_is_valid(origin) then v.nvim_set_current_win(origin) end
  end
  open_windows(view)
  view.nav = item_nav.attach(view.list_buf, {
    win = function() return view.list_win end,
    items = function() return view.items end,
    enter = function() open_run(view) end,
    focused = function() draw_preview(view) end,
  })
  set_keymaps(view)
  local group = v.nvim_create_augroup("vimfy_stats_runs_" .. view.list_win, {})
  v.nvim_create_autocmd("WinResized", {
    group = group,
    callback = util.entry(function()
      if vim.list_contains(vim.v.event.windows, view.list_win) then draw(view) end
      if vim.list_contains(vim.v.event.windows, view.preview_win) then draw_preview(view) end
    end),
  })
  v.nvim_create_autocmd("WinClosed", {
    group = group,
    pattern = { tostring(view.list_win), tostring(view.preview_win), tostring(origin) },
    callback = util.entry(function(event)
      v.nvim_del_augroup_by_id(group)
      local closing = tonumber(event.match)
      for _, win in ipairs({ view.preview_win, view.list_win }) do
        if win ~= closing and v.nvim_win_is_valid(win) then v.nvim_win_close(win, true) end
      end
    end),
  })
  refresh(view)
  return view
end

return M
