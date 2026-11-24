local key_capture = require("vimficiency.explore.input.key_capture")
local keymaps     = require("vimficiency.explore.input.keymaps")
local registry    = require("vimficiency.explore.runtime.registry")

local M = {}
---@alias VF.Explore.AutocmdInstaller fun(view: VF.Explore.Active, layout: VF.Explore.Layout)

---@param view VF.Explore.Active
---@param layout VF.Explore.Layout
---@param capture_key fun(key: string|nil, typed: string|nil)
---@param install_autocmds VF.Explore.AutocmdInstaller
function M.attach(view, layout, capture_key, install_autocmds)
  key_capture.install(capture_key)
  keymaps.install(layout.scratch_buf, layout.list_buf, view.header_handlers)
  install_autocmds(view, layout)
end

---@param view VF.Explore.Active
function M.detach(view)
  registry.clear(view)
  if not registry.has_any() then key_capture.uninstall() end
end

return M
