-- The reducer over journal samples, the Journal section of `:Vimfy stats`, and
-- one end-to-end run of the real `vimfy_journal` over a journal written here.

local config   = require("vimficiency.config")
local journal  = require("vimficiency.capture.journal")
local sampling = require("vimficiency.stats.sampling")
local stats    = require("vimficiency.stats")
local h        = require("_helpers")

local DAY = 86400

---@param overrides table|nil
---@return VF.StatsSampling.Sample
local function sample(overrides)
  return vim.tbl_extend("force", {
    window_size = 8,
    first_atom = 0,
    last_atom = 7,
    atom_count = 8,
    wall_epoch = 0,
    outcome = "scored",
    user_cost = 10,
    best_cost = 5,
    nav_only = false,
    net_zero = false,
    deadline_hit = false,
    lossy = false,
    analyze_ms = 1,
  }, overrides or {})
end

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

--- Journals `count` single-`l` atoms over one line, one tick each, after a
--- resync at the start of the line.
---@param count integer
local function journal_moves(count)
  local buf = h.new_buf({ "alpha beta gamma delta epsilon", "second line" })
  vim.wait(5, function() return false end)
  journal.start()
  local atom = { keys = "l", typed = "l", buf = buf, win = vim.api.nvim_get_current_win(), lossy = false }
  journal._for_test.resync()
  for col = 1, count do
    journal._for_test.close_tick()
    vim.api.nvim_win_set_cursor(0, { 1, col })
    journal._for_test.listener(atom, "record")
  end
  journal.stop()
  vim.api.nvim_buf_delete(buf, { force = true })
end

test("sampling.aggregate: normalized effort is a ratio of sums", function()
  local summary = sampling.aggregate({
    sample({ user_cost = 10, best_cost = 5 }),
    sample({ user_cost = 30, best_cost = 25 }),
  })[8]
  assert_eq(summary.all.windows, 2)
  assert_eq(sampling.normalized(summary.all), 75)
end)

test("sampling.aggregate: a best above the user's cost counts as optimal", function()
  local summary = sampling.aggregate({ sample({ user_cost = 10, best_cost = 14 }) })[8]
  assert_eq(sampling.normalized(summary.all), 100)
end)

test("sampling.aggregate: a net-zero window is all waste", function()
  local summary = sampling.aggregate({
    sample({ user_cost = 10, best_cost = 0, nav_only = true, net_zero = true }),
    sample({ user_cost = 10, best_cost = 10, nav_only = true }),
  })[8]
  assert_eq(sampling.normalized(summary.all), 50)
end)

test("sampling.aggregate: movement-only windows count and are also tallied apart", function()
  local summary = sampling.aggregate({
    sample({ user_cost = 10, best_cost = 10, nav_only = true }),
    sample({ user_cost = 10, best_cost = 2 }),
  })[8]
  assert_eq(summary.all.windows, 2)
  assert_eq(summary.nav.windows, 1)
  assert_eq(summary.edit.windows, 1)
  assert_eq(sampling.normalized(summary.nav), 100)
  assert_eq(sampling.normalized(summary.edit), 20)
  assert_eq(sampling.normalized(summary.all), 60)
end)

test("sampling.aggregate: unscored windows are counted by outcome and carry no cost", function()
  local summary = sampling.aggregate({
    sample({ outcome = "too_large", user_cost = 0, best_cost = 0 }),
    sample({ outcome = "no_result", user_cost = 9, best_cost = 0 }),
    sample({ outcome = "no_result", user_cost = 9, best_cost = 0 }),
    sample({ outcome = "crashed", user_cost = 0, best_cost = 0 }),
    sample({ user_cost = 10, best_cost = 5, deadline_hit = true }),
  })[8]
  assert_eq(summary.unscored, { too_large = 1, no_result = 2, crashed = 1 })
  assert_eq(summary.all.windows, 1)
  assert_eq(summary.deadline_hit, 1)
  assert_eq(sampling.normalized(summary.all), 50)
end)

