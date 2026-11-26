-- One shadow per buffer. Each delta keeps the old lines for Recall and the
-- replacement lines for View.
--
-- `on_lines` reports ranges only, never the replaced text; the shadow is where
-- that text is read from. Between marks `on_lines` only widens one pending
-- range, so a command that fires it once per line stays cheap.

---@class (exact) VF.BufferHistory
---@field mark              fun(buf: integer): integer, VF.BufferHistory.State
---@field lines_at          fun(buf: integer, seq: integer): string[]
---@field changes           fun(buf: integer, after: integer, through: integer): VF.Replay.Delta[], number
---@field splice            fun(lines: string[], first: integer, old_count: integer, replacement: string[])
---@field difference        fun(before: string[], after: string[]): VF.Replay.Delta?
---@field trim              fun(buf: integer, seq: integer)
---@field set_reset_handler fun(handler: fun(buf: integer): boolean)
---@field set_observer      fun(observer: VF.BufferHistory.Observer|nil)
---@field shutdown          fun()
local M = {}

local v = vim.api
local util = require("vimficiency.util")

---@class VF.BufferHistory.Delta
---@field old_lines string[]  # lines the change replaced
---@field forward VF.Replay.Delta
---@field min_height integer

---@class VF.BufferHistory.Pending
---@field min_height integer  # smallest buffer height while this range was pending
---@field first    integer  # 0-indexed first changed line
---@field old_last integer  # end of the changed range in the shadow
---@field new_last integer  # end of the changed range in the buffer

---@class VF.BufferHistory.State
---@field shadow   string[]|nil  # buffer text as of the last mark; nil until (re)seeded by `mark`
---@field pending  VF.BufferHistory.Pending|nil  # hull of every change since the last mark
---@field deltas   VF.BufferHistory.Delta[]
---@field base_seq integer       # seq of the text before `deltas[1]`
---@field dead     boolean       # set by shutdown/detach; the attach callbacks then detach themselves
---@field reloaded boolean?      # a named session spans a reload; compare its last shadow at the next mark

---@type table<integer, VF.BufferHistory.State>
local states = {}
---@type table<integer, VF.BufferHistory.State>
local detached = setmetatable({}, { __mode = "v" })

---@type fun(buf: integer): boolean
local reset_handler = function() return false end

--- Sees every text a mark establishes: the seed, then each folded delta in order.
---@class VF.BufferHistory.Observer
---@field on_seed  fun(buf: integer, lines: string[])  # `lines` is the live shadow; consume before returning
---@field on_delta fun(buf: integer, first: integer, old_count: integer, new_lines: string[], min_height: integer)

---@type VF.BufferHistory.Observer
local NO_OBSERVER = { on_seed = function() end, on_delta = function() end }
local observer = NO_OBSERVER

--- Replace `lines[first + 1 .. first + old_count]` with `replacement`, in place.
---@param lines string[]
---@param first integer
---@param old_count integer
---@param replacement string[]
function M.splice(lines, first, old_count, replacement)
  local new_count = #replacement
  local n = #lines
  if new_count ~= old_count then
    table.move(lines, first + old_count + 1, n, first + new_count + 1)
    for i = n + new_count - old_count + 1, n do lines[i] = nil end
  end
  table.move(replacement, 1, new_count, first + 1, lines)
end

