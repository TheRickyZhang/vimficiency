local config = require("vimficiency.config")
local atom_stream = require("vimficiency.capture.atom_stream")
local auto_suggest = require("vimficiency.capture.auto_suggest")
local subcommands = require("vimficiency.subcommands")
local recall_capture = require("vimficiency.capture.recall")
local ffi_config = require("vimficiency.ffi.config")
local util = require("vimficiency.util")

local M = {}

M.config = config

--- Named augroup for every autocmd the plugin installs.
--- Clearing on create keeps `setup()` idempotent.
M.AUGROUP = "Vimficiency"

-- Invocation roots: every `:Vimfy` and keymap invocation enters through one of these.

--- The only error boundary for `:Vimfy`. Everything below raises.
local function cmdline_entry(opts)
  local args = vim.split(opts.args, "%s+", { trimempty = true })
  local subcmd = table.remove(args, 1)

  --- No subcommand called
  if not subcmd then
    local cmd_lines = {}
    for _, cmd in pairs(subcommands) do
      table.insert(cmd_lines, string.format("  Vimfy %-20s %s", cmd.usage, cmd.desc))
    end
    table.sort(cmd_lines)
    util.show_output("Vimfy Commands", table.concat(cmd_lines, "\n"), {
      ui_keys = {
        title = "Vimfy Scratch Output Keys",
        docs = true,
      },
    })
    return
  end

  -- Otherwise, dispatch to subcommand handlers (cmd.run)
  ---@type VF.Subcommand?
  local cmd = subcommands[subcmd]
  if not cmd then
    vim.notify("Unknown subcommand '" .. subcmd .. "'. Run :Vimfy help for usage", vim.log.levels.ERROR)
    return
  end
  if #args < (cmd.min_args or 0) then
    vim.notify("Usage: Vimfy " .. cmd.usage, vim.log.levels.ERROR)
    return
  end

  local ok, err = pcall(cmd.run, args)
  if not ok then
    vim.notify(tostring(err), vim.log.levels.ERROR)
  end
end

--- `:Vimfy` tab completion: subcommand names first, then each subcommand's
--- per-position candidates.
local function autocomplete_command(arg_lead, cmd_line, cursor_pos)
  -- Escape pattern magic up front: arg_lead is used only as a `^`-anchored
  -- match prefix below, and the user can type `(`, `[`, `%`, etc.
  local prefix = "^" .. vim.pesc(arg_lead)
  local function matching(candidates)
    local matches = vim.tbl_filter(function(v) return v:find(prefix) ~= nil end, candidates)
    table.sort(matches)
    return matches
  end

  -- Not trimmed: a trailing "" is the (empty) argument being completed, and
  -- `#args` tells the subcommand which argument position that is.
  local args = vim.split(cmd_line:sub(1, cursor_pos), "%s+")
  table.remove(args, 1)

  if #args <= 1 then
    return matching(vim.tbl_keys(subcommands))
  end

  local cmd = subcommands[args[1]]
  local candidates = cmd and cmd.complete and cmd.complete[#args - 1]
  if not candidates then return {} end
  if type(candidates) == "function" then candidates = candidates() end
  return matching(candidates)
end


-- Lifecycle: setup, teardown, and dev reload.

--- Plugin entry point
--- called from user's config (`require("vimficiency").setup{...}`) and by `:Vimfy reload`.
--- In order:
--- * apply config to Lua and C++,
--- * register the `:Vimfy` command
--- * setup capture mechanism
---@param user_config? table  Keys from `config.lua` and `ffi/config.lua`; unknown keys warn
function M.setup(user_config)
  -- Require CmdAtom for our capture semantics to work
  if not atom_stream.supported() then
    error("vimfy: this Neovim lacks the CmdAtom event; session capture needs 0.13-dev / nightly", 0)
  end
  user_config = user_config or {}
  config.reset()

  -- Stash config so reload can re-run `setup()` with the same values.
  _G.__vimficiency_last_user_config = user_config

  vim.api.nvim_create_augroup(M.AUGROUP, { clear = true })

  local plugin_dir = debug.getinfo(1, "S").source:sub(2):match("(.*)/lua/")
  vim.cmd.helptags(plugin_dir .. "/doc")

  if not user_config.shiftwidth then
    -- Handle 'shiftwidth=0' means "follow tabstop" -> use real width
    user_config.shiftwidth = vim.fn.shiftwidth()
  end

  -- Lua is for runtime concerns
  local lua_consumed = config.apply(user_config)
  -- C++ is for per-process globals (keyboard cost model, Vim options), pushed once here.
  local cpp_consumed = ffi_config.apply_globals(user_config)

  local unknown = {}
  for key in pairs(user_config) do
    if not lua_consumed[key] and not cpp_consumed[key] then
      table.insert(unknown, tostring(key))
    end
  end
  -- Warn about unknown keys
  if #unknown > 0 then
    table.sort(unknown)
    vim.notify(
      "vimfy: unknown config keys ignored: " .. table.concat(unknown, ", "),
      vim.log.levels.WARN
    )
  end

  -- aliases of Vimfy and Vimficiency will work
  for _, name in ipairs({ "Vimfy", "Vimficiency" }) do
    vim.api.nvim_create_user_command(name, cmdline_entry, {
      nargs = "*",
      complete = autocomplete_command,
      desc = "Vimfy movement optimizer",
    })
  end

  -- Capture: recall always listens; auto-suggest only if the user configured a trigger.
  recall_capture.start_continuous_capture()

  auto_suggest.enable_configured()
end

--- Tear down runtime state that should not outlive a Lua reload.
---@return { dropped_active: integer, sim_closed: boolean }
function M.shutdown()
  local simulate = require("vimficiency.simulate")
  local session_store = require("vimficiency.session.store")

  -- Close any open replay tab.
  local sim_closed = false
  local ok = pcall(simulate.cleanup_compare)
  if ok then sim_closed = true end

  auto_suggest.disable()

  -- Drop active captures; finished records stay.
  local dropped_active = 0
  local ok_drop, n = pcall(session_store.teardown_active)
  if ok_drop then dropped_active = n or 0 end

  -- Tear down the CmdAtom capture and every subscriber last.
  atom_stream.shutdown()

  vim.api.nvim_create_augroup(M.AUGROUP, { clear = true })

  return { dropped_active = dropped_active, sim_closed = sim_closed }
end

--- Reload every `vimficiency.*` Lua module, then re-run `setup()`.
--- Finished session records survive; active captures do not.
---@param new_lib_path string|nil  Path to a freshly built .so; nil = keep
---                                the currently cached library.
---@return { dropped_active: integer, preserved_records: integer }
function M.reload_lua(new_lib_path)
  local session_store = require("vimficiency.session.store")

  -- Snapshot state before teardown.
  local status = M.shutdown()
  local dump = session_store.dump_for_reload()
  local preserved_records = 0
  for _ in pairs(dump.finished) do preserved_records = preserved_records + 1 end

  if new_lib_path and new_lib_path ~= "" then
    _G.__vimficiency_reload_lib_path = new_lib_path
  end
  for name in pairs(package.loaded) do
    if name == "vimficiency" or name:sub(1, 12) == "vimficiency." then
      package.loaded[name] = nil
    end
  end

  local fresh = require("vimficiency")
  fresh.setup(_G.__vimficiency_last_user_config)

  require("vimficiency.session.store").restore_from_dump(dump)

  return {
    dropped_active    = status.dropped_active,
    preserved_records = preserved_records,
  }
end

return M
