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
---@field _has_listener   fun(name: string): boolean
local M = {}
local keynorm = require("vimficiency.capture.keynorm")

--------------------------------------------------------------------------------
-- Types
--------------------------------------------------------------------------------

--- One completed user action: the replayable keys plus the scope they fired in.
--- The event's other fields (`type`, `lhs`, `count`, `changed`, `moved`) are
--- consumed by `classify` and `strip_synthetic_count` and not forwarded.
---@class VF.Atom
---@field keys     string        # Replayable post-mapping sequence (notation); "" when unavailable. Feed this to the simulator/optimizer.
---@field buf      integer       # Buffer the atom fired for (`ev.buf`).
---@field win      integer       # Focused window at atom time.

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
  local decision = classify(d.type, keys_raw, keys_norm, d.changed == true, d.moved == true)

  ---@type VF.Atom
  local atom = {
    keys = keys_norm,
    buf  = ev.buf,
    win  = vim.api.nvim_get_current_win(),
  }

  for _, notify_listener in pairs(listener_by_name) do
    notify_listener(atom, decision)
  end
end

local function ensure_autocmd()
  if autocmd_id then return end
  augroup = vim.api.nvim_create_augroup("VimficiencyAtoms", { clear = true })
  autocmd_id = vim.api.nvim_create_autocmd("CmdAtom", {
    group = augroup,
    callback = dispatch,
  })
end

--- Attach a named atom listener
---@param name string
---@param listener fun(atom: VF.Atom, decision: VF.Atom.Decision)
---@return boolean success
function M.add_listener(name, listener)
  assert(type(name) == "string" and name ~= "", "atom_stream.add_listener: name required")
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
    pcall(vim.api.nvim_del_augroup_by_id, augroup)
    augroup = nil
  end
  listener_by_name = {}
  listener_count = 0
end

M._classify = classify
function M._has_listener(name) return listener_by_name[name] ~= nil end

return M
