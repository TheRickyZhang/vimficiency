---@class VF.Explore.PhaseNavigate
---@field kind "Navigate"
---@field edit_index integer

---@class VF.Explore.PhaseTransform
---@field kind "Transform"
---@field edit_index integer

---@class VF.Explore.PhaseInsert
---@field kind "Insert"
---@field edit_index integer

---@alias VF.Explore.Phase
---| VF.Explore.PhaseNavigate
---| VF.Explore.PhaseTransform
---| VF.Explore.PhaseInsert

---@class VF.Explore.State
---@field phase VF.Explore.Phase
---@field is_completed boolean
---@field cursor VF.Position
---@field total_edits integer
---@field cost number
---@field seq string
---@field can_undo boolean
---@field can_redo boolean
---@field target_range VF.Range

---@class VF.Explore.Rejected
---@field reason string

--- Exactly one of `applied` / `rejected` is set; `outcome` names it.
---@class VF.Explore.ApplyResult
---@field outcome "applied"|"rejected"
---@field applied? table
---@field rejected? VF.Explore.Rejected

---@class VF.Explore.Recommendation
---@field text string
---@field cost_diff number
---@field distance number
---@field score number
---@field landing VF.Position
---@field literal_text string
---@field rank? integer

---@class VF.Explore.ReconfigurePlanResult
---@field reset boolean

---@class VF.Explore.HeaderRowsResult
---@field rows string[]

---@class VF.Explore.HeaderRows
---@field explored string[]
---@field optimal VF.Explore.HeaderRowsResult[]

local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

local function drain_warnings()
  local pending = core.unwrap(lib.vf_get_warnings(), "vf.Text").text
  if pending == "" then return end
  pcall(vim.notify, "[vimficiency] " .. pending, vim.log.levels.WARN)
end

-- Messages are those of src/LuaExports/proto/explore.proto unless qualified.
---@param slice VF.C.ByteSlice
---@param message string
---@return any
local function unwrap(slice, message)
  drain_warnings()
  return core.unwrap(slice, message:find(".", 1, true) and message or "vf.explore." .. message)
end

---@param fn fun(request: string, request_len: integer): VF.C.ByteSlice
---@param request_message string
---@param request table
---@param result_message string
---@return any
local function call(fn, request_message, request, result_message)
  local bytes = core.encode("vf.explore." .. request_message, request)
  return unwrap(fn(bytes, #bytes), result_message)
end

--- Kicks off the composition search on a C++ worker thread and returns a job id
--- immediately. Poll with `explore_poll` until it returns a view id.
---@param req VF.SearchRequest
---@param deadline_ms integer
---@return integer job_id
function M.explore_start_async(req, deadline_ms)
  return call(lib.vf_explore_start_async, "StartRequest",
    { search = req, deadline_ms = deadline_ms }, "vf.JobHandle").job_id
end

--- Returns the view_id once the worker finishes, or nil while still computing.
---@return integer|nil
function M.explore_poll(job_id)
  local handle = unwrap(lib.vf_explore_poll(job_id), "ViewHandle")
  return handle and handle.view_id
end

function M.explore_cancel(job_id)
  return lib.vf_explore_cancel(job_id) == 1
end

function M.explore_destroy(view_id)
  return lib.vf_explore_destroy(view_id) == 1
end

---@return VF.Explore.State
function M.explore_state(view_id)
  return unwrap(lib.vf_explore_state(view_id), "State")
end

---@param sort_mode "effort"|"distance"|"score"
---@return VF.Explore.Recommendation[]
function M.explore_recommendations(view_id, max_count, optimizer_overrides, sort_mode)
  return call(lib.vf_explore_recommendations, "RecommendationsRequest", {
    view_id = view_id,
    max_count = max_count,
    optimizer_overrides = optimizer_overrides,
    sort_mode = sort_mode,
  }, "Recommendations").items
end

---@return VF.Explore.ReconfigurePlanResult
function M.explore_reconfigure_plan(view_id, optimizer_overrides)
  return call(lib.vf_explore_reconfigure_plan, "ReconfigurePlanRequest",
    { view_id = view_id, optimizer_overrides = optimizer_overrides }, "ReconfigurePlanResult")
end

---@return string[]
function M.explore_current_lines(view_id)
  return unwrap(lib.vf_explore_current_lines(view_id), "vf.Lines").lines
end

---@return VF.Explore.ApplyResult
function M.explore_accept_snapshot(view_id, lines, new_row, new_col, raw_keys, insert_mode)
  return call(lib.vf_explore_accept_snapshot, "SnapshotRequest", {
    view_id = view_id,
    lines = lines,
    cursor = { row = new_row, col = new_col },
    raw_keys = raw_keys,
    insert_mode = insert_mode,
  }, "ApplyResult")
end

---@return VF.Explore.ApplyResult
function M.explore_undo(view_id)
  return unwrap(lib.vf_explore_undo(view_id), "ApplyResult")
end

---@return VF.Explore.ApplyResult
function M.explore_redo(view_id)
  return unwrap(lib.vf_explore_redo(view_id), "ApplyResult")
end

---@return VF.Explore.HeaderRows
function M.explore_header_rows(view_id)
  return unwrap(lib.vf_explore_header_rows(view_id), "HeaderRows")
end

return M
