---@class (exact) VF.Ui.Appearance
---@field current fun(): VF.Ui.Preferences
---@field save fun(overrides: { selection_color?: string, selection_strength?: integer })
---@field reset fun()
local M = {}

local config = require("vimficiency.config")
local validate = require("vimficiency.config.validate")
local profile = require("vimficiency.settings.profile")
local VERSION = 1

local function saved()
  local envelope = profile.load("ui")
  local overrides = {}
  if envelope.version == VERSION then
    local data = envelope.data
    if validate.is_selection_color(data.selection_color) then
      overrides.selection_color = data.selection_color
    end
    if validate.is_selection_strength(data.selection_strength) then
      overrides.selection_strength = data.selection_strength
    end
  end
  return overrides
end

function M.current()
  return vim.tbl_extend("force", config.ui, saved())
end

function M.save(overrides)
  profile.save("ui", VERSION, vim.tbl_extend("force", saved(), overrides))
end

function M.reset()
  profile.clear("ui")
end

return M
