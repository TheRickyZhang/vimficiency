---@class (exact) VF.Stats.Query
---@field summary     fun(scope: VF.Stats.Scope, scale?: VF.Stats.ChartScale): VF.Stats.Summary
---@field runs        fun(scope: VF.Stats.Scope, order: VF.Stats.Order, ascending: boolean, offset: integer, limit: integer): VF.Stats.RunList
---@field run         fun(id: VF.Stats.RunId): VF.Stats.RunDetail|nil, string|nil
local M = {}

local ffi_stats = require("vimficiency.ffi.stats")
local journal = require("vimficiency.capture.journal")
local log = require("vimficiency.stats.log")
local sampling = require("vimficiency.stats.sampling")

--- What a summary or run list reduces.
---@class VF.Stats.Scope
---@field current_only boolean
---@field weight VF.Stats.Weight
---@field filter VF.Stats.Filter
---@field chart_unit? string
---@field charted_size integer   # 0: the middle window size

---@param scope VF.Stats.Scope
---@return VF.Stats.SummaryRequest
local function request(scope)
  return {
    session_log = log.path(),
    stamp = log.stamp(),
    current_only = scope.current_only,
    journal_dir = journal.dir(),
    sample_params = sampling.params(),
    weight = scope.weight,
    filter = scope.filter,
    chart_unit = scope.chart_unit,
    charted_size = scope.charted_size,
  }
end

---@param scope VF.Stats.Scope
---@param scale? VF.Stats.ChartScale
---@return VF.Stats.Summary
function M.summary(scope, scale)
  local params = request(scope)
  if scale then
    params.chart_unit = scale.unit
    params.chart_bucket_size = scale.bucket_size
    params.chart_buckets = scale.buckets
  end
  return ffi_stats.summary(params)
end

--- The runs `M.summary` reduces under the same scope, one page of them.
---@param scope VF.Stats.Scope
---@param order VF.Stats.Order
---@param ascending boolean
---@param offset integer
---@param limit integer
---@return VF.Stats.RunList
function M.runs(scope, order, ascending, offset, limit)
  return ffi_stats.runs({
    scope = request(scope), order = order, ascending = ascending, offset = offset, limit = limit,
  })
end

--- A listed journal run as its search saw it, or why it is unavailable.
---@param id VF.Stats.RunId
---@return VF.Stats.RunDetail|nil detail
---@return string|nil err
function M.run(id)
  return ffi_stats.run({ id = id, journal_dir = journal.dir(), sample_params = sampling.params() })
end

return M
