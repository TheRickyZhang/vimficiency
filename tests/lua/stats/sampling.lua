-- End-to-end runs of the real `vimfy_journal` over a journal written here,
-- read back through the summary and the `:Vimfy stats` tab.

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

--- From the stats, opens the journal's chart and lists its movement-only
--- runs. <Esc> twice comes back to the stats.
local function open_movement_runs()
  enter_line("overview", "Daily percent efficiency")
  enter_line("chart", "Movement only")
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

--- Journals `count` single-`l` atoms over one line, one tick each, after a
--- resync at the start of the line.
---@param count integer
---@param filetype? string
---@param before_stop? fun(path: string)
local function journal_moves(count, filetype, before_stop)
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
  if before_stop then before_stop(assert(journal.path())) end
  journal.stop()
  vim.api.nvim_buf_delete(buf, { force = true })
end

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
    assert_eq(j.nav_timing[1].timing.n, 3)
    assert_true(j.nav_timing[1].timing.p50 > 0)
    assert_eq(stats_query.summary(TOP).journal.commands.spans, 3)
    local lock = journal.dir() .. "/sampler.lock"
    local inode = assert(vim.uv.fs_stat(lock)).ino
    assert_eq(refresh(), nil)
    assert_eq(assert(vim.uv.fs_stat(lock)).ino, inode)
    assert_eq(journal_summary().score.spans, 3)

    stats_view.open()
    enter_line("overview", "Movement search · 3 samples")
    local chart, lines = stats_pane("chart")
    assert_true(line_with(lines, "median ms (t)") ~= nil)
    assert_true(line_with(lines, "Buffer positions in the larger unpadded region") ~= nil)
    local title = vim.api.nvim_win_get_config(chart).title[1][1]
    assert_match(title, "Movement search · 1–64 ·")
    assert_match(title, "ms p50 · n=3")
    h.feed("<CR>")
    local runs, listed = stats_pane("runs")
    assert_match(vim.api.nvim_win_get_config(runs).title[1][1], "sort: slowest ▼")
    assert_eq(listed[#listed], "runs 1–3 of 3")
    h.feed("q")
    h.feed("q")
    h.feed("q")
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
    open_movement_runs()
    local chart_win, lines = stats_pane("chart")
    assert_true(line_with(lines, "% efficiency · 3 runs (1.000)") ~= nil)
    local chart_title = vim.api.nvim_win_get_config(chart_win).title[1][1]
    assert_true(chart_title:match("^ Stats › Journal · [%d.]+%% efficiency · 3 runs of 2 commands · Movement only: [%d.]+%% efficiency · 3 runs $")
      ~= nil, chart_title)
    local runs_win, runs = stats_pane("runs")
    local title = vim.api.nvim_win_get_config(runs_win).title[1][1]
    assert_true(title:find("Stats › Movement only", 1, true) ~= nil, title)
    assert_eq(runs[#runs], "runs 1–3 of 3")
    local _, preview = stats_pane("preview")
    assert_match(preview[1], ".vfj · 2 commands to atom ")
    assert_eq(preview[2], " you:   l l")
    assert_true(line_with(preview, " before ") ~= nil, "the text before")
    assert_true(line_with(preview, " │ after ") ~= nil, "the text after")
    h.feed("<Esc>")
    -- Under the whole bar, its runs by unpadded region size.
    h.feed("j")
    local _, chart_lines = stats_pane("chart")
    local bar_row = vim.api.nvim_win_get_cursor(chart_win)[1]
    assert_match(chart_lines[bar_row - 1], "▄", "movement bar uses half blocks")
    local bar, caption = chart_lines[bar_row], chart_lines[bar_row + 1]
    local left = assert(bar:find("▄", 1, true)) - 1
    local right = vim.api.nvim_win_get_width(chart_win) - vim.fn.strdisplaywidth(bar)
    assert_eq(left, right, "the bar is centered")
    assert_match(caption, "    Size ")
    local first, last = caption:find("1–64", 1, true)
    local before = vim.fn.strdisplaywidth(caption:sub(1, first - 1)) - left
    local after = vim.fn.strdisplaywidth(caption:sub(last + 1))
    assert_true(math.abs(before - after) <= 1, "the range is centered under the bar")
    assert_eq(line_with(chart_lines, "Efficiency:"), nil)
    h.feed("<CR>")
    runs_win, runs = stats_pane("runs")
    title = vim.api.nvim_win_get_config(runs_win).title[1][1]
    assert_true(title:find("Stats › Movement only · 1–64 positions", 1, true) ~= nil, title)
    assert_eq(runs[#runs], "runs 1–3 of 3")
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
    open_movement_runs()
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
    local detail = assert(stats_query.run(row))

    vim.api.nvim_win_set_cursor(runs_win, { 3, 0 })
    h.silence_notify(function() h.feed("<CR>") end)
    assert_true(vim.wait(5000, function() return #simulate._debug_get_windows() == 2 end), "replay opened")
    assert_true(vim.api.nvim_get_current_tabpage() ~= stats_tab, "a replay tab")
    local pool = simulate._debug_get_pool()
    assert_eq(#assert(pool.user).tokens, #detail.recording.steps)
    assert_eq(pool.user.tokens[1].kind, "recorded")
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

test("journal and session Play show the same recorded edits regardless of their keys", function()
  local simulate = require("vimficiency.simulate")
  in_nav_runs(function(runs_win, rows)
    local detail = assert(stats_query.run(rows[2]))
    local commands = {
      { keys = "l", kind = "motion" }, { keys = "==", kind = "operator" },
      { keys = "iconst<Esc>", kind = "insert" },
    }
    detail.user_seq = "l==iconst<Esc>"
    detail.goal_lines = { "const" }
    detail.end_row, detail.end_col = 0, 4
    detail.recording.steps = {
      { keys = "l", row = detail.start_row, col = detail.start_col + 1, mode = "n", deltas = {} },
      { keys = "==iconst<Esc>", row = 0, col = 4, mode = "n",
        deltas = { { first = 0, old_count = #detail.lines, lines = detail.goal_lines } } },
    }
    local function user_states()
      assert_true(vim.wait(5000, function() return #simulate._debug_get_windows() > 0 end))
      local user = assert(simulate._debug_get_pool().user)
      assert_eq(user.tokens[2].text, "==iconst<Esc>")
      assert_eq(user.tokens[2].kind, "recorded")
      assert_eq(#user.states, 3)
      assert_eq(user.states[2].cursor, { detail.start_row + 1, detail.start_col + 1 })
      assert_eq(user.states[3], { lines = { "const" }, cursor = { 1, 4 }, mode = "n" })
      return user.states
    end
    local journal_states
    h.with_patch({ { stats_query, "run", function() return detail end } }, function()
      vim.api.nvim_win_set_cursor(runs_win, { 3, 0 })
      h.feed("<CR>")
      journal_states = user_states()
      simulate.cleanup_compare()
    end)
    require("vimficiency.session.playback").play(h.fake_result({
      lines = detail.lines, goal_lines = detail.goal_lines,
      start_row = detail.start_row, start_col = detail.start_col, end_row = 0, end_col = 4,
      commands = commands, user_seq = detail.user_seq, key_count = #commands, recording = detail.recording,
    }), "replaygap")
    assert_eq(user_states(), journal_states)
    simulate.cleanup_compare()
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
    local targets = render.build_page(stats_query.summary(TOP), TOP, { scoring = false }).items
    stats_view.open()
    local overview_win, overview = stats_pane("overview")
    assert_true(line_with(overview, "    Run length: [2] [4]") ~= nil)
    ---@return string footer
    local function nav_runs()
      open_movement_runs()
      local _, runs = stats_pane("runs")
      h.feed("<Esc>")
      h.feed("<Esc>")
      return runs[#runs]
    end
    assert_eq(nav_runs(), "runs 1–3 of 3")

    for _, t in ipairs(targets) do
      if t.id == "run_length" then vim.api.nvim_win_set_cursor(overview_win, { t.cursor[1] + 1, t.cursor[2] }) end
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
  local old = os.time() - (config.journal.keep_days + 1) * 86400
  h.with_patch({ { config.journal.sampling, "window_sizes", { 2 } } }, function()
    local path
    journal_moves(6, nil, function(active_path)
      path = active_path
      journal._for_test.close_tick()
      vim.wait(10, function() return false end)
      assert(vim.uv.fs_utime(path, old, old))
      assert_eq(refresh(), nil)
      assert_true(vim.uv.fs_stat(path) ~= nil, "an idle writer still owns the journal")
    end)
    assert(vim.uv.fs_utime(path, old, old))
    assert_eq(refresh(), nil)

    assert_eq(vim.uv.fs_stat(path), nil)
    assert_eq(vim.uv.fs_stat((path:gsub("%.vfj$", ".samples"))), nil)
    assert_true(vim.uv.fs_stat(journal.dir() .. "/rollup.vfr") ~= nil)
    local j = journal_summary()
    assert_eq(j.journal_bytes, 0)
    assert_eq(j.score.spans, 3)
    assert_eq(j.nav.spans, 3)
    assert_eq(stats_query.summary(TOP).journal.commands.spans, 3)
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

  local vanished = vim.fs.joinpath(journal.dir(), "vanished.vfj")
  vim.fn.writefile({}, vanished)
  local stat = vim.uv.fs_stat
  h.with_patch({ { vim.uv, "fs_stat", function(path)
    if path == vanished then assert(vim.uv.fs_unlink(path)) end
    return stat(path)
  end } }, function()
    vim.api.nvim_exec_autocmds("CursorHold", { group = "VimficiencyJournalSampling", modeline = false })
  end)
  assert_true(sampling.running(), "a run started")
  _, overview = stats_pane("overview")
  assert_true(assert(line_with(overview, "Scoring journal…")):match("Scoring journal… %d+%%") ~= nil)
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

test("stats view: shows new journal coverage before the scorer finishes", function()
  h.use_temp_data_home()
  journal_moves(16)
  assert_eq(refresh(), nil)
  local path = assert(vim.fn.globpath(journal.dir(), "*.samples", false, true)[1])
  local file = assert(io.open(path, "rb"))
  local records = ffi_core.decode_message("vf.journal.SampleFile", file:read("*a")).records
  file:close()
  local total = journal_summary().journal_bytes
  local checkpoint
  local prefix = {}
  for _, record in ipairs(records) do
    prefix[#prefix + 1] = record
    if record.body == "journal_size" and record.journal_size < total then
      checkpoint = record.journal_size
      break
    end
  end
  assert_true(checkpoint ~= nil and checkpoint > 0)
  local bytes = ffi_core.encode("vf.journal.SampleFile", { records = prefix })
  assert(vim.uv.fs_unlink(path))
  h.with_patch({ { sampling, "running", function() return true end },
    { sampling, "when_done", function() return true end } }, function()
    stats_view.open()
    assert_true(line_with(select(2, stats_pane("overview")), "Scoring journal… 0%") ~= nil)
    file = assert(io.open(path, "wb"))
    assert(file:write(bytes))
    file:close()
    local status = string.format("Scoring journal… %d%%", math.floor(100 * checkpoint / total))
    assert_true(vim.wait(3000, function()
      return line_with(select(2, stats_pane("overview")), status) ~= nil
    end), status)
    h.feed("q")
  end)
end)

test("sampling background: retries unchanged bytes after a failed run", function()
  h.use_temp_data_home()
  journal_moves(4)
  local now, attempts = 0, 0
  h.with_patch({
    { vim.uv, "now", function() return now end },
    { sampling, "refresh", function(done)
      attempts = attempts + 1
      done(attempts == 1 and "failed" or nil)
    end },
  }, function()
    sampling.start_background()
    sampling._for_test.maybe_run()
    sampling._for_test.maybe_run()
    assert_eq(attempts, 1)
    now = now + 300000
    sampling._for_test.maybe_run()
    assert_eq(attempts, 2)
    assert_eq(sampling.last_error(), nil)
    now = now + 300000
    sampling._for_test.maybe_run()
    assert_eq(attempts, 2)
    sampling.stop_background()
  end)
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
