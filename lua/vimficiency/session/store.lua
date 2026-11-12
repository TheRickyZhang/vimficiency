-- Canonical session store: `active_sessions` and `finished_sessions`, both
-- keyed by id, plus two indexes spanning both: `named_to_id` for named aliases
-- and `recall_id_order` for the rolling recall ring. Every alias kind
-- (`alias.lua`) resolves to an id here.

---@class (exact) VF.SessionStore
---
--- Kinds
---@field session_type            fun(kind: VF.Session.Kind): VF.Session.Type
---
--- Creating and storing active records
---@field new_active_session      fun(id: string, stream_sub: string|nil, win: integer, buf: integer, start_state: VF.BufferState, kind: "mark"|"watch"|"recall"): VF.Session.Active
---@field has_room_for_named      fun(alias: string): boolean
---@field store_named             fun(alias: string, record: VF.Session.Active): boolean
---@field store_recall            fun(record: VF.Session.Active)
---@field append_atom             fun(record: VF.Session.Active, atom: VF.Atom)
---@field ingest_recall_atom      fun(atom: VF.Atom)
---@field abort_recall            fun(buf: integer, win: integer)
---
--- Lifecycle
---@field convert_to_finished           fun(id: string, result: VF.Session.Result, finish_alias: string|nil, kind?: VF.Session.Kind): boolean
---@field register_fetched_result fun(alias: string, result: VF.Session.Result): string|nil, string|nil
---@field remove                  fun(id: string)
---@field rename_alias            fun(old_alias: string, new_alias: string): boolean, string|nil
---
--- Lookup by alias or id
---@field get_id                  fun(alias: string): string|nil
---@field get_active              fun(alias: string): VF.Session.Active|nil
---@field get_result              fun(alias: string): VF.Session.Result|nil
---@field has_result              fun(alias: string): boolean
---@field get_result_by_id        fun(id: string): VF.Session.Result|nil
---@field get_finish_alias        fun(alias: string): string|nil
---@field list_aliases            fun(): string[]
---
--- `@` / `$` selectors
---@field get_last_finished_id    fun(): string|nil
---@field get_last_suggest_id     fun(): string|nil
---@field set_last_suggest_id     fun(id: string)
---
--- View-model
---@field summarize               fun(id: string): VF.Session.Summary|nil
---@field summarize_all           fun(): VF.Session.Summary[]
---
--- Reload
---@field teardown_active         fun(): integer
---@field dump_for_reload         fun(): VF.Session.ReloadDump
---@field restore_from_dump       fun(dump: VF.Session.ReloadDump)

local M = {}

---@class VF.Session.ReloadDump
---@field finished      table<string, VF.Session.Finished>
---@field named         table<string, string>
---@field order         string[]
---@field last_finished string|nil
---@field last_suggest  string|nil

local alias_mod = require("vimficiency.session.alias")
local atom_stream = require("vimficiency.capture.atom_stream")
local util = require("vimficiency.util")
local config = require("vimficiency.config")

--------------------------------------------------------------------------------
-- Types
--------------------------------------------------------------------------------

--- How a session came to exist and how it ends. Set at creation (mark/watch by
--- the user, recall by the ring); auto_suggest promotes a recall record to the
--- suggest_* that fired, at finish. The trigger detail exists only for suggest
--- because only suggest has several triggers.
---@alias VF.Session.Kind
---| "mark"          # `:Vimfy start` → `:Vimfy finish`
---| "watch"         # `:Vimfy watch` → idle timer
---| "recall"        # rolling ring → `:Vimfy recall`
---| "suggest_idle"  # recall record finished by auto_suggest.idle
---| "suggest_keys"  # recall record finished by auto_suggest.keys
---| "suggest_cost"  # recall record finished by auto_suggest.cost

--- Kind with the suggest trigger folded away; what pickers and stats group by.
---@alias VF.Session.Type "mark" | "watch" | "recall" | "suggest"

local CREATABLE_KINDS = { mark = true, watch = true, recall = true }
local SUGGEST_KINDS = { suggest_idle = true, suggest_keys = true, suggest_cost = true }

