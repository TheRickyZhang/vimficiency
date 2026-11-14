-- CmdAtom-driven capture

-- `##CmdAtom` (Neovim 0.13-dev+) fires once per atomic user action, after the
-- action has run, carrying the resolved post-mapping `keys` alongside the
-- as-typed `lhs`.--

-- The event is *deferred*: under fast typeahead every atom for a burst of input
-- arrives back-to-back once control returns to the main loop, by which point the
-- buffer is already at its final state. Subscribers that need pre-action buffer
-- state must therefore shift (atom i's start == atom i-1's end), not snapshot at
-- atom time. See `dev/lua/atom-stream.md`.

---@class (exact) VF.AtomStream
---@field supported       fun(): boolean
---@field add_listener    fun(name: string, listener: fun(atom: VF.Atom, decision: VF.Atom.Decision)): boolean
---@field remove_listener fun(name: string)
---@field shutdown        fun()
--- Test-only: exercise the decision table without driving real input, and
--- assert that sessions and shutdown detach their listeners.
---@field _classify       fun(atom_type: string, keys_raw: string|nil, keys_norm: string, changed: boolean, moved: boolean): VF.Atom.Decision
---@field _describe_abort fun(atom_type: string, keys_raw: string|nil, keys_norm: string, lhs_norm: string, changed: boolean, moved: boolean): string
---@field _lossy_passthrough fun(keys_raw: string|nil, lhs_norm: string, changed: boolean, moved: boolean, permissive: boolean): boolean
---@field _has_listener   fun(name: string): boolean
local M = {}
local config = require("vimficiency.config")
local keynorm = require("vimficiency.capture.keynorm")
local util = require("vimficiency.util")

--------------------------------------------------------------------------------
-- Types
--------------------------------------------------------------------------------

--- One completed user action: the replayable keys plus the scope they fired in.
--- The event's other fields (`type`, `lhs`, `count`, `changed`, `moved`) are
--- consumed by `classify`, `describe_abort` and `strip_synthetic_count` and not forwarded.
---@class VF.Atom
---@field keys         string        # Replayable post-mapping sequence (notation); "" when unavailable. Feed this to the simulator/optimizer.
---@field buf          integer       # Buffer the atom fired for (`ev.buf`).
---@field win          integer       # Focused window at atom time.
---@field lossy        boolean       # `keys` is the typed lhs of a Lua-callback mapping (lossy_mappings ~= "drop"); replay may diverge.
---@field abort_reason string|nil    # Only for "abort": what made the atom unreplayable and what it did, for the user-facing drop notice.

-- What we tag this atom as, informing our decision
---@alias VF.Atom.Decision
---| "record"  # Replayable keys; append to in-scope recordings.
---| "skip"    # Unreplayable but provably inert: neither changed text nor moved the cursor.
---| "abort"   # Unreplayable and had an effect; an in-scope session can no longer be trusted.

---@param atom_type string
---@param keys_raw string|nil
---@param keys_norm string
---@param changed boolean
---@param moved boolean
---@return VF.Atom.Decision
local function classify(atom_type, keys_raw, keys_norm, changed, moved)
  local replayable = keys_raw ~= nil and keys_raw ~= ""
    and atom_type ~= "excmd" and not keynorm.is_mouse(keys_norm)
  if replayable then return "record" end
  return (changed or moved) and "abort" or "skip"
end

--- Names the unreplayable action and its effect, e.g. "ex command `:m .+1` changed
--- the buffer and moved the cursor" or "Lua mapping `<leader>V` moved the cursor".
---@param atom_type string
---@param keys_raw string|nil
---@param keys_norm string
---@param lhs_norm string
---@param changed boolean
---@param moved boolean
---@return string
local function describe_abort(atom_type, keys_raw, keys_norm, lhs_norm, changed, moved)
  local what
  if atom_type == "excmd" then
    what = "ex command `" .. keys_norm:gsub("<[NC][LR]>$", "") .. "`"
  elseif keynorm.is_mouse(keys_norm) then
    what = "mouse input `" .. keys_norm .. "`"
  elseif keys_raw == nil then
    what = "Lua mapping `" .. lhs_norm .. "` (lossy_mappings = \"warn\" or \"allow\" records its typed keys instead)"
  else
    what = "mapping `" .. lhs_norm .. "` with no replayable key expansion"
  end
  local effect = changed and moved and "changed the buffer and moved the cursor"
    or changed and "changed the buffer"
    or "moved the cursor"
  return what .. " " .. effect
