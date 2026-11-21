-- CmdAtom-driven capture

-- `##CmdAtom` (Neovim 0.13-dev+) fires once per atomic user action, after the
-- action has run, carrying the resolved post-mapping `keys` alongside the
-- as-typed `lhs`.--

-- The event is *deferred*: under fast typeahead every atom for a burst of input
-- arrives back-to-back once control returns to the main loop, by which point the
-- buffer is already at its final state. Subscribers that need pre-action buffer
-- state must therefore shift (atom i's start == atom i-1's end), not read it at
-- atom time. See `dev/lua/atom-stream.md`.

---@class (exact) VF.AtomStream
---@field supported       fun(): boolean
---@field add_listener    fun(name: string, listener: fun(atom: VF.Atom, decision: VF.Atom.Decision)): boolean
---@field remove_listener fun(name: string)
---@field shutdown        fun()
---@field in_live_file_buffer fun(buf: integer, win: integer): boolean
--- Test-only: exercise the decision table without driving real input, and
--- assert that sessions and shutdown detach their listeners.
---@field _classify       fun(atom_type: string, keys_raw: string|nil, keys_norm: string, changed: boolean, moved: boolean): VF.Atom.Decision
---@field _describe_abort fun(atom_type: string, keys_raw: string|nil, keys_norm: string, lhs_norm: string, changed: boolean, moved: boolean): string
---@field _lossy_passthrough fun(keys_raw: string|nil, lhs_norm: string, changed: boolean, moved: boolean, permissive: boolean): boolean
---@field _has_listener   fun(name: string): boolean
local M = {}
local config = require("vimficiency.config")
local ffi_session = require("vimficiency.ffi.session")
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
---@field typed        string        # Physical keys pressed since the previous atom (notation). Cost the user by these; never replay them. Atoms delivered together cannot be told apart, so an earlier one carries the keys of a later one, which gets "".
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

--------------------------------------------------------------------------------
-- Pressed keys
--------------------------------------------------------------------------------

local on_key_ns = vim.api.nvim_create_namespace("vimfy_atom_typed")

-- What Neovim read as typed since the last atom, in order: `vim.on_key`'s
-- `typed`, plus what plugins read themselves with getchar(), which on_key never
-- sees. Keys a plugin fed as typed are read again, so they are kept as `fed`.
---@type { keys: string, fed: boolean }[]
local inputs = {}
local any_fed = false
-- Whether the latest mapping ran in Visual mode. Atoms are deferred; `on_key` is not.
local last_mapping_in_visual = false

local function reset_pressed()
  inputs, any_fed = {}, false
end

---@param raw string
---@param fed boolean
local function add_input(raw, fed)
  local keys = keynorm.without_mouse(keynorm.normalize(raw))
  if keys == "" then return end
  inputs[#inputs + 1] = { keys = keys, fed = fed }
  any_fed = any_fed or fed
end

---@param key string
---@param typed string
local function on_key(key, typed)
  local mode = vim.api.nvim_get_mode().mode
  -- Terminal-mode keys never end in an atom, so they would pile onto the next one.
  if mode == "t" then return end
  if key ~= typed then
    local lead = mode:sub(1, 1)
    last_mapping_in_visual = lead == "v" or lead == "V" or lead == "\22"
  end
  if typed ~= "" then add_input(typed, false) end
end

-- getchar(1) only peeks; getchar(0) returns 0 or "" when nothing is pending.
local function wrap_getchar(getchar)
  return function(...)
    local ret = getchar(...)
    local expr = ...
    if autocmd_id and expr ~= 1 and expr ~= true and ret ~= 0 and ret ~= "" then
      add_input(type(ret) == "number" and vim.fn.nr2char(ret) or ret, false)
    end
    return ret
  end
end

local function wrap_feedkeys(feedkeys)
  return function(keys, mode, ...)
    if autocmd_id and mode and mode:find("t", 1, true) then add_input(keys, true) end
    return feedkeys(keys, mode, ...)
  end
end

-- Installed once and left in place: unwrapping could drop another plugin's
-- wrapper around ours. They record only while the autocmd exists. Plugins that
-- saved the original function before this, and Vimscript callers, go unseen.
local wrapped = false
local function wrap_input_functions()
  if wrapped then return end
  wrapped = true
  vim.fn.getchar = wrap_getchar(vim.fn.getchar)
  vim.fn.getcharstr = wrap_getchar(vim.fn.getcharstr)
  vim.fn.feedkeys = wrap_feedkeys(vim.fn.feedkeys)
  vim.api.nvim_feedkeys = wrap_feedkeys(vim.api.nvim_feedkeys)
end

--- The keys pressed for the atom being dispatched, and forgets them.
---@return string
local function take_pressed()
  local taken, fed = inputs, any_fed
  reset_pressed()
  if fed then return ffi_session.typed_keys(taken) end
  local parts = {}
  for i, input in ipairs(taken) do parts[i] = input.keys end
  return table.concat(parts)
end

local function dispatch(ev)
  local d = ev.data
  local keys_raw = d.keys
  -- An operator over a Lua-mapped motion arrives with keys, unlike the bare
  -- mapping, but they hold the callback and are as lossy as its nil.
  if keys_raw and d.type ~= "excmd" and keynorm.has_callback(keys_raw) then keys_raw = nil end
  local keys_norm = strip_synthetic_count(keynorm.normalize(keys_raw), d.count)
  local changed, moved = d.changed == true, d.moved == true
  -- Neovim reports a mapped move inside a selection twice: alone with nil keys,
  -- then within the Visual atom's keys. The first is a no-op to us.
  if d.type == "mapping" and keys_raw == nil and not changed and last_mapping_in_visual then moved = false end
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
    typed = take_pressed(),
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
  reset_pressed()
  wrap_input_functions()
  vim.on_key(on_key, on_key_ns)
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
    vim.on_key(nil, on_key_ns)
  end
end

--- Tear down the autocmd and drop every listener.
function M.shutdown()
  if autocmd_id then
    vim.api.nvim_del_autocmd(autocmd_id)
    autocmd_id = nil
    vim.on_key(nil, on_key_ns)
  end
  if augroup then
    vim.api.nvim_del_augroup_by_id(augroup)
    augroup = nil
  end
  listener_by_name = {}
  listener_count = 0
end

--- Scope shared by the whole-stream listeners (recall, journal).
---@param buf integer
---@param win integer
---@return boolean
function M.in_live_file_buffer(buf, win)
  return vim.api.nvim_buf_is_valid(buf)
    and vim.bo[buf].buftype == ""
    and vim.api.nvim_win_is_valid(win)
    and vim.api.nvim_win_get_buf(win) == buf
end

M._classify = classify
M._describe_abort = describe_abort
M._lossy_passthrough = lossy_passthrough
function M._has_listener(name) return listener_by_name[name] ~= nil end

return M
