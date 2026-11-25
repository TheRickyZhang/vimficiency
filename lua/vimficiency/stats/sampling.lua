-- Scores the capture journal for `:Vimfy stats`. The scoring is `vimfy_journal`
-- (src/Journal/), a separate low-priority process run in the background; this
-- module starts it, and src/Stats/ reads the `vf.journal.SampleFile` it leaves
-- beside each journal and the rollups of the old journals it deletes. Stats
-- show the scoring status until the worker finishes.

---@class (exact) VF.StatsSampling
---@field refresh          fun(on_done: fun(err: string|nil))
---@field start_background fun()
---@field stop_background  fun()
---@field running          fun(): boolean
---@field when_done        fun(on_done: fun(err: string|nil)): boolean
---@field last_error       fun(): string|nil
---@field params           fun(): table
---@field _for_test        { maybe_run: fun() }
local M = {}

local config = require("vimficiency.config")
local ffi_core = require("vimficiency.ffi.core")
local journal = require("vimficiency.capture.journal")
local stats_log = require("vimficiency.stats.log")

-- Each crash costs one window: the next run records it and steps past it.
local MAX_CRASHES = 20

-- Background runs: the first lets startup settle; later ones wait for Neovim to
-- go idle with new journal content, at most once per interval.
local STARTUP_DELAY_MS = 30 * 1000
local RUN_INTERVAL_MS = 5 * 60 * 1000
local LOWEST_PRIORITY = 19

local BUSY_EXIT_CODE = 2
local LOCKED = "another sampler is scoring the journal"

--- Callbacks of the refresh in flight; nil when none is.
---@type (fun(err: string|nil))[]|nil
local waiters = nil

---@class VF.Stats.BackgroundRun
---@field group integer
---@field timer? uv.uv_timer_t
---@field last_run_ms number
---@field scored_bytes? integer
---@field err? string
---@type VF.Stats.BackgroundRun|nil
local background_run
local AUGROUP = "VimficiencyJournalSampling"

local function tool_path()
  return vim.fs.joinpath(vim.fs.dirname(ffi_core.lib_path), "vimfy_journal")
end

--- `vf.journal.SampleParams`: what samples are made, and read back, under.
---@return table
function M.params()
  local sampling = config.journal.sampling
  return {
    window_sizes = sampling.window_sizes,
    deadline_ms = sampling.deadline_ms,
    idle_gap_ms = sampling.idle_gap_ms,
    stamp = stats_log.stamp(),
  }
end

---@param err string|nil
local function finish(err)
  local done = waiters --[[@as (fun(err: string|nil))[] ]]
  waiters = nil
  for _, on_done in ipairs(done) do on_done(err) end
end

---@param request string
---@param crashes integer
local function spawn(request, crashes)
  local ok, proc = pcall(vim.system, { tool_path() }, { stdin = request, text = true }, vim.schedule_wrap(function(result)
    if result.signal == 0 then
      if result.code == BUSY_EXIT_CODE then
        finish(LOCKED)
      else
        finish(result.code ~= 0 and vim.trim(result.stderr):gsub("%s*\n%s*", " ") or nil)
      end
    elseif crashes < MAX_CRASHES then
      spawn(request, crashes + 1)
    else
      finish(string.format("vimfy_journal died %d times (signal %d)", crashes + 1, result.signal))
    end
  end))
  if not ok then
    finish(tostring(proc))
    return
  end
  -- Scoring is never urgent; the editor's own work comes first.
  vim.uv.os_setpriority(proc.pid, LOWEST_PRIORITY)
end

--- Brings every journal's samples up to date, then calls `on_done` on the main
--- loop. Only new journal content is scored, so a refresh with nothing new is
--- cheap. A refresh already in flight is joined rather than repeated.
---@param on_done fun(err: string|nil)
function M.refresh(on_done)
  if waiters then
    waiters[#waiters + 1] = on_done
    return
  end
  if not vim.uv.fs_stat(journal.dir()) then
    on_done(nil)
    return
  end
  if not vim.uv.fs_stat(tool_path()) then
    on_done("vimfy_journal is not installed; run `:Lazy build vimficiency`, or `./scripts/install.sh` in the plugin directory")
    return
  end
  local request = ffi_core.encode("vf.journal.SampleRequest",
    { journal_dir = journal.dir(), params = M.params(), keep_days = config.journal.keep_days })
  waiters = { on_done }
  spawn(request, 0)
  if waiters then
    vim.api.nvim_exec_autocmds("User", { pattern = "VimficiencyJournalScoringStarted", modeline = false })
  end
end

---@return boolean
function M.running()
  return waiters ~= nil
end

--- Calls `on_done` when the run in flight finishes; false when none is.
---@param on_done fun(err: string|nil)
---@return boolean
function M.when_done(on_done)
  if not waiters then return false end
  waiters[#waiters + 1] = on_done
  return true
end

--- Why the last background run failed; nil once one succeeds.
---@return string|nil
function M.last_error()
  return background_run and background_run.err
end

---@return integer
local function journal_bytes()
  local total = 0
  for name, type in vim.fs.dir(journal.dir()) do
    if type == "file" and name:match("%.vfj$") then
      total = total + vim.uv.fs_stat(vim.fs.joinpath(journal.dir(), name)).size
    end
  end
  return total
end

local function maybe_run()
  local run = background_run
  if not run or waiters or not vim.uv.fs_stat(journal.dir()) then return end
  if vim.uv.now() - run.last_run_ms < RUN_INTERVAL_MS then return end
  local bytes = journal_bytes()
  if bytes == run.scored_bytes then return end
  run.last_run_ms = vim.uv.now()
  M.refresh(function(err)
    if not err then run.scored_bytes = bytes end
    if err ~= LOCKED then run.err = err end
  end)
end

--- Scores new journal content in the background: once shortly after startup,
--- then whenever Neovim goes idle with more of it.
function M.start_background()
  M.stop_background()
  if not config.journal.sampling.auto then return end
  local run = { group = vim.api.nvim_create_augroup(AUGROUP, { clear = true }), last_run_ms = -math.huge }
  background_run = run
  vim.api.nvim_create_autocmd("CursorHold", { group = run.group, callback = maybe_run })
  run.timer = vim.defer_fn(function()
    if background_run ~= run then return end
    run.timer = nil
    maybe_run()
  end, STARTUP_DELAY_MS)
end

function M.stop_background()
  local run = background_run
  if not run then return end
  background_run = nil
  if run.timer and not run.timer:is_closing() then
    run.timer:stop()
    run.timer:close()
  end
  vim.api.nvim_del_augroup_by_id(run.group)
end

M._for_test = { maybe_run = maybe_run }

return M
