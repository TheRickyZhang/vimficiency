local ffi_core = require("vimficiency.ffi.core")
local util = require("vimficiency.util")
local windows = require("vimficiency.ui.windows")
local v = vim.api

---@class (exact) VF.DevReload
---@field run fun()
local M = {}

local build_buf
local in_progress = false

local BUILDING_TITLE = " Vimfy reload · Building… "
local BUILDING_FOOTER = " q hides this window · the build keeps running in the background "
local FAILED_TITLE = " Vimfy reload · Failed "
local FAILED_FOOTER = " q closes this window "

local function show_build(buf)
  local win = vim.fn.bufwinid(buf)
  if win ~= -1 then return win end

  local width, height, row, col = windows.centered_popup_geometry(
    math.floor(vim.o.columns * 0.8), math.floor(vim.o.lines * 0.6))
  win = v.nvim_open_win(buf, true, {
    relative = "editor",
    row = row,
    col = col,
    width = width,
    height = height,
    style = "minimal",
    border = "rounded",
    title = BUILDING_TITLE,
    title_pos = "center",
    footer = BUILDING_FOOTER,
    footer_pos = "center",
  })
  windows.configure_scratch_window(win)
  v.nvim_win_set_cursor(win, { v.nvim_buf_line_count(buf), 0 })
  return win
end

local function finish_build(buf, message, level)
  in_progress = false
  if v.nvim_buf_is_valid(buf) then
    if level == vim.log.levels.INFO then
      v.nvim_buf_delete(buf, { force = true })
    else
      local wins = vim.fn.win_findbuf(buf)
      if #wins == 0 then
        message = message .. "\nBuild log: :sbuffer " .. buf
      end
      for _, win in ipairs(wins) do
        v.nvim_win_set_config(win, {
          title = FAILED_TITLE,
          title_pos = "center",
          footer = FAILED_FOOTER,
          footer_pos = "center",
        })
      end
    end
  end
  vim.notify(message, level, { title = "Vimfy" })
end

local function reload_library(buf, build_dir)
  -- dlopen caches by inode. Copy the library to load a fresh image.
  local src = build_dir .. "/" .. ffi_core.lib_filename
  local unique = build_dir .. "/libvimficiency-reload-"
    .. string.format("%.0f", vim.uv.hrtime())
    .. ffi_core.lib_filename:match("%.[^.]+$")
  local copied, copy_err = vim.uv.fs_copyfile(src, unique)
  if not copied then
    finish_build(buf, "Vimfy rebuilt, but copying the library for reload failed: "
      .. tostring(copy_err) .. " — restart Neovim.", vim.log.levels.WARN)
    return
  end

  local ok, result = pcall(function()
    return require("vimficiency").reload_lua(unique)
  end)
  -- The loaded image remains mapped after the copy is unlinked.
  vim.uv.fs_unlink(unique)

  if not ok then
    finish_build(buf, "Vimfy rebuilt, but Lua reload failed: "
      .. tostring(result) .. " — restart Neovim.", vim.log.levels.ERROR)
    return
  end

  finish_build(buf, string.format(
    "Rebuild + Lua reload complete.\nPreserved finished records: %d\nDropped active captures: %d",
    result.preserved_records, result.dropped_active), vim.log.levels.INFO)
end

--- Rebuild the C++ library in a live build float, then hot-reload Vimfy.
function M.run()
  if in_progress then
    if v.nvim_buf_is_valid(build_buf) then show_build(build_buf) end
    return
  end

  -- Prebuilt installs have a library but no CMake cache.
  local build_dir = util.find_plugin_root() .. "/build"
  if vim.fn.filereadable(build_dir .. "/CMakeCache.txt") == 0 then
    vim.notify("vimfy reload: no CMake build tree at " .. build_dir ..
      "\n(installed from a prebuilt; run `./scripts/install.sh` or `:Lazy build vimficiency` to rebuild from source)",
      vim.log.levels.ERROR)
    return
  end

  if build_buf and v.nvim_buf_is_valid(build_buf) then
    v.nvim_buf_delete(build_buf, { force = true })
  end
  local buf = v.nvim_create_buf(false, true)
  build_buf = buf
  vim.bo[buf].bufhidden = "hide"
  windows.set_buffer_keymaps(buf, {
    { lhs = "q", handler = "<cmd>hide<cr>", desc = "Hide Vimfy build log" },
    { lhs = "<Esc>", handler = "<cmd>hide<cr>", desc = "Hide Vimfy build log" },
  })
  local win = show_build(buf)

  in_progress = true
  local ok, job = pcall(v.nvim_win_call, win, function()
    return vim.fn.jobstart({ "cmake", "--build", build_dir, "-j" }, {
      term = true,
      on_exit = vim.schedule_wrap(util.entry(function(_, code)
        if code == 0 then
          reload_library(buf, build_dir)
        else
          finish_build(buf, ("Vimfy build failed (exit %d)."):format(code), vim.log.levels.ERROR)
        end
      end)),
    })
  end)

  if not ok or job <= 0 then
    finish_build(buf, "Vimfy build could not start: "
      .. (job == -1 and "cmake is unavailable." or tostring(job)), vim.log.levels.ERROR)
  elseif v.nvim_win_is_valid(win) then
    v.nvim_win_set_cursor(win, { v.nvim_buf_line_count(buf), 0 })
  end
end

return M
