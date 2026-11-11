local ffi_core = require("vimficiency.ffi.core")
local config = require("vimficiency.config")
local session = require("vimficiency.session")
local auto_suggest = require("vimficiency.capture.auto_suggest")
local alias_mod = require("vimficiency.session.alias")
local explore = require("vimficiency.explore")
local explore_result = require("vimficiency.explore.result")
local dev_reload = require("vimficiency.dev_reload")
local util = require("vimficiency.util")

---@class VF.Subcommand
---@field desc string
---@field usage string
---@field min_args? integer  Dispatch raises "Usage: Vimfy <usage>" when fewer are given
---@field complete? (string[]|fun(): string[])[]  Candidates indexed by argument position; prefix filtering is generic
---@field fn fun(args: string[])

---@type table<string, VF.Subcommand>
return {
  start = {
    desc = "Start a manual session",
    usage = "start <alias>",
    min_args = 1,
    complete = { session.list_manual },
    fn = function(args) session.start(args[1]) end,
  },

  watch = {
    desc = "Start a watch session (manual start, auto end on idle)",
    usage = "watch <alias>",
    min_args = 1,
    complete = { session.list_manual },
    fn = function(args) session.watch(args[1]) end,
  },

  finish = {
    desc = "Finish a manual session and show results",
    usage = "finish <alias>",
    min_args = 1,
    complete = { session.list_manual },
    fn = function(args) session.finish(args[1]) end,
  },

  recall = {
    desc = "Finish a retrospective recall window (N commands ago or Ns)",
    usage = "recall <N|Ns>",
    min_args = 1,
    complete = { alias_mod.TIME_HINTS },
    fn = function(args) session.recall(args[1]) end,
  },

  close = {
    desc = "Close a session without finishing",
    usage = "close <alias>",
    min_args = 1,
    complete = { session.list_closeable },
    fn = function(args) session.close(args[1]) end,
  },

  play = {
    desc = "Play your sequence side-by-side with the optimizer's suggestions",
    usage = "play <selector>|@|$ [windows]",
    min_args = 1,
    complete = { session.list_selectors_and_saved },
    fn = function(args) session.simulate(args[1], tonumber(args[2])) end,
  },

  focus = {
    desc = "Focus replay on the Nth buffer",
    usage = "focus <N>",
    min_args = 1,
    fn = function(args)
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
    fn = function() require("vimficiency.simulate").escape() end,
  },

  save = {
    desc = "Copy a finished session result to disk (keeps the session copy)",
    usage = "save <selector>|@|$ [<name>]",
    min_args = 1,
    complete = { session.list_selectors },
    fn = function(args) session.save(args[1], args[2]) end,
  },

  store = {
    desc = "Move a finished session from memory to disk (removes the session copy)",
    usage = "store <selector>|@|$ [<name>]",
    min_args = 1,
    complete = { session.list_selectors },
    fn = function(args) session.store(args[1], args[2]) end,
  },

  fetch = {
    desc = "Copy a saved result from disk into the current session",
    usage = "fetch <name> [<alias>]",
    min_args = 1,
    complete = { session.list_saved },
    fn = function(args) session.fetch(args[1], args[2] or args[1]) end,
  },

  view = {
    desc = "View a finished result full-screen (selector or saved name)",
    usage = "view [<selector>|@|$|<name>]",
    complete = { session.list_selectors_and_saved },
    fn = function(args) session.view(args[1]) end,
  },

  rm = {
    desc = "Delete a saved result from disk",
    usage = "rm <name>",
    min_args = 1,
    complete = { session.list_saved },
    fn = function(args) session.rm(args[1]) end,
  },

  list = {
    desc = "Open the interactive session picker",
    usage = "list",
    fn = function() require("vimficiency.session.picker").open() end,
  },

  stats = {
    desc = "Show lifetime session stats (efficiency, motions, beats)",
    usage = "stats",
    fn = function() require("vimficiency.stats").open() end,
  },

  suggest = {
    desc = "Control auto-suggest (cost-based suggestions)",
    usage = "suggest <on|off|toggle>",
    min_args = 1,
    complete = { { "on", "off", "toggle" } },
    fn = function(args)
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

  reload = {
    desc = "Rebuild the C++ library",
    usage = "reload",
    fn = dev_reload.run,
  },

  config = {
    desc = "Show current configuration",
    usage = "config",
    fn = function() util.show_output("Vimfy Config", ffi_core.debug_config()) end,
  },

  version = {
    desc = "Show the plugin version",
    usage = "version",
    fn = function()
      vim.notify("vimficiency " .. ffi_core.version_string(), vim.log.levels.INFO)
    end,
  },

  help = {
    desc = "Show help",
    usage = "help",
    fn = function() vim.cmd("help vimficiency") end,
  },

  explore = {
    desc = "Open the interactive explore scratch view for a finished result",
    usage = "explore <selector>|@|$",
    min_args = 1,
    complete = { session.list_selectors_and_saved },
    fn = function(args)
      local selector = args[1]
      local result, err = session.resolve_result(selector)
      if not result then
        error("vimfy explore failed: " .. (err or "unknown error"), 0)
      end
      local valid, validation_err = explore_result.validate(result)
      if not valid then
        error("vimfy explore failed: " .. validation_err, 0)
      end
      ---@cast result VF.Explore.OpenResult
      explore.open(selector, result)
    end,
  },
}
