-- tests/lua/stats/view.lua
-- The `:Vimfy stats` tab over a real session log, kept to what outlives a
-- layout change: it opens and closes, the keys move, sort, page and re-query,
-- and the two formatters with logic of their own. What a drill lists is
-- tested in tests/Unit/Stats/, and on a real journal in sampling.lua.

local config = require("vimficiency.config")
local highlights = require("vimficiency.ui.highlights")
local sequence_display = require("vimficiency.ui.sequence_display")
local stats_query = require("vimficiency.stats.query")
local render = require("vimficiency.stats.render")
local stats_log = require("vimficiency.stats.log")
local stats_view = require("vimficiency.stats.view")
local h = require("_helpers")

local v = vim.api

---@param lines string[]
---@param needle string
local function assert_line(lines, needle)
  for _, line in ipairs(lines) do
    if line:find(needle, 1, true) then return end
  end
  error("no line has " .. needle .. " in\n" .. table.concat(lines, "\n"), 2)
end

---@param lines string[]
---@param needle string
local function assert_no_line(lines, needle)
  for _, line in ipairs(lines) do
    if line:find(needle, 1, true) then error("a line has " .. needle .. ": " .. line, 2) end
  end
end

local IDLE = { scoring = false }

---@type VF.Stats.Scope
local TOP = { current_only = false, weight = "effort", filter = {}, charted_size = 0 }

---@return VF.Stats.Summary
local function summary()
  return stats_query.summary(TOP)
end

---@param s VF.Stats.Summary
---@return string[] lines
local function build(s)
  return render.build_page(s, TOP, IDLE).lines
end

--- A summary with `n` sessions behind it, so the Sessions section is drawn.
---@param n integer
---@return VF.Stats.Summary
local function with_sessions(n)
  local s = summary()
  s.lifetime.sessions = n
  return s
end

---@param user_seq string
---@param best_seq string
---@param best_cost number
---@param epoch integer
local function finish(user_seq, best_seq, best_cost, epoch)
  stats_log.append("mark", h.finished_result(user_seq, {
    user_cost = 3.0,
    key_count = 2,
    finished_wall_epoch = epoch,
    commands = { { keys = user_seq, kind = "operator" } },
    optimal_results = { { seq = best_seq, cost = best_cost } },
  }))
end

---@return integer
local function two_days_ago_noon()
  local today = os.date("*t") --[[@as osdate]]
  return os.time({ year = today.year, month = today.month, day = today.day - 2, hour = 12 })
end

--- Twelve `dw` sessions now, against the optimizer's `de`, and three `x`
--- sessions two days ago that waste more.
local function seed()
  h.use_temp_data_home()
  for _ = 1, 12 do finish("dw", "de", 2.0, os.time()) end
  for _ = 1, 3 do finish("x", "x", 1.0, two_days_ago_noon()) end
end

---@alias VF.Test.StatsPane "overview"|"chart"|"runs"|"preview"

---@return table<VF.Test.StatsPane, { win: integer, buf: integer }>
local function panes()
  local out = {}
  for _, win in ipairs(v.nvim_tabpage_list_wins(0)) do
    local buf = v.nvim_win_get_buf(win)
    local name = v.nvim_buf_get_name(buf):match("^vimficiency://stats/%d+/(%a+)$")
    if name then out[name] = { win = win, buf = buf } end
  end
  return out
end

--- A pane's page: for the overview, what `render.build_page` drew.
---@param name VF.Test.StatsPane
---@return string[]
local function text(name)
  return v.nvim_buf_get_lines(panes()[name].buf, 0, -1, false)
end

local function header()
  return v.nvim_buf_get_lines(panes().overview.buf, 0, 1, false)[1]
end

