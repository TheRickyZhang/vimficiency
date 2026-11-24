-- tests/lua/stats/log.lua
-- The session log through the real library: stamps, grouping, the legacy move.

local config = require("vimficiency.config")
local ffi_core = require("vimficiency.ffi.core")
local ffi_stats = require("vimficiency.ffi.stats")
local ffi_versions = require("vimficiency.ffi.versions")
local journal = require("vimficiency.capture.journal")
local sampling = require("vimficiency.stats.sampling")
local stats = require("vimficiency.stats")
local stats_log = require("vimficiency.stats.log")
local h = require("_helpers")

---@param user_seq string
---@param epoch integer
local function finish(user_seq, epoch)
  stats_log.append("mark", h.finished_result(user_seq, {
    user_cost = 3.0,
    key_count = 2,
    finished_wall_epoch = epoch,
    commands = { { keys = user_seq, kind = "operator" } },
  }))
end

---@return table[]  # `vf.stats.LogRecord`
local function records()
  local fh = assert(io.open(stats_log.path(), "rb"))
  local bytes = fh:read("*a")
  fh:close()
  return ffi_core.decode_message("vf.stats.SessionLog", bytes).records
end

test("stats.log: sessions round-trip under the current stamp", function()
  h.use_temp_data_home()
  finish("dw", 10)
  finish("de", 20)

  local sessions = stats.summary({ shown = 1, weight = "effort", filter = {}, charted_size = 0 }).sessions
  assert_eq(sessions.groups, 1)
  assert_eq(sessions.sessions, 2)
  assert_true(sessions.current)
  local prefix = "scoring " .. ffi_versions.versions().scoring_version .. "·"
  assert_true(sessions.label:find(prefix, 1, true) == 1, sessions.label)

  local first = records()[3].session
  assert_eq(first.user_seq, "dw")
  assert_eq(first.best_seq, "dw")
  assert_eq(first.outcome, "scored")
  assert_eq(first.kind, "mark")
  assert_eq(first.commands, { { keys = "dw", kind = "operator" } })
  assert_eq(records()[4].session.finished_wall_epoch, 20)
end)