---@param kind VF.Session.Kind
---@return VF.Session.Type
function M.session_type(kind)
  if SUGGEST_KINDS[kind] then return "suggest" end
  assert(CREATABLE_KINDS[kind], "invalid session kind: " .. tostring(kind))
  return kind --[[@as VF.Session.Type]]
end

--- Fields meaningful across a session's whole lifecycle.
---@class VF.Session.Core
---@field id           string
---@field kind            VF.Session.Kind
---@field started_mono_ns integer        # vim.uv.hrtime() when the record entered the store (fetch time for disk-loaded records). Monotonic, process-local: the clock for recall ordering, eviction, and "N ago" display

--- Capturing. Owns its atom listener and, for watch, the idle trigger.
---@class VF.Session.Active : VF.Session.Core
---@field win          integer           # Window where session started
---@field buf          integer           # Buffer where session started
---@field start_state  VF.BufferState    # Cursor/viewport at start
---@field atoms        VF.Atom[]
---@field last_atom_mono_ns integer|nil # vim.uv.hrtime() of the last appended atom; nil until the first. Drives idle eviction
---@field stream_sub   string|nil        # Atom listener name this record owns (mark/watch); nil for recall (shared listener)
---@field watch_disarm fun()|nil         # Watch only: disarm for the idle end-trigger armed at :Vimfy watch. Called on finish/destroy so the timer and listener are released.

--- Finished or disk-loaded. `result` carries key_count, kind, and the only
--- persisted clock, `finished_wall_epoch`.
---@class VF.Session.Finished : VF.Session.Core
---@field finished_mono_ns integer       # vim.uv.hrtime() at finish (fetch time for disk-loaded records)
---@field finish_alias     string|nil    # Literal alias passed to finish (`a`, `3s`, ...); the default filename for `:Vimfy save`
---@field result           VF.Session.Result

--- VF.Session.Result: data for a completed session, ready for simulate().
---@class VF.Session.Result
---@field lines string[]               # Trimmed buffer lines used for optimization
---@field goal_lines string[]|nil      # Goal buffer slice for recomputing explore recommendations
---@field start_row integer            # 0-indexed, relative to lines
---@field start_col integer            # 0-indexed
---@field end_row integer              # 0-indexed, relative to lines
---@field end_col integer              # 0-indexed
---@field has_lines_above boolean|nil  # Whether the optimization slice omitted lines above
---@field has_lines_below boolean|nil  # Whether the optimization slice omitted lines below
---@field prefix string|nil            # Reserved boundary prefix; empty at the whole-buffer level (see compute.lua)
---@field suffix string|nil            # Reserved boundary suffix; empty at the whole-buffer level
---@field diffs VF.Diff.Region[]|nil   # Optimizer character-level diff regions for column highlighting; absent on pre-diff saved files
---@field window_height integer|nil    # Captured window height for page/scroll semantics
---@field scroll_amount integer|nil    # Captured 'scroll' option for Ctrl-D/U semantics
---@field user_seq string              # What the user typed (keytrans string), mouse events stripped
---@field user_cost number             # Effort cost of user's sequence
---@field optimal_results VF.Optimizer.Result[] # Top N results from optimizer (seq + cost)
---@field capture_debug table|nil      # Per-atom key boundaries behind user_seq, persisted only for debugging
---@field key_count integer            # Captured key events at finish (authoritative; user_seq is bytes, not keys)
---@field finished_wall_epoch integer  # os.time() at finish. Wall clock because it is saved to disk; monotonic values do not survive a reboot
---@field analyze_ms number|nil        # Wall-time of the optimizer analyze() FFI call only (ms); running-model telemetry
---@field kind? VF.Session.Kind        # Set by convert_to_finished; absent on saved files that predate it (fetch treats them as mark)

--- Normalized view-model for list/suggest UIs.
--- `display_alias` is presentation-only; use `id` for follow-up actions.
---@class VF.Session.Summary
---@field id            string
---@field kind          VF.Session.Kind
---@field type          VF.Session.Type    # `session_type(kind)`. Switch on this for exhaustive dispatch.
---@field display_alias string|nil     # For display. Time-varying for recall; do not re-feed.
---@field status        "active" | "finished"
---@field started_mono_ns  integer
---@field finished_mono_ns integer|nil  # nil while active
---@field key_count     integer        # Keystrokes captured so far
---@field preview       string         # First ~20 chars of user_seq for display
---@field result        VF.Session.Result|nil

