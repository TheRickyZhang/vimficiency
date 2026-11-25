---@class (exact) VF.Settings.Appearance
---@field open fun()
local M = {}

local appearance = require("vimficiency.ui.appearance")
local config = require("vimficiency.config")
local highlights = require("vimficiency.ui.highlights")
local modal = require("vimficiency.settings.modal")
local validate = require("vimficiency.config.validate")
local util = require("vimficiency.util")
local v = vim.api

local ns = v.nvim_create_namespace("vimfy_appearance")
local SAMPLE = "  Selected item    "

---@type VF.SettingsWidget.Handle?
local active

local function setup_path()
  local directory = vim.fn.stdpath("config")
  local plugin_file = directory .. "/lua/plugins/vimficiency.lua"
  local path = config.setup_path or (vim.fn.filereadable(plugin_file) == 1 and plugin_file or directory)
  return vim.fn.fnamemodify(path, ":~")
end

function M.open()
  if active and active.is_open() then active.close() end
  local prefs = appearance.current()
  ---@type VF.SettingsWidget.Handle?
  local handle
  local color_row = { kind = "action", label = "" }

  local function refresh()
    prefs = appearance.current()
    color_row.label = "Selection color   [" .. prefs.selection_color .. "]"
    highlights.refresh_selection()
  end

  color_row.run = function()
    vim.ui.input({ prompt = "Selection color (name, hex, IncSearch, or colorscheme) ", default = prefs.selection_color },
      util.entry(function(input)
        if not input or not handle or not handle.is_open() then return end
        input = vim.trim(input)
        if not validate.is_selection_color(input) then
          vim.notify("Use a color name like yellow, a #RRGGBB color, IncSearch, or colorscheme", vim.log.levels.WARN)
          return
        end
        appearance.save({ selection_color = input })
        refresh()
        handle.refresh()
      end))
  end

  refresh()
  local examples = "  e.g. yellow, LightBlue, tomato, #FFD166, IncSearch, colorscheme"
  local rows = {
    color_row,
    { kind = "hint", text = examples },
    { kind = "setting", label = "Selection strength", value_kind = "int", min = 0, max = 100, step = 5,
      get = function() return prefs.selection_strength end,
      set = function(value)
        appearance.save({ selection_strength = value })
        refresh()
      end },
  }
  local sample_row = #rows + 3
  vim.list_extend(rows, {
    { kind = "separator" },
    { kind = "hint", text = "Selection preview" },
    { kind = "hint", text = SAMPLE .. "   Unselected item" },
    { kind = "separator" },
  })
  local reset_row = #rows + 1
  local reset_label = "Reset to defaults"
  local config_hint = "All settings are in your Vimficiency setup at"
  local config_path = setup_path()
  vim.list_extend(rows, {
    { kind = "action", label = reset_label, run = function()
      appearance.reset()
      refresh()
    end },
    { kind = "separator" },
    { kind = "hint", text = config_hint },
    { kind = "hint", text = config_path },
  })

  handle = modal.open(rows, nil, {
    title = "Vimfy Appearance",
    on_render = function(buf, line_for_row, row_prefix)
      v.nvim_buf_clear_namespace(buf, ns, 0, -1)
      ---@type { [1]: integer, [2]: string, [3]: string }[]
      local marks = {
        { 2, examples, "Comment" },
        { sample_row, SAMPLE, highlights.NAV_CURRENT },
        { reset_row, reset_label, "Special" },
        { reset_row + 2, config_hint, "Comment" },
        { reset_row + 3, config_path, "Comment" },
      }
      for _, mark in ipairs(marks) do
        v.nvim_buf_set_extmark(buf, ns, line_for_row[mark[1]] - 1, #row_prefix, {
          end_col = #row_prefix + #mark[2], hl_group = mark[3],
        })
      end
    end,
  })
  active = handle
end

return M
