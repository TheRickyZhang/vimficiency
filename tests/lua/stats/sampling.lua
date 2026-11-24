-- The Journal section of `:Vimfy stats`, and end-to-end runs of the real
-- `vimfy_journal` over a journal written here, read back through the summary.

local config    = require("vimficiency.config")
local journal   = require("vimficiency.capture.journal")
local ffi_core  = require("vimficiency.ffi.core")
local ffi_stats = require("vimficiency.ffi.stats")
local sampling  = require("vimficiency.stats.sampling")
local stats_query = require("vimficiency.stats.query")
local render = require("vimficiency.stats.render")
local stats_log = require("vimficiency.stats.log")
local stats_view = require("vimficiency.stats.view")
local h         = require("_helpers")

---@param lines string[]
---@param needle string
---@return string|nil
local function line_with(lines, needle)
  for _, line in ipairs(lines) do
    if line:find(needle, 1, true) then return line end
  end
end

--- The open stats view's pane `name` (overview, chart, runs or preview).
---@param name string
---@return integer win
---@return string[] lines
local function stats_pane(name)
  for _, win in ipairs(vim.api.nvim_tabpage_list_wins(0)) do
    local buf = vim.api.nvim_win_get_buf(win)
    if vim.api.nvim_buf_get_name(buf):match("^vimficiency://stats/%d+/" .. name .. "$") then
      return win, vim.api.nvim_buf_get_lines(buf, 0, -1, false)
    end
  end
  error("no stats pane " .. name)
end

--- The open stats view's header: the first line of its overview.
---@return string
local function stats_header()
  return select(2, stats_pane("overview"))[1]
end

--- Presses <CR> on the line of pane `name` holding `text`.
---@param name string
---@param text string
local function enter_line(name, text)
  local win, lines = stats_pane(name)
  vim.api.nvim_set_current_win(win)
  for row, line in ipairs(lines) do
    if line:find(text, 1, true) then
      vim.api.nvim_win_set_cursor(win, { row, 4 })
      h.feed("<CR>")
      return
    end
  end
  error("no line with " .. text .. " in " .. name)
end

local JOURNAL_ROWS = { nav = "Movement only:", edit = "With edits:" }

--- From the stats, opens the journal's chart and lists the runs of its row
--- `kind`. <Esc> twice comes back to the stats.
---@param kind "nav"|"edit"
local function open_journal_runs(kind)
  enter_line("overview", "Daily percent efficiency")
  enter_line("chart", JOURNAL_ROWS[kind])
end

--- Runs a refresh to completion.
---@return string|nil err
local function refresh()
  local done, err = false, nil
  sampling.refresh(function(e) done, err = true, e end)
  assert_true(vim.wait(30000, function() return done end), "refresh finished")
  return err
end

---@type VF.Stats.Scope
local TOP = { current_only = false, weight = "effort", filter = {}, charted_size = 0 }

---@return VF.Stats.JournalSummary
local function journal_summary()
  return stats_query.summary(TOP).journal
end

---@param summary VF.Stats.Summary
---@param background VF.Stats.Background
---@return VF.Stats.Page
local function page_of(summary, background)
  return render.build_page(summary, background)
end

--- Journals `count` single-`l` atoms over one line, one tick each, after a
--- resync at the start of the line.
---@param count integer
---@param filetype? string
local function journal_moves(count, filetype)
  local buf = h.new_buf({ "alpha beta gamma delta epsilon", "second line" })
  vim.bo[buf].filetype = filetype or ""
  vim.wait(5, function() return false end)
  journal.start()
  local atom = { keys = "l", typed = "l", kind = "motion", buf = buf, win = vim.api.nvim_get_current_win(), lossy = false }
  journal._for_test.resync()
  for col = 1, count do
    journal._for_test.close_tick()
    vim.api.nvim_win_set_cursor(0, { 1, col })
    journal._for_test.listener(atom, "record")
  end
  journal.stop()
  vim.api.nvim_buf_delete(buf, { force = true })