--------------------------------------------------------------------------------
-- Constructor
--------------------------------------------------------------------------------

---@param id string
---@param stream_sub string|nil  Atom listener this record owns (mark/watch); nil for recall
---@param win integer
---@param buf integer
---@param start_state VF.BufferState
---@param kind "mark"|"watch"|"recall"
---@return VF.Session.Active
function M.new_active_session(id, stream_sub, win, buf, start_state, kind)
  assert(type(id) == "string" and id ~= "", "session.id must be nonempty string")
  assert(stream_sub == nil or type(stream_sub) == "string", "stream_sub must be a string or nil")
  assert(vim.api.nvim_win_is_valid(win), "win must be a valid window id")
  assert(vim.api.nvim_buf_is_valid(buf), "buf is not valid: " .. buf)
  assert(CREATABLE_KINDS[kind],
    "kind must be 'mark', 'watch', or 'recall', got: " .. tostring(kind))

  return {
    id = id,
    stream_sub = stream_sub,
    win = win,
    buf = buf,
    start_state = start_state,
    started_mono_ns = vim.uv.hrtime(),
    atoms = {},
    kind = kind,
  }
end

---@param record VF.Session.Active
---@param atom VF.Atom
function M.append_atom(record, atom)
  table.insert(record.atoms, atom)
  record.last_atom_mono_ns = vim.uv.hrtime()
end

--------------------------------------------------------------------------------
-- Constants
--------------------------------------------------------------------------------

-- Concurrent active named-alias sessions. Eviction (window closed, cursor drift,
-- idle) is what bounds them in practice; this is a runaway guard against
-- something starting named sessions in a loop. Finished records keep their
-- alias slot (for `save @` / `view <alias>`) but do not count against it.
local NAMED_CAPACITY = 100

--------------------------------------------------------------------------------
-- Storage
--------------------------------------------------------------------------------

---@type table<string, VF.Session.Active>
local active_sessions = {}

---@type table<string, VF.Session.Finished>
local finished_sessions = {}

---@type table<string, string>  -- named alias -> id
local named_to_id = {}

---@type string[]  -- ordered deque of recall ids (oldest first, newest last)
local recall_id_order = {}

---@type string|nil  The id of the most recently finished session; behind the
---                   `@` alias. Cleared when the record is destroyed (name
---                   overwrite, recall eviction, recall disable).
local last_finished_id = nil

---@type string|nil  The id of the most recent suggest regression that was
---                   surfaced (auto_suggest gate passed); behind the `$` alias.
---                   A subset of `last_finished_id` history: only set for
---                   suggest finishes that actually fired a toast, not for
---                   gate-suppressed or duplicate ones.
local last_suggest_id = nil

---@param id string
---@return VF.Session.Core|nil
local function any_session(id)
  return active_sessions[id] or finished_sessions[id]
end

--------------------------------------------------------------------------------
-- Alias resolution
--------------------------------------------------------------------------------

--- Youngest recall record index whose `started_mono_ns <= target_mono_ns`, or nil
--- if the window reaches before the oldest retained record. Each atom is a
--- complete command, so every ring entry is already a clean command boundary —
--- no mode-based backward snapping is needed (unlike the old per-keystroke ring).
---@param target_mono_ns integer
---@return integer|nil index
local function resolve_recall_cutoff(target_mono_ns)
  for i = #recall_id_order, 1, -1 do
    local rec = any_session(recall_id_order[i])
    if rec and rec.started_mono_ns <= target_mono_ns then
      return i
    end
  end
  return nil
end

--- Id of the most recently finished session, or nil if none / evicted.
---@return string|nil id
function M.get_last_finished_id()
  if not last_finished_id then return nil end
  if not finished_sessions[last_finished_id] then return nil end
  return last_finished_id
end

--- Id of the most recent suggest regression, or nil if none / evicted.
---@return string|nil id
function M.get_last_suggest_id()
  if not last_suggest_id then return nil end
  if not finished_sessions[last_suggest_id] then return nil end
  return last_suggest_id
end

