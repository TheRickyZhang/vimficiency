-- Append-only JSONL log of every finished session. One JSON object per
-- line at stdpath("data")/vimficiency/sessions.jsonl. This is the source
-- of truth for :Vimfy stats, and also flags optimizer-beats.
--
-- Log-first (not aggregate-first) so the stats schema can evolve: if we
-- later change the efficiency metric or add new buckets, we re-reduce the
-- raw records instead of being stuck with whatever shape we committed to
-- at write time. See dev/plan discussion for the full rationale.

local M = {}

local SCHEMA_VERSION = 1

local function log_path()
  return vim.fn.stdpath("data") .. "/vimficiency/sessions.jsonl"
end

M._log_path = log_path

local function ensure_dir()
  local dir = vim.fn.stdpath("data") .. "/vimficiency"
  if vim.fn.isdirectory(dir) == 0 then
    vim.fn.mkdir(dir, "p")
  end
end

--- Pick the single lowest-cost entry from `optimal_results`. Motion
--- comparisons use this (not the full Top-N list) so the optimizer
--- histogram isn't inflated by suboptimal alternatives.
local function best_opt(optimal_results)
  if #optimal_results == 0 then return nil end
  local best = optimal_results[1]
  for i = 2, #optimal_results do
    if optimal_results[i].cost < best.cost then
      best = optimal_results[i]
    end
  end
  return best
end

--- Build a log record from the finished-session result.
---@param kind VF.Session.Kind
---@param result VF.Session.Result
---@return table
function M.build_record(kind, result)
  local best = best_opt(result.optimal_results)
  local best_cost = best and best.cost or nil
  local best_seq  = best and best.seq  or nil
  local beats = (best_cost ~= nil) and (result.user_cost < best_cost)
  return {
    v = SCHEMA_VERSION,
    finish_epoch  = result.finished_wall_epoch,
    type          = require("vimficiency.session.store").session_type(kind),
    kind          = kind,
    key_count     = result.key_count,
    user_cost     = result.user_cost,
    best_opt_cost = best_cost,
    user_seq      = result.user_seq,
    best_opt_seq  = best_seq,
    beats         = beats or false,
    analyze_ms    = result.analyze_ms,
  }
end

--- Append one record to the log. Best-effort: on failure, warn once via
--- vim.notify but never raise — a session finish must not be blocked by
--- stats plumbing.
function M.append(record)
  ensure_dir()
  local path = log_path()
  local fh, open_err = io.open(path, "a")
  if not fh then
    vim.schedule(function()
      vim.notify("vimfy stats: could not open log: " ..
        tostring(open_err or "?"), vim.log.levels.WARN)
    end)
    return
  end
  fh:write(vim.json.encode(record))
  fh:write("\n")
  fh:close()
end

--- Read every record from the log. Silently drops malformed lines (a
--- trailing partial line from a crash is the canonical case; we don't
--- want a single bad byte to blank the stats view).
---@return table[] records
---@return integer skipped  count of lines that failed to decode
function M.read_all()
  local path = log_path()
  local fh = io.open(path, "r")
  if not fh then return {}, 0 end
  local records = {}
  local skipped = 0
  for line in fh:lines() do
    if line ~= "" then
      local ok, decoded = pcall(vim.json.decode, line)
      if ok and type(decoded) == "table" then
        records[#records + 1] = decoded
      else
        skipped = skipped + 1
      end
    end
  end
  fh:close()
  return records, skipped
end

return M
