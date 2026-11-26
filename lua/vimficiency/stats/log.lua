-- The session log, stdpath("data")/vimficiency/sessions.vfs: one record per
-- finished session under the stamp it was scored with, so `:Vimfy stats` only
-- compares scores made the same way (src/LuaExports/proto/stats.proto); src/Stats/
-- reads it. The unstamped `sessions.jsonl` before it is kept in `legacy/` and
-- not read.

---@class (exact) VF.StatsLog
---@field path   fun(): string
---@field stamp  fun(): table
---@field append fun(kind: VF.Session.Kind, result: VF.Session.Result, saved_filename: string|nil)
---@field _for_test { session_of: fun(kind: VF.Session.Kind, result: VF.Session.Result, saved_filename: string|nil): VF.Stats.Session }
local M = {}

local config = require("vimficiency.config")
local ffi_config = require("vimficiency.ffi.config")
local ffi_optimizer = require("vimficiency.ffi.optimizer")
local ffi_stats = require("vimficiency.ffi.stats")

local function dir()
  return vim.fn.stdpath("data") .. "/vimficiency"
end

function M.path()
  return dir() .. "/sessions.vfs"
end

--- What a score made in this Neovim depends on, as a `vf.stats.Stamp`. Both
--- sessions and journal samples are scored with exactly these.
---@return table
function M.stamp()
  return {
    config = ffi_config.applied_globals(),
    optimizer_overrides = ffi_optimizer.encode_optimizer_overrides({ shared = config.optimizer }),
    slice_padding = config.SLICE_PADDING,
    max_search_lines = config.MAX_SEARCH_LINES,
    results_calculated = config.RESULTS_CALCULATED,
  }
end

local function retire_v1()
  local v1 = dir() .. "/sessions.jsonl"
  if not vim.uv.fs_stat(v1) then return end
  local legacy = dir() .. "/legacy"
  vim.fn.mkdir(legacy, "p")
  local target = legacy .. "/sessions.v1.jsonl"
  if not vim.uv.fs_stat(target) then vim.uv.fs_rename(v1, target) end
end

--- The lowest-cost entry, so the token tables compare against one sequence
--- rather than the whole top-N.
---@param optimal_results VF.Optimizer.Result[]
---@return VF.Optimizer.Result|nil
local function best_opt(optimal_results)
  local best = optimal_results[1]
  for i = 2, #optimal_results do
    if optimal_results[i].cost < best.cost then best = optimal_results[i] end
  end
  return best
end

---@param kind VF.Session.Kind
---@param result VF.Session.Result
---@param saved_filename string|nil
---@return VF.Stats.Session
local function session_of(kind, result, saved_filename)
  local best = best_opt(result.optimal_results)
  return {
    finished_wall_epoch = result.finished_wall_epoch,
    saved_filename = saved_filename,
    kind = kind,
    outcome = result.unbeaten and "unbeaten" or best and "scored" or "no_result",
    user_cost = result.user_cost,
    best_cost = best and best.cost or 0,
    user_seq = result.user_seq,
    best_seq = best and best.seq or "",
    commands = result.commands,
    typed_insert = result.user_typed_insert,
    key_count = result.key_count,
    analyze_ms = result.analyze_ms,
    query = result.query,
  }
end

--- Warns instead of raising: a session must finish even when its log write fails.
---@param kind VF.Session.Kind
---@param result VF.Session.Result
---@param saved_filename string|nil  # its file on the `sessions` shelf
function M.append(kind, result, saved_filename)
  vim.fn.mkdir(dir(), "p")
  retire_v1()
  local err = ffi_stats.append_session(M.path(), M.stamp(), session_of(kind, result, saved_filename))
  if err then
    vim.schedule(function()
      vim.notify("vimfy stats: could not append to the session log: " .. err, vim.log.levels.WARN)
    end)
  end
end

M._for_test = { session_of = session_of }

return M
