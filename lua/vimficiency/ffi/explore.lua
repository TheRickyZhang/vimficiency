local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

local function drain_warnings()
  local pending = core.unwrap(lib.vf_get_warnings())
  if pending == "" then return end
  pcall(vim.notify, "[vimficiency] " .. pending, vim.log.levels.WARN)
end

-- The result shapes below are packed field for field by the pack* functions in
-- src/LuaExports/InteractiveExports.cpp.
---@param slice VF.C.ByteSlice
---@return any
local function unwrap(slice)
  drain_warnings()
  return core.unwrap(slice)
end

--- Kicks off the composition search on a C++ worker thread and returns a job id
--- immediately. Poll with `explore_poll` until it returns a view id.
---@param req VF.SearchRequest
---@param deadline_ms integer
---@return integer job_id
function M.explore_start_async(req, deadline_ms)
  return core.with_search_request(req, function(request)
    return unwrap(lib.vf_explore_start_async(request, deadline_ms))
  end)
end

--- Returns the view_id once the worker finishes, or nil while still computing.
---@return integer|nil
function M.explore_poll(job_id)
  return unwrap(lib.vf_explore_poll(job_id))
end

function M.explore_cancel(job_id)
  return lib.vf_explore_cancel(job_id) == 1
end

function M.explore_destroy(view_id)
  return lib.vf_explore_destroy(view_id) == 1
end

---@return VF.Explore.State
function M.explore_state(view_id)
  return unwrap(lib.vf_explore_state(view_id))
end

---@return VF.Explore.Recommendation[]
function M.explore_recommendations(view_id, max_count, optimizer_overrides, sort_mode)
  return unwrap(lib.vf_explore_recommendations(
    view_id, max_count,
    optimizer_overrides, #optimizer_overrides,
    sort_mode, #sort_mode))
end

---@return VF.Explore.ReconfigurePlanResult
function M.explore_reconfigure_plan(view_id, optimizer_overrides)
  return unwrap(lib.vf_explore_reconfigure_plan(
    view_id, optimizer_overrides, #optimizer_overrides))
end

---@return string[]
function M.explore_current_lines(view_id)
  return unwrap(lib.vf_explore_current_lines(view_id))
end

---@return VF.Explore.ApplyResult
function M.explore_accept_snapshot(view_id, lines, new_row, new_col, raw_keys, insert_mode)
  local slices, count = core.byte_slices(lines)
  return unwrap(lib.vf_explore_accept_snapshot(
    view_id, slices, count, new_row, new_col,
    raw_keys, #raw_keys, insert_mode))
end

---@return VF.Explore.ApplyResult
function M.explore_undo(view_id)
  return unwrap(lib.vf_explore_undo(view_id))
end

---@return VF.Explore.ApplyResult
function M.explore_redo(view_id)
  return unwrap(lib.vf_explore_redo(view_id))
end

---@return VF.Explore.HeaderRows
function M.explore_header_rows(view_id)
  return unwrap(lib.vf_explore_header_rows(view_id))
end

return M
