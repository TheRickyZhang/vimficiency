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

---@param req VF.SearchRequest
---@param results_calculated integer
---@return VF.Optimizer.Result[] results, number user_cost, string debug, VF.Diff.Region[] diffs
function M.analyze(req, results_calculated)
  ---@type VF.Optimizer.AnalyzeResult
  local result = core.call(lib.vf_analyze, "vf.search.AnalyzeRequest",
    { search = req, results_calculated = results_calculated }, "vf.search.AnalyzeResult")
  return result.results, result.user_cost, result.dbg, result.diffs
end

--- `vf.search.AnalyzeResult` (src/LuaExports/proto/search.proto).
---@class VF.Optimizer.AnalyzeResult
---@field results VF.Optimizer.Result[]
---@field user_cost number
---@field dbg string
---@field diffs VF.Diff.Region[]   # The planner's regions the search ran against; empty for pure navigation

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
