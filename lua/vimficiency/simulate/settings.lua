-- View-screen UI preferences.
--
-- Mirror of the explore settings pattern:
--   - hardcoded defaults in config/init.lua's `fields.view`
--   - user setup declarations merged over by `config.apply`
--   - sidecar file auto-layered on top, mutated on every toggle
--
-- The only things local to a view session are:
--   1. include_user_sequence: whether the user's typed sequence is
--      shown as the leftmost pane.
--   2. window_count: total side-by-side panes (1..4). If
--      include_user_sequence is true, the user pane consumes one slot
--      and the remaining slots surface the top-ranked optimal_results.
--
-- Settings modal opened via `gs` from
-- the replay grid. Unlike explore, view itself has no persistent
-- single-buffer controller; the settings still live outside the replay
-- state machine.
local config = require("vimficiency.config")
local settings_profile = require("vimficiency.settings.profile")
local settings_ui = require("vimficiency.settings.modal")

local M = {}

-- Session-scoped in-memory store. Seeded lazily from
-- `config.view` + sidecar; mutated by the settings modal.
---@type table|nil
local current_settings

local VIEW_SCHEMA_VERSION = 1

local function settings_store()
  if current_settings == nil then
    current_settings = vim.deepcopy(config.view or {})
    local envelope = settings_profile.load("view")
    if envelope.version == VIEW_SCHEMA_VERSION then
      for k, v in pairs(envelope.data) do
        if current_settings[k] ~= nil or config.view[k] == nil then
          current_settings[k] = v
        end
      end
    end
  end
  return current_settings
end

local function update_setting(key, value)
  local store = settings_store()
  store[key] = value
  settings_profile.save("view", VIEW_SCHEMA_VERSION, store)
end

---Public accessor — `simulate/pool.lua` reads these on every replay to
---decide whether to include the user sequence and how many optimal
---results to surface.
---@return table  copy of the current settings
function M.get_settings()
  return vim.deepcopy(settings_store())
end

---Public mutator so the replay grid's direct keymaps (+/-, u) can
---toggle settings without routing through the modal. Writes through
---to the same sidecar the modal uses.
---@param key string
---@param value any
function M.set_setting(key, value)
  update_setting(key, value)
end

---Build the settings schema and open the shared widget. Returns the
---widget handle so callers can implement `gs` toggle (close if the
---modal is already open).
---@param opts? { on_change?: fun(), on_close?: fun() }
---@return VF.SettingsWidget.Handle?
function M.open_settings(opts)
  opts = opts or {}
  -- Grid is a single row of up to 4 panes. When the user pane is
  -- included it consumes one of the slots — with window_count = 1 and
  -- include_user_sequence = true, only the user pane is displayed.
  local WINDOW_MIN, WINDOW_MAX = 1, 4

  local schema = {
    { kind = "setting",
      label = "Show user pane",
      value_kind = "bool",
      get = function() return settings_store().include_user_sequence end,
      set = function(v) update_setting("include_user_sequence", v) end },
    { kind = "setting",
      label = "Window count",
      value_kind = "int", min = WINDOW_MIN, max = WINDOW_MAX,
      get = function() return settings_store().window_count end,
      set = function(v) update_setting("window_count", v) end },
    { kind = "separator" },
    { kind = "action",
      label = "reset to default settings",
      run = function()
        settings_profile.clear("view")
        current_settings = nil
        vim.notify("vimfy view: settings reset to defaults",
          vim.log.levels.INFO)
      end },
  }
  return settings_ui.open(schema, opts.on_change, {
    title = "View Settings",
    on_close = opts.on_close,
  })
end

return M
