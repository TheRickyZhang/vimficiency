local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

---@param overrides? VF.OptimizerOverrides
---@return string
function M.encode_optimizer_overrides(overrides)
  if not overrides then return "" end
  local lines = {}
  for _, scope in ipairs({ "shared", "nav", "transform", "composition" }) do
    local kvs = overrides[scope]
    if kvs then
      for k, v in pairs(kvs) do
        local encoded
        if type(v) == "boolean" then
          encoded = v and "1" or "0"
        else
          encoded = tostring(v)
        end
        lines[#lines + 1] = scope .. ":" .. k .. "=" .. encoded
      end
    end
  end
  -- A stored encoding is compared with a later one of the same overrides.
  table.sort(lines)
  return table.concat(lines, "\n")
end

local optimizer_defaults_cache = nil

---@return table<"shared"|"nav"|"transform"|"composition", table<string, number|boolean>>
function M.get_optimizer_defaults()
  if optimizer_defaults_cache == nil then
    local defaults = core.unwrap(lib.vf_get_optimizer_defaults(), "vf.search.OptimizerDefaults")
    optimizer_defaults_cache = {}
    for scope, params in pairs(defaults) do
      local values = {}
      for _, param in ipairs(params) do
        values[param.name] = param[param.value]
      end
      optimizer_defaults_cache[scope] = values
    end
  end
  return optimizer_defaults_cache
end

--- A session over its whole buffers; rows and columns are 0-indexed.
---@class VF.Optimizer.SessionSpan
---@field initial_lines string[]
---@field goal_lines string[]
---@field start_row integer
---@field start_col integer
---@field end_row integer
---@field end_col integer
---@field window_height integer
---@field scroll_amount integer
---@field user_seq string
---@field user_typed string

--- Cuts `span` to the rows a search needs. Raises when that region is taller
--- than `max_search_lines`.
---@param span VF.Optimizer.SessionSpan
---@param padding integer
---@param max_search_lines integer
---@return VF.SearchRequest
function M.build_search_request(span, padding, max_search_lines)
  local request = vim.tbl_extend("error", span, {
    padding = padding,
    max_search_lines = max_search_lines,
  })
  return core.call(lib.vf_build_search_request, "vf.search.BuildSearchRequest",
    request, "vf.search.SearchRequest")
end

---@param req VF.SearchRequest
---@param results_calculated integer
---@return VF.Optimizer.Result[] results, number user_cost, string debug, VF.Diff.Region[] diffs, boolean unbeaten
function M.analyze(req, results_calculated)
  ---@type VF.Optimizer.AnalyzeResult
  local result = core.call(
    lib.vf_analyze,
    "vf.search.AnalyzeRequest",
    { search = req, results_calculated = results_calculated },
    "vf.search.AnalyzeResult"
  )
  return result.results, result.user_cost, result.dbg, result.diffs, result.unbeaten
end

--- `vf.search.AnalyzeResult` (src/LuaExports/proto/search.proto).
---@class VF.Optimizer.AnalyzeResult
---@field results VF.Optimizer.Result[]
---@field user_cost number
---@field dbg string
---@field diffs VF.Diff.Region[]   # The planner's regions the search ran against; empty for pure navigation
---@field unbeaten boolean         # `results` is empty because no modeled sequence comes close to the user's keys

--- Kicks off `analyze` on a C++ worker thread and returns a job id immediately.
--- `deadline_ms` 0 = run to natural caps. Poll with `analyze_poll` until it
--- stops returning nil.
---@param req VF.SearchRequest
---@param results_calculated integer
---@param deadline_ms integer
---@return integer job_id
function M.analyze_start_async(req, results_calculated, deadline_ms)
  return core.call(lib.vf_analyze_start_async, "vf.search.AnalyzeRequest", {
    search = req,
    results_calculated = results_calculated,
    deadline_ms = deadline_ms,
  }, "vf.JobHandle").job_id
end

--- Returns the analyze result once the worker finishes, or nil while it is
--- still computing.
---@param job_id integer
---@return VF.Optimizer.AnalyzeResult|nil
function M.analyze_poll(job_id)
  return core.unwrap(lib.vf_analyze_poll(job_id), "vf.search.AnalyzeResult")
end

---@param job_id integer
---@return boolean
function M.analyze_cancel(job_id)
  return lib.vf_analyze_cancel(job_id) == 1
end

return M