end

--- Lines of a summary with no sessions and the journal fields overridden.
---@param fields table
---@param background? VF.Stats.Background
---@return string[] lines     # the stats
---@return string[] details   # the journal chart's own page
local function lines_with_journal(fields, background)
  h.use_temp_data_home()
  local summary = stats_query.summary(TOP)
  summary.journal = vim.tbl_extend("force", summary.journal,
    { present = true, scored_bytes = 100, journal_bytes = 100 }, fields)
  return page_of(summary, background or { scoring = false }).lines, render.chart_page(summary, "journal", 80, 30).lines
end

test("stats journal section: shows progress and never waits for scoring", function()
  h.use_temp_data_home()
  assert_true(line_with(page_of(stats_query.summary(TOP), { scoring = false }).lines,
    "journal = { enabled = true }") ~= nil)
  local lines = lines_with_journal({ scored_bytes = 25 }, { scoring = true })
  assert_true(line_with(lines, "Scoring journal…") ~= nil)
  assert_eq(line_with(lines, "average |"), nil)
  assert_eq(line_with(lines, "No scored runs"), nil)
  -- Scoring that needs nothing is not in the stats.
  assert_eq(line_with(lines_with_journal({ scored_bytes = 25 }), "of the journal"), nil)
  assert_true(line_with((lines_with_journal({}, { scoring = false, err = "boom" })),
    "Last background scoring failed: boom") ~= nil)
  h.with_patch({ { config.journal.sampling, "auto", false } }, function()
    assert_true(line_with((lines_with_journal({ scored_bytes = 0 })),
      ":Vimfy stats refresh scores the rest") ~= nil)
  end)
end)

test("stats journal section: charts one size and lists every size on one line", function()
  local lines, details = lines_with_journal({
    charted_size = 8,
    sizes = {
      { size = 4, score = { spans = 1, score = 90 } },
      { size = 8, score = { spans = 2, score = 75 } },
      { size = 16, score = { spans = 0, score = 0 } },
    },
    score = { spans = 2, score = 75 }, chart_score = { spans = 2, score = 75 },
    nav = { spans = 1, score = 100 },
    edit = { spans = 1, score = 50 },
    days = { { days_ago = 0, score = 75, spans = 2, drill = { days = { first = 20000, last = 20000 } } } },
    unscored = { { outcome = "too_large", runs = 1, drill = {} }, { outcome = "no_result", runs = 2, drill = {} } },
    deadline_hit = 1,
  })
  assert_true(line_with(lines, "    Run length: [4] [8] [16]") ~= nil)
  assert_true(line_with(lines, "    Daily percent efficiency") ~= nil)
  assert_true(line_with(lines, "    75.0% average | 2 samples") ~= nil)
  assert_eq(line_with(lines, "By run length"), nil)
  -- The rest of the section is on the chart's own page.
  local rest = {
    "Efficiency:     75.0%  (2 runs of 8 commands)",
    "Movement only:  100.0% (1 run)",
    "With edits:     50.0% (1 run)",
    "Not scored:     1 too large",
    "                2 no result",
    "Search cut short by the deadline: 1 run",
  }
  for _, text in ipairs(rest) do
    assert_eq(line_with(lines, text), nil, text)
    assert_true(line_with(details, text) ~= nil, text)
  end
end)

test("sampling.refresh: nothing to do without a journal directory", function()
  h.use_temp_data_home()
  assert_eq(refresh(), nil)
  local j = journal_summary()
  assert_eq(j.present, false)
  assert_eq(j.score.spans, 0)
end)

test("sampling.refresh: a failed launch releases the refresh for retry", function()
  h.use_temp_data_home()
  journal_moves(4)
  h.with_patch({ { vim, "system", function() error("sampler unavailable", 0) end } }, function()
    assert_eq(refresh(), "sampler unavailable")
    assert_true(not sampling.running())
  end)
  assert_eq(refresh(), nil)
end)

