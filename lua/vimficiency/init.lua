local config = require("vimficiency.config")
local atoms = require("vimficiency.capture.atoms")
local auto_suggest = require("vimficiency.capture.auto_suggest")
local commands = require("vimficiency.commands")
local recall_capture = require("vimficiency.capture.recall")
local ffi_config = require("vimficiency.ffi.config")

local M = {}

M.config = config

--- Named augroup for every autocmd the plugin installs.
--- Clearing on create keeps `setup()` idempotent.
M.AUGROUP = "Vimficiency"

-- Invocation roots: every `:Vimfy` and keymap invocation enters through one of these.

--- The only error boundary for `:Vimfy`. Everything below raises.
local function cmdline_entry(opts)
  local ok, err = pcall(commands.handle, opts.args)
  if not ok then
    vim.notify(tostring(err), vim.log.levels.ERROR)
  end
end


-- Binding keys: no plugin API. CmdAtom reports the resolved command, so a key
-- bound to a `:Vimfy` action produces an inert excmd atom and is excluded from
-- recordings on its own — users bind directly with
-- `vim.keymap.set('n', lhs, '<Cmd>Vimfy start a<CR>', { silent = true })` or a
-- `<Plug>Vimfy*` map. The `<Plug>` maps below are registered the same way.

-- Lifecycle: setup, teardown, and dev reload.

function M.setup(user_config)
  user_config = user_config or {}
  config.reset()

  -- Stash config so reload can re-run `setup()` with the same values.
  _G.__vimficiency_last_user_config = user_config

  vim.api.nvim_create_augroup(M.AUGROUP, { clear = true })

  local plugin_dir = debug.getinfo(1, "S").source:sub(2):match("(.*)/lua/")
  vim.cmd.helptags(plugin_dir .. "/doc")

  if not user_config.shiftwidth then
    -- vim.fn.shiftwidth() resolves the "0 means follow tabstop" case to the
    -- effective width; vim.o.shiftwidth can be 0, which the C++ side can't model.
    user_config.shiftwidth = vim.fn.shiftwidth()
  end

  local lua_consumed = config.apply(user_config)
  local cpp_consumed = ffi_config.configure(user_config)

  local unknown = {}
  for key in pairs(user_config) do
    if not lua_consumed[key] and not cpp_consumed[key] then
      table.insert(unknown, tostring(key))
    end
  end
  if #unknown > 0 then
    table.sort(unknown)
    vim.notify(
      "vimfy: unknown config keys ignored: " .. table.concat(unknown, ", "),
      vim.log.levels.WARN
    )
  end

  for _, name in ipairs({ "Vimfy", "Vimficiency" }) do
    vim.api.nvim_create_user_command(name, cmdline_entry, {
      nargs = "*",
      complete = commands.complete,
      desc = "Vimfy movement optimizer",
    })
  end

  local function register_plug(name, spec)
    vim.keymap.set("n", "<Plug>Vimfy" .. name, "<Cmd>Vimfy " .. spec .. "<CR>",
      { silent = true, desc = "Vimfy " .. spec })
  end
  for _, alias in ipairs({ "a", "b", "c", "d", "e" }) do
    local upper = alias:upper()
    register_plug("Start" .. upper, "start " .. alias)
    register_plug("Watch" .. upper, "watch " .. alias)
    register_plug("Finish" .. upper, "finish " .. alias)
    register_plug("Close" .. upper, "close " .. alias)
    register_plug("Play" .. upper, "play " .. alias)
  end
  register_plug("SuggestOn", "suggest on")
  register_plug("SuggestOff", "suggest off")
  register_plug("SuggestToggle", "suggest toggle")
  register_plug("List", "list")
  register_plug("Config", "config")
  register_plug("Help", "help")

  if not atoms.available() then
    vim.schedule(function()
      vim.notify(
        "vimfy: this Neovim lacks the CmdAtom event (needs 0.13-dev / nightly). " ..
        "Commands and views work, but session capture (mark/watch/recall/suggest) " ..
        "is disabled until you upgrade.",
        vim.log.levels.WARN)
    end)
  end

  recall_capture.install()

  if config.auto_suggest and auto_suggest.is_configured() then
    auto_suggest.enable()
  end
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

  pcall(auto_suggest.disable)

  -- Drop active captures; finished records stay.
  local dropped_active = 0
  local ok_drop, n = pcall(session_store.teardown_active)
  if ok_drop then dropped_active = n or 0 end

  -- Tear down the CmdAtom capture and every subscriber last.
  pcall(atoms.shutdown)

  pcall(vim.api.nvim_create_augroup, M.AUGROUP, { clear = true })

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
  for _ in pairs(dump.records) do preserved_records = preserved_records + 1 end

  if new_lib_path and new_lib_path ~= "" then
    _G.__vimficiency_reload_lib_path = new_lib_path
  end
  for name in pairs(package.loaded) do
    if name == "vimficiency" or name:sub(1, 12) == "vimficiency." then
      package.loaded[name] = nil
    end
  end

  local fresh = require("vimficiency")
  fresh.setup(_G.__vimficiency_last_user_config or {})

  require("vimficiency.session.store").restore_from_dump(dump)

  return {
    dropped_active    = status.dropped_active,
    preserved_records = preserved_records,
  }
end

return M
