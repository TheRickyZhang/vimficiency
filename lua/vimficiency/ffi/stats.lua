---@class (exact) VF.FfiStats
---@field append_session fun(path: string, stamp: table, session: VF.Stats.Session): string|nil
---@field summary        fun(request: VF.Stats.SummaryRequest): VF.Stats.Summary
---@field runs           fun(request: VF.Stats.RunsRequest): VF.Stats.RunList
---@field run            fun(request: VF.Stats.RunRequest): VF.Stats.RunDetail|nil, string|nil
local M = {}

local core = require("vimficiency.ffi.core")

local lib = core.lib

--- `vf.stats.Session` (src/LuaExports/proto/stats.proto).
---@class VF.Stats.Session
---@field finished_wall_epoch integer
---@field kind VF.Session.Kind
---@field outcome "no_result"|"scored"|"unbeaten"
---@field user_cost number
---@field best_cost number        # scored only
---@field user_seq string
---@field best_seq string
---@field commands VF.Session.Command[]
---@field typed_insert? string    # The pressed insert text; nil when the result predates it
---@field key_count integer
---@field analyze_ms number

---@alias VF.Stats.Outcome "scored"|"too_large"|"no_result"|"crashed"|"unsupported"|"unbeaten"
---@alias VF.Stats.Weight "effort"|"uniform"
---@alias VF.Stats.Order "worst"|"newest"|"slowest"|"user_cost"|"best_cost"|"saved"|"typed"

--- `vf.stats.Filter` (src/LuaExports/proto/summary.proto). Every field is
--- optional; Lua copies a row's `drill` rather than building one.
---@class VF.Stats.Filter
---@field source? "both"|"journal"|"sessions"
---@field epoch_seconds? { first: integer, last: integer }
---@field days? { first: integer, last: integer }   # inclusive local days since 1970-01-01
---@field window_size? integer   # 0: the charted size
---@field kind? "any"|"nav"|"edit"
---@field outcomes? VF.Stats.Outcome[]
---@field commands? { tokens: string[], side: "either"|"user"|"optimizer", absent?: boolean }[]   # any of `tokens`; none with `absent`
---@field over_typed? boolean    # only runs where the user typed more than the best sequence
---@field beats? boolean         # only runs whose keys cost less than the best sequence
---@field timed? boolean
---@field query_size? { first: integer, last: integer }

--- `vf.stats.SummaryRequest` (src/LuaExports/proto/summary.proto).
---@class VF.Stats.SummaryRequest
---@field session_log string
---@field stamp table
---@field current_only boolean   # leave out every other stamp's sessions and rollups
---@field journal_dir string
---@field sample_params table
---@field weight VF.Stats.Weight
---@field filter VF.Stats.Filter
---@field chart_unit? string
---@field chart_bucket_size? integer
---@field chart_buckets? integer
---@field charted_size integer   # 0: the middle window size

---@class VF.Stats.Score
---@field spans integer
---@field score number   # meaningless when `spans` is 0

---@class VF.Stats.DayScore
---@field days_ago integer
---@field score number
---@field spans integer   # the runs the score is over
---@field drill VF.Stats.Filter

---@class VF.Stats.Timing
---@field n integer
---@field p50 number
---@field p95 number
---@field p99 number
---@field max number
---@field drill? VF.Stats.Filter

---@class VF.Stats.TimingBucket
---@field first integer
---@field last integer
---@field timing VF.Stats.Timing
---@field deadline_hit integer
---@field score VF.Stats.Score     # over its scoring runs, timed or not
---@field drill? VF.Stats.Filter   # those runs

--- Unbeaten runs holding a command the search does not model; a run counts
--- under each tag it holds, and under `other` when it holds none.
---@class VF.Stats.UnbeatenTag
---@field name string
---@field runs integer
---@field drill VF.Stats.Filter

---@class VF.Stats.Summary
---@field chart_unit string
---@field lifetime { sessions: integer, mark: integer, watch: integer, recall: integer, suggest: integer, commands: integer }
---@field sessions VF.Stats.SessionSummary
---@field journal VF.Stats.JournalSummary
---@field scoring_label string   # the current stamp's
---@field hidden_runs integer    # sessions and rolled-up runs `current_only` left out

---@class VF.Stats.SessionSummary
---@field sessions integer
---@field score VF.Stats.Score
---@field chart_score VF.Stats.Score
---@field days VF.Stats.DayScore[]
---@field no_result integer
---@field unbeaten integer
---@field beats integer
---@field timing? VF.Stats.Timing
---@field no_result_drill VF.Stats.Filter
---@field unbeaten_drill VF.Stats.Filter
---@field beats_drill VF.Stats.Filter
---@field commands VF.Stats.CommandSummary
---@field unbeaten_tags VF.Stats.UnbeatenTag[]