test("sampling.refresh: scores a journal written by this Neovim, once", function()
  h.use_temp_data_home()
  journal_moves(6)

  h.with_patch({ { config.journal.sampling, "window_sizes", { 2 } } }, function()
    assert_eq(refresh(), nil)

    local j = journal_summary()
    assert_true(j.journal_bytes > 0)
    assert_eq(j.scored_bytes, j.journal_bytes)
    assert_eq(j.charted_size, 2)
    assert_eq(j.score.spans, 3)
    assert_eq(j.nav.spans, 3)
    assert_eq(stats_query.summary(TOP).commands.runs, 3)
    local lock = journal.dir() .. "/sampler.lock"
    local inode = assert(vim.uv.fs_stat(lock)).ino
    assert_eq(refresh(), nil)
    assert_eq(assert(vim.uv.fs_stat(lock)).ino, inode)
    assert_eq(journal_summary().score.spans, 3)
  end)
end)

test("ffi.stats.run: a listed journal run is cut again as it was scored", function()
  h.use_temp_data_home()
  journal_moves(6)

  h.with_patch({ { config.journal.sampling, "window_sizes", { 2 } } }, function()
    assert_eq(refresh(), nil)
    local list = ffi_stats.runs({
      scope = {
        session_log = stats_log.path(),
        stamp = stats_log.stamp(),
        current_only = false,
        journal_dir = journal.dir(),
        sample_params = sampling.params(),
        weight = "effort",
        filter = { source = "journal" },
        charted_size = 0,
      },
      order = "newest",
      offset = 0,
      limit = 10,
    })
    assert_eq(#list.runs, 3)
    for _, row in ipairs(list.runs) do
      local run = assert(ffi_stats.run({ id = row.id, journal_dir = journal.dir(), sample_params = sampling.params() }))
      assert_eq(run.lines, { "alpha beta gamma delta epsilon", "second line" })
      assert_eq(run.keys, row.user_keys)
      assert_eq(run.keys, "ll")
      assert_eq(run.typed, "ll")
      assert_eq({ run.start_row, run.end_row, run.end_col - run.start_col }, { 0, 0, 2 })
    end

    local run, err = ffi_stats.run({
      id = { run = "session", session = 0 }, journal_dir = journal.dir(), sample_params = sampling.params(),
    })
    assert_eq({ run, err }, { nil, "a session run has no journal window" })
  end)
end)

test("stats view: narrows to the movement-only journal runs", function()
  h.use_temp_data_home()
  journal_moves(6)

  h.with_patch({ { config.journal.sampling, "window_sizes", { 2 } } }, function()
    assert_eq(refresh(), nil)
    stats_view.open()
    local runs
    for _, kind in ipairs({ "nav", "edit" }) do
      open_journal_runs(kind)
      local runs_win
      runs_win, runs = stats_pane("runs")
      local title = vim.api.nvim_win_get_config(runs_win).title[1][1]
      assert_true(title:find("Stats › " .. kind, 1, true) ~= nil, title)
      assert_eq(runs[#runs], kind == "nav" and "runs 1–3 of 3" or "no runs to list of 0")
      local _, preview = stats_pane("preview")
      if kind == "nav" then
        assert_eq(preview[1], " you:   l l")
        assert_true(line_with(preview, " before ") ~= nil, "the text before")
        assert_true(line_with(preview, " │ after ") ~= nil, "the text after")
      else
        assert_eq(preview, { "" })
      end
      h.feed("<Esc>")
      h.feed("<Esc>")
    end
    vim.cmd("silent! tabonly")
  end)
end)

--- Opens the stats view over a scored six-move journal, narrowed to its nav
--- runs, and runs `fn` with the runs window and the rows it lists.
---@param fn fun(runs_win: integer, rows: VF.Stats.RunRow[])
local function in_nav_runs(fn)
  h.use_temp_data_home()
  journal_moves(6, "lua")
  h.with_patch({ { config.journal.sampling, "window_sizes", { 2 } } }, function()
    assert_eq(refresh(), nil)
    local nav_drill = stats_query.summary(TOP).journal.nav_drill
    stats_view.open()
    open_journal_runs("nav")
    local rows = stats_query.runs({ current_only = false, weight = "effort", filter = nav_drill, charted_size = 0 }, "worst", false, 0, 50).runs
    assert_eq(#rows, 3)
    local runs_win = stats_pane("runs")
    vim.api.nvim_set_current_win(runs_win)
    local ok, err = pcall(fn, runs_win, rows)
    vim.cmd("silent! tabonly")
    if not ok then error(err, 0) end
  end)
end

---@param tokens VF.Sequence.Token[]
---@return string
local function token_text(tokens)
  local parts = {}
  for i, token in ipairs(tokens) do parts[i] = token.text end
  return table.concat(parts)
end

test("stats view: <CR> on a journal run replays it, and q comes back to the same row", function()
  local simulate = require("vimficiency.simulate")
  in_nav_runs(function(runs_win, rows)
    local stats_tab = vim.api.nvim_get_current_tabpage()
    local tabs = #vim.api.nvim_list_tabpages()
    local header = stats_header()
    local row = rows[2]
    assert_true(row.best_seq ~= "", "the run has a best sequence")
    local detail = assert(stats_query.run(row.id))

    vim.api.nvim_win_set_cursor(runs_win, { 3, 0 })
    h.silence_notify(function() h.feed("<CR>") end)
    assert_true(vim.wait(5000, function() return #simulate._debug_get_windows() == 2 end), "replay opened")
    assert_true(vim.api.nvim_get_current_tabpage() ~= stats_tab, "a replay tab")
    local pool = simulate._debug_get_pool()
    assert_eq(token_text(assert(pool.user).tokens), row.user_keys)
    assert_eq(#pool.suggestions, 1)
    assert_eq(token_text(pool.suggestions[1].tokens), row.best_seq)
    assert_eq({ pool.user.cost, pool.suggestions[1].cost },
      { string.format("%.2f", row.user_cost), string.format("%.2f", row.best_cost) })
    assert_eq(detail.filetype, "lua")
    for _, window in ipairs(simulate._debug_get_windows()) do
      assert_eq(vim.api.nvim_buf_get_lines(window.buf, 0, -1, true), detail.lines)
      assert_true(vim.treesitter.highlighter.active[window.buf] ~= nil, "highlighted as lua")
    end

    h.feed("q")
    assert_eq(vim.api.nvim_get_current_tabpage(), stats_tab)
    assert_eq(vim.api.nvim_get_current_win(), runs_win)
    assert_eq(vim.api.nvim_win_get_cursor(runs_win), { 3, 1 })
    assert_eq(#vim.api.nvim_list_tabpages(), tabs)
    for _, name in ipairs({ "overview", "runs" }) do stats_pane(name) end
    assert_eq(stats_header(), header)
  end)
end)

test("stats view: <CR> on a run whose journal is gone warns and opens nothing", function()
  in_nav_runs(function(runs_win)
    for name in vim.fs.dir(journal.dir()) do
      if name:match("%.vfj$") then vim.uv.fs_unlink(vim.fs.joinpath(journal.dir(), name)) end
    end
    local tabs = #vim.api.nvim_list_tabpages()
    local warnings = {}
    -- The run the list opened on was already looked up for its preview.
    vim.api.nvim_win_set_cursor(runs_win, { 3, 0 })
    h.with_patch({ { vim, "notify", function(msg, level)
      if level == vim.log.levels.WARN then warnings[#warnings + 1] = msg end
    end } }, function()
      h.feed("<CR>")
    end)
    assert_eq(#warnings, 1)
    assert_true(warnings[1]:find("^run not available: journal .* is gone$") ~= nil, warnings[1])
    assert_eq(#vim.api.nvim_list_tabpages(), tabs)
  end)
end)

test("stats view: right on the run length row switches the chart and runs", function()
  h.use_temp_data_home()
  journal_moves(6)

  h.with_patch({ { config.journal.sampling, "window_sizes", { 2, 4 } } }, function()
    assert_eq(refresh(), nil)
    local targets = render.build_page(stats_query.summary(TOP), { scoring = false }).items
    stats_view.open()
    local overview_win, overview = stats_pane("overview")
    assert_true(line_with(overview, "    Run length: [2] [4]") ~= nil)
    ---@return string footer
    local function nav_runs()
      open_journal_runs("nav")
      local _, runs = stats_pane("runs")
      h.feed("<Esc>")
      h.feed("<Esc>")
      return runs[#runs]
    end
    assert_eq(nav_runs(), "runs 1–3 of 3")

    for _, t in ipairs(targets) do
      -- The page's rows are under the two header lines.
      if t.id == "run_length" then vim.api.nvim_win_set_cursor(overview_win, { t.cursor[1] + 3, t.cursor[2] }) end
    end
    h.feed("l")
    local header = stats_header()
    assert_true(header:find("^ Stats    all scorings · .* · runs of 4$") ~= nil, header)
    assert_eq(nav_runs(), "runs 1–1 of 1")
    _, overview = stats_pane("overview")
    assert_true(line_with(overview, " [4]") ~= nil)
    vim.cmd("silent! tabonly")
  end)
end)

test("sampling.refresh: an old journal is rolled up and deleted, and still counts", function()
  h.use_temp_data_home()
  journal_moves(6)
  local path
  for name in vim.fs.dir(journal.dir()) do
    if name:match("%.vfj$") then path = vim.fs.joinpath(journal.dir(), name) end
  end
  local old = os.time() - (config.journal.keep_days + 1) * 86400
  vim.uv.fs_utime(path, old, old)

  h.with_patch({ { config.journal.sampling, "window_sizes", { 2 } } }, function()
    assert_eq(refresh(), nil)

    assert_eq(vim.uv.fs_stat(path), nil)
    assert_eq(vim.uv.fs_stat((path:gsub("%.vfj$", ".samples"))), nil)
    assert_true(vim.uv.fs_stat(journal.dir() .. "/rollup.vfr") ~= nil)
    local j = journal_summary()
    assert_eq(j.journal_bytes, 0)
    assert_eq(j.score.spans, 3)
    assert_eq(j.nav.spans, 3)
    assert_eq(stats_query.summary(TOP).commands.runs, 3)
  end)
end)

test("sampling.refresh: other window sizes rescore the journal", function()
  h.use_temp_data_home()
  journal_moves(6)

  h.with_patch({ { config.journal.sampling, "window_sizes", { 2 } } }, function()
    assert_eq(refresh(), nil)
  end)
  h.with_patch({ { config.journal.sampling, "window_sizes", { 4 } } }, function()
    -- Samples of the old settings are left out, not mixed in, until rescored.
    local stale = journal_summary()
    assert_eq(stale.score.spans, 0)
    assert_eq(stale.scored_bytes, 0)

    assert_eq(refresh(), nil)
    local j = journal_summary()
    assert_eq(j.charted_size, 4)
    assert_eq(j.score.spans, 1)
  end)
end)

test("sampling background: scores new journal content, then waits out the interval", function()
  h.use_temp_data_home()
  journal_moves(16)
  sampling.start_background()
  stats_view.open()
  local _, overview = stats_pane("overview")
  assert_true(line_with(overview, "average | 0 samples") ~= nil)

  vim.api.nvim_exec_autocmds("CursorHold", { group = "VimficiencyJournalSampling", modeline = false })
  assert_true(sampling.running(), "a run started")
  _, overview = stats_pane("overview")
  assert_true(line_with(overview, "Scoring journal…") ~= nil)
  assert_eq(line_with(overview, "average |"), nil)
  local done = false
  assert_true(sampling.when_done(function() done = true end))
  assert_true(vim.wait(30000, function() return done end), "run finished")
  assert_true(journal_summary().scored_bytes > 0)
  assert_eq(sampling.last_error(), nil)
  -- The open view redrew in place.
  _, overview = stats_pane("overview")
  assert_eq(line_with(overview, "Scoring journal…"), nil)
  assert_true(line_with(overview, "average | 2 samples") ~= nil)
  vim.cmd("silent! tabonly")

  journal_moves(4)
  sampling._for_test.maybe_run()
  assert_true(not sampling.running(), "no second run within the interval")
  sampling.stop_background()
end)

test("sampling background: off when journal.sampling.auto is", function()
  h.use_temp_data_home()
  journal_moves(6)
  h.with_patch({ { config.journal.sampling, "auto", false } }, function()
    sampling.start_background()
    sampling._for_test.maybe_run()
    assert_true(not sampling.running())
  end)
  sampling.stop_background()
end)

test("sampling background: restart closes the old timer and ignores its queued callback", function()
  h.use_temp_data_home()
  journal_moves(4)
  local timer, queued
  local defer = vim.defer_fn
  h.with_patch({ { vim, "defer_fn", function(fn, delay)
    timer, queued = defer(fn, delay), fn
    return timer
  end } }, function()
    sampling.start_background()
  end)

  sampling.start_background()
  assert_true(timer:is_closing())
  queued()
  assert_true(not sampling.running())
  sampling.stop_background()
end)

test("sampling.refresh: a sampler keeps its lock after its Neovim exits", function()
  h.use_temp_data_home()
  journal_moves(4)
  local dir = journal.dir()
  local fifo = dir .. "/000.vfj"
  assert_eq(vim.system({ "mkfifo", fifo }):wait().code, 0)
  local tool = vim.fs.joinpath(vim.fs.dirname(ffi_core.lib_path), "vimfy_journal")
  local request = ffi_core.encode("vf.journal.SampleRequest",
    { journal_dir = dir, params = sampling.params(), keep_days = config.journal.keep_days })
  local launch = string.format(
    "lua local proc = vim.system({%q}, { stdin = vim.base64.decode(%q), stdout = false, stderr = false }, function() end)"
      .. " vim.wait(5000, function() return vim.uv.fs_stat(%q) ~= nil end) io.stdout:write(proc.pid)",
    tool, vim.base64.encode(request), dir .. "/sampler.lock")
  local editor = vim.system({ vim.v.progpath, "--headless", "-u", "NONE", "-i", "NONE",
    "-c", launch, "-c", "qa!" }, { text = true }):wait(10000)
  local pid = assert(tonumber(editor.stdout), editor.stderr)

  local writer
  local flags = bit.bor(vim.uv.constants.O_WRONLY, vim.uv.constants.O_NONBLOCK)
  local reading = vim.wait(5000, function()
    writer = vim.uv.fs_open(fifo, flags, 0)
    return writer ~= nil
  end)
  vim.uv.fs_unlink(fifo)
  local err = reading and refresh()
  local samples = vim.fn.glob(dir .. "/*.samples", false, true)
  local killed = vim.uv.kill(pid, "sigkill")
  if writer then vim.uv.fs_close(writer) end

  assert_eq(editor.code, 0)
  assert_true(reading, "the sampler reached the journal FIFO")
  local busy = "another sampler is scoring the journal"
  assert_eq(err, busy)
  assert_eq(samples, {})
  assert_eq(killed, 0)

  for _ = 1, 10 do
    err = refresh()
    if err ~= busy then break end
  end
  assert_eq(err, nil)
  local summary = journal_summary()
  assert_eq(summary.scored_bytes, summary.journal_bytes)
end)