test("stats.log: the header comes first, then a stamp once, before its first session", function()
  h.use_temp_data_home()
  finish("a", 1)
  finish("b", 2)
  local bodies = {}
  for _, r in ipairs(records()) do bodies[#bodies + 1] = r.body end
  assert_eq(bodies, { "header", "stamp", "session", "session" })
end)

test("stats.log: another stamp's sessions form their own group", function()
  h.use_temp_data_home()
  finish("old", 1)
  h.with_patch({ { config, "RESULTS_CALCULATED", config.RESULTS_CALCULATED + 1 } }, function()
    finish("new", 2)
    local current = stats.summary({ shown = 1, weight = "effort", filter = {}, charted_size = 0 }).sessions
    assert_eq(current.groups, 2)
    assert_true(current.current)
    assert_eq(current.sessions, 1)
    assert_eq(current.other_sessions, 1)
    local older = stats.summary({ shown = 2, weight = "effort", filter = {}, charted_size = 0 }).sessions
    assert_true(not older.current)
    assert_true(current.label ~= older.label, "labels tell stamps apart")
  end)
end)

test("stats.log: a truncated last record is not read", function()
  h.use_temp_data_home()
  finish("whole", 1)
  local fh = assert(io.open(stats_log.path(), "ab"))
  fh:write("\10\100partial")
  fh:close()
  assert_eq(stats.summary({ shown = 1, weight = "effort", filter = {}, charted_size = 0 }).sessions.sessions, 1)
end)

test("stats.log: the unstamped v1 log moves to legacy and is not read", function()
  local tmp = h.use_temp_data_home()
  local dir = tmp .. "/nvim/vimficiency"
  vim.fn.mkdir(dir, "p")
  vim.fn.writefile({ '{"v":1,"user_seq":"old"}' }, dir .. "/sessions.jsonl")

  finish("new", 1)
  assert_eq(stats.summary({ shown = 1, weight = "effort", filter = {}, charted_size = 0 }).lifetime.sessions, 1)
  assert_true(vim.uv.fs_stat(dir .. "/sessions.jsonl") == nil, "v1 log moved away")
  assert_eq(vim.fn.readfile(dir .. "/legacy/sessions.v1.jsonl"), { '{"v":1,"user_seq":"old"}' })
end)

test("stats.log: a row's drill filter lists its sessions through ffi.stats.runs", function()
  h.use_temp_data_home()
  local now = os.time()
  finish("dw", now - 60)
  finish("de", now - 30)

  local day = stats.summary({ shown = 1, weight = "effort", filter = {}, charted_size = 0 }).sessions.days[1]
  local list = ffi_stats.runs({
    scope = {
      session_log = stats_log.path(),
      stamp = stats_log.stamp(),
      shown_group = 0,
      journal_dir = journal.dir(),
      sample_params = sampling.params(),
      weight = "effort",
      filter = day.drill,
      charted_size = 0,
    },
    order = "newest",
    offset = 0,
    limit = 10,
  })
  assert_eq(day.drill.source, "sessions")
  assert_eq(list.total, 2)
  assert_eq(list.runs[1].id, { run = "session", session = 1 })
  assert_eq(list.runs[1].user_keys, "de")
  assert_eq(list.runs[1].outcome, "scored")
  assert_eq(list.runs[2].wall_epoch, now - 60)
end)

---@param user_typed_insert string|nil
---@return VF.Stats.CommandSummary
local function commands_after_ten_inserts(user_typed_insert)
  h.use_temp_data_home()
  for epoch = 1, 10 do
    stats_log.append("mark", h.finished_result("if()<C-g>U<Left><BS><Esc>", {
      user_cost = 3.0,
      finished_wall_epoch = epoch,
      commands = { { keys = "if()<C-g>U<Left><BS><Esc>", kind = "insert" } },
      user_typed_insert = user_typed_insert,
      optimal_results = { { seq = "if)<Esc>", cost = 1.0 } },
    }))
  end
  return stats.summary({ shown = 1, weight = "effort", filter = {}, charted_size = 0 }).commands
end

test("stats.log: a session's insert text is the keys pressed in it, not the keys that ran", function()
  local commands = commands_after_ten_inserts("f(<BS>")
  assert_eq(commands.user_typed_chars, 20)
  assert_eq(commands.optimizer_typed_chars, 20)
  assert_eq(#commands.more, 1)
  assert_eq(commands.more[1].text, "<BS>")
  assert_eq(commands.more[1].user, 10)
end)

test("stats.log: pressing nothing in Insert mode still counts the optimizer's insert text", function()
  local commands = commands_after_ten_inserts("")
  assert_eq(commands.user_typed_chars, 0)
  assert_eq(commands.optimizer_typed_chars, 20)
end)

test("stats.log: a session without pressed insert keys counts no insert text", function()
  local commands = commands_after_ten_inserts(nil)
  assert_eq(commands.user_typed_chars, 0)
  assert_eq(commands.optimizer_typed_chars, 0)
end)

test("stats.log: the lowest-cost result is the best, and beating it still scores", function()
  local s = stats_log._for_test.session_of("mark", h.fake_result({
    user_cost = 1.0,
    optimal_results = {
      { seq = "hi",  cost = 8.0 },
      { seq = "lo",  cost = 2.0 },
    },
  }))
  assert_eq(s.outcome, "scored")
  assert_eq(s.best_cost, 2.0)
  assert_eq(s.best_seq, "lo")
end)

test("stats.log: no result and unbeaten are outcomes, not costs", function()
  local failed = stats_log._for_test.session_of("mark",
    h.fake_result({ optimal_results = {} }))
  assert_eq(failed.outcome, "no_result")
  local unbeaten = stats_log._for_test.session_of("mark",
    h.fake_result({ optimal_results = {}, unbeaten = true }))
  assert_eq(unbeaten.outcome, "unbeaten")
end)