--- The one place an alias of any kind becomes a session id.
---@param alias string
---@return string|nil id
local function convert_alias_to_id(alias)
  local kind, value = alias_mod.parse(alias)
  if not kind then return nil end

  if kind == "named" then return named_to_id[alias] end
  if kind == "last_finished" then return M.get_last_finished_id() end
  if kind == "last_suggest" then return M.get_last_suggest_id() end

  if kind == "recall_key" then
    local index = #recall_id_order - value + 1
    if index >= 1 and index <= #recall_id_order then
      return recall_id_order[index]
    end
    return nil
  end

  -- recall_time: `Ns`
  local target = vim.uv.hrtime() - value * 1e9

  -- Do not silently fall back to a shorter-than-requested window.
  local snapped = resolve_recall_cutoff(target)
  if not snapped then return nil end
  return recall_id_order[snapped]
end

--------------------------------------------------------------------------------
-- Internal mutators
--------------------------------------------------------------------------------

--- Detach atom capture for a record and drop it from whichever table holds it.
---@param id string
---@return boolean removed
local function destroy_record(id)
  local active = active_sessions[id]
  if not active and not finished_sessions[id] then return false end
  if active then
    if active.watch_disarm then active.watch_disarm() end
    if active.stream_sub then atom_stream.remove_listener(active.stream_sub) end
  end
  active_sessions[id] = nil
  finished_sessions[id] = nil
  if last_finished_id == id then last_finished_id = nil end
  if last_suggest_id == id then last_suggest_id = nil end
  return true
end

--- Splice an id out of `recall_id_order`, if present.
---@param id string
local function unindex_recall(id)
  for i = 1, #recall_id_order do
    if recall_id_order[i] == id then
      table.remove(recall_id_order, i)
      return
    end
  end
end

--- Evict oldest recall ids only when both retention caps say so.
local function evict_old_recall()
  local now = vim.uv.hrtime()
  local max_age_ns = config.MAX_RETENTION_SECONDS * 1e9
  while #recall_id_order > 0 do
    local oldest_id = recall_id_order[1]
    local rec = any_session(oldest_id)
    if not rec then
      table.remove(recall_id_order, 1)
    else
      local age_ns = now - rec.started_mono_ns
      local count_would_drop = #recall_id_order > config.KEY_SESSION_CAPACITY
      local age_would_drop = age_ns > max_age_ns
      if count_would_drop and age_would_drop then
        table.remove(recall_id_order, 1)
        destroy_record(oldest_id)
      else
        break
      end
    end
  end
end

--------------------------------------------------------------------------------
-- Public API
--------------------------------------------------------------------------------

--- Whether a session can be stored under `alias` without exceeding
--- NAMED_CAPACITY. Reusing an alias always has room (it replaces). Callers
--- check this before registering a listener so a refusal has nothing to undo.
---@param alias string
---@return boolean
function M.has_room_for_named(alias)
  if named_to_id[alias] then return true end
  local active_count = 0
  for _, id in pairs(named_to_id) do
    if active_sessions[id] then
      active_count = active_count + 1
    end
  end
  return active_count < NAMED_CAPACITY
end

--- Store a session under a named alias, replacing any record it already names.
---@param alias string
---@param record VF.Session.Active
---@return boolean overwrote  True if an existing session was replaced
function M.store_named(alias, record)
  assert(M.has_room_for_named(alias), "named session capacity reached")
  local existing_id = named_to_id[alias]

  if existing_id then
    destroy_record(existing_id)
  end

  named_to_id[alias] = record.id
  active_sessions[record.id] = record
  return existing_id ~= nil
end

--- Store a recall session at the newest position, evicting if caps demand.
---@param record VF.Session.Active
function M.store_recall(record)
  active_sessions[record.id] = record
  table.insert(recall_id_order, record.id)
  evict_old_recall()
end

---@param alias string
---@return VF.Session.Active|nil
function M.get_active(alias)
  local id = convert_alias_to_id(alias)
  if not id then return nil end
  return active_sessions[id]
end

---@param alias string
---@return VF.Session.Result|nil
function M.get_result(alias)
  local id = convert_alias_to_id(alias)
  if not id then return nil end
  local rec = finished_sessions[id]
  return rec and rec.result or nil
