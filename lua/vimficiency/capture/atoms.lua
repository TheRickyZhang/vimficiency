-- CmdAtom-driven capture. Replaces the `vim.on_key` key_tracking layer.
--
-- `##CmdAtom` (Neovim 0.13-dev+) fires once per atomic user action, after the
-- action has run, carrying the resolved post-mapping `keys` alongside the
-- as-typed `lhs`. This sidesteps every `vim.on_key` hazard the old layer fought:
-- operator-pending duplication, mapping-LHS leakage, and single-key trigger
-- retraction all disappear because the event already reports one clean,
-- replayable unit per command.
--
-- The event is *deferred*: under fast typeahead every atom for a burst of input
-- arrives back-to-back once control returns to the main loop, by which point the
-- buffer is already at its final state. Subscribers that need pre-action buffer
-- state must therefore shift (atom i's start == atom i-1's end), not snapshot at
-- atom time. See `dev/lua/atom-capture.md`.

local M = {}
local uv = vim.uv
local keynorm = require("vimficiency.capture.keynorm")

--------------------------------------------------------------------------------
-- Types
--------------------------------------------------------------------------------

---@class VF.Atom
---@field type     string        # CmdAtom type: operator|insert|motion|normal|visual|jump|scroll|excmd|mouse|mapping
---@field keys     string        # Replayable post-mapping sequence (notation); "" when unavailable. Feed this to the simulator/optimizer.
---@field lhs      string        # As-typed LHS (notation). The physical keystrokes, for effort/display.
---@field count    integer|nil   # Count folded out of the LHS when a mapping fired (e.g. `3Z`→dd gives lhs=Z, count=3, keys=3dd).
---@field reg      string|nil    # Register the command used, if any.
---@field changed  boolean       # The action modified buffer text.
---@field moved    boolean       # The action moved the cursor.
---@field undoseq  integer|nil   # `undotree().seq_cur` after the action; recall uses it to detect no-op edits.
---@field buf      integer       # Buffer the atom fired for (`ev.buf`).
---@field win      integer       # Focused window at atom time.
---@field t        integer       # hrtime when observed.

--- Atom-intrinsic admission verdict. Buffer/window scoping is the subscriber's
--- job (it compares against its own session), not decided here.
---@alias VF.Atom.Verdict
---| "record"  # Replayable keys; append to in-scope recordings.
---| "mouse"   # Mouse/scroll-wheel activity; don't record, but flag `had_mouse`.
---| "skip"    # Inert for recordings (meta, no-op lossy callback).
---| "abort"   # Lossy *and* consequential; an in-scope session can no longer be trusted.

--------------------------------------------------------------------------------
-- Classification
--------------------------------------------------------------------------------

--- Decide an atom's admission verdict from its raw event fields.
---
--- The ordering is load-bearing:
---   1. Mouse (by key token) is skipped-with-flag whatever else it looks like.
---   2. Ex commands are opaque to the optimizer: a changed buffer can't be
---      replayed as motion (abort), an unchanged one is just a cmdline jump (skip).
---   3. `keys == nil` is a Lua-callback mapping: harmless if it neither changed
---      nor moved (skip), otherwise the edit happened off-sequence (abort).
---   4. `keys == ""` is an unreplayable resolution (abort).
---   5. Anything else carries replayable keys (record).
---@param atom_type string
---@param keys_raw string|nil
---@param keys_norm string
---@param changed boolean
---@param moved boolean
---@return VF.Atom.Verdict
local function classify(atom_type, keys_raw, keys_norm, changed, moved)
  if keys_raw ~= nil and keys_raw ~= "" and keynorm.is_mouse(keys_norm) then
    return "mouse"
  end
  if atom_type == "excmd" then
    return changed and "abort" or "skip"
  end
  if keys_raw == nil then
    return (changed or moved) and "abort" or "skip"
  end
  if keys_raw == "" then
    return "abort"
  end
  return "record"
end

--------------------------------------------------------------------------------
-- Subscriber fan-out
--------------------------------------------------------------------------------

---@type table<string, fun(atom: VF.Atom, verdict: VF.Atom.Verdict)>
local subs = {}
local subs_count = 0
local augroup = nil
local autocmd_id = nil

--- True when the running Neovim exposes `##CmdAtom`.
---@return boolean
function M.available()
  return vim.fn.exists("##CmdAtom") == 1
end

-- Vim folds a *synthetic* count of 1 onto commands the user gave no count to:
-- `ifoo`→`1ifoo`, `A`→`1A`, `rX`→`1rX` all arrive with count=1 and a leading
-- `1`. Genuine counts arrive as count>=2 (or nil), e.g. `2dd`, `J`→`2J`. Drop
-- the synthetic digit so replay and effort match what the user actually typed;
-- stripping only the first char keeps content digits (`i1`→`1i1`→`i1`) intact.
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
  local verdict = classify(d.type, keys_raw, keys_norm, d.changed == true, d.moved == true)

  ---@type VF.Atom
  local atom = {
    type    = d.type,
    keys    = keys_norm,
    lhs     = strip_synthetic_count(keynorm.normalize(d.lhs), d.count),
    count   = d.count,
    reg     = d.reg,
    changed = d.changed == true,
    moved   = d.moved == true,
    undoseq = d.undoseq,
    buf     = ev.buf,
    win     = vim.api.nvim_get_current_win(),
    t       = uv.hrtime(),
  }

  for _, on_atom in pairs(subs) do
    on_atom(atom, verdict)
  end
end

local function ensure_listener()
  if autocmd_id then return end
  augroup = vim.api.nvim_create_augroup("VimficiencyAtoms", { clear = true })
  autocmd_id = vim.api.nvim_create_autocmd("CmdAtom", {
    group = augroup,
    callback = dispatch,
  })
end

--- Attach a named atom subscriber. Returns false on a name collision (the
--- existing subscriber is left untouched) so a double-install surfaces instead
--- of silently shadowing.
---@param name string
---@param on_atom fun(atom: VF.Atom, verdict: VF.Atom.Verdict)
---@return boolean success
function M.subscribe(name, on_atom)
  assert(type(name) == "string" and name ~= "", "atoms.subscribe: name required")
  -- No ##CmdAtom (pre-0.13 Neovim): capture is unavailable. Fail closed so
  -- callers (watch/suggest/recall) degrade to a no-op rather than erroring when
  -- the autocmd can't be created.
  if not M.available() then return false end
  if subs[name] then return false end
  subs[name] = on_atom
  subs_count = subs_count + 1
  ensure_listener()
  return true
end

--- Detach a named subscriber; removes the autocmd when the last one leaves.
---@param name string
function M.unsubscribe(name)
  if not subs[name] then return end
  subs[name] = nil
  subs_count = subs_count - 1
  if subs_count == 0 and autocmd_id then
    vim.api.nvim_del_autocmd(autocmd_id)
    autocmd_id = nil
  end
end

---@param name string
---@return boolean
function M.is_subscribed(name)
  return subs[name] ~= nil
end

--- Tear down the autocmd and drop every subscriber.
function M.shutdown()
  if autocmd_id then
    vim.api.nvim_del_autocmd(autocmd_id)
    autocmd_id = nil
  end
  if augroup then
    pcall(vim.api.nvim_del_augroup_by_id, augroup)
    augroup = nil
  end
  subs = {}
  subs_count = 0
end

-- Test-only: exercise the verdict table without driving real input.
M._classify = classify

return M
