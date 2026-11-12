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
local util = require("vimficiency.util")

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
local function aliases_and_filenames()
  return vim.list_extend(aliases(), disk.list())
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
  play = {
    desc = "Play your sequence side-by-side with the optimizer's suggestions",
    usage = "play <alias>|<filename> [windows]",
    min_args = 1,
    complete = { aliases_and_filenames },
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

  view = {
    desc = "View a finished result full-screen",
    usage = "view [<alias>|<filename>]",
    complete = { aliases_and_filenames },
    run = function(args) archive.view(args[1]) end,
  },

  explore = {
    desc = "Open the interactive explore scratch view for a finished result",
    usage = "explore <alias>|<filename>",
    min_args = 1,
    complete = { aliases_and_filenames },
    run = function(args)
      local alias_or_filename = args[1]
      explore.open(alias_or_filename, archive.resolve_result(alias_or_filename))
    end,
  },


  -- Persist and browse results.
  save = {
    desc = "Copy a finished session result to disk (keeps the session copy)",
    usage = "save <alias> [<filename>]",
    min_args = 1,
    complete = { aliases },
    run = function(args) archive.save(args[1], args[2]) end,
  },

  store = {
    desc = "Move a finished session from memory to disk (removes the session copy)",
    usage = "store <alias> [<filename>]",
    min_args = 1,
    complete = { aliases },
    run = function(args) archive.store(args[1], args[2]) end,
  },

  fetch = {
    desc = "Copy a saved result from disk into the current session",
    usage = "fetch <filename> [<alias>]",
    min_args = 1,
    complete = { disk.list },
    run = function(args) archive.fetch(args[1], args[2] or args[1]) end,
  },

  rm = {
    desc = "Delete a saved result from disk",
    usage = "rm <filename>",
    min_args = 1,
    complete = { disk.list },
    run = function(args) archive.rm(args[1]) end,
  },

  list = {
    desc = "Open the interactive session picker",
    usage = "list",
    run = function() require("vimficiency.session.picker").open() end,
  },

  stats = {
    desc = "Show lifetime session stats (efficiency, motions, beats)",
    usage = "stats",
    run = function() require("vimficiency.stats").open() end,
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
  reload = {
    desc = "Rebuild the C++ library",
    usage = "reload",
    run = dev_reload.run,
  },

  config = {
    desc = "Show current configuration",
    usage = "config",
    run = function() util.show_output("Vimfy Config", ffi_core.debug_config()) end,
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
