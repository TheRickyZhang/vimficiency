local handlers   = require("vimficiency.explore.input.handlers")
local activation = require("vimficiency.explore.runtime.activation")
local ffi_explore = require("vimficiency.ffi.explore")
local layout_mod = require("vimficiency.explore.runtime.layout")
local lifecycle  = require("vimficiency.explore.runtime.lifecycle")
local poller     = require("vimficiency.async.poller")
local result_mod = require("vimficiency.explore.runtime.result")
local settings   = require("vimficiency.explore.settings")
local state_mod  = require("vimficiency.explore.state")
local timing     = require("vimficiency.explore.timing")
local view_model = require("vimficiency.explore.view")
local util       = require("vimficiency.util")

local M = {}
local v = vim.api

local function render_computing(list_buf)
  vim.bo[list_buf].modifiable = true
  v.nvim_buf_set_lines(list_buf, 0, -1, false, { "", "  Computing optimal sequences…", "" })
  vim.bo[list_buf].modifiable = false
end

local function install_teardown(view, layout)
  v.nvim_create_autocmd("WinClosed", {
    group = view.augroup,
    pattern = { tostring(layout.scratch_win), tostring(layout.list_win) },
    callback = util.entry(function()
      lifecycle.destroy(view)
    end),
    desc = "vimfy explore: tear down view when a primary pane closes",
  })
end

---@param label string
---@param result VF.Session.Result
---@param header_handlers table<string, function>
---@return VF.Explore.Active
function M.start(label, result, header_handlers)
  local source_buf = v.nvim_get_current_buf()
  local source_win = v.nvim_get_current_win()

  local store = settings.settings_store()
  local request = result_mod.search_request(result, source_win, state_mod.resolve_overrides())

  local layout = layout_mod.build(label, result.lines, source_buf, source_win)
  local view = view_model.create(label, result, nil, layout, header_handlers)

  view.augroup = v.nvim_create_augroup("VimficiencyExplore" .. layout.scratch_tab, { clear = true })
  require("vimficiency.explore.runtime.registry").set(view)

  local job_id
  local ok, err = pcall(function()
    install_teardown(view, layout)
    render_computing(layout.list_buf)
    local job = ffi_explore.explore_start_async(request, store.view.compute_deadline_ms)
    job_id = job
    local t0 = vim.uv.hrtime()
    view.poll_handle = poller.start(
      function() return ffi_explore.explore_poll(job) end,
      function() ffi_explore.explore_cancel(job) end,
      function(view_id)
        view.view_id = view_id
        timing.note("composition search", (vim.uv.hrtime() - t0) / 1e6)
        activation.attach(view, layout, handlers.capture_key, handlers.install)
        state_mod.refresh_ui(view)
      end,
      function() lifecycle.destroy(view) end)
  end)
  if not ok then
    if job_id then ffi_explore.explore_cancel(job_id) end
    lifecycle.destroy(view)
    error(err, 0)
  end

  return view
end

return M
