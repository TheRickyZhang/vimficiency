local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

---@param key_seq VF.KeyEvent[]
---@return string
function M.build_sequence(key_seq)
  local events = {}
  for i, ev in ipairs(key_seq) do
    events[i] = { mode = ev.mode, key_typed = ev.key_typed }
  end
  return core.call(lib.vf_build_sequence, "vf.session.BuildSequenceRequest",
    { events = events }, "vf.Text").text
end

---@param start_lines string[]
---@param end_lines string[]
---@param start_row integer
---@param end_row integer
---@param padding integer
---@return integer
---@return integer
function M.compute_search_region(start_lines, end_lines, start_row, end_row, padding)
  local region = core.call(lib.vf_compute_search_region, "vf.session.SearchRegionRequest", {
    start_lines = start_lines,
    end_lines = end_lines,
    start_row = start_row,
    end_row = end_row,
    padding = padding,
  }, "vf.session.SearchRegion")
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
  local reason = core.call(lib.vf_manual_evict_reason, "vf.session.ManualEvictRequest", {
    start_row = start_row,
    cursor_row = cursor_row,
    last_key_time_ns = last_key_time_ns,
    now_ns = now_ns,
    max_search_lines = max_search_lines,
    manual_idle_timeout_seconds = manual_idle_timeout_seconds,
  }, "vf.session.ManualEvict").reason
  if reason == "drift" then
    return "cursor drifted beyond MAX_SEARCH_LINES (" .. max_search_lines .. ")"
  end
  if reason == "idle" then
    return "idle for more than " .. manual_idle_timeout_seconds .. "s"
  end
  return nil
end

return M
