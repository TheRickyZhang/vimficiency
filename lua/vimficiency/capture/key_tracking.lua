-- lua/vimficiency/key_tracking.lua
-- `vim.on_key()` exposes executed keys, not raw physical input.
-- See `dev/lua/neovim_on_key_issues.md` for the mapping-resolution details.
local M = {}
local uv = vim.uv
local ffi_session = require("vimficiency.ffi.session")
local keynorm = require("vimficiency.capture.keynorm")

--------------------------------------------------------------------------------
-- Types
--------------------------------------------------------------------------------

---@class VF.KeyEvent
---@field t integer              # hrtime timestamp
---@field win integer            # window id (useful for multi-session matching)
---@field buf integer            # buffer id
---@field mode string            # vim mode at time of keypress
---@field key_sent_raw string    # raw key sent (for debugging)
---@field key_sent string        # keytrans'd key sent
---@field key_typed_raw string   # raw typed key (before mappings)
---@field key_typed string       # keytrans'd typed key

-- Both handlers drop events with empty `typed`. Neovim leaves `typed` empty
-- for keys that were not typed by the user: `nvim_feedkeys` without the `t`
-- flag, `:normal`, and the RHS of a mapping expansion. The plugin's own
-- replay (simulate/precompute.lua) never passes `t`, so that one check is
-- what keeps plugin-generated keys out of every recording.

-- `key` as `vim.on_key` reports it for any mapping whose RHS is a Lua
-- callback: K_SPECIAL, KS_EXTRA, KE_LUA. Nothing else produces this code.
M.LUA_KEY = "\128\253g"

-- Manual sessions currently recording, keyed by on_key namespace, so
-- `uncount_trigger` can reach their key sequences.
---@type table<integer, fun(): VF.Session.Active|nil>
local manual_sessions = {}
---@type table<string, {on_event: fun(event: VF.KeyEvent), on_resolution?: fun(typed: string), on_trigger?: fun()}>
local global_subs = {}

---@param get_session fun(): VF.Session.Active|nil
---@param reset_session fun(reason: string, level: integer)
---@param should_evict fun(session: VF.Session.Active): string|nil  Optional: return a reason string to drop the session; nil to keep. Runs before the window-sameness check and before the key is appended, so eviction fires on every keystroke regardless of which window is focused.
---@return integer nsid
function M.attach(get_session, reset_session, should_evict)
	local nsid = nil

	local function on_key(key, typed)
		local session = get_session()
		if not session then return end

		-- Eviction checks inspect `session.win`, not the current window.
		if should_evict then
			local reason = should_evict(session)
			if reason then
				reset_session(reason, vim.log.levels.WARN)
				return
			end
		end

		local curr_win = vim.api.nvim_get_current_win()
		-- Keys from other windows are ignored without aborting the session.
		if curr_win ~= session.win then return end

		typed = typed or ""
		local mode_full = vim.api.nvim_get_mode().mode
		local m = mode_full:sub(1, 1)

		-- Multi-key mapping resolution: strip the already-recorded LHS
		-- bytes, and keep `session.key_count` in sync with `#key_seq`.
		-- The global on_key's recall fan-out (session_store.strip_recall_pre_resolution)
		-- already maintains the `key_count == #key_seq` invariant for
		-- recall records; mirroring it here makes the invariant hold for
		-- manual/watch sessions too, so anything that reads `key_count`
		-- during capture (metrics, summarize_all, future live counters)
		-- stays consistent.
		if #typed > 1 and typed ~= key then
			local popped = M.strip_matching_tail(session.key_seq, typed)
			if popped > 0 then
				session.key_count = math.max(0, (session.key_count or 0) - popped)
			end
			typed = ""
		end

		if typed == "" then return end

		-- Cmdline activity is meta, not motion — drop unconditionally.
		if m == "c" then return end
		if m == "n" and key == ":" then return end

		session.key_seq[#session.key_seq + 1] = {
			t = uv.hrtime(),
			mode = mode_full,
			win = curr_win,
			buf = vim.api.nvim_get_current_buf(),
			key_sent_raw = key,
			key_sent = keynorm.normalize(key),
			key_typed_raw = typed,
			key_typed = keynorm.normalize(typed),
		}
		session.key_count = (session.key_count or 0) + 1
	end

	nsid = vim.on_key(on_key, nsid)
	manual_sessions[nsid] = get_session
	return nsid
end

function M.detach(nsid)
	if nsid then
		vim.on_key(nil, nsid)
		manual_sessions[nsid] = nil
	end
end

--- Build a key sequence string from key events, with deduplication.
---@param key_seq VF.KeyEvent[]
---@return string
function M.build_sequence(key_seq)
	return ffi_session.build_sequence(key_seq)
end

--- Remove the trailing events whose `key_typed_raw` bytes match `typed_raw`.
---@param key_seq VF.KeyEvent[]
---@param typed_raw string
---@return integer popped
function M.strip_matching_tail(key_seq, typed_raw)
	if #typed_raw == 0 or #key_seq == 0 then return 0 end
	local acc = ""
	local i = #key_seq
	while i > 0 and #acc < #typed_raw do
		local ev = key_seq[i]
		acc = (ev.key_typed_raw or "") .. acc
		i = i - 1
	end
	if acc ~= typed_raw then return 0 end
	local popped = #key_seq - i
	for j = #key_seq, i + 1, -1 do
		key_seq[j] = nil
	end
	return popped
