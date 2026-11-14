-- Auto-suggest surfaces optimizer results without an explicit `:Vimfy finish`.
-- It reads `config.auto_suggest` and arms any configured idle/keys/cost triggers.

local M = {}

local config        = require("vimficiency.config")
local end_trigger   = require("vimficiency.capture.end_trigger")
local poller        = require("vimficiency.async.poller")
local compute       = require("vimficiency.session.compute")
local session_store = require("vimficiency.session.store")

local SUBSCRIBER_IDLE = "auto_suggest_idle"
local SUBSCRIBER_KEYS = "auto_suggest_keys"
local SUBSCRIBER_COST = "auto_suggest_cost"

---@type fun()[]  Disarm handles from every armed trigger. Empty == disabled.
local disarms = {}
---@type string|nil  Fingerprint of the most recent shown suggestion.
local last_fingerprint = nil
---@type integer  hrtime of the most recent shown toast (notif_cooldown_ms gate).
local last_notify_hrtime = 0
---@type VF.Async.PollHandle|nil  In-flight analyze worker; single-flight guard.
local in_flight = nil

---@param result VF.Session.Result
---@return string
local function fingerprint_result(result)
  -- Include the top result so slice changes still count as new suggestions.
  local first_opt = result.optimal_results[1]
  local opt_str = first_opt
    and string.format("%s:%.4f", first_opt.seq, first_opt.cost)
    or ""
  return string.format("%d:%d:%d:%d|%s|%s",
    result.start_row, result.start_col, result.end_row, result.end_col,
    result.user_seq, opt_str)
end

-- Compact, glanceable toast: how long ago the capture was (keys + seconds) and
-- the effort delta (user → best). The full side-by-side breakdown is one command
-- away via the `$` selector (the record is already finished + stored at this
-- point). `id` lets a notifier (snacks/nvim-notify) replace the prior suggestion
-- in place instead of stacking; plain `vim.notify` ignores it.
---@param result VF.Session.Result
---@param started_mono_ns integer
local function render_suggestion(result, started_mono_ns)
  local optimal = result.optimal_results[1]
  if not optimal then return end

  -- Drop toasts landing within notif_cooldown_ms of the previous shown one.
  -- The record is already finished + stored, so a suppressed suggestion is
  -- still reachable via `$` / `:Vimfy list`.
  local notif_cooldown_ns = config.auto_suggest.notif_cooldown_ms * 1e6
  local now = vim.uv.hrtime()
  if last_notify_hrtime > 0 and (now - last_notify_hrtime) < notif_cooldown_ns then
    return
  end
  last_notify_hrtime = now

  local ago = string.format("%d keys · %.0fs ago",
    result.key_count, (now - started_mono_ns) / 1e9)

  local timing = result.analyze_ms and string.format(" · %.0fms", result.analyze_ms) or ""
  local body = string.format(
    "vimfy regression · %s · %.0f → %.0f%s\n:Vimfy view $  ·  play $  ·  explore $",
    ago, result.user_cost, optimal.cost, timing)

  vim.notify(body, vim.log.levels.INFO, { title = "vimfy suggest", id = "vimfy_suggest" })
end

