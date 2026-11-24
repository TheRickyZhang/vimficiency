-- tests/lua/stats/view.lua
-- The `:Vimfy stats` tab over a real session log: what the stats page draws,
-- its items, the chart float, the run list float, and the keys that move
-- between items, re-sort, re-weight, switch the scorings counted and the run
-- length, and page. The reduction itself is tested in tests/Unit/Stats/.

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

---@param s string
---@param pattern string
local function assert_pattern(s, pattern)
  if not s:match(pattern) then error(vim.inspect(s) .. " does not match " .. vim.inspect(pattern), 2) end
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
  return render.build_page(s, IDLE).lines
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

-- The overview's first lines are the header; its page is under them.
local HEADER_ROWS = 2

--- A pane's page: for the overview, what `render.build_page` drew.
---@param name VF.Test.StatsPane
---@return string[]
local function text(name)
  return v.nvim_buf_get_lines(panes()[name].buf, name == "overview" and HEADER_ROWS or 0, -1, false)
end

local function header()
  return v.nvim_buf_get_lines(panes().overview.buf, 0, HEADER_ROWS, false)[1]
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
  local row = v.nvim_win_get_cursor(panes().overview.win)[1] - 1 - HEADER_ROWS
  local page = render.build_page(stats_query.summary(TOP), { scoring = false })
  for _, it in ipairs(page.items) do
    if row >= it.row and row <= it.last_row then return it.id end
  end
end