end

--- Whether a Lua-callback atom (`keys == nil`) is recorded under its typed lhs.
--- A no-effect callback stays a skip regardless, so its keys never pollute a recording.
---@param keys_raw string|nil
---@param lhs_norm string
---@param changed boolean
---@param moved boolean
---@param permissive boolean
---@return boolean
local function lossy_passthrough(keys_raw, lhs_norm, changed, moved, permissive)
  return permissive and keys_raw == nil and lhs_norm ~= "" and (changed or moved)
end

--------------------------------------------------------------------------------
-- Listener fan-out
--------------------------------------------------------------------------------

---@type table<string, fun(atom: VF.Atom, decision: VF.Atom.Decision)>
local listener_by_name = {}
local listener_count = 0
local augroup = nil
local autocmd_id = nil

--- True when the running Neovim exposes `##CmdAtom`.
---@return boolean
function M.supported()
  return vim.fn.exists("##CmdAtom") == 1
end

-- Vim prepends an additional 1 onto commands, which we want to drop for accurate replay and effort
---@param s string
---@param count integer|nil
---@return string
local function strip_synthetic_count(s, count)
  if count == 1 and s:sub(1, 1) == "1" then return s:sub(2) end
  return s
end

local function dispatch(ev)
  local d = ev.data
  local keys_raw = d.keys
  local keys_norm = strip_synthetic_count(keynorm.normalize(keys_raw), d.count)
  local changed, moved = d.changed == true, d.moved == true
  local lhs_norm = keynorm.normalize(d.lhs)
  local lossy = lossy_passthrough(keys_raw, lhs_norm, changed, moved, config.lossy_mappings ~= "drop")
  if lossy then
    -- `lhs` arrives without the count that `keys` would have carried.
    keys_raw = d.lhs
    keys_norm = (d.count and d.count > 1 and tostring(d.count) or "") .. lhs_norm
  end
  local decision = classify(d.type, keys_raw, keys_norm, changed, moved)

  ---@type VF.Atom
  local atom = {
    keys  = keys_norm,
    buf   = ev.buf,
    win   = vim.api.nvim_get_current_win(),
    lossy = lossy,
  }
  if decision == "abort" then
    atom.abort_reason = describe_abort(d.type, keys_raw, keys_norm, lhs_norm, changed, moved)
  end

  for _, notify_listener in pairs(listener_by_name) do
    notify_listener(atom, decision)
  end
end

local function ensure_autocmd()
  if autocmd_id then return end
  augroup = vim.api.nvim_create_augroup("VimficiencyAtoms", { clear = true })
  autocmd_id = vim.api.nvim_create_autocmd("CmdAtom", {
    group = augroup,
    callback = util.entry(dispatch),
  })
end

--- Attach a named atom listener
---@param name string
---@param listener fun(atom: VF.Atom, decision: VF.Atom.Decision)
---@return boolean success
function M.add_listener(name, listener)
  if listener_by_name[name] then return false end
  listener_by_name[name] = listener
  listener_count = listener_count + 1
  ensure_autocmd()
  return true
end

--- Detach a named listener
---@param name string
function M.remove_listener(name)
  if not listener_by_name[name] then return end
  listener_by_name[name] = nil
  listener_count = listener_count - 1
  if listener_count == 0 and autocmd_id then
    -- removes the autocmd when the last one leaves
    vim.api.nvim_del_autocmd(autocmd_id)
    autocmd_id = nil
  end
end

--- Tear down the autocmd and drop every listener.
function M.shutdown()
  if autocmd_id then
    vim.api.nvim_del_autocmd(autocmd_id)
    autocmd_id = nil
  end
  if augroup then
    vim.api.nvim_del_augroup_by_id(augroup)
    augroup = nil
  end
  listener_by_name = {}
  listener_count = 0
end

M._classify = classify
M._describe_abort = describe_abort
M._lossy_passthrough = lossy_passthrough
function M._has_listener(name) return listener_by_name[name] ~= nil end

return M
