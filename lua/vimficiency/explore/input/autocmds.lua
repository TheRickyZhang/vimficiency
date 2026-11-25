local util = require("vimficiency.util")

local v = vim.api

local M = {}

---@class VF.Explore.AutocmdHandlers
---@field on_cursor_moved fun()
---@field on_buffer_changed fun(event?: table)
---@field on_insert_enter fun()
---@field on_insert_text_changed fun()
---@field on_scroll fun()

--- Registers interactive handlers in the view's augroup.
---@param view VF.Explore.Active
---@param scratch_buf integer
---@param handlers VF.Explore.AutocmdHandlers
function M.install(view, scratch_buf, handlers)
  v.nvim_create_autocmd("CursorMoved", {
    group = view.augroup,
    buffer = scratch_buf,
    callback = util.entry(handlers.on_cursor_moved),
    desc = "vimfy explore: forward cursor move to view",
  })
  -- TextChangedI is deliberately NOT here: buffer state mid-typing isn't a valid target.
  v.nvim_create_autocmd({ "TextChanged", "InsertLeave" }, {
    group = view.augroup,
    buffer = scratch_buf,
    callback = util.entry(handlers.on_buffer_changed),
    desc = "vimfy explore: validate buffer state vs. planned fencepost",
  })
  v.nvim_create_autocmd("InsertEnter", {
    group = view.augroup,
    buffer = scratch_buf,
    callback = util.entry(handlers.on_insert_enter),
    desc = "vimfy explore: begin insert phase",
  })
  v.nvim_create_autocmd("TextChangedI", {
    group = view.augroup,
    buffer = scratch_buf,
    callback = util.entry(handlers.on_insert_text_changed),
    desc = "vimfy explore: live-refresh insert remaining",
  })
  -- WinScrolled / WinResized fire for any window in the tab; filter to
  -- the scratch window so the header only re-pins when relevant.
  v.nvim_create_autocmd({ "WinScrolled", "WinResized", "VimResized" }, {
    group = view.augroup,
    callback = util.entry(function()
      if not v.nvim_win_is_valid(view.scratch.win) then return end
      handlers.on_scroll()
    end),
    desc = "vimfy explore: re-pin header virt_lines on scroll/resize",
  })
end

return M