--- Shared path for every trigger.
--- Returns the cooldown signal for `end_trigger`.
---
---@param recall_alias string                        Recall alias to analyze.
---@param gate? fun(result: VF.Session.Result): boolean|nil  Optional veto. Return false to suppress notification; the record is still consumed.
---@param kind "suggest_idle"|"suggest_keys"|"suggest_cost"  Which trigger fired; the recall record is promoted to it at finish.
---@return boolean counted
local function fire_with_window(recall_alias, gate, kind)
  -- Single-flight: a previous suggest analysis is still running on the worker.
  -- Skip this fire (cooldown-like); the in-flight job finishes and renders.
  if in_flight then return false end

  local active = session_store.get_active(recall_alias)
  if not active then return false end  -- queue too young or no clean boundary

  -- Pin the resolved id; recall aliases are time-varying.
  local id = active.id
  local deadline_ms = config.auto_suggest.compute_deadline_ms

  -- The ring is cross-buffer; a record from a buffer the user has left is
  -- skipped silently but still counts toward the cooldown.
  if active.buf ~= vim.api.nvim_get_current_buf() then return true end

  -- Snapshot the live buffer on the main thread, then offload the analyze to a
  -- worker; finish + render in the callback.
  local end_state = compute.capture_end_state(active)
  in_flight = compute.compute_result_for_active_async(active, end_state, deadline_ms, function(result)
    in_flight = nil
    if not result then return end

    -- Cost gate: consume the record but suppress the notification.
    if gate and not gate(result) then
      session_store.convert_to_finished(id, result, recall_alias, kind)
      return
    end

    local fp = fingerprint_result(result)
    if fp == last_fingerprint then
      session_store.convert_to_finished(id, result, recall_alias, kind)
      return
    end

    if not session_store.convert_to_finished(id, result, recall_alias, kind) then
      return
    end

    last_fingerprint = fp
    -- Pin this regression as `$` (distinct from `@`'s any-finish history).
    session_store.set_last_suggest_id(id)

    render_suggestion(result, active.started_mono_ns)
  end)

  -- Work was started; refresh the cooldown so we don't re-fire while the worker runs.
  return true
end

---@return boolean counted
local function fire_idle()
  local cfg = config.auto_suggest
  if not cfg or not cfg.idle then return false end
  return fire_with_window(cfg.idle.window, nil, "suggest_idle")
end

---@return boolean counted
local function fire_keys()
  local cfg = config.auto_suggest
  if not cfg or not cfg.keys then return false end
  return fire_with_window(tostring(cfg.keys.every), nil, "suggest_keys")
end

---@return boolean counted
local function fire_cost()
  local cfg = config.auto_suggest
  if not cfg then return false end
  local cost = cfg.cost or cfg.default_cost
  local m, b = cost.m, cost.b
  return fire_with_window(cost.window, function(result)
    local optimal = result.optimal_results[1]
    if not optimal then return false end
    return result.user_cost > m * optimal.cost + b
  end, "suggest_cost")
end

--- Startup entry: arm only the triggers declared in `setup()`. Default-config
--- users get nothing here; their path is `:Vimfy suggest on`.
---@return boolean armed
function M.enable_configured()
  if #disarms > 0 then return false end
  local cfg = config.auto_suggest
  if not cfg then return false end  -- master off (auto_suggest = false)

  local cooldown_ms = cfg.cooldown_ms

  if cfg.idle then
    local d = end_trigger.arm_idle({
      name        = SUBSCRIBER_IDLE,
      idle_ms     = cfg.idle.ms,
      cooldown_ms = cooldown_ms,
      on_fire     = fire_idle,
    })
    if d then table.insert(disarms, d) end
  end

  if cfg.keys then
    local d = end_trigger.arm_keys({
      name        = SUBSCRIBER_KEYS,
      every       = cfg.keys.every,
      cooldown_ms = cooldown_ms,
      on_fire     = fire_keys,
    })
    if d then table.insert(disarms, d) end
  end

  if cfg.cost then
    local d = end_trigger.arm_idle({
      name        = SUBSCRIBER_COST,
      idle_ms     = cfg.cost.ms,
      cooldown_ms = cooldown_ms,
      on_fire     = fire_cost,
    })
    if d then table.insert(disarms, d) end
  end

  return #disarms > 0
end

--- `:Vimfy suggest on`: the declared triggers, or with zero config the turnkey
--- default cost gate (fire_cost falls back to cfg.default_cost when cfg.cost is absent).
---@return boolean armed
function M.enable()
  if M.enable_configured() then return true end
  if #disarms > 0 then return false end
  local cfg = config.auto_suggest
  if not cfg then return false end

  local d = end_trigger.arm_idle({
    name        = SUBSCRIBER_COST,
    idle_ms     = cfg.default_cost.ms,
    cooldown_ms = cfg.cooldown_ms,
    on_fire     = fire_cost,
  })
  if d then table.insert(disarms, d) end
  return #disarms > 0
end

function M.disable()
  if #disarms == 0 then return end
  for _, d in ipairs(disarms) do d() end
  disarms = {}
  if in_flight then
    poller.cancel(in_flight)
    in_flight = nil
  end
  last_fingerprint = nil
  last_notify_hrtime = 0
end

---@return boolean
function M.is_enabled()
  return #disarms > 0
end

-- Test-only exports.
M._for_test = {
  fire_idle = fire_idle,
  fire_keys = fire_keys,
  fire_cost = fire_cost,
  render_suggestion = render_suggestion,
  get_last_fingerprint = function() return last_fingerprint end,
  -- The fire_* helpers now kick the analyze onto a worker; tests must wait for
  -- this to clear (e.g. vim.wait) before asserting on the rendered suggestion.
  is_in_flight = function() return in_flight ~= nil end,
  reset = function()
    if in_flight then
      poller.cancel(in_flight)
      in_flight = nil
    end
    last_fingerprint = nil
    last_notify_hrtime = 0
  end,
}

return M
