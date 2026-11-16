-- Per-buffer line history: one shadow copy of the buffer as of its last mark,
-- plus the lines replaced between consecutive marks, so an earlier text can be
-- rebuilt on demand instead of being snapshotted up front.
--
-- `on_lines` reports ranges only, never the replaced text; the shadow is where
-- that text is read from. Between marks `on_lines` only widens one pending
-- range, so a command that fires it once per line stays cheap.

---@class (exact) VF.BufferHistory
---@field mark              fun(buf: integer): integer
---@field lines_at          fun(buf: integer, seq: integer): string[]
---@field trim              fun(buf: integer, seq: integer)
---@field set_reset_handler fun(handler: fun(buf: integer))
---@field shutdown          fun()
local M = {}

local v = vim.api
local util = require("vimficiency.util")

---@class VF.BufferHistory.Delta
---@field first     integer   # 0-indexed first changed line
---@field old_lines string[]  # lines the change replaced
---@field new_count integer   # how many lines replaced them

---@class VF.BufferHistory.Pending
---@field first    integer  # 0-indexed first changed line
---@field old_last integer  # end of the changed range in the shadow
---@field new_last integer  # end of the changed range in the buffer

---@class VF.BufferHistory.State
---@field shadow   string[]|nil  # buffer text as of the last mark; nil until (re)seeded by `mark`
---@field pending  VF.BufferHistory.Pending|nil  # hull of every change since the last mark
---@field deltas   VF.BufferHistory.Delta[]
---@field base_seq integer       # seq of the text before `deltas[1]`
---@field dead     boolean       # set by shutdown/detach; the attach callbacks then detach themselves

---@type table<integer, VF.BufferHistory.State>
local states = {}

---@type fun(buf: integer)
local reset_handler = function() end

--- Replace `lines[first + 1 .. first + old_count]` with `replacement`, in place.
---@param lines string[]
---@param first integer
---@param old_count integer
---@param replacement string[]
local function splice(lines, first, old_count, replacement)
  local new_count = #replacement
  local n = #lines
  if new_count ~= old_count then
    table.move(lines, first + old_count + 1, n, first + new_count + 1)
    for i = n + new_count - old_count + 1, n do lines[i] = nil end
  end
  table.move(replacement, 1, new_count, first + 1, lines)
end

--- Every seq handed out so far stops being rebuildable. The shadow is reseeded
--- by the next `mark`, not here: `on_reload` fires before the new text is read.
---@param state VF.BufferHistory.State
---@param buf integer
local function reset(state, buf)
  state.base_seq = state.base_seq + #state.deltas
  state.deltas = {}
  state.shadow = nil
  state.pending = nil
  reset_handler(buf)
end

---@param state VF.BufferHistory.State
---@param first integer
---@param last integer
---@param new_last integer
local function widen_pending(state, first, last, new_last)
  if not state.shadow then return end
  local pending = state.pending
  if not pending then
    state.pending = { first = first, old_last = last, new_last = new_last }
    return
  end

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
    first = first,
    old_lines = vim.list_slice(shadow, first + 1, old_last),
    new_count = #new_lines,
  }
  splice(shadow, first, old_last - first, new_lines)
end

local guarded_widen_pending = util.entry(widen_pending)
local guarded_reset = util.entry(reset)

---@param buf integer
---@return VF.BufferHistory.State
local function attach(buf)
  ---@type VF.BufferHistory.State
  local state = { shadow = nil, pending = nil, deltas = {}, base_seq = 0, dead = false }
  states[buf] = state

  v.nvim_buf_attach(buf, false, {
    on_lines = function(_, _, _, first, last, new_last)
      if state.dead then return true end
      guarded_widen_pending(state, first, last, new_last)
    end,
    on_reload = function()
      if not state.dead then guarded_reset(state, buf) end
    end,
    on_detach = function()
      if state.dead then return end
      state.dead = true
      states[buf] = nil
      guarded_reset(state, buf)
    end,
  })
  return state
end

--- Sequence number of `buf`'s current text, which `lines_at` can rebuild until
--- it is trimmed or reset. Starts recording `buf` on first use.
---@param buf integer
---@return integer
function M.mark(buf)
  local state = states[buf] or attach(buf)
  if state.shadow then
    sync(state, buf)
  else
    state.shadow = v.nvim_buf_get_lines(buf, 0, -1, false)
  end
  return state.base_seq + #state.deltas
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
    splice(lines, delta.first, delta.new_count, delta.old_lines)
  end
  return lines
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

--- `handler(buf)` runs when `buf`'s earlier seqs stop being rebuildable: reload
--- or unload.
---@param handler fun(buf: integer)
function M.set_reset_handler(handler)
  reset_handler = handler
end

function M.shutdown()
  for _, state in pairs(states) do state.dead = true end
  states = {}
end

return M
