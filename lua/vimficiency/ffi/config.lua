local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

---@type table|nil  # the `vf.config.Config` last pushed
local applied = nil

--- `user_config`'s `vf.config.Config` fields as a request. Repeated fields are
--- sorted so the same config always serializes to the same bytes.
---@return table request
---@return table<string, true> consumed  the keys that are `vf.config.Config` fields
local function build_request(user_config)
  local consumed = {}
  local request = {}
  for key, value in pairs(user_config) do
    if core.has_field("vf.config.Config", key) then
      consumed[key] = true
      request[key] = value
    end
  end

  if user_config.keys then
    request.keys = {}
    for key_index, info in pairs(user_config.keys) do
      local key = vim.tbl_extend("error", { key = key_index }, info)
      table.insert(request.keys, key)
    end
    table.sort(request.keys, function(a, b) return a.key < b.key end)
  end

  if user_config.count_penalty_overrides then
    request.count_penalty_overrides = {}
    local classes = vim.tbl_keys(user_config.count_penalty_overrides)
    table.sort(classes, function(a, b) return tostring(a) < tostring(b) end)
    for _, class in ipairs(classes) do
      local class_field = type(class) == "number" and "class_index" or "class_name"
      table.insert(request.count_penalty_overrides, vim.tbl_extend("error",
        { [class_field] = class }, user_config.count_penalty_overrides[class]))
    end
  end
  return request, consumed
end

--- Push the process-global C++ state: the keyboard cost model (layout, per-key
--- costs, effort weights, count penalties) and the shiftwidth fallback for older requests.
--- Everything else in `user_config` is Lua-owned and passed per call or read lazily.
---@return table<string, true> consumed  the keys that are `vf.config.Config` fields
function M.apply_globals(user_config)
  local request, consumed = build_request(user_config)
  core.call(lib.vf_apply_config, "vf.config.Config", request, "vf.Empty")
  applied = request
  return consumed
end

--- The `vf.config.Config` last pushed, for a process that has to score the way
--- this one does; empty before any `apply_globals`.
---@return table
function M.applied_globals()
  return applied or {}
end

function M.reset_globals()
  lib.vf_reset_config()
  applied = nil
end

return M
