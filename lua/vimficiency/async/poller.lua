local util = require("vimficiency.util")

local M = {}

-- Cadence for checking the worker. The search is off-thread, so this only
-- bounds how quickly the result surfaces once it is ready, not the main
-- thread's responsiveness. Implementation detail, not a user-facing setting.
local POLL_INTERVAL_MS = 25

---@class VF.Async.PollHandle
---@field done boolean
---@field cancelled boolean
---@field close_timer fun()
---@field cancel_fn fun()

--- Polls a pending worker job on a uv timer and resolves it on the main thread.
--- Shared by the explore (View) and suggest (analyze) async paths — they differ
--- only in the injected callbacks. `poll_fn()` returns a non-nil value once the
--- worker finishes, or nil while still computing; `on_ready` is called exactly
--- once with that value. An error in either callback ends the job:
--- `on_error` releases what the caller tied to it, then the error surfaces at
--- `util.entry`. `cancel_fn()` signals the worker to stop and is invoked by
--- `M.cancel` only if the job has not finished.
---@param poll_fn fun(): any
---@param cancel_fn fun()
---@param on_ready fun(value: any)
---@param on_error fun()
---@return VF.Async.PollHandle
function M.start(poll_fn, cancel_fn, on_ready, on_error)
  local timer = assert(vim.uv.new_timer())
  local handle = { done = false, cancelled = false, cancel_fn = cancel_fn }

  local function close_timer()
    if not timer:is_closing() then
      timer:stop()
      timer:close()
    end
  end
  handle.close_timer = close_timer

  local started, start_err = timer:start(0, POLL_INTERVAL_MS, vim.schedule_wrap(util.entry(function()
    -- Guard against an already-queued tick firing after we resolved/cancelled:
    -- polling a taken job raises (its id is no longer live).
    if handle.done or handle.cancelled then return end
    local ok, err = pcall(function()
      local value = poll_fn()
      if value == nil then return end
      handle.done = true
      close_timer()
      on_ready(value)
    end)
    if not ok then
      -- A failed poll must stop the timer, or the error repeats every tick.
      handle.done = true
      close_timer()
      on_error()
      error(err, 0)
    end
  end)))
  if not started then
    close_timer()
    error(start_err, 0)
  end

  return handle
end

--- Stops polling and cancels the worker if it has not finished. Idempotent.
---@param handle VF.Async.PollHandle
function M.cancel(handle)
  if handle.cancelled then return end
  handle.cancelled = true
  handle.close_timer()
  if not handle.done then
    handle.cancel_fn()
  end
end

return M
