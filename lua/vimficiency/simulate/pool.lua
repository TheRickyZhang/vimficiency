local max = math.max
local min = math.min

local tokenize = require("vimficiency.simulate.tokenize")
local history = require("vimficiency.capture.buffer_history")

local M = {}

---@class VF.Replay.LayoutSlot
---@field is_user boolean
---@field default_rank integer

---@alias VF.Replay.PoolRankKey "user" | integer

---@param pool_arg VF.Replay.PoolArg
---@param initial_lines string[]
---@return VF.Replay.Pool
function M.build(pool_arg, initial_lines)
  local pool = { user = nil, user_is_top = false, suggestions = {} }
  if pool_arg.user then
    local recording = pool_arg.user.recording
    local lines = initial_lines
    local states = { { lines = lines, cursor = { recording.row + 1, recording.col }, mode = recording.mode } }
    local tokens = {}
    local keys = {}
    for i, step in ipairs(recording.steps) do
      if #step.deltas > 0 then
        lines = vim.list_slice(lines)
        for _, delta in ipairs(step.deltas) do
          history.splice(lines, delta.first, delta.old_count, delta.lines)
        end
      end
      states[i + 1] = { lines = lines, cursor = { step.row + 1, step.col }, mode = step.mode }
      tokens[i] = { text = step.typed or step.keys, kind = "recorded" }
      keys[i] = step.keys
    end
    local top = pool_arg.suggestions[1]
    pool.user_is_top = top ~= nil and top.seq ~= "" and top.seq == table.concat(keys)
    pool.user = { tokens = tokens, cost = pool_arg.user.cost, states = states }
  end
  for i, sug in ipairs(pool_arg.suggestions or {}) do
    pool.suggestions[i] = {
      tokens = tokenize.for_animation(sug.seq),
      cost = sug.cost,
      states = nil,
    }
  end
  return pool
end

---@param pool VF.Replay.Pool
---@param key VF.Replay.PoolRankKey
---@return VF.Replay.PoolEntry?
function M.entry_by_rank(pool, key)
  if key == "user" then return pool.user end
  return pool.suggestions[key]
end

---@param pool VF.Replay.Pool
---@return integer window_count
---@return boolean include_user
---@return integer requested_count
function M.effective_layout(pool)
  local settings = require("vimficiency.simulate.settings").get_settings()
  local include_user = pool.user ~= nil and (settings.include_user_sequence or #pool.suggestions == 0)
  local requested_count = max(1, min(4, settings.window_count))
  local window_count = requested_count
  local suggestion_slots = window_count - (include_user and 1 or 0)
  local total_suggestions = #pool.suggestions
  if suggestion_slots > total_suggestions then
    suggestion_slots = total_suggestions
    window_count = suggestion_slots + (include_user and 1 or 0)
  end
  if window_count < 1 then window_count = 1 end
  return window_count, include_user, requested_count
end

---@param pool VF.Replay.Pool
---@return VF.Replay.LayoutSlot[]
function M.layout_plan(pool)
  local window_count, include_user = M.effective_layout(pool)
  local plan = {}
  if include_user then
    plan[#plan + 1] = { is_user = true, default_rank = 0 }
  end
  local suggestion_slots = window_count - (include_user and 1 or 0)
  for rank = 1, suggestion_slots do
    plan[#plan + 1] = { is_user = false, default_rank = rank }
  end
  return plan
end

---@param plan VF.Replay.LayoutSlot[]
---@return VF.Replay.PoolRankKey[]
function M.ranks_for_plan(plan)
  local ranks = {}
  for _, slot in ipairs(plan) do
    ranks[#ranks + 1] = slot.is_user and "user" or slot.default_rank
  end
  return ranks
end

return M