---@class VF.Stats.JournalSummary
---@field present boolean
---@field scored_bytes integer
---@field journal_bytes integer
---@field sizes { size: integer, score: VF.Stats.Score }[]
---@field charted_size integer
---@field score VF.Stats.Score
---@field nav VF.Stats.Score
---@field edit VF.Stats.Score
---@field chart_score VF.Stats.Score
---@field days VF.Stats.DayScore[]
---@field unscored { outcome: VF.Stats.Outcome, runs: integer, drill: VF.Stats.Filter }[]
---@field unbeaten integer
---@field deadline_hit integer
---@field rollup_runs_without_commands integer
---@field nav_drill VF.Stats.Filter
---@field edit_drill VF.Stats.Filter
---@field unbeaten_drill VF.Stats.Filter
---@field nav_timing VF.Stats.TimingBucket[]
---@field edit_timing VF.Stats.TimingBucket[]
---@field commands VF.Stats.CommandSummary
---@field unbeaten_tags VF.Stats.UnbeatenTag[]

---@class VF.Stats.CommandRow
---@field text string
---@field user integer
---@field optimizer integer
---@field user_share number        # Fraction of the user's commands
---@field optimizer_share number   # Fraction of the optimizer's commands
---@field ratio number             # user_share / optimizer_share
---@field split string
---@field drill VF.Stats.Filter

---@class VF.Stats.CommandSummary
---@field spans integer   # the sessions or runs counted
---@field more VF.Stats.CommandRow[]
---@field less VF.Stats.CommandRow[]
---@field never VF.Stats.CommandRow[]   # Only the user uses these, most used first; never in `more`
---@field user_typed_chars integer
---@field optimizer_typed_chars integer
---@field typed_drill VF.Stats.Filter   # the runs where the user typed more

--- `vf.stats.RunsRequest` (src/LuaExports/proto/summary.proto).
---@class VF.Stats.RunsRequest
---@field scope VF.Stats.SummaryRequest   # its sessions and charted-size runs, under its filter
---@field order VF.Stats.Order
---@field ascending? boolean  # else largest first; a run with no value there is last either way
---@field offset integer
---@field limit integer

--- A journal run (`journal`) or a session (`session`, its index in the log).
---@class VF.Stats.RunId
---@field run "journal"|"session"
---@field journal? { file: string, window_size: integer, last_atom: integer }
---@field session? integer

---@class VF.Stats.RunRow
---@field id VF.Stats.RunId
---@field wall_epoch integer
---@field user_keys string
---@field best_seq string
---@field user_cost number
---@field best_cost number   # an unbeaten run's is its user_cost
---@field outcome VF.Stats.Outcome
---@field nav_only boolean
---@field window_size integer   # 0 for a session
---@field analyze_ms number
---@field user_display string   # typed text collapsed to ⟨n⟩
---@field best_display string
---@field user_typed_chars integer   # typed in insert mode
---@field best_typed_chars integer

---@class VF.Stats.RunList
---@field total integer   # rolled-up runs included
---@field rolled_up integer
---@field rollup_runs_without_commands integer
---@field runs VF.Stats.RunRow[]

--- `vf.stats.RunRequest` (src/LuaExports/proto/summary.proto): a journal run,
--- found under the params it was sampled with.
---@class VF.Stats.RunRequest
---@field id VF.Stats.RunId
---@field journal_dir string
---@field sample_params table

--- A journal run as its search saw it. Rows and columns are 0-indexed and
--- relative to `lines`.
---@class VF.Stats.RunDetail
---@field recording VF.Replay.Recording
---@field commands VF.Session.Command[]
---@field lines string[]
---@field goal_lines string[]   # the same slice as the run ended
---@field start_row integer
---@field start_col integer
---@field end_row integer
---@field end_col integer
---@field keys string     # resolved, as the search scored them (gj/gk as j/k)
---@field typed string    # physical
---@field indent VF.IndentOptions?
---@field filetype string # as the run started

--- Appends a finished session to the log at `path`, under `stamp` (a
--- `vf.stats.Stamp`; the library adds its version). Returns the error rather
--- than raising it, so a failed write cannot stop a session from finishing.
---@param path string
---@param stamp table
---@param session VF.Stats.Session
---@return string|nil err
function M.append_session(path, stamp, session)
  local bytes = core.encode("vf.stats.AppendSessionRequest",
    { path = path, stamp = stamp, session = session })
  local _, err = core.decode(lib.vf_append_session(bytes, #bytes), "vf.Empty")
  return err
end

---@param request VF.Stats.SummaryRequest
---@return VF.Stats.Summary
function M.summary(request)
  return core.call(lib.vf_stats_summary, "vf.stats.SummaryRequest", request, "vf.stats.Summary")
end

---@param request VF.Stats.RunsRequest
---@return VF.Stats.RunList
function M.runs(request)
  return core.call(lib.vf_stats_runs, "vf.stats.RunsRequest", request, "vf.stats.RunList")
end

--- Returns the error rather than raising it when the run is a session's, has
--- no slice, or its journal is gone or no longer cuts into that window under
--- `sample_params`: the view reports those as unavailable.
---@param request VF.Stats.RunRequest
---@return VF.Stats.RunDetail|nil detail
---@return string|nil err
function M.run(request)
  local bytes = core.encode("vf.stats.RunRequest", request)
  return core.decode(lib.vf_stats_run(bytes, #bytes), "vf.stats.RunDetail")
end

return M
