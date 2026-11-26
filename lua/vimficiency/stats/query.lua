---@class (exact) VF.Stats.Query
---@field summary     fun(scope: VF.Stats.Scope, scale?: VF.Stats.ChartScale): VF.Stats.Summary
---@field journal_coverage fun(): VF.Stats.JournalCoverage
---@field runs        fun(scope: VF.Stats.Scope, order: VF.Stats.Order, ascending: boolean, offset: integer, limit: integer): VF.Stats.RunList
---@field run         fun(row: VF.Stats.RunRow): VF.Session.Viewable|nil, string|nil
local M = {}

local disk = require("vimficiency.session.disk")
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

function M.journal_coverage()
  return ffi_stats.stats_journal_coverage({ journal_dir = journal.dir(), params = sampling.params() })
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

--- A listed run's text and keys, or why they are unavailable. Sessions and
--- journal runs differ only here: a session's result was saved as it finished,
--- a journal run is cut from its journal again.
---@param row VF.Stats.RunRow
---@return VF.Session.Viewable|nil run
---@return string|nil err
function M.run(row)
  if row.id.run == "session" then
    if row.saved_filename == "" then return nil, "this session was not saved" end
    local result, err, is_missing = disk.load("sessions", row.saved_filename)
    if is_missing then return nil, "its saved result was removed" end
    return result, err
  end
  local detail, err = ffi_stats.run({ id = row.id, journal_dir = journal.dir(), sample_params = sampling.params() })
  if not detail then return nil, err end
  return {
    recording = detail.recording,
    lines = detail.lines,
    goal_lines = detail.goal_lines,
    start_row = detail.start_row,
    start_col = detail.start_col,
    end_row = detail.end_row,
    end_col = detail.end_col,
    indent = detail.indent,
    filetype = detail.filetype,
    user_seq = detail.keys,
    user_typed = detail.typed,
    user_cost = row.user_cost,
    optimal_results = row.best_seq == "" and {} or { { seq = row.best_seq, cost = row.best_cost } },
  }, nil
end

return M
