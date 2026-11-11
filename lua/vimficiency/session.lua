local v = vim.api

local alias_mod = require("vimficiency.session.alias")
local config = require("vimficiency.config")
local compute = require("vimficiency.session.compute")
local atom_stream = require("vimficiency.capture.atom_stream")
local end_trigger = require("vimficiency.capture.end_trigger")
local result_view = require("vimficiency.session.result_view")
local session_store = require("vimficiency.session.store")
local util = require("vimficiency.util")

local M = {}

---@param alias string
local function validate_manual_alias(alias)
  if not alias_mod.is_valid_manual(alias) then
    error("Invalid session alias '" .. tostring(alias) .. "'. " ..
      "Manual aliases are alphabetic only (e.g. 'a', 'refactor').", 0)
  end
end

--- Reason to drop an active manual/watch session, or nil to keep it. Checked on
--- every atom: window closed, cursor drifted out of range, or idle too long.
---@param session VF.Session.Active
---@param end_kind VF.Session.EndKind
---@return string|nil
local function manual_evict_reason(session, end_kind)
  local noun = end_kind == "auto" and "watch" or "session"
  if not v.nvim_win_is_valid(session.win) then
    return "Vimfy: " .. noun .. " [" .. session.id .. "] dropped — window closed"
  end
  local cursor = v.nvim_win_get_cursor(session.win)
  local reason = compute.manual_should_evict(session, cursor[1] - 1, vim.uv.hrtime())
  if reason then
    return "Vimfy: " .. noun .. " dropped — " .. reason
  end
  return nil
end

--- Atom handler for one manual/watch session. Eviction runs first on every atom,
--- then atoms are scoped to the session window and admitted by decision.
---@param alias string
---@param id string
---@param end_kind VF.Session.EndKind
---@return fun(atom: VF.Atom, decision: VF.Atom.Decision)
local function session_atom_handler(alias, id, end_kind)
  return function(atom, decision)
    local session = session_store.get_active(alias)
    if not session or session.id ~= id then return end

    local reason = manual_evict_reason(session, end_kind)
    if reason then
      session_store.remove(id)
      vim.schedule(function() vim.notify(reason, vim.log.levels.WARN) end)
      return
    end

    if atom.win ~= session.win then return end

    if decision == "abort" then
      session_store.remove(id)
      vim.schedule(function()
        vim.notify("Vimfy: session [" .. alias .. "] dropped — an unreplayable " ..
          "command (ex command, mouse, or lossy mapping) changed the buffer or moved the cursor",
          vim.log.levels.WARN)
      end)
    elseif decision == "record" then
      table.insert(session.atoms, atom)
      session.key_count = session.key_count + 1
    end
  end
end

---@param alias string
---@param end_kind VF.Session.EndKind
---@return VF.Session.Active
local function start_manual_session(alias, end_kind)
  validate_manual_alias(alias)
  if not session_store.can_store_manual(alias) then
    error("Manual session capacity reached", 0)
  end

  local buf = v.nvim_get_current_buf()
  local win = v.nvim_get_current_win()
  local id = util.new_id(buf)
  local start_state = util.capture_state(buf, win)

  atom_stream.add_listener(id, session_atom_handler(alias, id, end_kind))

  return session_store.new_active_session(
    id, id, win, buf, start_state, "manual", end_kind)
end

---@param verb string
---@param alias string
---@param overwrote boolean
local function notify_started(verb, alias, overwrote)
  vim.notify("vimfy " .. verb .. " [" .. alias .. "]" .. (overwrote and " (overwrote existing)" or ""),
    vim.log.levels.INFO)
end

---@param alias string
function M.start(alias)
  local active = start_manual_session(alias, "manual")
  notify_started("started", alias, session_store.store_manual(alias, active))
end

---@param alias string
function M.watch(alias)
  local cfg = config.watch
  if not cfg or not cfg.idle then
    error("Watch is not configured. Add `watch = { idle = { ms = N }, cooldown_ms = N }` to setup{}.", 0)
  end

  local active = start_manual_session(alias, "auto")
  local id = active.id

  local disarm = end_trigger.arm_idle({
    name = "watch_" .. id,
    idle_ms = cfg.idle.ms,
    cooldown_ms = cfg.cooldown_ms,
    on_fire = function()
      local rec = session_store.get_active(alias)
      if not rec or rec.id ~= id then return false end
      M.finish(alias, "watch_idle")
      return true
    end,
  })
  if not disarm then
    atom_stream.remove_listener(active.stream_sub)
    error("vimfy watch [" .. alias .. "] failed to arm idle trigger", 0)
  end

  active.watch_disarm = disarm
  notify_started("watching", alias, session_store.store_manual(alias, active))
end

---@param active VF.Session.Active
---@param alias string
---@param reason VF.Session.FinishReason
local function finish_active(active, alias, reason)
  local result, err = compute.compute_result_for_active(active)
  if not result then
    -- Not raised: watch's idle timer also lands here. A manual record is dropped
    -- (its window is gone); a recall slice stays in the ring for a later window.
    util.show_output("finish()", err or "unknown error", {
      ui_keys = { title = "Vimfy Scratch Output Keys", docs = true },
    })
    if active.start_kind == "manual" then
      session_store.remove(active.id)
    end
    vim.schedule(function() vim.notify("finish()", vim.log.levels.ERROR) end)
    return
  end

  -- `active` came from get_active a moment ago and nothing ran in between.
  assert(session_store.mark_finished(active.id, result, alias, nil, reason))

  local message = result_view.format_message("vimfy finished [" .. alias .. "]", result)
  if result.analyze_ms then
    message = message .. string.format("\n  optimizer: %.0f ms", result.analyze_ms)
  end
  vim.notify(message, vim.log.levels.INFO)
end

---@param alias string
---@param reason VF.Session.FinishReason
function M.finish(alias, reason)
  local session_type = alias_mod.parse(alias)
  if session_type == "recall_key" or session_type == "recall_time" then
    error("`:Vimfy finish " .. alias .. "` is not a manual handle. " ..
      "Use `:Vimfy recall " .. alias .. "` for retrospective windows.", 0)
  end
  if not session_type then
    error("Invalid session alias '" .. alias .. "'. Manual aliases are alphabetic only.", 0)
  end

  local active = session_store.get_active(alias)
  if not active then
    error("Session '" .. alias .. "' not found or already finished.", 0)
  end
  finish_active(active, alias, reason)
end

---@param alias string
function M.recall(alias)
  local session_type = alias_mod.parse(alias)
  if session_type == "manual" then
    error("`:Vimfy recall " .. alias .. "` expects a recall window (N or Ns). " ..
      "Use `:Vimfy finish " .. alias .. "` to finish a manual session.", 0)
  end
  if session_type ~= "recall_key" and session_type ~= "recall_time" then
    error("Invalid recall alias '" .. tostring(alias) .. "'. " ..
      "Expected N (commands ago) or Ns (seconds ago).", 0)
  end

  local active = session_store.get_active(alias)
  if not active then
    if session_type == "recall_key" then
      error("No recall session found for '" .. alias .. "' commands ago.", 0)
    end
    error("No recall session found within '" .. alias .. "'. " ..
      "Try a larger window or indexing by key count (e.g. 'recall 20').", 0)
  end
  finish_active(active, alias, "manual")
end

---@param alias string
function M.close(alias)
  local active = session_store.get_active(alias)
  if not active then
    error("Session '" .. tostring(alias) .. "' not found or already closed", 0)
  end
  session_store.remove(active.id)
  vim.notify("vimfy closed [" .. alias .. "]", vim.log.levels.INFO)
end

return M