end

--- Id-keyed variant of `get_result`.
--- Use this after pinning a time-varying alias to a stable id.
---@param id string
---@return VF.Session.Result|nil
function M.get_result_by_id(id)
  if not util.is_session_id(id) then return nil end
  local rec = finished_sessions[id]
  return rec and rec.result or nil
end

---@param alias string
---@return boolean
function M.has_result(alias)
  return M.get_result(alias) ~= nil
end

--- Remove a session entirely by stable id.
---@param id string
function M.remove(id)
  -- Removing by alias is unsafe because recall aliases drift.
  assert(util.is_session_id(id),
    "session_store.remove requires a session id (from util.new_id), not an alias: "
    .. tostring(id))

  if not any_session(id) then return end

  -- Un-index from whichever side holds this id.
  local name = nil
  for a, nid in pairs(named_to_id) do
    if nid == id then name = a; break end
  end
  if name then
    named_to_id[name] = nil
  else
    unindex_recall(id)
  end

  destroy_record(id)
end

--- Move an active session to `finished_sessions` without touching its indexes.
---@param id string
---@param result VF.Session.Result
---@param finish_alias string|nil   Literal alias the caller used for this finish (`a`, `3s`, etc.); the default filename for `:Vimfy save @`
---@param kind? VF.Session.Kind     Promotion: a recall record finished by auto_suggest takes the suggest_* that fired. Applied atomically with the status transition so a failed compute/finish path never leaves a mislabeled record. Omit to keep the record's own kind.
---@return boolean success
function M.convert_to_finished(id, result, finish_alias, kind)
  local active = active_sessions[id]
  if not active then return false end

  if kind then
    assert(SUGGEST_KINDS[kind] and active.kind == "recall",
      "convert_to_finished kind only promotes a recall record to suggest_*; got " ..
      tostring(active.kind) .. " -> " .. tostring(kind))
  end

  if active.watch_disarm then active.watch_disarm() end
  if active.stream_sub then atom_stream.remove_listener(active.stream_sub) end

  local finished = {
    id               = id,
    kind             = kind or active.kind,
    started_mono_ns  = active.started_mono_ns,
    finished_mono_ns = vim.uv.hrtime(),
    finish_alias     = finish_alias,
    result           = result,
  }
  result.kind = finished.kind
  active_sessions[id] = nil
  finished_sessions[id] = finished
  last_finished_id = id

  -- Append to the persistent stats log. Best-effort — the log module
  -- swallows I/O errors, so this call must not raise and must not block
  -- the finish transition. Required deferred require (log → … → store
  -- would cycle if loaded eagerly at the top of this file).
  local ok, stats_log = pcall(require, "vimficiency.stats.log")
  if ok and stats_log then
    local record = stats_log.build_record(finished.kind, result)
    if record then stats_log.append(record) end
  end

  return true
end

--- Resolve an alias to its session id.
---@param alias string
---@return string|nil id
function M.get_id(alias)
  return convert_alias_to_id(alias)
end

--- Mark a session as the most recent suggest regression (drives `$`).
--- Called by auto_suggest for every new-fingerprint result, shown toast or not.
---@param id string
function M.set_last_suggest_id(id)
  last_suggest_id = id
end

--- The alias a record was finished under (the default `:Vimfy save` filename).
---@param alias string
---@return string|nil
function M.get_finish_alias(alias)
  local id = convert_alias_to_id(alias)
  local rec = id and finished_sessions[id]
  return rec and rec.finish_alias or nil
end

--- Insert a disk-loaded result as a finished session under `alias`.
---@param alias string  Must satisfy `alias.is_named`.
---@param result VF.Session.Result
---@return string|nil id
---@return string|nil err
function M.register_fetched_result(alias, result)
  if not alias_mod.is_named(alias) then
    return nil, "alias '" .. tostring(alias) .. "' is not a named alias (letters only)"
  end
  if named_to_id[alias] then
    return nil, "alias '" .. alias .. "' is already in use in the current session"
  end
  assert(type(result) == "table", "result must be a table")

  -- `util.new_id()` needs a valid buffer; the id itself is opaque.
  local id = util.new_id(vim.api.nvim_get_current_buf())

  local now_mono_ns = vim.uv.hrtime()
  finished_sessions[id] = {
    id               = id,
    kind             = result.kind or "mark",
    started_mono_ns  = now_mono_ns,
    finished_mono_ns = now_mono_ns,
    finish_alias     = alias,
    result           = result,
  }
  named_to_id[alias] = id
  last_finished_id = id
  return id, nil
