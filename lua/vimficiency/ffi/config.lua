local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

--- Push the process-global C++ state: the keyboard cost model (layout, per-key
--- costs, effort weights, count penalties) plus the Vim options the simulator needs
--- (shiftwidth). Applied once per `setup()`; every search reads it from C++ globals.
--- Everything else in `user_config` is Lua-owned and passed per call or read lazily.
---@return table<string, true> consumed  the keys that are `vf.config.Config` fields
function M.apply_globals(user_config)
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
  end

  if user_config.count_penalty_overrides then
    request.count_penalty_overrides = {}
    for class, override in pairs(user_config.count_penalty_overrides) do
      local class_field = type(class) == "number" and "class_index" or "class_name"
      table.insert(request.count_penalty_overrides,
        vim.tbl_extend("error", { [class_field] = class }, override))
    end
  end

  core.call(lib.vf_apply_config, "vf.config.Config", request, "vf.Empty")
  return consumed
end

function M.reset_globals()
  lib.vf_reset_config()
end

return M