local function footer()
  local lines = text("runs")
  return lines[#lines]
end

--- The run list float's title.
---@return string
local function runs_title()
  local title = v.nvim_win_get_config(panes().runs.win).title
  return assert(title)[1][1]
end

--- The 1-indexed byte column in the run list's header line that `name` starts at.
---@param name string
---@return integer
local function runs_column(name)
  return assert(text("runs")[1]:find(name, 1, true))
end

--- Presses `keys` in pane `name`; an error a keymap reports fails the test.
---@param name VF.Test.StatsPane
---@param keys string
local function press(name, keys)
  v.nvim_set_current_win(panes()[name].win)
  local errors = {}
  h.with_patch({ { vim, "notify", function(msg, level)
    if level == vim.log.levels.ERROR then errors[#errors + 1] = msg end
  end } }, function()
    h.feed(keys)
  end)
  assert_eq(errors, {}, "keymap errors")
end

---@param items VF.Stats.Item[]
---@param id string
---@return VF.Stats.Item
local function item(items, id)
  for _, it in ipairs(items) do
    if it.id == id then return it end
  end
  error("no item " .. id)
end

--- The item of the open view's stats the cursor is in.
---@return string|nil id
local function item_under_cursor()
  local row = v.nvim_win_get_cursor(panes().overview.win)[1] - 1
  local page = render.build_page(stats_query.summary(TOP), TOP, { scoring = false })
  for _, it in ipairs(page.items) do
    if row >= it.row and row <= it.last_row then return it.id end
  end
end

---@param name VF.Test.StatsPane
---@param it VF.Stats.Item
local function put_cursor_on(name, it)
  v.nvim_win_set_cursor(panes()[name].win, { it.cursor[1] + 1, it.cursor[2] })
end

local HEADER = " Stats    all scorings · weight: effort · runs of 8"

---@param fn fun()
local function in_view(fn)
  stats_view.open()
  local ok, err = pcall(fn)
  vim.cmd("silent! tabonly")
  if not ok then error(err, 0) end
end

test("stats pages: command rows show the ratio and the shares, and a merged row's split goes to its runs", function()
  h.use_temp_data_home()
  local s = with_sessions(1)
  local commands = s.sessions.commands
  commands.spans = 1
  commands.never = {
    { text = "ce|cw", ratio = math.huge, user = 12, optimizer = 0, user_share = 0.125, optimizer_share = 0,
      split = "ce 5 · cw 7", drill = {} },
  }
  commands.user_typed_chars = 40
  commands.optimizer_typed_chars = 30
  commands.more = {
    { text = "w", ratio = 2.5, user = 5, optimizer = 2, user_share = 0.05, optimizer_share = 0.2, split = "", drill = {} },
    { text = "<BS>", ratio = 12, user = 5, optimizer = 2, user_share = 0.24, optimizer_share = 0.02, split = "", drill = {} },
  }
  commands.less = {
    { text = "$", ratio = 0.004, user = 1, optimizer = 20, user_share = 0.0001, optimizer_share = 0.025,
      split = "", drill = {} },
  }
  local lines = build(s)
  assert_line(lines, "    Commands · 1 scored session")
  -- Each list is its own table, the numbers right-aligned.
  assert_line(lines, "w       2.50×    5.0% vs 20.0%")
  assert_line(lines, "<BS>   12.00×   24.0% vs  2.0%")
  assert_line(lines, "ce|cw   ∞×   12.5% vs 0.0%")
  assert_eq(item(render.build_page(s, TOP, IDLE).items, "sessions:ce|cw (you only)").label,
    "Sessions › ce|cw (you only) · ce 5 · cw 7")
  assert_line(lines, "$   <0.01×   <0.1% vs 2.5%")
  assert_no_line(lines, "… and")
  assert_line(lines, "Extra typed text: 33% (40 vs 30 chars)")
  -- A source with nothing recorded has no tables.
  assert_no_line(lines, "scored run")
end)

test("stats run list: the preview puts the text before and after side by side and marks what changed", function()
  local row = { id = { run = "journal" }, user_keys = "gjdw", best_seq = "", outcome = "no_result" }
  local detail = {
    lines = { "a", "\tb", "c" }, goal_lines = { "a", "\tB", "c", "d" }, keys = "jdw",
    start_row = 1, start_col = 1, end_row = 3, end_col = 1,
  }
  local preview
  h.with_patch({ { vim.o, "tabstop", 2 } }, function()
    ---@diagnostic disable-next-line: param-type-mismatch
    preview = render.run_preview(row, detail, nil, 44)
  end)
  local cell = string.rep(" ", 19)
  assert_eq(preview.lines, {
    -- The resolved keys, which parse into tokens.
    " you:   " .. sequence_display.inline("jdw"),
    " found: —",
    "",
    " before" .. string.rep(" ", 14) .. " │ after" .. string.rep(" ", 15),
    " a" .. cell .. " │ a" .. cell,
    "   b" .. string.rep(" ", 17) .. " │   B" .. string.rep(" ", 17),
    " c" .. cell .. " │ c" .. cell,
    " " .. cell .. "  │ d" .. cell,
  })
  local divider = 1 + 20
  local after = divider + #" │ "
  assert_eq(preview.marks, {
    { row = 5, col = 1, end_col = divider, hl = highlights.VIEW_DIFF_DELETE },
    { row = 5, col = after, end_col = after + 20, hl = highlights.VIEW_DIFF_ADD },
    -- The cursor starts on the `b`, past its tab, and ends past the `d`.
    { row = 5, col = 3, end_col = 4, hl = highlights.VIEW_CURSOR_START, priority = 200 },
    { row = 7, col = after, end_col = after + 20, hl = highlights.VIEW_DIFF_ADD },
    { row = 7, col = after + 1, end_col = after + 2, hl = highlights.VIEW_CURSOR_END, priority = 200 },
  })

  ---@diagnostic disable-next-line: param-type-mismatch
  assert_eq(render.run_preview(row, nil, "journal gone", 44).lines[4], " Text not available: journal gone")
end)

--- Opens the run list of the Commands row labelled `label`.
---@param label string
local function open_runs(label)
  local page = render.build_page(summary(), TOP, IDLE)
  put_cursor_on("overview", item(page.items, "sessions:" .. label))
  press("overview", "<CR>")
end

test("stats view: opens a tab with the stats in one full-width pane, and q returns", function()
  h.use_temp_data_home()
  local tab, win = v.nvim_get_current_tabpage(), v.nvim_get_current_win()
  stats_view.open()
  assert_true(v.nvim_get_current_tabpage() ~= tab, "a new tab")
  local p = panes()
  assert_eq(v.nvim_get_current_win(), p.overview.win)
  assert_eq(p.runs, nil)
  assert_eq(v.nvim_tabpage_list_wins(0), { p.overview.win })
  assert_eq(vim.bo[p.overview.buf].filetype, "vimficiency_stats")
  assert_eq(vim.bo[p.overview.buf].modifiable, false)
  assert_eq(v.nvim_win_get_width(p.overview.win), vim.o.columns)
  assert_eq(header(), HEADER)
  assert_eq(text("overview"), render.build_page(summary(), TOP, IDLE, vim.o.columns).lines)

  press("overview", "q")
  assert_eq(v.nvim_get_current_tabpage(), tab)
  assert_eq(v.nvim_get_current_win(), win)
  assert_eq(#v.nvim_list_tabpages(), 1)
end)

test("stats view: j and k move between the items, and stop at the ends", function()
  seed()
  in_view(function()
    local items = render.build_page(summary(), TOP, IDLE).items
    local win = panes().overview.win
    assert_eq(v.nvim_win_get_cursor(win), { items[1].cursor[1] + 1, items[1].cursor[2] })
    press("overview", "k")
    assert_eq(item_under_cursor(), items[1].id)
    press("overview", "j")
    assert_eq(item_under_cursor(), "chart:sessions")
    press("overview", "k")
    press("overview", "2j")
    assert_eq(item_under_cursor(), items[3].id)
    press("overview", "99j")
    assert_eq(item_under_cursor(), items[#items].id)
    press("overview", "k")
    assert_eq(item_under_cursor(), items[#items - 1].id)

    -- A cursor another motion left on plain text goes to the nearest item.
    -- The headless feed never idles, so CursorMoved is fired by hand.
    press("overview", "gg")
    v.nvim_exec_autocmds("CursorMoved", { buffer = panes().overview.buf })
    assert_eq(item_under_cursor(), items[1].id)
  end)
end)

test("stats view: a journal with only unsupported runs opens its details", function()
  h.use_temp_data_home()
  local s = summary()
  s.journal.present = true
  s.journal.unscored = { { outcome = "unsupported", runs = 3, drill = { outcomes = { "unsupported" } } } }
  h.with_patch({ { stats_query, "summary", function() return s end } }, function()
    in_view(function()
      put_cursor_on("overview", item(render.build_page(s, TOP, IDLE).items, "chart:journal"))
      press("overview", "<CR>")
      assert_line(text("chart"), "3 unsupported")
    end)
  end)
end)

test("stats view: outcome headings are skipped and categories open their own runs", function()
  h.use_temp_data_home()
  for _, keys in ipairs({ "p", "w", "x" }) do
    stats_log.append("mark", h.fake_result({
      user_seq = keys,
      commands = { { keys = keys, kind = "normal" } },
      optimal_results = {},
      unbeaten = keys ~= "x",
    }))
  end

  in_view(function()
    put_cursor_on("overview", item(render.build_page(summary(), TOP, IDLE).items, "chart:sessions"))
    press("overview", "<CR>")
    local chart = panes().chart
    local lines = text("chart")
    assert_line(lines, "Unbeaten (100% efficient): 2 sessions")
    assert_line(lines, "Not scored: 1 session")
    assert_true(#lines > v.nvim_win_get_height(chart.win), "details extend below the chart viewport")
    assert_eq(v.nvim_win_call(chart.win, function() return vim.fn.line("w0") end), 1)
    press("chart", "<C-d>")
    v.nvim_exec_autocmds("CursorMoved", { buffer = chart.buf })
    assert_true(v.nvim_win_call(chart.win, function() return vim.fn.line("w0") end) > 1, "the chart scrolled")
    assert_eq(v.nvim_win_call(chart.win, function() return vim.fn.line("w$") end), #lines)

    for row, line in ipairs(lines) do
      if line:find("1 paste", 1, true) then v.nvim_win_set_cursor(chart.win, { row, 8 }) end
    end
    press("chart", "j")
    assert_match(lines[v.nvim_win_get_cursor(panes().chart.win)[1]], "1 other")
    press("chart", "<CR>")
    assert_match(runs_title(), "unbeaten sessions · other")
    assert_eq(footer(), "runs 1–1 of 1")
    press("runs", "q")

    press("chart", "j")
    assert_match(lines[v.nvim_win_get_cursor(panes().chart.win)[1]], "1 no result")
    press("chart", "<CR>")
    assert_match(runs_title(), "sessions not scored · no result")
    assert_eq(footer(), "runs 1–1 of 1")
    press("runs", "q")

    press("chart", "3k")
    assert_eq(v.nvim_win_call(chart.win, function() return vim.fn.line("w0") end), 1,
      "moving back to a bucket reveals the top of the plot")
  end)
end)

test("stats view: s sorts by the current column, down, up, then off", function()
  seed()
  in_view(function()
    open_runs("dw (you only)")
    ---@param column string
    ---@param title string
    local function press_s_on(column, title)
      v.nvim_win_set_cursor(panes().runs.win, { 3, runs_column(column) - 1 })
      press("runs", "s")
      assert_match(runs_title(), title, column)
    end
    press_s_on("saved", "sort: saved ▼")
    assert_match(text("runs")[1], "saved ▼")
    press_s_on("saved", "sort: saved ▲")
    assert_match(text("runs")[1], "saved ▲")
    press_s_on("saved", "sort: worst")
    assert_true(not text("runs")[1]:find("▲", 1, true), "no column is marked")
    press_s_on("when", "sort: newest ▼")
    press_s_on("found", "sort: best_cost ▼")
    assert_eq(footer(), "runs 1–12 of 12")

    -- h and l change the column s sorts by.
    local preview = panes().preview.buf
    local tick = v.nvim_buf_get_changedtick(preview)
    press("runs", "l")
    assert_eq(v.nvim_buf_get_changedtick(preview), tick, "same run keeps its preview")
    press("runs", "s")
    assert_match(runs_title(), "sort: saved ▼")
    press("runs", "3h")
    press("runs", "s")
    assert_match(runs_title(), "sort: newest ▼")
  end)
end)

test("stats view: w and v re-query", function()
  seed()
  h.with_patch({ { config, "RESULTS_CALCULATED", config.RESULTS_CALCULATED + 1 } }, function()
    finish("dd", "dd", 3.0, os.time())
    in_view(function()
      assert_match(header(), "    all scorings · ")
      assert_no_line(text("overview"), "hidden")
      local effort_view = text("overview")

      press("overview", "w")
      assert_match(header(), "weight: uniform")
      assert_line(text("overview"), "62.5% average | 16 samples")
      press("overview", "w")
      assert_match(header(), "weight: effort")
      assert_eq(text("overview"), effort_view)

      press("overview", "v")
      assert_match(header(), "    " .. summary().scoring_label .. " · ")
      assert_line(text("overview"), "  15 runs from other scorings hidden (v)")
      press("overview", "v")
      assert_match(header(), "    all scorings · ")
    end)
  end)
end)

test("stats view: ] and [ page the run list and stop at its ends", function()
  h.use_temp_data_home()
  for i = 1, 60 do finish("dw", "de", 2.0, os.time() - i) end
  in_view(function()
    open_runs("dw (you only)")
    assert_eq(footer(), "runs 1–50 of 60")
    assert_match(runs_title(), "page 1/2")
    press("runs", "G")
    press("runs", "]")
    assert_eq(footer(), "runs 51–60 of 60")
    assert_match(runs_title(), "page 2/2")
    assert_eq(v.nvim_win_get_cursor(panes().runs.win), { 2, 1 })
    press("runs", "]")
    assert_eq(footer(), "runs 51–60 of 60")
    press("runs", "[")
    assert_eq(footer(), "runs 1–50 of 60")
    press("runs", "[")
    assert_eq(footer(), "runs 1–50 of 60")
  end)
end)
