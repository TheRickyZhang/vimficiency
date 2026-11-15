-- Per-buffer line history: one shadow copy of the buffer plus the lines each
-- change replaced, so an earlier text can be rebuilt on demand instead of being
-- snapshotted up front.
--
-- `on_lines` reports ranges only, never the replaced text; the shadow is where
-- that text is read from.

---@class (exact) VF.BufferHistory
---@field track             fun(buf: integer)
---@field seq               fun(buf: integer): integer
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

---@class VF.BufferHistory.State
---@field shadow   string[]|nil  # buffer text as of the last change; nil until (re)seeded by `track`
---@field deltas   VF.BufferHistory.Delta[]
---@field base_seq integer       # seq of the text before `deltas[1]`
---@field emptied  boolean       # the last change deleted every line
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
--- by the next `track`, not here: `on_reload` fires before the new text is read.
---@param state VF.BufferHistory.State
---@param buf integer
local function reset(state, buf)
  state.base_seq = state.base_seq + #state.deltas
  state.deltas = {}
  state.shadow = nil
  reset_handler(buf)
end

---@param state VF.BufferHistory.State
---@param buf integer
---@param first integer
---@param last integer
---@param new_last integer
local function record_change(state, buf, first, last, new_last)
  local shadow = state.shadow
  if not shadow then return end

  -- Deleting every line is reported as leaving zero lines, though the buffer
  -- shows one empty line. The next change is reported against either view; the
  -- line count tells which.
  local reported_count = #shadow + new_last - last
  if state.emptied and reported_count ~= 0 and reported_count ~= v.nvim_buf_line_count(buf) then
    last = last + 1
  end
  state.emptied = reported_count == 0
  local new_lines = state.emptied and { "" } or v.nvim_buf_get_lines(buf, first, new_last, false)
  state.deltas[#state.deltas + 1] = {
    first = first,
    old_lines = vim.list_slice(shadow, first + 1, last),
    new_count = #new_lines,
  }
  splice(shadow, first, last - first, new_lines)

  -- A misreported range would otherwise corrupt every later rebuild silently.
  if #shadow ~= v.nvim_buf_line_count(buf) then reset(state, buf) end
end

local guarded_record_change = util.entry(record_change)
local guarded_reset = util.entry(reset)

--- Start recording `buf`'s changes, or reseed it after a reset. Idempotent.
---@param buf integer
function M.track(buf)
  local state = states[buf]
  if state then
    if not state.shadow then state.shadow = v.nvim_buf_get_lines(buf, 0, -1, false) end
    return
  end

  ---@type VF.BufferHistory.State
  state = {
    shadow = v.nvim_buf_get_lines(buf, 0, -1, false),
    deltas = {},
    base_seq = 0,
    emptied = false,
    dead = false,
  }
  states[buf] = state

  v.nvim_buf_attach(buf, false, {
    on_lines = function(_, _, _, first, last, new_last)
      if state.dead then return true end
      guarded_record_change(state, buf, first, last, new_last)
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
end

--- Sequence number of `buf`'s current text.
---@param buf integer
---@return integer
function M.seq(buf)
  local state = states[buf]
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

--- `handler(buf)` runs when `buf`'s earlier seqs stop being rebuildable:
--- reload, unload, or a change report that disagrees with the buffer.
---@param handler fun(buf: integer)
function M.set_reset_handler(handler)
  reset_handler = handler
end

function M.shutdown()
  for _, state in pairs(states) do state.dead = true end
  states = {}
end

return M