end

--- Move a named alias to a new named alias in-place. Refuses if the source is missing
--- or the target is already taken. Only the `named_to_id` index and a finished
--- record's default save filename are touched.
---@param old_alias string
---@param new_alias string
---@return boolean ok
---@return string|nil err
function M.rename_alias(old_alias, new_alias)
  if old_alias == new_alias then
    return false, "source and target aliases are identical"
  end
  if not alias_mod.is_named(new_alias) then
    return false, "invalid target alias '" .. tostring(new_alias) ..
      "' (named aliases are letters only)"
  end
  local id = named_to_id[old_alias]
  if not id then
    return false, "no named alias '" .. tostring(old_alias) .. "' in the workspace"
  end
  if named_to_id[new_alias] then
    return false, "alias '" .. new_alias .. "' is already in use"
  end
  named_to_id[old_alias] = nil
  named_to_id[new_alias] = id
  local finished = finished_sessions[id]
  if finished then finished.finish_alias = new_alias end
  return true, nil
end

--- All named and recall-key aliases that currently resolve, for tab completion.
---@return string[]
function M.list_aliases()
  local aliases = {}

  for alias, _ in pairs(named_to_id) do
    table.insert(aliases, alias)
  end

  for i = 1, #recall_id_order do
    table.insert(aliases, tostring(i))
  end

  return aliases
end

--------------------------------------------------------------------------------
-- Session summary (normalized view-model)
--------------------------------------------------------------------------------

---@param id string
---@return integer|nil index
local function find_recall_index(id)
  for i = 1, #recall_id_order do
    if recall_id_order[i] == id then return i end
  end
  return nil
end

---@param id string
---@return string|nil alias
local function find_name(id)
  for a, nid in pairs(named_to_id) do
    if nid == id then return a end
  end
  return nil
end

