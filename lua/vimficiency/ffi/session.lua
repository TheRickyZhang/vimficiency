local ffi = require("ffi")
local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

local KeyEvents = ffi.typeof("VFKeyEvent[?]")

---@param key_seq VF.KeyEvent[]
---@return string
function M.build_sequence(key_seq)
  local count = #key_seq
  local events = KeyEvents(count) --[[@as VF.C.KeyEvent[] ]]
  for i = 1, count do
    local ev = key_seq[i]
    core.set_slice(events[i - 1].mode, ev.mode)
    core.set_slice(events[i - 1].key_typed, ev.key_typed)
  end
  local slice = lib.vf_build_sequence(events, count)
  core.keep_alive(key_seq, events)
  return core.unwrap(slice)
end

---@param start_lines string[]
---@param end_lines string[]
---@param start_row integer
---@param end_row integer
---@param padding integer
---@return integer
---@return integer
function M.compute_search_region(start_lines, end_lines, start_row, end_row, padding)
  local start_slices, start_count = core.byte_slices(start_lines)
  local end_slices, end_count = core.byte_slices(end_lines)
  local slice = lib.vf_compute_search_region(
    start_slices,
    start_count,
    end_slices,
    end_count,
    start_row,
    end_row,
    padding
  )
  core.keep_alive(start_lines, end_lines, start_slices, end_slices)
  local region = core.unwrap(slice)
  return region.region_start, region.region_end
end


---@param start_row integer
---@param cursor_row integer
---@param last_key_time_ns integer|nil
---@param now_ns integer
---@param max_search_lines integer
---@param manual_idle_timeout_seconds integer
---@return string|nil
function M.manual_evict_reason(
  start_row,
  cursor_row,
  last_key_time_ns,
  now_ns,
  max_search_lines,
  manual_idle_timeout_seconds
)
  local has_last_key = last_key_time_ns ~= nil
  local code = lib.vf_manual_evict_reason(
    start_row,
    cursor_row,
    last_key_time_ns or 0,
    has_last_key,
    now_ns,
    max_search_lines,
    manual_idle_timeout_seconds
  )
  if code == 1 then
    return "cursor drifted beyond MAX_SEARCH_LINES (" .. max_search_lines .. ")"
  end
  if code == 2 then
    return "idle for more than " .. manual_idle_timeout_seconds .. "s"
  end
  return nil
end

return M
