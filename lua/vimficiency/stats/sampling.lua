-- Scores the capture journal for `:Vimfy stats`. The scoring is `vimfy_journal`
-- (src/Journal/), a separate process; this module runs it and reads the
-- `vf.journal.SampleFile` it leaves beside each journal.

---@class (exact) VF.StatsSampling
--- Running the sampler
---@field refresh      fun(on_done: fun(err: string|nil))
--- Reading what it wrote
---@field read_samples fun(): VF.StatsSampling.Sample[]
---@field aggregate    fun(samples: VF.StatsSampling.Sample[]): table<integer, VF.StatsSampling.SizeSummary>
---@field normalized   fun(totals: VF.StatsSampling.Totals): number|nil
local M = {}

local config = require("vimficiency.config")
local ffi_config = require("vimficiency.ffi.config")
local ffi_core = require("vimficiency.ffi.core")
local ffi_optimizer = require("vimficiency.ffi.optimizer")
local journal = require("vimficiency.capture.journal")

--- `vf.journal.Sample` (src/LuaExports/proto/journal.proto).
---@class VF.StatsSampling.Sample
---@field window_size integer
---@field first_atom integer
---@field last_atom integer
---@field atom_count integer
---@field wall_epoch integer
---@field outcome "scored"|"too_large"|"no_result"|"crashed"|"unsupported"
---@field user_cost number
---@field best_cost number
---@field nav_only boolean
---@field net_zero boolean
---@field deadline_hit boolean
---@field lossy boolean
---@field analyze_ms number

---@class VF.StatsSampling.Totals
---@field windows integer
---@field user_cost number
---@field best_cost number

---@class VF.StatsSampling.SizeSummary
---@field all VF.StatsSampling.Totals
---@field nav VF.StatsSampling.Totals     # text unchanged
---@field edit VF.StatsSampling.Totals
---@field by_day table<integer, VF.StatsSampling.Totals>  # keyed by floor(epoch / 86400)
---@field unscored table<string, integer>                 # outcome -> windows
---@field deadline_hit integer

-- Each crash costs one window: the next run records it and steps past it.
local MAX_CRASHES = 20

--- Callbacks of the refresh in flight; nil when none is.
---@type (fun(err: string|nil))[]|nil
local waiters = nil

---@param path string
---@return string|nil
local function read_file(path)
  local fh = io.open(path, "rb")
  if not fh then return nil end
  local bytes = fh:read("*a")
  fh:close()
  return bytes
end

local function tool_path()
  return vim.fs.joinpath(vim.fs.dirname(ffi_core.lib_path), "vimfy_journal")
end

local function lock_path()
  return vim.fs.joinpath(journal.dir(), "sampling.lock")
end

--- Two samplers appending to one samples file would interleave, so only one
--- Neovim samples at a time. A lock whose owner has exited is taken over.
---@return boolean
local function acquire_lock()
  local path = lock_path()
  for _ = 1, 2 do
    local fd = vim.uv.fs_open(path, "wx", 420)
    if fd then
      vim.uv.fs_write(fd, tostring(vim.uv.os_getpid()))
      vim.uv.fs_close(fd)
      return true
    end
    local owner = tonumber(read_file(path))
    if owner and vim.uv.kill(owner, 0) == 0 then return false end
    vim.uv.fs_unlink(path)
  end
  return false
end

---@return string  # serialized `vf.journal.SampleRequest`
local function encode_request()
  local sampling = config.journal.sampling
  return ffi_core.encode("vf.journal.SampleRequest", {
    journal_dir = journal.dir(),
    params = {
      window_sizes = sampling.window_sizes,
      slice_padding = config.SLICE_PADDING,
      max_search_lines = config.MAX_SEARCH_LINES,
      results_calculated = config.RESULTS_CALCULATED,
      deadline_ms = sampling.deadline_ms,
      idle_gap_ms = sampling.idle_gap_ms,
      optimizer_overrides = ffi_optimizer.encode_optimizer_overrides({ shared = config.optimizer }),
      config = ffi_config.applied_globals(),
    },
  })
end

---@param err string|nil
local function finish(err)
  vim.uv.fs_unlink(lock_path())
  local done = waiters --[[@as (fun(err: string|nil))[] ]]
  waiters = nil
  for _, on_done in ipairs(done) do on_done(err) end
end

---@param request string
---@param crashes integer
local function spawn(request, crashes)
  vim.system({ tool_path() }, { stdin = request, text = true }, vim.schedule_wrap(function(result)
    if result.signal == 0 then
      finish(result.code ~= 0 and vim.trim(result.stderr):gsub("%s*\n%s*", " ") or nil)
    elseif crashes < MAX_CRASHES then
      spawn(request, crashes + 1)
    else
      finish(string.format("vimfy_journal died %d times (signal %d)", crashes + 1, result.signal))
    end
  end))
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
  if not acquire_lock() then
    on_done("another Neovim is sampling the journal")
    return
  end
  waiters = { on_done }
  spawn(encode_request(), 0)
end

--- Every sample on disk. Call after a refresh: a samples file still being
--- written can end mid-record.
---@return VF.StatsSampling.Sample[]
function M.read_samples()
  local samples = {}
  local dir = journal.dir()
  for name in vim.fs.dir(dir) do
    if name:match("%.samples$") then
      local bytes = read_file(vim.fs.joinpath(dir, name)) --[[@as string]]
      for _, record in ipairs(ffi_core.decode_message("vf.journal.SampleFile", bytes).records) do
        if record.body == "sample" then samples[#samples + 1] = record.sample end
      end
    end
  end
  return samples
end

---@return VF.StatsSampling.Totals
local function new_totals()
  return { windows = 0, user_cost = 0, best_cost = 0 }
end

---@param totals VF.StatsSampling.Totals
---@param sample VF.StatsSampling.Sample
local function add(totals, sample)
  totals.windows = totals.windows + 1
  totals.user_cost = totals.user_cost + sample.user_cost
  -- A search that found nothing cheaper than the user's keys says they were
  -- optimal, not better than optimal.
  totals.best_cost = totals.best_cost + math.min(sample.best_cost, sample.user_cost)
end

--- Totals per window size. Only scored windows carry cost; the rest are
--- counted by outcome.
---@param samples VF.StatsSampling.Sample[]
---@return table<integer, VF.StatsSampling.SizeSummary>
function M.aggregate(samples)
  local by_size = {}
  for _, sample in ipairs(samples) do
    local summary = by_size[sample.window_size]
    if not summary then
      summary = {
        all = new_totals(),
        nav = new_totals(),
        edit = new_totals(),
        by_day = {},
        unscored = {},
        deadline_hit = 0,
      }
      by_size[sample.window_size] = summary
    end

    if sample.outcome ~= "scored" then
      summary.unscored[sample.outcome] = (summary.unscored[sample.outcome] or 0) + 1
    elseif sample.user_cost > 0 then
      local day = math.floor(sample.wall_epoch / 86400)
      summary.by_day[day] = summary.by_day[day] or new_totals()
      add(summary.all, sample)
      add(sample.nav_only and summary.nav or summary.edit, sample)
      add(summary.by_day[day], sample)
      if sample.deadline_hit then summary.deadline_hit = summary.deadline_hit + 1 end
    end
  end
  return by_size
end

--- Best effort as a percentage of the effort spent: 100 is optimal. Totals are
--- summed before dividing, so long windows weigh more than short ones.
---@param totals VF.StatsSampling.Totals
---@return number|nil  # nil without any scored window
function M.normalized(totals)
  if totals.user_cost <= 0 then return nil end
  return 100 * totals.best_cost / totals.user_cost
end

return M