--- Build a VF.Session.Summary for the session with this id.
---@param id string
---@return VF.Session.Summary|nil
function M.summarize(id)
  local active = active_sessions[id]
  local finished = finished_sessions[id]
  local core = active or finished
  if not core then return nil end

  local name = find_name(id)
  local recall_index = find_recall_index(id)

  local display_alias
  if name then
    display_alias = name
  elseif recall_index then
    -- Recall aliases drift; this is presentation-only.
    display_alias = tostring(#recall_id_order - recall_index + 1)
  end

  local result = finished and finished.result
  local user_seq = (result and result.user_seq) or ""
  local preview = user_seq:sub(1, 20)
  if #user_seq > 20 then preview = preview .. "…" end

  return {
    id            = id,
    kind          = core.kind,
    type          = M.session_type(core.kind),
    display_alias = display_alias,
    status        = active and "active" or "finished",
    started_mono_ns  = core.started_mono_ns,
    finished_mono_ns = finished and finished.finished_mono_ns or nil,
    -- `or 0`: saved files predating key_count are still loadable
    key_count     = active and #active.atoms or (result.key_count or 0),
    preview       = preview,
    result        = result,
  }
end

--- Summarize every known session. Order is unspecified.
---@return VF.Session.Summary[]
function M.summarize_all()
  local out = {}
  for _, sessions in ipairs({ active_sessions, finished_sessions }) do
    for id, _ in pairs(sessions) do
      table.insert(out, M.summarize(id))
    end
  end
  return out
end

--------------------------------------------------------------------------------
-- Recall install
--------------------------------------------------------------------------------

-- Buffer state immediately *before* the next atom in each buffer. A CmdAtom
-- fires after its action, so a recall record created at atom i needs the state
-- that preceded it — which is the state captured right after atom i-1. We keep
-- one rolling snapshot per buffer and shift it forward each atom. The first
-- atom in a buffer has no predecessor, so it seeds the snapshot without spawning
-- a record (recalling "before the very first command" is not meaningful).
---@type table<integer, VF.BufferState>
local recall_prev_state = {}

--- Ingest one recorded atom into the rolling recall ring.
---
--- Callers are responsible for decision filtering (only "record" atoms) and
--- buffer/window validity.
---@param atom VF.Atom
function M.ingest_recall_atom(atom)
  -- Extend every still-active recall record in this buffer with this atom.
  for _, id in ipairs(recall_id_order) do
    local rec = active_sessions[id]
    if rec and rec.buf == atom.buf then M.append_atom(rec, atom) end
  end

  local prev = recall_prev_state[atom.buf]
  -- Shift: this atom's post-state becomes the predecessor for the next one.
  recall_prev_state[atom.buf] = util.capture_state(atom.buf, atom.win)

  -- Without a predecessor snapshot we can't frame "before this atom"; seed and wait.
  if not prev then return end

  local id = util.new_id(atom.buf)
  local rec = M.new_active_session(id, nil, atom.win, atom.buf, prev, "recall")
  M.append_atom(rec, atom)
  M.store_recall(rec)
end

--- An unreplayable atom changed `buf`: its active recall records can no longer
--- reach their end state, so drop them and restart the predecessor chain from
--- the post-abort state.
---@param buf integer
---@param win integer
function M.abort_recall(buf, win)
  for _, id in ipairs(vim.list_slice(recall_id_order)) do
    local rec = active_sessions[id]
    if rec and rec.buf == buf then M.remove(id) end
  end
  recall_prev_state[buf] = util.capture_state(buf, win)
end

--------------------------------------------------------------------------------
-- Reload support
--------------------------------------------------------------------------------

--- Tear down every active session and return the number dropped.
--- Routes through `M.remove` rather than calling `destroy_record` directly,
--- so `named_to_id` and `recall_id_order` get un-indexed alongside the record
--- deletion. Calling `destroy_record` alone leaves dangling alias/ring entries
--- that would survive `dump_for_reload` → `restore_from_dump` and resurrect
--- phantom sessions (get_id returns ids with no backing record, list() shows
--- aliases that resolve to nothing).
---@return integer dropped
function M.teardown_active()
  local to_destroy = {}
  for id, _ in pairs(active_sessions) do
    to_destroy[#to_destroy + 1] = id
  end
  for _, id in ipairs(to_destroy) do
    M.remove(id)
  end
  return #to_destroy
end

--- Snapshot finished records and indexes for plugin reload. Runs after
--- `teardown_active`, so only finished records exist to carry over.
---@return VF.Session.ReloadDump
function M.dump_for_reload()
  local finished_copy = {}
  for id, rec in pairs(finished_sessions) do
    finished_copy[id] = rec
  end
  local named_copy = {}
  for alias, id in pairs(named_to_id) do
    named_copy[alias] = id
  end
  local order_copy = {}
  for i, id in ipairs(recall_id_order) do
    order_copy[i] = id
  end
  return {
    finished      = finished_copy,
    named         = named_copy,
    order         = order_copy,
    last_finished = last_finished_id,
    last_suggest  = last_suggest_id,
  }
end

--- Rehydrate a previous `dump_for_reload()` into this fresh module.
--- Filters the named and recall indexes against `finished` as it goes —
--- any id that doesn't have a backing record gets dropped. With the
--- current `teardown_active` routing through `M.remove`, dumps should
--- already be clean, but this guard keeps the invariant explicit: after
--- restore, every entry in `named_to_id` and `recall_id_order`
--- resolves to a real record.
---@param dump VF.Session.ReloadDump
function M.restore_from_dump(dump)
  active_sessions = {}
  finished_sessions = dump.finished

  named_to_id = {}
  for alias, id in pairs(dump.named) do
    if finished_sessions[id] then
      named_to_id[alias] = id
    end
  end

  recall_id_order = {}
  for _, id in ipairs(dump.order) do
    if finished_sessions[id] then
      recall_id_order[#recall_id_order + 1] = id
    end
  end

  last_finished_id =
    (dump.last_finished and finished_sessions[dump.last_finished])
      and dump.last_finished
      or nil

  last_suggest_id =
    (dump.last_suggest and finished_sessions[dump.last_suggest])
      and dump.last_suggest
      or nil
end

return M
