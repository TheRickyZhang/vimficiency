-- Shared Vimfy highlight groups.
--
-- This module is the single canonical place for highlight names + styles
-- used across Vimfy UIs. Keep it narrow: semantic groups only (what
-- the UI is trying to say), not literal colors. Actual colors either link
-- to built-in groups (so they respect the user's colorscheme) or are
-- computed in UI-local helpers that layer on top (see explore/highlights.lua
-- for the alpha-blended rank palette).
--
-- Public semantic groups are installed with `default = true`, so users can
-- override them with their own `:highlight` commands without us clobbering
-- them on the next refresh. Internal masking groups may update directly
-- when they need to track the active colorscheme.
--
-- Callers:
--   - require this module at module scope to pick up the names.
--   - call `M.refresh()` before first use if colorscheme may have changed
--     since Vimfy loaded. (The module calls it once at require time
--     as a convenience.)

local v = vim.api
local appearance = require("vimficiency.ui.appearance")
local validate = require("vimficiency.config.validate")

local M = {}

-- =============================================================================
-- Names — semantic groups exposed to callers
-- =============================================================================

-- Picker (session picker float)
M.PICKER_PANE_ACTIVE   = "VimficiencyPickerPaneActive"   -- currently-selected pane label
M.PICKER_PANE_INACTIVE = "VimficiencyPickerPaneInactive" -- the other pane label
M.PICKER_HEADER_DIM    = "VimficiencyPickerHeaderDim"    -- labels like "pane:" / "sort:"
M.PICKER_SECTION       = "VimficiencyPickerSection"      -- "── Category ──" dividers

-- Simulate / replay (side-by-side sequence playback)
M.REPLAY_CURRENT       = "VimficiencyReplayCurrent"       -- token at the current replay step
M.REPLAY_CURSOR        = "VimficiencyReplayCursor"        -- cursor cell in normal mode
M.REPLAY_CURSOR_INSERT = "VimficiencyReplayCursorInsert"  -- cursor cell in insert mode
M.REPLAY_CURSOR_VISUAL = "VimficiencyReplayCursorVisual"  -- cursor / visual selection
M.REPLAY_ACTIVE        = "VimficiencyReplayActive"        -- active window's label

-- Run preview (side-by-side initial/final in the stats run list)
M.VIEW_DIFF_DELETE  = "VimficiencyViewDiffDelete"  -- changed columns on the initial side
M.VIEW_DIFF_ADD     = "VimficiencyViewDiffAdd"     -- changed columns on the final side
M.VIEW_CURSOR_START = "VimficiencyViewCursorStart" -- start cursor cell (initial side)
M.VIEW_CURSOR_END   = "VimficiencyViewCursorEnd"   -- end cursor cell (final side)

-- Stats view (`:Vimfy stats`)
M.STATS_SECTION_TITLE = "VimficiencyStatsSectionTitle" -- overview section titles
M.STATS_DRILL         = "VimficiencyStatsDrill"        -- overview rows <CR> narrows to
M.STATS_EFFICIENCY    = "VimficiencyStatsEfficiency"   -- a line chart's scores
M.STATS_VOLUME        = "VimficiencyStatsVolume"       -- a line chart's sample counts
M.STATS_RULE          = "VimficiencyStatsRule"         -- the line between the overview's bands
M.STATS_MOVEMENT      = "VimficiencyStatsMovement"     -- a breakdown bar's movement-only runs
M.STATS_EDIT          = "VimficiencyStatsEdit"         -- a breakdown bar's runs with edits
M.STATS_NOT_SCORED    = "VimficiencyStatsNotScored"    -- rows of runs left out of the score
M.STATS_UNBEATEN      = "VimficiencyStatsUnbeaten"     -- rows of unbeaten runs

-- Item navigation (`ui/item_nav.lua`)
M.NAV_CURRENT = "VimficiencyNavCurrent" -- the item the keys act on

-- Shared settings modal
M.SETTINGS_ACTION_ACTIVE = "VimficiencySettingsActionActive" -- selected action row
M.SETTINGS_CURSOR_HIDDEN = "VimficiencySettingsCursorHidden" -- selected row only; no cursor cell

local function blend(base, tint, strength)
  local color = 0
  for shift = 16, 0, -8 do
    local normal = bit.band(bit.rshift(base, shift), 0xff)
    local selected = bit.band(bit.rshift(tint, shift), 0xff)
    local channel = math.floor((normal * (100 - strength) + selected * strength) / 100)
    color = color + bit.lshift(channel, shift)
  end
  return color
end

local function selection_style(name, style, legacy)
  local generated = name .. "Default"
  v.nvim_set_hl(0, generated, style)
  local current = v.nvim_get_hl(0, { name = name, create = false })
  v.nvim_set_hl(0, name, { link = generated, default = not vim.deep_equal(current, legacy) })
end

function M.refresh_selection()
  local prefs = appearance.current()
  local normal = v.nvim_get_hl(0, { name = "Normal", link = false })
  local visual = v.nvim_get_hl(0, { name = "Visual", link = false })
  local dark = vim.o.background == "dark"
  local base = normal.bg or (dark and 0x000000 or 0xffffff)
  local color = prefs.selection_color
  local group = validate.selection_color_group(color) == "IncSearch"
    and v.nvim_get_hl(0, { name = "IncSearch", link = false }) or visual
  local background = group.reverse and (group.fg or normal.fg) or group.bg
  local tint = validate.selection_color_rgb(color) or background or visual.bg or normal.fg
    or (dark and 0xffffff or 0x000000)
  local strength = prefs.selection_strength
  selection_style(M.NAV_CURRENT, { bg = blend(base, tint, strength) },
    visual.bg and { bg = blend(base, visual.bg, 50) } or { link = "Visual" })
end

-- =============================================================================
-- Install
-- =============================================================================

---(Re-)install every highlight group this module owns. Safe to call on every
---UI open so a colorscheme change between loads is picked up. `default = true`
---means user overrides survive.
function M.refresh()
  v.nvim_set_hl(0, M.PICKER_PANE_ACTIVE,   { link = "PmenuSel",   default = true })
  v.nvim_set_hl(0, M.PICKER_PANE_INACTIVE, { link = "Comment",    default = true })
  v.nvim_set_hl(0, M.PICKER_HEADER_DIM,    { link = "NonText",    default = true })
  v.nvim_set_hl(0, M.PICKER_SECTION,       { link = "Title",      default = true })

  v.nvim_set_hl(0, M.REPLAY_CURRENT,       { link = "IncSearch",  default = true })
  v.nvim_set_hl(0, M.REPLAY_CURSOR,        { link = "Search",     default = true })
  v.nvim_set_hl(0, M.REPLAY_CURSOR_INSERT, { link = "DiffAdd",    default = true })
  v.nvim_set_hl(0, M.REPLAY_CURSOR_VISUAL, { link = "Visual",     default = true })
  v.nvim_set_hl(0, M.REPLAY_ACTIVE,        { link = "Title",      default = true })

  v.nvim_set_hl(0, M.VIEW_DIFF_DELETE,  { link = "DiffDelete", default = true })
  v.nvim_set_hl(0, M.VIEW_DIFF_ADD,     { link = "DiffAdd",    default = true })
  v.nvim_set_hl(0, M.VIEW_CURSOR_START, { link = "Search",     default = true })
  v.nvim_set_hl(0, M.VIEW_CURSOR_END,   { link = "IncSearch",  default = true })

  v.nvim_set_hl(0, M.STATS_SECTION_TITLE, { link = "Title",      default = true })
  v.nvim_set_hl(0, M.STATS_DRILL,         { link = "Identifier", default = true })
  v.nvim_set_hl(0, M.STATS_EFFICIENCY,    { link = "String",     default = true })
  v.nvim_set_hl(0, M.STATS_VOLUME,        { link = "Comment",    default = true })
  v.nvim_set_hl(0, M.STATS_RULE,          { link = "NonText",    default = true })
  v.nvim_set_hl(0, M.STATS_MOVEMENT,      { link = "Function",   default = true })
  v.nvim_set_hl(0, M.STATS_EDIT,          { link = "Type",       default = true })
  v.nvim_set_hl(0, M.STATS_NOT_SCORED,    { link = "DiagnosticError", default = true })
  v.nvim_set_hl(0, M.STATS_UNBEATEN,      { link = "DiagnosticOk",    default = true })

  M.refresh_selection()

  v.nvim_set_hl(0, M.SETTINGS_ACTION_ACTIVE, { bold = true, default = true })
  -- `blend = 100` makes the cursor cell fully transparent. Combined with the
  -- guicursor swap installed by `ui.windows.hide_cursor_while_active`, the terminal
  -- cursor disappears in modern TUIs; in older ones the cursor remains the
  -- terminal default but the row-selection extmark still indicates state.
  v.nvim_set_hl(0, M.SETTINGS_CURSOR_HIDDEN, { blend = 100, default = true })
end

M.refresh()
v.nvim_create_autocmd("ColorScheme", {
  group = v.nvim_create_augroup("VimfySelectionColors", { clear = true }),
  callback = M.refresh_selection,
})

return M
