-- Shared auto-fire engine for watch and auto_suggest.
-- Supports idle triggers and keystroke-count triggers.

local M = {}

local atom_stream = require("vimficiency.capture.atom_stream")
local util = require("vimficiency.util")

---@class VF.Capture.EndTriggerOpts
---@field name         string                       # Unique subscriber name.
---@field idle_ms      integer                      # Idle threshold before fire.
---@field cooldown_ms  integer                      # Minimum time between fires.
---@field on_fire      fun(): boolean?              # Callback. Truthy → refresh cooldown.

--- Arm an idle trigger.
---@param opts VF.Capture.EndTriggerOpts
---@return fun()|nil disarm
function M.arm_idle(opts)
  local idle_ms     = opts.idle_ms
  local cooldown_ns = opts.cooldown_ms * 1e6
  local on_fire     = opts.on_fire
  local name        = opts.name

  local timer = vim.uv.new_timer()
  if not timer then return nil end

  local disarmed = false
  local last_fire_hrtime = 0

  local function fire()
    if disarmed then return end
    local now_ns = vim.uv.hrtime()
    if last_fire_hrtime > 0 and (now_ns - last_fire_hrtime) < cooldown_ns then
      return
    end
    local counted = on_fire()
    if counted then
      last_fire_hrtime = now_ns
    end
  end

  local function reset_timer()
    timer:stop()
    timer:start(idle_ms, 0, vim.schedule_wrap(util.entry(fire)))
  end

  local ok = atom_stream.add_listener(name, function()
    reset_timer()
  end)
  if not ok then
    timer:close()
    return nil
  end

  return function()
    if disarmed then return end
    disarmed = true
    atom_stream.remove_listener(name)
    timer:stop()
    timer:close()
  end
end

---@class VF.Capture.ArmKeysOpts
---@field name         string                       # Unique subscriber name.
---@field every        integer                      # Fire every N real keystrokes.
---@field cooldown_ms  integer                      # Minimum time between fires.
---@field on_fire      fun(): boolean?              # Callback. Truthy → refresh cooldown.

--- Arm a keystroke-count trigger.
---@param opts VF.Capture.ArmKeysOpts
---@return fun()|nil disarm
function M.arm_keys(opts)
  local every       = opts.every
  local cooldown_ns = opts.cooldown_ms * 1e6
  local on_fire     = opts.on_fire
  local name        = opts.name

  local disarmed = false
  local count = 0
  local last_fire_hrtime = 0

  -- One atom == one command, so `every` now counts commands, not raw
  -- keystrokes. The trigger fires every N captured atoms.
  local ok = atom_stream.add_listener(name, function(_, decision)
    if decision ~= "record" then return end
    count = count + 1
    if count < every then return end
    count = 0
    local now_ns = vim.uv.hrtime()
    if last_fire_hrtime > 0 and (now_ns - last_fire_hrtime) < cooldown_ns then
      return
    end
    -- Defer so `on_fire` can safely touch Neovim API.
    vim.schedule(util.entry(function()
      if disarmed then return end
      local counted = on_fire()
      if counted then
        last_fire_hrtime = vim.uv.hrtime()
      end
    end))
  end)
  if not ok then return nil end

  return function()
    if disarmed then return end
    disarmed = true
    atom_stream.remove_listener(name)
  end
end

return M
