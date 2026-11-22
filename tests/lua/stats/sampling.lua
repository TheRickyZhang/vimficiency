-- The Journal section of `:Vimfy stats`, and end-to-end runs of the real
-- `vimfy_journal` over a journal written here, read back through the summary.

local config   = require("vimficiency.config")
local journal  = require("vimficiency.capture.journal")
local sampling = require("vimficiency.stats.sampling")
local stats    = require("vimficiency.stats")
local h        = require("_helpers")

---@param lines string[]
---@param needle string
---@return string|nil
local function line_with(lines, needle)
  for _, line in ipairs(lines) do
    if line:find(needle, 1, true) then return line end
  end
end

--- Runs a refresh to completion.
---@return string|nil err
local function refresh()
  local done, err = false, nil
  sampling.refresh(function(e) done, err = true, e end)
  assert_true(vim.wait(30000, function() return done end), "refresh finished")
  return err
end

---@return VF.Stats.JournalSummary
local function journal_summary()
  return stats.summary(1).journal
end

--- Journals `count` single-`l` atoms over one line, one tick each, after a
--- resync at the start of the line.
---@param count integer
local function journal_moves(count)
  local buf = h.new_buf({ "alpha beta gamma delta epsilon", "second line" })
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
---@return string[]
local function lines_with_journal(fields, background)
  h.use_temp_data_home()
  local summary = stats.summary(1)
  summary.journal = vim.tbl_extend("force", summary.journal,
    { present = true, scored_bytes = 100, journal_bytes = 100 }, fields)
  return (stats.build_lines(summary, background or { scoring = false }))
end

test("stats journal section: shows progress and never waits for scoring", function()
  h.use_temp_data_home()
  assert_true(line_with(stats.build_lines(stats.summary(1), { scoring = false }),
    "journal = { enabled = true }") ~= nil)
  local lines = lines_with_journal({ scored_bytes = 25 }, { scoring = true })
  assert_true(line_with(lines,
    "Scored under the current settings: 25% of the journal · scoring in the background…") ~= nil)
  assert_true(line_with(lines, "No scored runs of 8 commands yet") ~= nil)
  assert_true(line_with(lines_with_journal({}, { scoring = false, err = "boom" }),
    "Last background scoring failed: boom") ~= nil)
  h.with_patch({ { config.journal.sampling, "auto", false } }, function()
    assert_true(line_with(lines_with_journal({ scored_bytes = 0 }),
      ":Vimfy stats refresh scores the rest") ~= nil)
  end)
end)

test("stats journal section: charts the middle size and lists every size", function()
  local lines = lines_with_journal({
    charted_size = 8,
    sizes = {
      { size = 4, score = { spans = 1, score = 90 } },
      { size = 8, score = { spans = 2, score = 75 } },
      { size = 16, score = { spans = 0, score = 0 } },
    },
    score = { spans = 2, score = 75 },
    nav = { spans = 1, score = 100 },
    edit = { spans = 1, score = 50 },
    days = { { days_ago = 0, score = 75 } },
    unscored = { { outcome = "too_large", runs = 1 } },
    deadline_hit = 1,
  })
  assert_true(line_with(lines, "Score:    75.0 / 100  (2 runs of 8 commands)") ~= nil)
  assert_true(line_with(lines, "30-day score, runs of 8 commands:") ~= nil)
  assert_true(line_with(lines, "By run length:  4 → 90.0   8 → 75.0   16 → —") ~= nil)
  assert_true(line_with(lines, "Movement only:  100.0 (1 run)   With edits: 50.0 (1 run)") ~= nil)
  assert_true(line_with(lines, "Not scored:     1 too large") ~= nil)
  assert_true(line_with(lines, "Search cut short by the deadline: 1 run") ~= nil)
end)

test("sampling.refresh: nothing to do without a journal directory", function()
  h.use_temp_data_home()
  assert_eq(refresh(), nil)
  local j = journal_summary()
  assert_eq(j.present, false)
  assert_eq(j.score.spans, 0)
end)

test("sampling.refresh: scores a journal written by this Neovim, once", function()
  h.use_temp_data_home()
  journal_moves(6)

  h.with_patch({ { config.journal.sampling, "window_sizes", { 2 } } }, function()
    assert_eq(refresh(), nil)

    -- The sixth atom is the journal's last tick, which is never cut.
    local j = journal_summary()
    assert_true(j.journal_bytes > 0)
    assert_eq(j.scored_bytes, j.journal_bytes)
    assert_eq(j.charted_size, 2)
    assert_eq(j.score.spans, 2)
    assert_eq(j.nav.spans, 2)
    assert_eq(stats.summary(1).commands.runs, 2)
    assert_eq(vim.uv.fs_stat(journal.dir() .. "/sampling.lock"), nil)

    assert_eq(refresh(), nil)
    assert_eq(journal_summary().score.spans, 2)
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
  journal_moves(6)
  sampling.start_background()

  sampling._for_test.maybe_run()
  assert_true(sampling.running(), "a run started")
  local done = false
  assert_true(sampling.when_done(function() done = true end))
  assert_true(vim.wait(30000, function() return done end), "run finished")
  assert_true(journal_summary().scored_bytes > 0)
  assert_eq(sampling.last_error(), nil)

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

test("sampling.refresh: yields to a live sampler and takes over a dead one's lock", function()
  h.use_temp_data_home()
  journal_moves(4)
  local lock = journal.dir() .. "/sampling.lock"

  vim.fn.writefile({ tostring(vim.uv.os_getpid()) }, lock)
  assert_eq(refresh(), "another Neovim is sampling the journal")
  assert_true(vim.uv.fs_stat(lock) ~= nil, "a live owner keeps its lock")

  local exited = vim.system({ "true" })
  exited:wait()
  vim.fn.writefile({ tostring(exited.pid) }, lock)
  assert_eq(refresh(), nil)
  assert_eq(vim.uv.fs_stat(lock), nil)
end)
