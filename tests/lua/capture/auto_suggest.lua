-- tests/lua/auto_suggest.lua
-- Unit tests for fire_idle's dedup/failure-throttle behavior. The actual
-- cooldown timer is owned by end_trigger.arm_idle; fire_idle communicates
-- with it by returning a boolean (true = "this fire counted, refresh
-- cooldown"; false = "no-op, don't lock out the next tick").
-- Monkey-patches session_store and session with stubs so we can drive
-- outcomes without a live queue or the C++ optimizer.

local config        = require("vimficiency.config")
local auto_suggest  = require("vimficiency.capture.auto_suggest")
local compute       = require("vimficiency.session.compute")
local session_store = require("vimficiency.session.store")
local h             = require("_helpers")

local fire_idle   = auto_suggest._for_test.fire_idle
local fire_keys   = auto_suggest._for_test.fire_keys
local fire_cost   = auto_suggest._for_test.fire_cost
local render      = auto_suggest._for_test.render_suggestion
local get_last_fp = auto_suggest._for_test.get_last_fingerprint
local reset       = auto_suggest._for_test.reset

local IDLE_CFG = h.auto_suggest_config({ idle = { ms = 200, window = "3s" }, cooldown_ms = 5000 })
local KEYS_CFG = h.auto_suggest_config({ keys = { every = 30 }, cooldown_ms = 5000 })
local COST_M2  = h.auto_suggest_config({ cost = { m = 2.0, b = 0.0, ms = 300, window = "100" }, cooldown_ms = 5000 })
local COST_M1  = h.auto_suggest_config({ cost = { m = 1.0, b = 0.0, ms = 300, window = "100" }, cooldown_ms = 5000 })

local ACTIVE = { id = "fake-id", kind = "recall", started_mono_ns = vim.uv.hrtime() }

--- Run `fn` with auto_suggest stubs installed. `spec` keys:
---   cfg     — config.auto_suggest value (default IDLE_CFG)
---   compute — analyze stub returning `result` or `nil, err` (default fake_result())
---   finish  — session_store.convert_to_finished stub (default no-op true)
---   active  — session_store.get_active stub (default returns ACTIVE)
---
--- The async compute is stubbed to resolve `on_done` synchronously, so the
--- fire_* helpers complete in-line and the assertions below stay synchronous.
local function harness(spec, fn)
  spec = spec or {}
  reset()
  ACTIVE.buf = vim.api.nvim_get_current_buf()
  local compute_fn = spec.compute or function() return h.fake_result() end
  local patches = {
    { config, "auto_suggest", spec.cfg or IDLE_CFG },
    { session_store, "get_active",     spec.active  or function() return ACTIVE end },
    { session_store, "convert_to_finished", spec.finish  or function() return true end },
    { compute,       "capture_end_state", function() return {} end },
    { compute,       "compute_result_for_active_async",
        function(_active, _end_state, _deadline_ms, on_done)
          on_done(compute_fn())
          return nil
        end },
  }
  h.with_patch(patches, fn)
end

--- Helper for kind-capture tests: the 4th convert_to_finished arg is the promotion.
local function capture_kind(cfg, compute_stub, trigger)
  local captured
  harness({
    cfg = cfg,
    compute = compute_stub,
    finish = function(...)
      captured = select(4, ...)
      return true
    end,
  }, trigger)
  return captured
end

--------------------------------------------------------------------------------
-- fire_idle
--------------------------------------------------------------------------------

test("fire_idle: no-op when window unresolved (queue too young)", function()
  harness({ active = function() return nil end }, function()
    assert_eq(fire_idle(), false, "unresolved window should not count")
  end)
end)

test("fire_idle: compute and finish failures still count", function()
  harness({ compute = function() return nil, "rejected" end }, function()
    assert_eq(fire_idle(), true,
      "compute failure must count as a fire so retries respect cooldown")
  end)
  harness({ finish = function() return false end }, function()
    assert_eq(fire_idle(), true,
      "store failure must count as a fire so retries respect cooldown")
  end)
end)

test("fire_idle: success counts and records fingerprint", function()
  harness({}, function()
    assert_eq(fire_idle(), true, "success should count")
    assert_true(get_last_fp() ~= nil, "success should record a fingerprint")
  end)
end)

test("fire_idle: promotes the recall record to suggest_idle on takeover", function()
  local captured = { id = "fake-id", kind = "recall", started_mono_ns = vim.uv.hrtime(),
    buf = vim.api.nvim_get_current_buf() }
  harness({
    active = function() return captured end,
    -- Mirror convert_to_finished's real contract: the promotion is applied
    -- atomically with the status transition. A failing finish leaves kind
    -- untouched.
    finish = function(...)
      local kind = select(4, ...)
      if kind then captured.kind = kind end
      return true
    end,
  }, function()
    fire_idle()
    assert_eq(captured.kind, "suggest_idle",
      "recall -> suggest promotion must happen at finish time")
  end)
end)

test("fire_idle: failures leave kind = 'recall'", function()
  for _, spec in ipairs({
    {
      label = "compute failure",
      patch = { compute = function() return nil, "rejected" end },
    },
    {
      label = "finish failure",
      patch = { finish = function() return false end },
    },
  }) do
    local captured = { id = "fake-id", kind = "recall", started_mono_ns = vim.uv.hrtime(),
      buf = vim.api.nvim_get_current_buf() }
    spec.patch.active = function() return captured end
    harness(spec.patch, function()
      fire_idle()
      assert_eq(captured.kind, "recall",
        spec.label .. " must not mutate kind")
    end)
  end
end)

test("fire_idle: single-flight skips a second fire while one is in flight", function()
  reset()
  local pending_on_done
  local finish_count = 0
  -- A fake poll handle with no-op lifecycle methods so reset()/disable() can
  -- safely cancel it on a failure path.
  local fake_handle = { done = false, cancelled = false,
    close_timer = function() end, cancel_fn = function() end }
  ACTIVE.buf = vim.api.nvim_get_current_buf()
  h.with_patch({
    { config, "auto_suggest", IDLE_CFG },
    { session_store, "get_active",     function() return ACTIVE end },
    { session_store, "convert_to_finished", function() finish_count = finish_count + 1; return true end },
    { compute, "capture_end_state", function() return {} end },
    { compute, "compute_result_for_active_async", function(_active, _end_state, _deadline_ms, on_done)
        pending_on_done = on_done
        return fake_handle
      end },
  }, function()
    assert_eq(fire_idle(), true, "first fire kicks the worker and counts")
    assert_eq(auto_suggest._for_test.is_in_flight(), true, "worker is in flight")
    assert_eq(fire_idle(), false, "second fire must be skipped while one is in flight")

    pending_on_done(h.fake_result())
    assert_eq(auto_suggest._for_test.is_in_flight(), false, "in_flight clears on completion")
    assert_eq(finish_count, 1, "only the first fire's result was finished")
  end)
  reset()
end)

--------------------------------------------------------------------------------
-- fire_keys: derives window from cfg.keys.every
--------------------------------------------------------------------------------

test("fire_keys: passes tostring(every) as the recall window", function()
  local captured_alias
  harness({
    cfg = KEYS_CFG,
    active = function(alias) captured_alias = alias; return ACTIVE end,
  }, function()
    fire_keys()
    assert_eq(captured_alias, "30",
      "fire_keys resolves its window from cfg.keys.every (no separate knob)")
  end)
end)

test("fire_keys: no-op when cfg.keys is absent", function()
  harness({}, function()
    assert_eq(fire_keys(), false, "unconfigured trigger must not count")
  end)
end)

--------------------------------------------------------------------------------
-- fire_cost: gate on user_cost vs m*optimal+b
--------------------------------------------------------------------------------

test("fire_cost: gate failure counts and finishes the record but does not fingerprint", function()
  -- 5 > 2*10+0 == 20 → false → gate fails → suppress notify, still count.
  local finish_called = false
  harness({
    cfg = COST_M2,
    compute = function() return h.fake_result({
      user_cost = 5,
      optimal_results = { { seq = "y", cost = 10.0 } },
    }) end,
    finish = function() finish_called = true; return true end,
  }, function()
    assert_eq(fire_cost(), true, "gate failure must still count (optimizer ran)")
    assert_eq(get_last_fp(), nil,
      "gate failure must not record fingerprint — nothing was surfaced")
    assert_eq(finish_called, true,
      "gate failure must still finish the record so next tick doesn't re-analyze it")
  end)
end)

test("fire_cost: gate pass records fingerprint", function()
  -- 20 > 1*10+0 == 10 → true → gate passes.
  harness({
    cfg = COST_M1,
    compute = function() return h.fake_result({
      user_cost = 20,
      optimal_results = { { seq = "y", cost = 10.0 } },
    }) end,
  }, function()
    assert_eq(fire_cost(), true)
    assert_true(get_last_fp() ~= nil,
      "gate pass must fingerprint (the shown suggestion)")
  end)
end)

test("fire_cost: empty optimal_results suppresses notify", function()
  harness({
    cfg = COST_M1,
    compute = function() return h.fake_result({ user_cost = 20, optimal_results = {} }) end,
  }, function()
    assert_eq(fire_cost(), true, "optimizer ran, still count")
    assert_eq(get_last_fp(), nil,
      "no optimal result to compare against → suppress")
  end)
end)

--------------------------------------------------------------------------------
-- Kind plumbing: each trigger must pass its suggest_* kind through to
-- convert_to_finished so the record is promoted and the header can print it.
--------------------------------------------------------------------------------

test("auto_suggest triggers pass their suggest_* kind", function()
  assert_eq(capture_kind(IDLE_CFG, nil, fire_idle), "suggest_idle")
  assert_eq(capture_kind(KEYS_CFG, nil, fire_keys), "suggest_keys")
  local compute_stub = function() return h.fake_result({
    user_cost = 20,
    optimal_results = { { seq = "y", cost = 10.0 } },
  }) end
  assert_eq(capture_kind(COST_M1, compute_stub, fire_cost), "suggest_cost")
end)

--------------------------------------------------------------------------------
-- Turnkey default: `:Vimfy suggest on` with no configured triggers falls back
-- to the built-in cost gate (DEFAULT_COST = m 1.5, b 2.0).
--------------------------------------------------------------------------------

local NO_TRIGGERS = h.auto_suggest_config(nil)
NO_TRIGGERS.cooldown_ms = 5000

test("fire_cost: uses default gate when cfg.cost absent", function()
  harness({
    cfg = NO_TRIGGERS,
    compute = function() return h.fake_result({
      user_cost = 100,
      optimal_results = { { seq = "y", cost = 1.0 } },
    }) end,
  }, function()
    assert_eq(fire_cost(), true, "optimizer ran")
    assert_true(get_last_fp() ~= nil,
      "100 > 1.5*1+2 = 3.5 → default gate passes")
  end)
end)

test("fire_cost: default gate suppresses near-optimal sequences", function()
  harness({
    cfg = NO_TRIGGERS,
    compute = function() return h.fake_result({
      user_cost = 3,
      optimal_results = { { seq = "y", cost = 1.0 } },
    }) end,
  }, function()
    assert_eq(fire_cost(), true, "optimizer ran")
    assert_eq(get_last_fp(), nil,
      "3 <= 1.5*1+2 = 3.5 → default gate suppresses")
  end)
end)

test("enable: turnkey default arms a trigger with no config", function()
  auto_suggest.disable()  -- clean baseline regardless of prior tests
  h.with_patch({ { config, "auto_suggest", NO_TRIGGERS } }, function()
    assert_eq(auto_suggest.is_enabled(), false)
    assert_eq(auto_suggest.enable(), true,
      "enable must arm the default cost gate with no configured triggers")
    assert_eq(auto_suggest.is_enabled(), true)
    auto_suggest.disable()
    assert_eq(auto_suggest.is_enabled(), false)
  end)
end)

--------------------------------------------------------------------------------
-- render_suggestion: notif_cooldown_ms throttles visible toasts. A suppressed
-- toast does not advance last_notify_hrtime; only a shown one does.
--------------------------------------------------------------------------------

local SUGGEST_RESULT = h.fake_result({
  user_cost = 20,
  optimal_results = { { seq = "ciwx", cost = 1.0 } },
})

test("render_suggestion: suppresses a toast within notif_cooldown_ms", function()
  local count = 0
  h.with_patch({
    { config, "auto_suggest", h.auto_suggest_config(nil, { notif_cooldown_ms = 600000 }) },
    { vim, "notify", function() count = count + 1 end },
  }, function()
    reset()
    render(SUGGEST_RESULT, vim.uv.hrtime())
    render(SUGGEST_RESULT, vim.uv.hrtime())
    assert_eq(count, 1, "second toast within cooldown must be suppressed")
  end)
end)

test("render_suggestion: notif_cooldown_ms = 0 never suppresses", function()
  local count = 0
  h.with_patch({
    { config, "auto_suggest", h.auto_suggest_config(nil, { notif_cooldown_ms = 0 }) },
    { vim, "notify", function() count = count + 1 end },
  }, function()
    reset()
    render(SUGGEST_RESULT, vim.uv.hrtime())
    render(SUGGEST_RESULT, vim.uv.hrtime())
    assert_eq(count, 2, "no cooldown → both toasts show")
  end)
end)