end

--- Remove the trailing event if it is a Lua-callback mapping trigger.
---@param key_seq VF.KeyEvent[]
---@return integer popped  0 or 1
function M.pop_trigger(key_seq)
	local last = key_seq[#key_seq]
	if not last or last.key_sent_raw ~= M.LUA_KEY then return 0 end
	key_seq[#key_seq] = nil
	return 1
end

--- Retract the keystroke that invoked the calling Vimfy entry point.
---
--- Why this exists: a key bound to a Vimfy action is not editing motion, but
--- `vim.on_key` has already reported it (as `key = LUA_KEY`, `typed = LHS`)
--- by the time the mapping's callback runs, so every active recording has
--- appended it. The `#typed > 1` branch in the handlers above catches the
--- multi-key case, because a multi-byte `typed` on a single event can only
--- be a resolved mapping. A single-key LHS (`Z`, or `Y` remapped to a
--- `<Plug>`) is one byte and looks exactly like a plain keystroke there, so
--- it is recorded.
---
--- Why retroactive removal is correct: Neovim runs the callback synchronously,
--- immediately after that on_key event, so when a Vimfy-registered callback
--- starts, the newest event in every recording is its own trigger (or the
--- multi-key branch has already stripped it and nothing is pending). No user
--- keystroke can land between the two. Popping the newest event, gated on it
--- carrying LUA_KEY, therefore removes exactly the trigger and nothing else.
---
--- Why not widen the handler check to "key == LUA_KEY" instead: that would
--- strip the LHS of every Lua-callback mapping the user owns, including real
--- editing actions. Only Vimfy's own entry points know they are not motion,
--- so the retraction is issued from them (init.lua `keymap_entry`), scoped to
--- exactly those bindings.
function M.uncount_trigger()
	for _, get_session in pairs(manual_sessions) do
		local session = get_session()
		if session and session.key_seq then
			local popped = M.pop_trigger(session.key_seq)
			if popped > 0 then
				session.key_count = math.max(0, (session.key_count or 0) - popped)
			end
		end
	end
	for _, sub in pairs(global_subs) do
		if sub.on_trigger then sub.on_trigger() end
	end
end

-- Shared global `vim.on_key()` namespace for key-count and time-based sessions.

local global_nsid = nil
local global_subs_count = 0

local function ensure_global_listener()
	if global_nsid then return end

	local function on_key(key, typed)
		typed = typed or ""
		local mode = vim.api.nvim_get_mode().mode
		local m = mode:sub(1, 1)

		-- Multi-key mapping resolution: tell subscribers to strip the LHS.
		if #typed > 1 and typed ~= key then
			for _, sub in pairs(global_subs) do
				if sub.on_resolution then sub.on_resolution(typed) end
			end
			return
		end

		if typed == "" then return end
		if m == "c" then return end
		if m == "n" and key == ":" then return end

		---@type VF.KeyEvent
		local event = {
			t = uv.hrtime(),
			mode = mode,
			win = vim.api.nvim_get_current_win(),
			buf = vim.api.nvim_get_current_buf(),
			key_sent_raw = key,
			key_sent = keynorm.normalize(key),
			key_typed_raw = typed,
			key_typed = keynorm.normalize(typed),
		}

		for _, sub in pairs(global_subs) do
			sub.on_event(event)
		end
	end

	global_nsid = vim.on_key(on_key, global_nsid)
end

--- Attach a named global key listener.
--- `on_resolution` is called on multi-key mapping resolution events;
--- `on_trigger` from `uncount_trigger`, and should `pop_trigger` each recording.
---@param on_key_event fun(event: VF.KeyEvent)
---@param name string|nil             Subscriber name; defaults to "default"
---@param on_resolution (fun(typed_raw: string))|nil  Optional strip hook
---@param on_trigger (fun())|nil
---@return boolean success
function M.attach_global(on_key_event, name, on_resolution, on_trigger)
	name = name or "default"
	if global_subs[name] then
		return false
	end
	global_subs[name] = { on_event = on_key_event, on_resolution = on_resolution, on_trigger = on_trigger }
	global_subs_count = global_subs_count + 1
	ensure_global_listener()
	return true
end

--- Detach a named global key listener.
---@param name string|nil  Subscriber name; defaults to "default"
function M.detach_global(name)
	name = name or "default"
	if not global_subs[name] then return end
	global_subs[name] = nil
	global_subs_count = global_subs_count - 1

	if global_subs_count == 0 and global_nsid then
		vim.on_key(nil, global_nsid)
		global_nsid = nil
	end
end

--- Check if a named global listener is active.
---@param name string|nil  Subscriber name; defaults to "default"
---@return boolean
function M.is_global_attached(name)
	name = name or "default"
	return global_subs[name] ~= nil
end

--- Tear down the shared global `vim.on_key` namespace.
function M.shutdown()
	if global_nsid then
		vim.on_key(nil, global_nsid)
		global_nsid = nil
	end
	global_subs = {}
	global_subs_count = 0
end

return M