function M.difference(before, after)
  local first, tail = 0, 0
  local shorter = math.min(#before, #after)
  while first < shorter and before[first + 1] == after[first + 1] do first = first + 1 end
  while tail < shorter - first and before[#before - tail] == after[#after - tail] do tail = tail + 1 end
  if first + tail == math.max(#before, #after) then return end
  return { first = first, old_count = #before - first - tail, lines = vim.list_slice(after, first + 1, #after - tail) }
end

--- The next mark reads the replacement text. `on_reload` fires before it is available.
---@param state VF.BufferHistory.State
---@param buf integer
local function reset(state, buf)
  state.pending = nil
  state.reloaded = reset_handler(buf)
  if state.reloaded then return end
  state.base_seq = state.base_seq + #state.deltas
  state.deltas = {}
  state.shadow = nil
end

---@param state VF.BufferHistory.State
---@param first integer
---@param last integer
---@param new_last integer
local function widen_pending(state, buf, first, last, new_last)
  if not state.shadow or state.reloaded then return end
  local pending = state.pending
  if not pending then
    state.pending = { first = first, old_last = last, new_last = new_last,
      min_height = math.min(#state.shadow, v.nvim_buf_line_count(buf)) }
    return
  end
  pending.min_height = math.min(pending.min_height, v.nvim_buf_line_count(buf))

  -- Lines past the pending range are unchanged, so they sit at the same offset
  -- in the shadow as in the buffer.
  if last > pending.new_last then
    pending.old_last = pending.old_last + last - pending.new_last
    pending.new_last = last
  end
  pending.new_last = pending.new_last + new_last - last
  pending.first = math.min(pending.first, first)
end

--- Fold the pending range into the shadow as one delta.
---@param state VF.BufferHistory.State
---@param buf integer
local function sync(state, buf)
  local pending = state.pending
  if not pending then return end
  state.pending = nil

  local shadow = state.shadow --[[@as string[] ]]
  local first, old_last, new_last = pending.first, pending.old_last, pending.new_last
  -- Deleting every line is reported as leaving zero lines, though the buffer
  -- shows one empty line, and later changes are reported against either view.
  -- Replacing the whole buffer is right whatever was reported.
  local line_count = v.nvim_buf_line_count(buf)
  if #shadow - old_last + new_last ~= line_count then
    first, old_last, new_last = 0, #shadow, line_count
  end

  local new_lines = v.nvim_buf_get_lines(buf, first, new_last, false)
  state.deltas[#state.deltas + 1] = {
    old_lines = vim.list_slice(shadow, first + 1, old_last),
    min_height = pending.min_height,
    forward = { first = first, old_count = old_last - first, lines = new_lines },
  }
  M.splice(shadow, first, old_last - first, new_lines)
  observer.on_delta(buf, first, old_last - first, new_lines, pending.min_height)
end

local guarded_widen_pending = util.entry(widen_pending)
local guarded_reset = util.entry(reset)

---@param buf integer
---@return VF.BufferHistory.State
local function attach(buf)
  ---@type VF.BufferHistory.State
  local state = detached[buf] or { shadow = nil, pending = nil, deltas = {}, base_seq = 0, dead = false }

  -- Unattached, the shadow would never see another change.
  local attached = v.nvim_buf_attach(buf, false, {
    on_lines = function(_, _, _, first, last, new_last)
      if state.dead then return true end
      guarded_widen_pending(state, buf, first, last, new_last)
    end,
    on_reload = function()
      if not state.dead then guarded_reset(state, buf) end
    end,
    on_detach = function()
      if state.dead then return end
      state.dead = true
      states[buf] = nil
      guarded_reset(state, buf)
      if state.reloaded then detached[buf] = state end
    end,
  })
  assert(attached, "vimfy buffer_history: cannot attach to unloaded buffer " .. buf)
  detached[buf] = nil
  state.dead = false
  states[buf] = state
  return state
end

--- Sequence number of `buf`'s current text, which `lines_at` can rebuild until
--- it is trimmed or reset. Starts recording `buf` on first use.
---@param buf integer
---@return integer
---@return VF.BufferHistory.State
function M.mark(buf)
  local state = states[buf] or attach(buf)
  if state.shadow and not state.reloaded then
    sync(state, buf)
  else
    local lines = v.nvim_buf_get_lines(buf, 0, -1, false)
    if state.shadow then
      local delta = M.difference(state.shadow, lines)
      if delta then
        state.deltas[#state.deltas + 1] = {
          old_lines = vim.list_slice(state.shadow, delta.first + 1, delta.first + delta.old_count),
          min_height = math.min(#state.shadow, #lines),
          forward = delta,
        }
      end
    end
    state.shadow, state.reloaded = lines, false
    observer.on_seed(buf, state.shadow)
  end
  return state.base_seq + #state.deltas, state
end

--- `buf`'s text as it was at `seq`.
---@param buf integer
---@param seq integer
---@return string[]
function M.lines_at(buf, seq)
  local state = states[buf]
  local lines = vim.list_slice(state.shadow)
  for i = #state.deltas, seq - state.base_seq + 1, -1 do
    local delta = state.deltas[i]
    M.splice(lines, delta.forward.first, #delta.forward.lines, delta.old_lines)
  end
  return lines
end

function M.changes(buf, after, through)
  local state = states[buf]
  local changes = {}
  local min_height = math.huge
  for i = after - state.base_seq + 1, through - state.base_seq do
    local delta = state.deltas[i]
    changes[#changes + 1] = delta.forward
    min_height = math.min(min_height, delta.min_height)
  end
  return changes, min_height
end

--- Forget how to rebuild anything older than `seq`.
---@param buf integer
---@param seq integer
function M.trim(buf, seq)
  local state = states[buf]
  local drop = seq - state.base_seq
  if drop <= 0 then return end
  state.deltas = vim.list_slice(state.deltas, drop + 1)
  state.base_seq = seq
end

--- Keep earlier marks across reload or unload while a named session needs them.
---@param handler fun(buf: integer): boolean
function M.set_reset_handler(handler)
  reset_handler = handler
end

---@param new_observer VF.BufferHistory.Observer|nil
function M.set_observer(new_observer)
  observer = new_observer or NO_OBSERVER
end

function M.shutdown()
  for _, state in pairs(states) do state.dead = true end
  states = {}
  detached = setmetatable({}, { __mode = "v" })
end

return M