test("sampling.aggregate: sizes and days are kept apart", function()
  local by_size = sampling.aggregate({
    sample({ window_size = 4, user_cost = 10, best_cost = 10, wall_epoch = 3 * DAY + 5 }),
    sample({ window_size = 8, user_cost = 10, best_cost = 5, wall_epoch = 3 * DAY + 5 }),
    sample({ window_size = 8, user_cost = 10, best_cost = 1, wall_epoch = 4 * DAY }),
  })
  assert_eq(sampling.normalized(by_size[4].all), 100)
  assert_eq(sampling.normalized(by_size[8].by_day[3]), 50)
  assert_eq(sampling.normalized(by_size[8].by_day[4]), 10)
  assert_eq(by_size[4].by_day[4], nil)
end)

test("sampling.normalized: nil without a scored window", function()
  assert_eq(sampling.normalized({ windows = 0, user_cost = 0, best_cost = 0 }), nil)
end)

test("stats journal section: one line per state before any score exists", function()
  local summary = stats.aggregate({})
  assert_true(line_with(stats.build_lines(summary, { state = "off" }), "journal = { enabled = true }") ~= nil)
  assert_true(line_with(stats.build_lines(summary, { state = "loading" }), "Scoring the journal") ~= nil)
  assert_true(line_with(stats.build_lines(summary, { state = "failed", err = "boom" }), "boom") ~= nil)
  assert_true(line_with(stats.build_lines(summary, { state = "ready", by_size = {} }),
    "No scored runs of 8 commands yet") ~= nil)
end)

test("stats journal section: charts the middle size and lists every size", function()
  local today = math.floor(os.time() / DAY) * DAY
  local view = {
    state = "ready",
    by_size = sampling.aggregate({
      sample({ window_size = 4, user_cost = 10, best_cost = 9, wall_epoch = today }),
      sample({ window_size = 8, user_cost = 10, best_cost = 5, wall_epoch = today }),
      sample({ window_size = 8, user_cost = 10, best_cost = 10, nav_only = true, wall_epoch = today }),
      sample({ window_size = 8, outcome = "too_large", wall_epoch = today }),
      sample({ window_size = 16, user_cost = 10, best_cost = 2, wall_epoch = today }),
    }),
  }
  local lines = stats.build_lines(stats.aggregate({}), view)

  assert_true(line_with(lines, "Score:    75.0 / 100  (2 runs of 8 commands)") ~= nil)
  assert_true(line_with(lines, "30-day normalized effort, runs of 8 commands:") ~= nil)
  assert_true(line_with(lines, "By run length:  4 → 90.0   8 → 75.0   16 → 20.0") ~= nil)
  assert_true(line_with(lines, "Movement only:  100.0 (1 runs)   With edits: 50.0 (1 runs)") ~= nil)
  assert_true(line_with(lines, "Not scored:     1 too large") ~= nil)
end)

test("sampling.refresh: nothing to do without a journal directory", function()
  h.use_temp_data_home()
  assert_eq(refresh(), nil)
  assert_eq(sampling.read_samples(), {})
end)

test("sampling.refresh: scores a journal written by this Neovim, once", function()
  h.use_temp_data_home()
  journal_moves(6)

  h.with_patch({ { config.journal.sampling, "window_sizes", { 2 } } }, function()
    assert_eq(refresh(), nil)

    -- The sixth atom is the journal's last tick, which is never cut.
    local samples = sampling.read_samples()
    assert_eq(#samples, 2)
    assert_eq({ samples[1].first_atom, samples[1].last_atom }, { 0, 1 })
    assert_eq({ samples[2].first_atom, samples[2].last_atom }, { 2, 3 })
    for _, s in ipairs(samples) do
      assert_eq(s.outcome, "scored")
      assert_eq(s.window_size, 2)
      assert_eq(s.nav_only, true)
      assert_true(s.user_cost > 0, "user cost")
      assert_true(s.best_cost > 0, "best cost")
    end
    assert_eq(vim.uv.fs_stat(journal.dir() .. "/sampling.lock"), nil)

    assert_eq(refresh(), nil)
    assert_eq(#sampling.read_samples(), 2)
  end)
end)

test("sampling.refresh: other window sizes rescore the journal", function()
  h.use_temp_data_home()
  journal_moves(6)

  h.with_patch({ { config.journal.sampling, "window_sizes", { 2 } } }, function()
    assert_eq(refresh(), nil)
  end)
  h.with_patch({ { config.journal.sampling, "window_sizes", { 4 } } }, function()
    assert_eq(refresh(), nil)
    local samples = sampling.read_samples()
    assert_eq(#samples, 1)
    assert_eq(samples[1].window_size, 4)
  end)
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