--- The text the open view highlights as its current item, a string per span.
---@param name? VF.Test.StatsPane
---@return string[]
local function highlighted(name)
  local buf = panes()[name or "overview"].buf
  local out = {}
  local marks = v.nvim_buf_get_extmarks(buf, v.nvim_get_namespaces().vimfy_item_nav, 0, -1, { details = true })
  for _, mark in ipairs(marks) do
    if mark[4].end_row > mark[2] then
      -- An item without spans highlights its whole lines.
      vim.list_extend(out, v.nvim_buf_get_lines(buf, mark[2], mark[4].end_row, false))
    else
      out[#out + 1] = v.nvim_buf_get_text(buf, mark[2], mark[3], mark[2], mark[4].end_col, {})[1]
    end
  end
  return out
end

--- The chart float's title, which names its current day.
---@return string
local function chart_title()
  return v.nvim_win_get_config(panes().chart.win).title[1][1]
end

---@param name VF.Test.StatsPane
---@param it VF.Stats.Item
local function put_cursor_on(name, it)
  local header_rows = name == "overview" and HEADER_ROWS or 0
  v.nvim_win_set_cursor(panes()[name].win, { it.cursor[1] + 1 + header_rows, it.cursor[2] })
end

local HEADER = " Stats    all scorings · weight: effort · runs of 8"

---@param fn fun()
local function in_view(fn)
  stats_view.open()
  local ok, err = pcall(fn)
  vim.cmd("silent! tabonly")
  if not ok then error(err, 0) end
end

test("stats pages: with no sessions, Sessions is one line", function()
  h.use_temp_data_home()
  local lines = build(summary())
  assert_line(lines, "  Sessions")
  assert_line(lines, "    No sessions yet: :Vimfy start, watch or recall to add some.")
  assert_no_line(lines, "Overall:")
  assert_no_line(lines, "Lifetime")
  assert_no_line(lines, "Efficiency")
  assert_line(lines, "No journal recorded")
end)

test("stats pages: Sessions holds the score, its chart's page the lifetime counts, and the hidden runs get a line", function()
  h.use_temp_data_home()
  local s = summary()
  s.lifetime = { sessions = 15, mark = 1, watch = 2, recall = 3, suggest = 9, commands = 30 }
  local lines = build(s)
  local counts = "    15 sessions  (mark 1 · watch 2 · recall 3 · suggest 9) · 30 commands"
  assert_no_line(lines, counts)
  assert_line(render.chart_page(s, "sessions", 80, 20).lines, counts)
  assert_line(lines, "    — average | 0 samples")
  assert_no_line(lines, "Scoring:")
  assert_no_line(lines, "hidden")
  s.hidden_runs = 4
  assert_line(build(s), "  4 runs from other scorings hidden (v)")
end)

test("stats pages: failed and unbeaten sessions are shown apart from the score", function()
  h.use_temp_data_home()
  local s = with_sessions(2)
  s.sessions.chart_score = { spans = 2, score = 87.5 }
  s.sessions.no_result = 1
  s.sessions.unbeaten = 1
  local lines = render.build_page(s, IDLE).lines
  assert_line(lines, "    87.5% average | 2 samples")
  assert_no_line(lines, "Not scored:")
  lines = render.chart_page(s, "sessions", 80, 20).lines
  assert_line(lines, "Not scored: 1 session with no solution found")
  assert_line(lines, "Unbeaten (100% efficient): 1 session")
end)

test("stats pages: command rows show the ratio and the shares, and a merged row's split goes to its runs", function()
  h.use_temp_data_home()
  local s = summary()
  s.commands.never = {
    { text = "ce|cw", ratio = math.huge, user = 12, optimizer = 0, user_share = 0.125, optimizer_share = 0,
      split = "ce 5 · cw 7", drill = {} },
  }
  s.commands.never_cut = 3
  s.commands.user_typed_chars = 40
  s.commands.optimizer_typed_chars = 30
  s.commands.more = {
    { text = "w", ratio = 2.5, user = 5, optimizer = 2, user_share = 0.05, optimizer_share = 0.2, split = "", drill = {} },
    { text = "<BS>", ratio = 12, user = 5, optimizer = 2, user_share = 0.24, optimizer_share = 0.02, split = "", drill = {} },
  }
  local lines = build(s)
  -- Each list is its own table, the numbers right-aligned.
  assert_line(lines, "w       2.50×    5.0% vs 20.0%")
  assert_line(lines, "<BS>   12.00×   24.0% vs  2.0%")
  assert_line(lines, "ce|cw   ∞×   12.5% vs 0.0%")
  assert_eq(item(render.build_page(s, IDLE).items, "ce|cw (you only)").label, "ce|cw (you only) · ce 5 · cw 7")
  assert_line(lines, "… and 3 more")
  assert_line(lines, "(no command is used 10 times yet)")
  assert_line(lines, "Extra typed text: 33% (40 vs 30 chars)")
end)

test("stats pages: dev timing is labeled as the last 30 days", function()
  h.use_temp_data_home()
  local s = summary()
  s.sessions.timing = { n = 3, p50 = 1, p95 = 2, p99 = 3, max = 4 }
  assert_line(build(s), "Optimizer time, last 30 days: p50 1.0 · p95 2.0 · p99 3.0 · max 4.0 ms  (n=3)")
end)

test("stats pages: the sections come in order, each title marked", function()
  h.use_temp_data_home()
  local page = render.build_page(summary(), IDLE, 60)
  local titles = {}
  for _, mark in ipairs(page.marks) do
    if mark.hl == highlights.STATS_SECTION_TITLE then titles[#titles + 1] = page.lines[mark.row + 1] end
  end
  assert_eq(titles, {
    string.rep(" ", 26) .. "Sessions",
    string.rep(" ", 26) .. "Journal",
    "  Underused commands",
    "  Overused commands",
  })
  local s = summary()
  s.sessions.timing = { n = 3, p50 = 1, p95 = 2, p99 = 3, max = 4 }
  assert_line(build(s), "  Dev")
end)

test("stats pages: every drillable row, each chart and the run length is an item", function()
  h.use_temp_data_home()
  local s = with_sessions(3)
  local function drill(tag) return { outcomes = { tag } } end
  local day = { days_ago = 1, score = 50, spans = 3, drill = { days = { first = 20412, last = 20412 } } }
  s.sessions.unbeaten, s.sessions.unbeaten_drill = 1, drill("unbeaten")
  s.sessions.no_result, s.sessions.no_result_drill = 2, drill("no_result")
  local beats = { outcomes = { "scored" }, beats = true }
  s.sessions.beats, s.sessions.beats_drill = 1, beats
  s.sessions.days = { day }
  s.journal = vim.tbl_extend("force", s.journal, {
    present = true, scored_bytes = 1, journal_bytes = 1, charted_size = 8,
    score = { spans = 3, score = 60 }, chart_score = { spans = 3, score = 60 },
    days = { day },
    sizes = { { size = 4, score = { spans = 1, score = 90 } }, { size = 8, score = { spans = 3, score = 60 } } },
    nav = { spans = 1, score = 100 }, nav_drill = { kind = "nav" },
    edit = { spans = 2, score = 40 }, edit_drill = { kind = "edit" },
    unscored = { { outcome = "too_large", runs = 1, drill = drill("too_large") } },
    unbeaten = 1, unbeaten_drill = { kind = "edit", outcomes = { "unbeaten" } },
  })
  local cw = { commands = { { tokens = { "cw" }, side = "either" } } }
  s.commands.more = { { text = "cw", ratio = 2, user = 8, optimizer = 4, user_share = 0.5, optimizer_share = 0.25, split = "", drill = cw } }
  s.commands.less = { { text = "w", ratio = 0.5, user = 4, optimizer = 8, user_share = 0.25, optimizer_share = 0.5, split = "", drill = {} } }
  local page = render.build_page(s, IDLE)

  -- A section's rows are on its chart's page; the stats keep the commands'.
  local rows = {
    { page, "cw (you more)", "cw", cw },
    { page, "w (you less)", "w ", {} },
    { render.chart_page(s, "sessions", 80, 30), "unbeaten sessions", "Unbeaten (100% efficient): 1 session", drill("unbeaten") },
    { render.chart_page(s, "sessions", 80, 30), "no-result sessions", "Not scored: 2 sessions", drill("no_result") },
    { render.chart_page(s, "sessions", 80, 30), "sessions that beat the optimizer",
      "You beat the optimizer: 1 session", beats },
    { render.chart_page(s, "journal", 80, 30), "nav", "Movement only:", { kind = "nav" } },
    { render.chart_page(s, "journal", 80, 30), "edit", "With edits:", { kind = "edit" } },
    { render.chart_page(s, "journal", 80, 30), "too large", "Not scored:     1 too large", drill("too_large") },
    { render.chart_page(s, "journal", 80, 30), "unbeaten runs", "Unbeaten (100% efficient): 1 run",
      { kind = "edit", outcomes = { "unbeaten" } } },
  }
  for _, expected in ipairs(rows) do
    local on, id = expected[1], expected[2]
    local it = item(on.items, id)
    assert_match(on.lines[it.row + 1], expected[3], id)
    assert_eq({ it.kind, it.drill, it.label }, { "drill", expected[4], id }, id)
    assert_eq(it.cursor, { it.row, 4 })
  end
  for _, it in ipairs(page.items) do
    assert_true(it.kind ~= "drill" or it.label:find("(you ", 1, true) ~= nil, it.id)
  end
  local journal = render.chart_page(s, "journal", 80, 30).lines
  assert_line(journal, "    Efficiency:     60.0%  (3 runs of 8 commands)")
  assert_line(journal, "    Scored under the current settings: 100% of the journal")
  assert_no_line(page.lines, "Scored under")
  local size = item(page.items, "run_length")
  local sizes = page.lines[size.row + 1]
  assert_eq(size.row, item(page.items, "chart:journal").last_row + 1)
  assert_eq(sizes, "    Run length: [4] [8]")
  assert_eq(size.selector, { values = { 4, 8 }, index = 2 })
  assert_eq(size.spans, nil)

  -- A chart lists nothing; a score of 50 fills half its five rows.
  local lines = page.lines
  local captions = {
    sessions = "         — average | 0 samples",
    journal = "       60.0% average | 3 samples",
  }
  for _, name in ipairs({ "sessions", "journal" }) do
    local chart = item(page.items, "chart:" .. name)
    assert_eq({ chart.kind, chart.chart, chart.drill }, { "chart", name, nil })
    assert_eq(chart.last_row - chart.row, 7)
    assert_eq(lines[chart.row + 1], "    Daily percent efficiency")
    local column = {}
    for row = chart.row + 1, chart.row + 6 do
      column[#column + 1] = vim.fn.strcharpart(lines[row + 1], 9 + 28, 1)
    end
    assert_eq(column, { " ", " ", "▄", "█", "█", "─" })
    assert_pattern(lines[chart.row + 2], "^    100 ┤")
    assert_pattern(lines[chart.row + 6], "^     20 ┤")
    assert_eq(lines[chart.last_row + 1], "         30d 25d  20d  15d  10d  5d  0d")
    assert_eq(lines[chart.row], captions[name])
  end
end)

test("stats pages: a chart on its own fills its float, and each day with data is an item", function()
  h.use_temp_data_home()
  local s = with_sessions(3)
  -- Local day 20412 is Nov 20 (2025).
  s.sessions.days = {
    { days_ago = 1, score = 50, spans = 1, drill = { days = { first = 20412, last = 20412 } } },
    { days_ago = 0, score = 100, spans = 4, drill = { days = { first = 20413, last = 20413 } } },
  }
  s.sessions.unbeaten, s.sessions.unbeaten_drill = 1, { outcomes = { "unbeaten" } }
  -- Under the chart: a blank line, the counts and the unbeaten sessions.
  local DETAILS = 3
  -- Between the axes 124 columns fit four per day, with four to spare.
  local page = render.chart_page(s, "sessions", 6 + 124 + 6, 12 + DETAILS)
  local lines = page.lines
  local day, today = item(page.items, "day:20412"), item(page.items, "day:20413")
  assert_eq({ day.kind, day.drill, day.label, day.score, day.samples },
    { "day", s.sessions.days[1].drill, "Nov 20", 50, 1 })
  assert_eq(#lines, 12 + DETAILS)
  assert_eq({ lines[9], lines[10], lines[11] }, {
    "",
    "    3 sessions  (mark 0 · watch 0 · recall 0 · suggest 0) · 0 commands",
    "    Unbeaten (100% efficient): 1 session",
  })
  -- The rows the stats leave out list their runs from here.
  local unbeaten = item(page.items, "unbeaten sessions")
  assert_eq({ unbeaten.kind, unbeaten.row, unbeaten.drill }, { "drill", 10, { outcomes = { "unbeaten" } } })

  -- Scores read against the left axis, sample counts against the right; the
  -- busiest day's four are the top of theirs.
  assert_eq(lines[1], "      ⠒⠒ efficiency %   ⠂⠂ samples per day")
  assert_pattern(lines[2], "^ 100 ┤.*├ 4$")
  assert_pattern(lines[6], "^  20 ┤ +│$")
  assert_eq(lines[7], "   0 └" .. string.rep("┬" .. string.rep("─", 19), 6) .. "┘ 0")
  assert_eq(vim.split(vim.trim(lines[8]), "%s+"), { "30d", "25d", "20d", "15d", "10d", "5d", "0d" })
  -- Both lines end at the top over today, and start from yesterday's 50 and
  -- 1; the counts' is dotted. A day's own value is one larger dot.
  assert_eq({
    vim.fn.strcharpart(lines[2], 6 + 28 * 4, 8), vim.fn.strcharpart(lines[3], 6 + 28 * 4, 8),
    vim.fn.strcharpart(lines[4], 6 + 28 * 4, 8), vim.fn.strcharpart(lines[5], 6 + 28 * 4, 8),
  }, { "    ⢀•  ", "  ⢀⢔⠉   ", " •⠡     ", " •      " })
  local hls = {}
  for _, mark in ipairs(page.marks) do
    if mark.row == 4 then hls[#hls + 1] = { lines[5]:sub(mark.col + 1, mark.end_col), mark.hl } end
  end
  assert_eq(hls, {
    { "•", highlights.STATS_VOLUME },
    { " 40", highlights.STATS_EFFICIENCY },
  })

  -- A day is its column in every row and the axis.
  assert_eq({ day.row, day.last_row }, { 1, 6 })
  assert_eq({ day.left, day.right, today.left }, { 6 + 28 * 4, 6 + 28 * 4 + 3, 6 + 29 * 4 })
  assert_eq(day.cursor, { 6, #"   0 └" + 28 * 4 * #"─" })
  local cells = {}
  for _, span in ipairs(day.spans) do cells[#cells + 1] = lines[span[1] + 1]:sub(span[2] + 1, span[3]) end
  assert_eq(cells, { "   ", "  ⢀", " •⠡", " • ", "   ", "───" })
  -- The most recent day is listed first.
  assert_eq(page.items[1].id, "day:20413")

  -- A tall chart fills its rows: 40, two per 5 points, a count at every fifth tick.
  lines = render.chart_page(s, "sessions", 127, 46 + DETAILS).lines
  for tick = 0, 19 do
    assert_pattern(lines[2 + tick * 2], string.format("^ %3d ┤.*%s$", 100 - tick * 5,
      tick % 5 == 0 and "├ " .. 4 - tick / 5 or "│"))
  end
  assert_pattern(lines[3], "^     │")
  assert_pattern(lines[42], "^   0 └┬")
  assert_eq(lines[46 + DETAILS], "")

  -- With no room, a day keeps one column and the axis its ends.
  page = render.chart_page(s, "sessions", 37, 7 + DETAILS)
  day = item(page.items, "day:20413")
  assert_eq({ day.left, day.right, #day.spans }, { 6 + 29, 6 + 30, 6 })
  assert_eq(page.lines[8], "      30d 25d  20d  15d  10d  5d  0d")

  -- In the stats the chart is one item.
  page = render.build_page(s, IDLE, 60)
  local chart = item(page.items, "chart:sessions")
  assert_eq(page.lines[chart.last_row], "      0 └" .. string.rep("┬────", 6))
  for _, it in ipairs(page.items) do assert_true(it.kind ~= "day", "no item for a day") end
end)

test("stats run list: every cell of a run is an item, a column wide", function()
  local row = { id = { run = "session", session = 0 }, wall_epoch = 0, user_cost = 3, best_cost = 2,
    outcome = "scored", analyze_ms = 1.5, user_typed_chars = 11, best_typed_chars = 2 }
  ---@diagnostic disable-next-line: missing-fields
  local lines = render.run_lines({ total = 2, rolled_up = 0, rollup_runs_without_commands = 0, runs = { row, row } }, 0)
  local items = render.run_items(lines, 2)
  assert_eq(#items, 12)
  local orders, cells = {}, {}
  for i = 7, 12 do
    local cell = items[i]
    orders[#orders + 1] = cell.order
    cells[#cells + 1] = lines[3]:sub(cell.spans[1][2] + 1, cell.spans[1][3])
    assert_eq({ cell.index, cell.row, cell.last_row, cell.cursor }, { 2, 2, 2, { 2, cell.spans[1][2] } })
  end
  assert_eq(orders, { "newest", "user_cost", "best_cost", "saved", "typed", "slowest" })
  -- The saving's minus sign is three bytes and one column.
  assert_eq(cells[2], "     3.0")
  assert_eq(cells[4], "      −33%")
  assert_eq(cells[5], "           +9")
  assert_eq(cells[6], "        1.5")
  assert_eq({ items[7].left, items[7].right, items[8].left }, { 1, 13, 15 })
  assert_eq(items[12].id, "2:slowest")
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
  local page = render.build_page(summary(), IDLE)
  put_cursor_on("overview", item(page.items, label))
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
  assert_eq(text("overview"), render.build_page(summary(), IDLE, vim.o.columns).lines)

  press("overview", "q")
  assert_eq(v.nvim_get_current_tabpage(), tab)
  assert_eq(v.nvim_get_current_win(), win)
  assert_eq(#v.nvim_list_tabpages(), 1)
end)

test("stats view: closing its window closes the view and its buffer", function()
  h.use_temp_data_home()
  local tab, win = v.nvim_get_current_tabpage(), v.nvim_get_current_win()
  stats_view.open()
  local prefix = "vimficiency://stats/" .. v.nvim_get_current_tabpage() .. "/"
  local buffers = {}
  for _, buf in ipairs(v.nvim_list_bufs()) do
    if vim.startswith(v.nvim_buf_get_name(buf), prefix) then buffers[#buffers + 1] = buf end
  end
  assert_eq(#buffers, 1)
  v.nvim_win_close(panes().overview.win, true)
  vim.wait(1000, function() return #v.nvim_list_tabpages() == 1 end)
  assert_eq(v.nvim_get_current_tabpage(), tab)
  assert_eq(v.nvim_get_current_win(), win)
  vim.wait(1000, function() return not v.nvim_buf_is_valid(buffers[1]) end)
  for _, buf in ipairs(buffers) do assert_eq(v.nvim_buf_is_valid(buf), false) end
end)

test("stats view: q still closes it after the tab it came from is gone", function()
  h.use_temp_data_home()
  stats_view.open()
  vim.cmd("silent tabonly")
  press("overview", "q")
  assert_eq(#v.nvim_list_tabpages(), 1)
  assert_eq(panes().overview, nil)
end)

test("stats view: look back switches the bucket size and the visible sample count", function()
  seed()
  in_view(function()
    local row = "    Look back: [minutes] [hours] [days] [weeks]"
    local function assert_selected(value)
      assert_eq(highlighted(), { row })
      local buf = panes().overview.buf
      local marks = v.nvim_buf_get_extmarks(buf, v.nvim_get_namespaces().vimfy_stats, 0, -1, { details = true })
      for _, mark in ipairs(marks) do
        if mark[4].hl_group == highlights.SETTINGS_ACTION_ACTIVE then
          assert_eq(row:sub(mark[3] + 1, mark[4].end_col), value)
          return
        end
      end
      error("Look back has no selected option")
    end

    assert_selected("[days]")
    press("overview", "2h")
    assert_line(text("overview"), "Minute percent efficiency")
    for ago = 30, 0, -5 do assert_line(text("overview"), ago .. "m") end
    assert_line(text("overview"), "average | 12 samples")
    assert_selected("[minutes]")
    press("overview", "3l")
    press("overview", "<Right>")
    assert_line(text("overview"), "Weekly percent efficiency")
    for ago = 30, 0, -5 do assert_line(text("overview"), ago .. "w") end
    assert_line(text("overview"), "average | 15 samples")
    assert_selected("[weeks]")
  end)
end)

test("stats view: j and k move between the items, and stop at the ends", function()
  seed()
  in_view(function()
    local items = render.build_page(summary(), IDLE).items
    local win = panes().overview.win
    assert_eq(v.nvim_win_get_cursor(win), { items[1].cursor[1] + 1 + HEADER_ROWS, items[1].cursor[2] })
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

test("stats view: with the room, the Commands sit beside the charts, and l and h cross between them", function()
  seed()
  h.with_patch({ { vim.o, "columns", 200 } }, function()
    in_view(function()
      local lines = text("overview")
      assert_pattern(lines[2], "^    Look back: .*Underused commands%s*$")
      for _, line in ipairs(lines) do assert_true(not vim.startswith(line, "  Underused"), line) end
      assert_true(vim.tbl_contains(lines, "  Dev"), "Dev stays under both")

      local selector = highlighted()
      assert_eq(#selector, 1)
      assert_eq(vim.trim(selector[1]), "Look back: [minutes] [hours] [days] [weeks]")

      -- The chart is highlighted over its column, no further.
      press("overview", "j")
      local chart = highlighted()
      assert_eq(#chart, 8)
      assert_true(vim.startswith(chart[1], "    Daily percent efficiency"), chart[1])
      assert_true(not chart[1]:find("Commands", 1, true), chart[1])

      -- l goes to the first command, wherever it is from.
      press("overview", "l")
      assert_eq(#highlighted(), 1)
      assert_true(vim.startswith(highlighted()[1], "    de "), highlighted()[1])
      press("overview", "j")
      assert_true(vim.startswith(highlighted()[1], "    dw "), highlighted()[1])
      press("overview", "l")
      assert_true(vim.startswith(highlighted()[1], "    dw "), highlighted()[1])

      -- h goes back to the chart, and j stays on its side.
      press("overview", "h")
      assert_eq(highlighted(), chart)
      press("overview", "99j")
      assert_true(not highlighted()[1]:find(" dw ", 1, true), highlighted()[1])

      -- The arrow keys are the same moves.
      press("overview", "<Right>")
      assert_true(vim.startswith(highlighted()[1], "    de "), highlighted()[1])
      press("overview", "<Down>")
      assert_true(vim.startswith(highlighted()[1], "    dw "), highlighted()[1])
      press("overview", "<Up>")
      assert_true(vim.startswith(highlighted()[1], "    de "), highlighted()[1])
      press("overview", "<Left>")
      assert_eq(highlighted(), chart)
    end)
  end)
end)

test("stats view: the current item is highlighted whole and the cursor hidden", function()
  seed()
  local HIDDEN = "VimficiencySettingsCursorHidden"
  -- Start outside any stats tab an earlier test left open.
  vim.cmd.tabnew()
  vim.cmd("silent! tabonly")
  assert_true(not vim.o.guicursor:find(HIDDEN, 1, true), "shown before")
  in_view(function()
    local page = render.build_page(summary(), IDLE, vim.o.columns)
    press("overview", "j")
    local selected = item(page.items, "chart:sessions")
    local chart = {}
    for row = selected.row, selected.last_row do
      chart[#chart + 1] = page.lines[row + 1]
    end
    assert_eq(highlighted(), chart)
    assert_match(vim.o.guicursor, HIDDEN)

    press("overview", "j")
    assert_eq(#highlighted(), 1)
    assert_true(vim.startswith(highlighted()[1], "    de "), highlighted()[1])

    -- The run list hides it too, once: leaving either puts it back.
    press("overview", "<CR>")
    assert_eq(select(2, vim.o.guicursor:gsub(HIDDEN, "")), 1)
    press("runs", "<Esc>")
    assert_eq(select(2, vim.o.guicursor:gsub(HIDDEN, "")), 1)
    press("overview", "q")
    assert_true(not vim.o.guicursor:find(HIDDEN, 1, true), "shown once closed")
  end)
end)

test("stats view: <CR> on a chart floats it over the whole editor, h and l move between its days", function()
  seed()
  in_view(function()
    local page = render.build_page(summary(), IDLE, vim.o.columns)
    local t = two_days_ago_noon()
    local earlier = os.date("%b ", t) .. tonumber(os.date("%d", t))
    local today = os.date("%b ") .. tonumber(os.date("%d"))
    put_cursor_on("overview", item(page.items, "chart:sessions"))

    press("overview", "<CR>")
    local p = panes()
    assert_eq(v.nvim_get_current_win(), p.chart.win)
    local float = v.nvim_win_get_config(p.chart.win)
    assert_eq({ float.relative, float.width, float.height },
      { "editor", vim.o.columns - 2, vim.o.lines - vim.o.cmdheight - 4 })
    assert_eq(text("overview"), page.lines)
    -- The plot and labels fit the float with whole rows and columns to spare.
    local lines = text("chart")
    assert_eq(#lines, float.height)
    assert_pattern(lines[2], "^ 100 ┤")
    local baseline
    for row, line in ipairs(lines) do
      if vim.startswith(line, "   0 └") then baseline = row end
    end
    assert_true(baseline ~= nil)
    local day_width = math.floor((float.width - 12) / 30)
    assert_eq(lines[baseline], "   0 └" .. string.rep("┬" .. string.rep("─", day_width * 5 - 1), 6) .. "┘ 0")

    -- The most recent day is current: its whole column under the legend,
    -- named in the title.
    assert_eq(chart_title(), " Stats › Sessions · " .. today .. ": 66.7% · 12 samples ")
    assert_eq(#highlighted("chart"), baseline - 1)
    press("chart", "l")
    assert_match(chart_title(), " · " .. today .. ": ")
    press("chart", "h")
    assert_match(chart_title(), " Stats › Sessions · " .. earlier .. ": ")
    press("chart", "h")
    assert_match(chart_title(), " · " .. earlier .. ": ")
    press("chart", "<Right>")
    assert_match(chart_title(), " · " .. today .. ": ")
    press("chart", "<Left>")

    -- A day's runs float over the chart, and closing them comes back to it.
    press("chart", "<CR>")
    assert_eq(runs_title(), " Stats › " .. earlier .. " · sort: worst · page 1/1 ")
    assert_eq(footer(), "runs 1–3 of 3")
    press("runs", "<Esc>")
    assert_eq(v.nvim_get_current_win(), p.chart.win)
    assert_match(chart_title(), " · " .. earlier .. ": ")

    press("chart", "<Esc>")
    assert_eq(panes().chart, nil)
    assert_eq(v.nvim_get_current_win(), p.overview.win)
    assert_eq(item_under_cursor(), "chart:sessions")
    press("overview", "<CR>")
    press("chart", "q")
    assert_eq(panes().chart, nil)
  end)
end)

test("stats view: <CR> on a row floats its runs over the whole width, and <Esc> or q closes them", function()
  seed()
  in_view(function()
    local page = render.build_page(summary(), IDLE, vim.o.columns)
    local label = "dw (you only)"
    local row = item(page.items, label)

    for _, key in ipairs({ "<Esc>", "q" }) do
      put_cursor_on("overview", row)
      press("overview", "<CR>")
      local p = panes()
      assert_eq(v.nvim_get_current_win(), p.runs.win)
      local list, preview = v.nvim_win_get_config(p.runs.win), v.nvim_win_get_config(p.preview.win)
      assert_eq(list.relative, "editor")
      assert_eq({ list.width, preview.width }, { vim.o.columns - 2, vim.o.columns - 2 })
      assert_true(preview.row > list.row + list.height, "the preview is under the list")
      assert_eq(runs_title(), " Stats › " .. label .. " · sort: worst · page 1/1 ")
      assert_eq(footer(), "runs 1–12 of 12")
      assert_eq(v.nvim_win_get_cursor(p.runs.win), { 2, 1 })
      -- The list takes two fifths of the rows, the preview the rest.
      assert_true(preview.height > list.height, "the preview is the taller")
      assert_eq(header(), HEADER)
      assert_eq(text("overview"), page.lines)

      press("runs", key)
      assert_eq(panes().runs, nil, key)
      assert_eq(panes().preview, nil, key)
      assert_eq(v.nvim_get_current_win(), panes().overview.win)
    end
  end)
end)

test("stats view: <CR> on a command row lists the runs using it, a column each", function()
  seed()
  in_view(function()
    open_runs("dw (you only)")
    assert_eq(runs_title(), " Stats › dw (you only) · sort: worst · page 1/1 ")
    assert_eq(footer(), "runs 1–12 of 12")
    local runs = text("runs")
    assert_pattern(runs[1], "^ when%s+you%s+found%s+saved%s+extra typed%s+search ms$")
    for i = 2, 13 do
      assert_pattern(runs[i], "^ %a%a%a %d%d %d%d:%d%d%s+3%.0%s+2%.0%s+−33%%%s+0%s+%d+%.%d$")
      assert_eq(vim.fn.strdisplaywidth(runs[i]), vim.fn.strdisplaywidth(runs[1]))
    end
  end)
end)

test("stats view: the preview caps a run's keys at two indented lines, and <CR> on a session opens nothing", function()
  h.use_temp_data_home()
  local long = "ciw" .. string.rep("a", 200) .. "<Esc>" .. string.rep("w", 12)
  finish(long, "de", 2.0, os.time())
  in_view(function()
    open_runs("w (you only)")
    local preview = text("preview")
    local width = v.nvim_win_get_width(panes().preview.win)
    assert_eq(#preview, 5)
    assert_eq(preview[1], " you:   ciw")
    assert_pattern(preview[2], "^        a+…$")
    assert_eq(vim.fn.strdisplaywidth(preview[2]), width - 1)
    assert_eq({ preview[3], preview[4], preview[5] }, { " found: de", "", " A session keeps no text." })
    local tabs = #v.nvim_list_tabpages()
    press("runs", "<CR>")
    assert_eq(#v.nvim_list_tabpages(), tabs)
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
    press("runs", "l")
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

test("stats view: the run list moves by cell, a row or a column at a time", function()
  seed()
  in_view(function()
    open_runs("dw (you only)")
    local win, buf = panes().runs.win, panes().runs.buf
    ---@return string
    local function cell()
      local mark = v.nvim_buf_get_extmarks(buf, v.nvim_get_namespaces().vimfy_item_nav, 0, -1, { details = true })[1]
      return v.nvim_buf_get_text(buf, mark[2], mark[3], mark[2], mark[4].end_col, {})[1]
    end
    assert_eq(v.nvim_win_get_cursor(win), { 2, 1 })
    assert_pattern(cell(), "^%a%a%a %d%d %d%d:%d%d$")
    press("runs", "l")
    assert_eq({ v.nvim_win_get_cursor(win), cell() }, { { 2, 15 }, "     3.0" })
    press("runs", "2j")
    assert_eq(v.nvim_win_get_cursor(win), { 4, 15 })
    press("runs", "l")
    press("runs", "k")
    assert_eq({ v.nvim_win_get_cursor(win)[1], cell() }, { 3, "     2.0" })
    press("runs", "99k")
    assert_eq(v.nvim_win_get_cursor(win)[1], 2)
    press("runs", "99h")
    assert_eq(v.nvim_win_get_cursor(win), { 2, 1 })
    assert_eq(footer(), "runs 1–12 of 12")
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

