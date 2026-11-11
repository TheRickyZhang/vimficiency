-- tests/lua/manual_autodrop.lua
-- Covers `compute.manual_should_evict` — the pure predicate that decides
-- when an in-progress manual session should be auto-dropped.
--
-- Triggers (checked in order; drift wins when both fire):
--   1. Cursor drifted more than MAX_SEARCH_LINES rows from start_state.row.
--   2. Idle for more than MANUAL_IDLE_TIMEOUT_SECONDS since the last captured key.
--
-- Pure tests: we construct minimal session-record shapes from scratch.
-- No store, no vim.api, no timers.

local compute = require("vimficiency.session.compute")
local config  = require("vimficiency.config")

local NS_PER_SEC = 1e9

--- Build a minimal fake record with just the fields the predicate reads.
---@param start_row integer
---@param last_atom_mono_ns integer|nil  nil for a session with no atoms yet
---@return table
local function fake_session(start_row, last_atom_mono_ns)
  return {
    start_state = { row = start_row },
    last_atom_mono_ns = last_atom_mono_ns,
  }
end

test("manual_should_evict: active nearby sessions are retained", function()
  local s = fake_session(0, nil)
  assert_eq(compute.manual_should_evict(s, 0, 1e15), nil)
  local now = 1e15
  local recent = fake_session(100, now - 10 * NS_PER_SEC)
  assert_eq(compute.manual_should_evict(recent, 105, now), nil)
end)

test("manual_should_evict: last key older than timeout → idle reason", function()
  local now = 1e15
  local stale = now - (config.MANUAL_IDLE_TIMEOUT_SECONDS + 1) * NS_PER_SEC
  local s = fake_session(50, stale)
  local reason = compute.manual_should_evict(s, 50, now)
  assert_true(reason ~= nil, "expected eviction")
  reason = assert(reason)
  assert_true(reason:find("idle", 1, true) ~= nil,
    "expected idle reason, got: " .. tostring(reason))
end)

test("manual_should_evict: cursor drift beyond search window evicts", function()
  local now = 1e15
  local below = fake_session(0, now - 1 * NS_PER_SEC)
  local reason = compute.manual_should_evict(below, config.MAX_SEARCH_LINES + 1, now)
  assert_true(reason ~= nil, "expected eviction")
  reason = assert(reason)
  assert_true(reason:find("drifted", 1, true) ~= nil,
    "expected drift reason, got: " .. tostring(reason))

  -- Start at row 1000, cursor at row 499: upward drift exceeds the window.
  local above = fake_session(1000, now - 1 * NS_PER_SEC)
  reason = compute.manual_should_evict(above, 499, now)
  assert_true(reason ~= nil, "expected eviction (upward drift)")
  reason = assert(reason)
  assert_true(reason:find("drifted", 1, true) ~= nil,
    "expected drift reason, got: " .. tostring(reason))

  local boundary = fake_session(0, now - 1 * NS_PER_SEC)
  assert_eq(compute.manual_should_evict(boundary, config.MAX_SEARCH_LINES - 1, now), nil)
end)

test("manual_should_evict: drift wins over idle and no atoms is safe", function()
  local now = 1e15
  local stale = now - (config.MANUAL_IDLE_TIMEOUT_SECONDS + 10) * NS_PER_SEC
  local s = fake_session(0, stale)
  local reason = compute.manual_should_evict(
    s, config.MAX_SEARCH_LINES + 5, now
  )
  assert_true(reason ~= nil, "expected eviction")
  reason = assert(reason)
  assert_true(reason:find("drifted", 1, true) ~= nil,
    "expected drift reason (drift checked before idle), got: " .. tostring(reason))

  local finished_shape = fake_session(0, nil)
  assert_eq(compute.manual_should_evict(finished_shape, 0, now), nil)
  reason = compute.manual_should_evict(
    finished_shape, config.MAX_SEARCH_LINES + 1, now
  )
  assert_true(reason ~= nil)
  reason = assert(reason)
  assert_true(reason:find("drifted", 1, true) ~= nil)
end)
