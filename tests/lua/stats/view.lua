-- tests/lua/stats/view.lua
-- The `:Vimfy stats` tab over a real session log: what the stats pages draw,
-- their drill targets, the two layouts, and the keys that narrow, re-sort,
-- re-weight, switch the scorings counted and the run length, and page. The reduction itself is tested in
-- tests/Unit/Stats/.

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

--- Every section's lines, both pages together.
---@param s VF.Stats.Summary
---@return string[] lines
local function build(s)
  local pages = render.build_pages(s, IDLE, "effort")
  return vim.list_extend(pages[1].lines, pages[2].lines)
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

---@alias VF.Test.StatsPane "header"|"overview"|"commands"|"runs"|"preview"

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

---@param name VF.Test.StatsPane
---@return string[]
local function text(name)
  return v.nvim_buf_get_lines(panes()[name].buf, 0, -1, false)
end

local function header()
  return text("header")[1]
end

local function footer()
  local lines = text("runs")
  return lines[#lines]
end

--- The run list float's title.
---@return string
local function runs_title()
  local title = v.nvim_win_get_config(panes().runs.win).title
  return title[1][1]
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

---@param targets VF.Stats.DrillTarget[]
---@param label string
---@param chart boolean
---@return VF.Stats.DrillTarget
local function target(targets, label, chart)
  for _, t in ipairs(targets) do
    if t.label == label and (t.cols ~= nil) == chart then return t end
  end
  error("no target " .. label)
end

---@param name VF.Test.StatsPane
---@param t VF.Stats.DrillTarget
local function put_cursor_on(name, t)
  v.nvim_win_set_cursor(panes()[name].win, { t.row + 1, t.cols and t.cols[1] or 0 })
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
  assert_no_line(lines, "Score:")
  assert_no_line(lines, "Lifetime")
  assert_no_line(lines, "Efficiency")
  assert_line(lines, "No journal recorded")
end)

test("stats pages: Sessions holds the lifetime counts, and the hidden runs get a line", function()
  h.use_temp_data_home()
  local s = summary()
  s.lifetime = { sessions = 15, mark = 1, watch = 2, recall = 3, suggest = 9, commands = 30 }
  local lines = build(s)
  assert_line(lines, "    15 sessions  (mark 1 · watch 2 · recall 3 · suggest 9) · 30 commands")
  assert_line(lines, "    Score:    — / 100  (0 scored sessions, weighted by effort)")
  assert_no_line(lines, "Scoring:")
  assert_no_line(lines, "hidden")
  s.hidden_runs = 4
  assert_line(build(s), "  4 runs from other scorings hidden (v)")
end)

test("stats pages: failed and unbeaten sessions are shown apart from the score", function()
  h.use_temp_data_home()
  local s = with_sessions(2)
  s.sessions.score = { spans = 2, score = 87.5 }
  s.sessions.no_result = 1
  s.sessions.unbeaten = 1
  local lines = render.build_pages(s, IDLE, "uniform")[1].lines
  assert_line(lines, "Score:    87.5 / 100  (2 scored sessions, weighted by uniform)")
  assert_line(lines, "Not scored: 1 session with no solution found")
  assert_line(lines, "Unbeaten (scored 100): 1 session")
end)

test("stats pages: command rows show the ratio, the counts and a merged row's split", function()
  h.use_temp_data_home()
  local s = summary()
  s.commands.never = {
    { text = "ce|cw", ratio = math.huge, user = 12, optimizer = 0, split = "ce 5 · cw 7", drill = {} },
  }
  s.commands.never_cut = 3
  s.commands.sessions = 3
  s.commands.runs = 2
  s.commands.user_typed_chars = 40
  s.commands.optimizer_typed_chars = 30
  local lines = build(s)
  assert_line(lines, "ce|cw        ∞×   (12 vs 0)   ce 5 · cw 7")
  assert_line(lines, "… and 3 more")
  assert_line(lines, "(no command is used 10 times yet)")
  assert_line(lines, "From 3 scored sessions and 2 journal runs of 8 commands.")
  assert_line(lines, "Characters typed in insert mode: you 40 · optimizer 30")
end)

test("stats pages: dev timing is labeled as the last 30 days", function()
  h.use_temp_data_home()
  local s = summary()
  s.sessions.timing = { n = 3, p50 = 1, p95 = 2, p99 = 3, max = 4 }
  assert_line(build(s), "Optimizer time, last 30 days: p50 1.0 · p95 2.0 · p99 3.0 · max 4.0 ms  (n=3)")
end)

