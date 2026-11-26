local ffi_core = require("vimficiency.ffi.core")
local config = require("vimficiency.config")
local session = require("vimficiency.session")
local archive = require("vimficiency.session.archive")
local disk = require("vimficiency.session.disk")
local playback = require("vimficiency.session.playback")
local session_store = require("vimficiency.session.store")
local auto_suggest = require("vimficiency.capture.auto_suggest")
local alias_mod = require("vimficiency.session.alias")
local explore = require("vimficiency.explore")
local dev_reload = require("vimficiency.dev_reload")
local ui_windows = require("vimficiency.ui.windows")

---@class VF.Subcommand
---@field desc string
---@field usage string
---@field min_args? integer  Dispatch raises "Usage: Vimfy <usage>" when fewer are given
---@field complete? (string[]|fun(): string[])[]  Candidates indexed by argument position; prefix filtering is generic
---@field run fun(args: string[])

-- Table of all non-root vimficiency commands, grouped by workflow stage. Order
-- is for the reader only; the bare `:Vimfy` listing sorts alphabetically.
-- Tab-completion candidates, by the handle grammar each command accepts.
local function named_aliases()
  return vim.tbl_filter(alias_mod.is_named, session_store.list_aliases())
end
--- Live aliases plus recall time hints: everything `close` accepts.
local function closeable()
  return vim.list_extend(session_store.list_aliases(), alias_mod.TIME_HINTS)
end
--- Every alias kind.
local function aliases()
  return vim.list_extend(closeable(), alias_mod.SPECIAL)
end
local function pins()
  return disk.list("pinned")
end
--- Saved sessions are left out: there is one per finished session.
local function aliases_and_pins()
  return vim.list_extend(aliases(), pins())
end

---@type table<string, VF.Subcommand>
return {
  -- Capture a session.
  start = {
    desc = "Start a mark session (finish it yourself)",
    usage = "start <alias>",
    min_args = 1,
    complete = { named_aliases },
    run = function(args) session.start(args[1]) end,
  },

  finish = {
    desc = "Finish a mark or watch session and show results",
    usage = "finish <alias>",
    min_args = 1,
    complete = { named_aliases },
    run = function(args) session.finish(args[1]) end,
  },


  close = {
    desc = "Close a session without finishing",
    usage = "close <alias>",
    min_args = 1,
    complete = { closeable },
    run = function(args) session.close(args[1]) end,
  },

  watch = {
    desc = "Start a watch session (auto end on idle)",
    usage = "watch <alias>",
    min_args = 1,
    complete = { named_aliases },
    run = function(args) session.watch(args[1]) end,
  },

  recall = {
    desc = "Finish a retrospective recall window (N commands ago or Ns)",
    usage = "recall <N|Ns>",
    min_args = 1,
    complete = { alias_mod.TIME_HINTS },
    run = function(args) session.recall(args[1]) end,
  },


  -- View a finished result.
  view = {
    desc = "Replay your sequence side-by-side with the optimizer's suggestions",
    usage = "view <alias>|<filename> [windows]",
    min_args = 1,
    complete = { aliases_and_pins },
    run = function(args) playback.simulate(args[1], tonumber(args[2])) end,
  },

  focus = {
    desc = "Focus replay on the Nth buffer",
    usage = "focus <N>",
    min_args = 1,
    run = function(args)
      local n = tonumber(args[1])
      if not n or n < 1 or n ~= math.floor(n) then
        error("Usage: Vimfy focus <N>  (1-indexed integer)", 0)
      end
      require("vimficiency.simulate").focus(n)
    end,
  },

  escape = {
    desc = "Restore the side-by-side replay layout",
    usage = "escape",
    run = function() require("vimficiency.simulate").escape() end,
  },

  explore = {
    desc = "Open the interactive explore scratch view for a finished result",
    usage = "explore <alias>|<filename>",
    min_args = 1,
    complete = { aliases_and_pins },
    run = function(args)
      local alias_or_filename = args[1]
      explore.open(alias_or_filename, archive.resolve_result(alias_or_filename))
    end,
  },


  -- Keep and browse results.
  pin = {
    desc = "Keep a copy of a finished result under a filename (default: the one it was saved under)",
    usage = "pin <alias>|<filename> [<filename>]",
    min_args = 1,
    complete = { aliases_and_pins },
    run = function(args) archive.pin(args[1], args[2]) end,
  },

  unpin = {
    desc = "Delete a pin",
    usage = "unpin <filename>",
    min_args = 1,
    complete = { pins },
    run = function(args) archive.unpin(args[1]) end,
  },

  rm = {
    desc = "Delete a saved session's result (its stats row stays)",
    usage = "rm <filename>",
    min_args = 1,
    complete = { function() return disk.list("sessions") end },
    run = function(args) archive.rm(args[1]) end,
  },

  list = {
    desc = "Open the interactive session picker",
    usage = "list",
    run = function() require("vimficiency.session.picker").open() end,
  },

  stats = {
    desc = "Open the stats tab (sessions, journal, commands; list any row's runs); `refresh` only scores the journal",
    usage = "stats [refresh]",
    complete = { { "refresh" } },
    run = function(args)
      if args[1] == nil then
        require("vimficiency.stats.view").open()
      elseif args[1] == "refresh" then
        require("vimficiency.stats.sampling").refresh(function(err)
          if err then
            vim.notify("vimfy: could not score the journal: " .. err, vim.log.levels.ERROR)
          else
            vim.notify("vimfy: journal scored")
          end
        end)
      else
        error("Usage: Vimfy stats [refresh]", 0)
      end
    end,
  },


  -- Auto-suggest.
  suggest = {
    desc = "Control auto-suggest (cost-based suggestions)",
    usage = "suggest <on|off|toggle>",
    min_args = 1,
    complete = { { "on", "off", "toggle" } },
    run = function(args)
      local action = args[1]
      if action ~= "on" and action ~= "off" and action ~= "toggle" then
        error("Usage: Vimfy suggest <on|off|toggle>", 0)
      end
      local turn_on = action == "on" or (action == "toggle" and not auto_suggest.is_enabled())
      if not turn_on then
        auto_suggest.disable()
        vim.notify("vimfy auto-suggest disabled", vim.log.levels.INFO)
      elseif config.auto_suggest == false then
        error("auto-suggest is turned off in setup{} (auto_suggest = false). Remove that to enable.", 0)
      elseif auto_suggest.enable() then
        vim.notify("vimfy auto-suggest enabled", vim.log.levels.INFO)
      else
        vim.notify("vimfy auto-suggest already enabled", vim.log.levels.WARN)
      end
    end,
  },


  -- Plugin-related
  appearance = {
    desc = "Customize selection highlighting",
    usage = "appearance",
    run = function() require("vimficiency.settings.appearance").open() end,
  },

  reload = {
    desc = "Rebuild and hot-reload Vimfy with live build output",
    usage = "reload",
    run = dev_reload.run,
  },

  config = {
    desc = "Show current configuration",
    usage = "config",
    run = function() ui_windows.show_output("Vimfy Config", ffi_core.debug_config()) end,
  },

  version = {
    desc = "Show the plugin version",
    usage = "version",
    run = function()
      vim.notify("vimficiency " .. ffi_core.version_string(), vim.log.levels.INFO)
    end,
  },

  help = {
    desc = "Show help",
    usage = "help",
    run = function() vim.cmd("help vimficiency") end,
  },
}
