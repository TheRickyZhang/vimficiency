local ffi_core = require("vimficiency.ffi.core")
local util = require("vimficiency.util")

local M = {}

local in_progress = false

local function parse_build_progress(line)
  local m = line:match("^(%[%s*%d+/%d+%])")
  if m then return m end
  m = line:match("^(%[%s*%d+%%%])")
  if m then return m end
  return nil
end

--- Rebuild the C++ library with cmake, then hot-reload it and every Lua module.
function M.run()
  if in_progress then
    vim.notify("vimfy reload already running", vim.log.levels.WARN)
    return
  end
  -- A prebuilt install has a library in `build/` but no CMake cache to
  -- rebuild from.
  local build_dir = util.find_plugin_root() .. "/build"
  if vim.fn.filereadable(build_dir .. "/CMakeCache.txt") == 0 then
    vim.notify("vimfy reload: no CMake build tree at " .. build_dir ..
      "\n(installed from a prebuilt; run `./scripts/install.sh` or `:Lazy build vimficiency` to rebuild from source)",
      vim.log.levels.ERROR)
    return
  end

  in_progress = true
  vim.notify("Rebuilding Vimfy...", vim.log.levels.INFO,
    { title = "Vimfy" })

  local output = {}
  local pending_line = ""

  local function on_stream(_, data)
    if not data or data == "" then return end
    local chunk = pending_line .. data
    local start = 1
    while true do
      local nl = chunk:find("\n", start, true)
      if not nl then break end
      local line = chunk:sub(start, nl - 1)
      if line ~= "" then output[#output + 1] = line end
      start = nl + 1
    end
    pending_line = chunk:sub(start)
  end

  local function on_exit(obj)
    in_progress = false
    if pending_line ~= "" then
      output[#output + 1] = pending_line
      pending_line = ""
    end
    vim.schedule(function()
      if obj.code == 0 then
        local progress_lines = {}
        for _, l in ipairs(output) do
          if parse_build_progress(l) then
            progress_lines[#progress_lines + 1] = l
          end
        end

        -- To pick up the freshly built C++, we need dlopen to return a
        -- new handle. Glibc's dlopen dedups by (dev, inode) — so
        -- overwriting libvimficiency.so in place, or hardlinking it,
        -- both yield cache hits. The only reliable way to defeat
        -- dedup is to present a file with a distinct inode, which
        -- means copying.
        --
        -- But the file only has to exist at the moment `ffi.load`
        -- runs: once dlopen has mmap'd the image, the code lives in
        -- memory independently of the directory entry. Unlinking
        -- immediately after a successful load keeps build/ clean and
        -- avoids accumulating `libvimficiency-reload-*.so` artifacts
        -- across sessions.
        local src = build_dir .. "/" .. ffi_core.lib_filename
        -- `%.0f` avoids scientific-notation filenames: `tostring` on a
        -- Lua number >= ~2^53 (hrtime ns reaches that after ~104 days
        -- of uptime) formats as `1.0742e+14`, an ugly filename. `%.0f`
        -- always emits decimal form.
        local unique = build_dir .. "/libvimficiency-reload-"
          .. string.format("%.0f", vim.uv.hrtime())
          .. ffi_core.lib_filename:match("%.[^.]+$")
        local copied, copy_err = vim.uv.fs_copyfile(src, unique)
        if not copied then
          vim.notify(
            "vimfy reload: rebuild succeeded but copying " ..
            ffi_core.lib_filename .. " for reload failed: " .. tostring(copy_err)
            .. " — restart Neovim to load new library.",
            vim.log.levels.WARN, { title = "Vimfy" })
          return
        end

        local reload_ok, reload_err = pcall(function()
          return require("vimficiency").reload_lua(unique)
        end)

        -- Drop the on-disk copy regardless of outcome. On success the
        -- mmap'd image survives via its inode; on failure the file is
        -- useless anyway. Done here (not inside reload_lua) so the
        -- reload API stays agnostic about how its path argument was
        -- produced.
        vim.uv.fs_unlink(unique)

        if not reload_ok then
          vim.notify(
            "vimfy reload: Lua reload crashed: " ..
            tostring(reload_err) .. " — restart Neovim.",
            vim.log.levels.ERROR, { title = "Vimfy" })
          return
        end

        local r = reload_err  -- pcall's second return on success
        local summary = string.format(
          "Rebuild + Lua reload complete.\n" ..
          "  preserved finished records: %d\n" ..
          "  dropped active captures: %d",
          (r and r.preserved_records) or 0,
          (r and r.dropped_active) or 0)
        local msg = summary
        if #progress_lines > 0 then
          msg = table.concat(progress_lines, "\n") .. "\n\n" .. summary
        end
        vim.notify(msg, vim.log.levels.INFO, { title = "Vimfy" })
      else
        local lines = { "vimfy rebuild failed (exit " .. tostring(obj.code) .. "):" }
        local start_idx = math.max(1, #output - 39)  -- last 40 lines
        for i = start_idx, #output do
          lines[#lines + 1] = "  " .. output[i]
        end
        vim.notify(table.concat(lines, "\n"), vim.log.levels.ERROR,
          { title = "Vimfy" })
      end
    end)
  end

  local ok, err = pcall(vim.system, {
    "cmake", "--build", build_dir, "-j",
  }, {
    text = true,
    stdout = on_stream,
    stderr = on_stream,
  }, on_exit)

  if not ok then
    in_progress = false
    vim.notify("vimfy reload: failed to spawn: " .. tostring(err),
      vim.log.levels.ERROR)
  end
end

return M