test("stats pages: Sessions and Journal come first and Commands and Dev second", function()
  h.use_temp_data_home()
  local s = summary()
  local pages = render.build_pages(s, IDLE, "effort")
  assert_eq(#pages, 2)
  assert_line(pages[1].lines, "  Sessions")
  assert_line(pages[1].lines, "  Journal")
  assert_no_line(pages[1].lines, "Commands —")
  assert_no_line(pages[1].lines, "  Dev")
  assert_line(pages[2].lines, "  Commands — you use MORE than the optimizer")
  assert_line(pages[2].lines, "  Dev")
  assert_no_line(pages[2].lines, "  Sessions")
end)

test("stats pages: every drillable row and chart day is a target carrying its drill", function()
  h.use_temp_data_home()
  local s = with_sessions(3)
  local function drill(tag) return { outcomes = { tag } } end
  local day = { days_ago = 1, score = 50, drill = { days = { first = 20412, last = 20412 } } }
  s.sessions.unbeaten, s.sessions.unbeaten_drill = 1, drill("unbeaten")
  s.sessions.no_result, s.sessions.no_result_drill = 2, drill("no_result")
  s.sessions.days = { day }
  s.journal = vim.tbl_extend("force", s.journal, {
    present = true, scored_bytes = 1, journal_bytes = 1, charted_size = 8,
    score = { spans = 3, score = 60 },
    days = { day },
    sizes = { { size = 4, score = { spans = 1, score = 90 } }, { size = 8, score = { spans = 3, score = 60 } } },
    nav = { spans = 1, score = 100 }, nav_drill = { kind = "nav" },
    edit = { spans = 2, score = 40 }, edit_drill = { kind = "edit" },
    unscored = { { outcome = "too_large", runs = 1, drill = drill("too_large") } },
    unbeaten = 1, unbeaten_drill = { kind = "edit", outcomes = { "unbeaten" } },
  })
  local cw = { commands = { { tokens = { "cw" }, side = "either" } } }
  s.commands.more = { { text = "cw", ratio = 2, user = 8, optimizer = 4, split = "", drill = cw } }
  s.commands.less = { { text = "w", ratio = 0.5, user = 4, optimizer = 8, split = "", drill = {} } }
  local pages = render.build_pages(s, IDLE, "effort")

  local rows = {
    { 1, "unbeaten sessions", "Unbeaten (scored 100): 1 session", drill("unbeaten") },
    { 1, "no-result sessions", "Not scored: 2 sessions", drill("no_result") },
    { 1, "nav", "Movement only:", { kind = "nav" } },
    { 1, "edit", "With edits:", { kind = "edit" } },
    { 1, "too large", "Not scored:     1 too large", drill("too_large") },
    { 1, "unbeaten runs", "Unbeaten (scored 100): 1 run", { kind = "edit", outcomes = { "unbeaten" } } },
    { 2, "cw (you more)", "cw", cw },
    { 2, "w (you less)", "w ", {} },
  }
  for _, expected in ipairs(rows) do
    local page = pages[expected[1]]
    local t = target(page.targets, expected[2], false)
    assert_match(page.lines[t.row + 1], expected[3], expected[2])
    assert_eq(t.drill, expected[4], expected[2])
  end
  -- Run length is a setting, not a row.
  assert_line(pages[1].lines, "    Run length: 4 (90.0) · [8] (60.0)   l to switch")
  for _, t in ipairs(pages[1].targets) do
    assert_true(not pages[1].lines[t.row + 1]:find("Run length", 1, true), "run length is no target")
  end

  -- Local day 20412 is Nov 20 (2025); both charts put it one column left of today.
  local lines = pages[1].lines
  local chart = {}
  for _, t in ipairs(pages[1].targets) do
    if t.cols then
      assert_eq(t.label, "Nov 20")
      assert_eq(t.drill, day.drill)
      chart[#chart + 1] = lines[t.row + 1]:sub(t.cols[1] + 1, t.cols[2])
      assert_eq(vim.fn.strdisplaywidth(lines[t.row + 1]:sub(1, t.cols[1])), 9 + 28)
    end
  end
  local column = { " ", " ", "█", "█", "─" }
  assert_eq(chart, vim.list_extend(vim.deepcopy(column), column))
  local axis = string.rep(" ", 9) .. "30d" .. string.rep(" ", 22) .. "today"
  local titles, axes = 0, 0
  for i, line in ipairs(lines) do
    if line == "    Daily score" then
      titles = titles + 1
      assert_pattern(lines[i + 1], "^    100 ┤")
    end
    if line == axis then axes = axes + 1 end
  end
  assert_eq({ titles, axes }, { 2, 2 })
end)

test("stats run list: each column owns its width and the gap after it", function()
  assert_eq(render.run_column_at(1), nil)
  assert_eq(render.run_column_at(2), "newest")
  assert_eq(render.run_column_at(15), "newest")
  assert_eq(render.run_column_at(16), "user_cost")
  assert_eq(render.run_column_at(26), "best_cost")
  assert_eq(render.run_column_at(36), "saved")
  assert_eq(render.run_column_at(48), "slowest")
  assert_eq(render.run_column_at(61), nil)
end)

test("stats run list: the preview puts the text before and after side by side and marks what changed", function()
  local row = { id = { run = "journal" }, user_keys = "dw", best_seq = "", outcome = "no_result" }
  local detail = {
    lines = { "a", "b", "c" }, goal_lines = { "a", "B", "c", "d" },
    start_row = 0, start_col = 0, end_row = 3, end_col = 0,
  }
  ---@diagnostic disable-next-line: param-type-mismatch
  local preview = render.run_preview(row, detail, nil, 44)
  local cell = string.rep(" ", 19)
  assert_eq(preview.lines, {
    " you:   " .. sequence_display.inline("dw"),
    " found: —",
    "",
    " before · cursor 1:1  │ after · cursor 4:1",
    " a" .. cell .. " │ a",
    " b" .. cell .. " │ B",
    " c" .. cell .. " │ c",
    " " .. cell .. "  │ d",
  })
  local divider = 1 + 20
  local after = divider + #" │ "
  assert_eq(preview.marks, {
    { row = 5, col = 1, end_col = divider, hl = highlights.VIEW_DIFF_DELETE },
    { row = 5, col = after, end_col = after + 1, hl = highlights.VIEW_DIFF_ADD },
    { row = 7, col = after, end_col = after + 1, hl = highlights.VIEW_DIFF_ADD },
  })

  ---@diagnostic disable-next-line: param-type-mismatch
  assert_eq(render.run_preview(row, nil, "journal gone", 44).lines[4], " Text not available: journal gone")
end)

--- Opens the run list of the Commands row labelled `label`.
---@param label string
local function open_runs(label)
  local pages = render.build_pages(summary(), IDLE, "effort")
  put_cursor_on("commands", target(pages[2].targets, label, false))
  press("commands", "<CR>")
end

test("stats view: opens a tab with the stats in two panes, and q returns", function()
  h.use_temp_data_home()
  local tab, win = v.nvim_get_current_tabpage(), v.nvim_get_current_win()
  stats_view.open()
  assert_true(v.nvim_get_current_tabpage() ~= tab, "a new tab")
  local p = panes()
  assert_eq(v.nvim_get_current_win(), p.overview.win)
  assert_eq(p.runs, nil)
  for _, name in ipairs({ "header", "overview", "commands" }) do
    assert_eq(vim.bo[p[name].buf].filetype, "vimficiency_stats", name)
    assert_eq(vim.bo[p[name].buf].modifiable, false, name)
  end
  assert_eq(header(), HEADER)
  local pages = render.build_pages(summary(), IDLE, "effort")
  assert_eq(text("overview"), pages[1].lines)
  assert_eq(text("commands"), pages[2].lines)

  press("commands", "q")
  assert_eq(v.nvim_get_current_tabpage(), tab)
  assert_eq(v.nvim_get_current_win(), win)
  assert_eq(#v.nvim_list_tabpages(), 1)
end)

test("stats view: closing one pane closes the view and its buffers", function()
  h.use_temp_data_home()
  local tab, win = v.nvim_get_current_tabpage(), v.nvim_get_current_win()
  stats_view.open()
  local prefix = "vimficiency://stats/" .. v.nvim_get_current_tabpage() .. "/"
  local buffers = {}
  for _, buf in ipairs(v.nvim_list_bufs()) do
    if vim.startswith(v.nvim_buf_get_name(buf), prefix) then buffers[#buffers + 1] = buf end
  end
  assert_eq(#buffers, 3)
  v.nvim_win_close(panes().commands.win, true)
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
  press("commands", "q")
  assert_eq(#v.nvim_list_tabpages(), 1)
  assert_eq(panes().commands, nil)
end)

test("stats view: the two stat columns sit side by side when they fit, else stacked", function()
  seed()
  h.with_patch({ { vim.o, "columns", 200 } }, function()
    in_view(function()
      local p = panes()
      assert_eq(v.nvim_win_get_position(p.commands.win)[1], v.nvim_win_get_position(p.overview.win)[1])
      assert_true(v.nvim_win_get_position(p.commands.win)[2] > 0, "commands on the right")
      local widest = 0
      for _, line in ipairs(text("overview")) do widest = math.max(widest, vim.fn.strdisplaywidth(line)) end
      assert_eq(v.nvim_win_get_width(p.overview.win), widest + 1)
      assert_eq(v.nvim_win_get_height(p.header.win), 1)
    end)
  end)
  h.with_patch({ { vim.o, "columns", 60 } }, function()
    in_view(function()
      local p = panes()
      assert_true(v.nvim_win_get_position(p.commands.win)[1] > v.nvim_win_get_position(p.overview.win)[1],
        "commands below")
    end)
  end)
end)

test("stats view: <CR> on a chart day floats its runs over the whole width, and <Esc> or q closes them", function()
  seed()
  in_view(function()
    local pages = render.build_pages(summary(), IDLE, "effort")
    local t = two_days_ago_noon()
    local label = os.date("%b ", t) .. tonumber(os.date("%d", t))
    local day = target(pages[1].targets, label, true)

    -- A column with no data lists nothing.
    v.nvim_win_set_cursor(panes().overview.win, { day.row + 1, day.cols[1] - 1 })
    press("overview", "<CR>")
    assert_eq(panes().runs, nil)

    for _, key in ipairs({ "<Esc>", "q" }) do
      put_cursor_on("overview", day)
      press("overview", "<CR>")
      local p = panes()
      assert_eq(v.nvim_get_current_win(), p.runs.win)
      local list, preview = v.nvim_win_get_config(p.runs.win), v.nvim_win_get_config(p.preview.win)
      assert_eq(list.relative, "editor")
      assert_eq({ list.width, preview.width }, { vim.o.columns - 2, vim.o.columns - 2 })
      assert_true(preview.row > list.row + list.height, "the preview is under the list")
      assert_eq(runs_title(), " Stats › " .. label .. " · sort: worst · page 1/1 ")
      assert_eq(footer(), "runs 1–3 of 3")
      assert_eq(v.nvim_win_get_cursor(p.runs.win), { 2, 0 })
      assert_eq(header(), HEADER)
      assert_eq(text("overview"), pages[1].lines)
      assert_eq(text("commands"), pages[2].lines)

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
    assert_pattern(runs[1], "^ when%s+you%s+found%s+saved%s+search ms$")
    for i = 2, 13 do
      assert_pattern(runs[i], "^ %a%a%a %d%d %d%d:%d%d%s+3%.0%s+2%.0%s+−33%%%s+%d+%.%d$")
      assert_eq(vim.fn.strdisplaywidth(runs[i]), vim.fn.strdisplaywidth(runs[1]))
    end
  end)
end)

test("stats view: the preview shows the run's keys in full, and <CR> on a session opens nothing", function()
  h.use_temp_data_home()
  local long = "ciw" .. string.rep("a", 200) .. "<Esc>" .. string.rep("w", 12)
  finish(long, "de", 2.0, os.time())
  in_view(function()
    open_runs("w (you only)")
    assert_eq(text("preview"), {
      " you:   " .. sequence_display.inline(long),
      " found: " .. sequence_display.inline("de"),
      "",
      " A session keeps no text.",
    })
    assert_eq(vim.wo[panes().preview.win].wrap, true)
    local tabs = #v.nvim_list_tabpages()
    press("runs", "<CR>")
    assert_eq(#v.nvim_list_tabpages(), tabs)
  end)
end)

test("stats view: s sorts by the column under the cursor, down, up, then off", function()
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

    -- The margin belongs to no column.
    v.nvim_win_set_cursor(panes().runs.win, { 3, 0 })
    press("runs", "s")
    assert_match(runs_title(), "sort: best_cost ▼")
  end)
end)

test("stats view: w and v re-query", function()
  seed()
  h.with_patch({ { config, "RESULTS_CALCULATED", config.RESULTS_CALCULATED + 1 } }, function()
    finish("dd", "dd", 3.0, os.time())
    in_view(function()
      assert_match(header(), "    all scorings · ")
      assert_no_line(text("overview"), "hidden")

      press("header", "w")
      assert_match(header(), "weight: uniform")
      assert_line(text("overview"), "weighted by uniform")
      press("header", "w")
      assert_line(text("overview"), "weighted by effort")

      press("overview", "v")
      assert_match(header(), "    " .. summary().scoring_label .. " · ")
      assert_line(text("overview"), "  15 runs from other scorings hidden (v)")
      press("overview", "v")
      assert_match(header(), "    all scorings · ")
    end)
  end)
end)

test("stats view: l cycles the charted run length, and in the run list only moves the cursor", function()
  seed()
  in_view(function()
    assert_eq(header(), HEADER)
    press("overview", "l")
    assert_match(header(), "runs of 16")
    press("commands", "l")
    assert_match(header(), "runs of 4")

    open_runs("dw (you only)")
    press("runs", "l")
    assert_eq(v.nvim_win_get_cursor(panes().runs.win), { 2, 1 })
    assert_match(header(), "runs of 4")
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
    assert_eq(v.nvim_win_get_cursor(panes().runs.win), { 1, 0 })
    press("runs", "]")
    assert_eq(footer(), "runs 51–60 of 60")
    press("runs", "[")
    assert_eq(footer(), "runs 1–50 of 60")
    press("runs", "[")
    assert_eq(footer(), "runs 1–50 of 60")
  end)
end)

