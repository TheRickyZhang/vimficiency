local ffi_core = require("vimficiency.ffi.core")
local config = require("vimficiency.config")
local session = require("vimficiency.session")
local archive = require("vimficiency.session.archive")
local disk = require("vimficiency.session.disk")
local playback = require("vimficiency.session.playback")
local simulate = require("vimficiency.simulate")
local session_store = require("vimficiency.session.store")
local auto_suggest = require("vimficiency.capture.auto_suggest")
local alias_mod = require("vimficiency.session.alias")
local explore = require("vimficiency.explore")
local dev_reload = require("vimficiency.dev_reload")
local ui_windows = require("vimficiency.ui.windows")

---@alias VF.SubcommandGroup "Named sessions"|"Recent edits"|"Inspect"|"Keep and browse"|"Plugin"|"View controls"

---@class VF.Subcommand
---@field name string
---@field group VF.SubcommandGroup  Heading a bare `:Vimfy` lists it under
---@field desc string
---@field args string  Argument syntax shown in the command list and usage messages
---@field min_args? integer  Dispatch raises "Usage: Vimfy <usage>" when fewer are given
---@field complete? (string[]|fun(): string[])[]  Candidates indexed by argument position; prefix filtering is generic
---@field run fun(args: string[])

---@class (exact) VF.Subcommands
---@field available fun(): VF.Subcommand[]
---@field find fun(name: string): VF.Subcommand?
local M = {}

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

---@type VF.Subcommand[]
local global_commands = {
  {
    name = "start",
    group = "Named sessions",
    desc = "Start a session",
    args = "<alias>",
    min_args = 1,
    complete = { named_aliases },
    run = function(args) session.start(args[1]) end,
  },

  {
    name = "watch",
    group = "Named sessions",
    desc = "Start a session that finishes on idle",
    args = "<alias>",
    min_args = 1,
    complete = { named_aliases },
    run = function(args) session.watch(args[1]) end,
  },

  {
    name = "finish",
    group = "Named sessions",
    desc = "Finish now and show results",
    args = "<alias>",
    min_args = 1,
    complete = { named_aliases },
    run = function(args) session.finish(args[1]) end,
  },

  {
    name = "close",
    group = "Named sessions",
    desc = "Discard an unfinished session",
    args = "<alias>",
    min_args = 1,
    complete = { closeable },
    run = function(args) session.close(args[1]) end,
  },

  {
    name = "suggest",
    group = "Recent edits",
    desc = "Control automatic suggestions",
    args = "<on|off|toggle>",
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

  {
    name = "recall",
    group = "Recent edits",
    desc = "Look back N recorded command atoms or N seconds",
    args = "<N|Ns>",
    min_args = 1,
    complete = { alias_mod.TIME_HINTS },
    run = function(args) session.recall(args[1]) end,
  },

  {
    name = "view",
    group = "Inspect",
    desc = "Replay your keys beside the suggestions",
    args = "<session> [windows]",
    min_args = 1,
    complete = { aliases_and_pins },
    run = function(args) playback.simulate(args[1], tonumber(args[2])) end,
  },

  {
    name = "explore",
    group = "Inspect",
    desc = "Explore a finished session step by step",
    args = "<session>",
    min_args = 1,
    complete = { aliases_and_pins },
    run = function(args)
      local alias_or_filename = args[1]
      explore.open(alias_or_filename, archive.resolve_result(alias_or_filename))
    end,
  },

  {
    name = "list",
    group = "Keep and browse",
    desc = "Open the session picker",
    args = "",
    run = function() require("vimficiency.session.picker").open() end,
  },

  {
    name = "stats",
    group = "Keep and browse",
    desc = "Open stats, or refresh journal scores",
    args = "[refresh]",
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

  {
    name = "pin",
    group = "Keep and browse",
    desc = "Keep a copy (defaults to its saved filename)",
    args = "<session> [filename]",
    min_args = 1,
    complete = { aliases_and_pins },
    run = function(args) archive.pin(args[1], args[2]) end,
  },

  {
    name = "unpin",
    group = "Keep and browse",
    desc = "Delete a pin",
    args = "<filename>",
    min_args = 1,
    complete = { pins },
    run = function(args) archive.unpin(args[1]) end,
  },

  {
    name = "rm",
    group = "Keep and browse",
    desc = "Delete the saved replay file",
    args = "<filename>",
    min_args = 1,
    complete = { function() return disk.list("sessions") end },
    run = function(args) archive.rm(args[1]) end,
  },

  {
    name = "appearance",
    group = "Plugin",
    desc = "Customize selection highlighting",
    args = "",
    run = function() require("vimficiency.settings.appearance").open() end,
  },

  {
    name = "reload",
    group = "Plugin",
    desc = "Rebuild and hot-reload Vimfy with live build output",
    args = "",
    run = dev_reload.run,
  },

  {
    name = "config",
    group = "Plugin",
    desc = "Show current configuration",
    args = "",
    run = function() ui_windows.show_output("Vimfy Config", ffi_core.debug_config()) end,
  },

  {
    name = "version",
    group = "Plugin",
    desc = "Show the plugin version",
    args = "",
    run = function()
      vim.notify("vimficiency " .. ffi_core.version_string(), vim.log.levels.INFO)
    end,
  },

  {
    name = "help",
    group = "Plugin",
    desc = "Show help",
    args = "",
    run = function() vim.cmd("help vimficiency") end,
  },
}

---@type VF.Subcommand[]
local view_commands = {
  {
    name = "focus",
    group = "View controls",
    desc = "Expand the Nth replay pane",
    args = "<N>",
    min_args = 1,
    run = function(args)
      local n = tonumber(args[1])
      if not n or n < 1 or n ~= math.floor(n) then
        error("Usage: Vimfy focus <N>  (1-indexed integer)", 0)
      end
      simulate.focus(n)
    end,
  },
  {
    name = "escape",
    group = "View controls",
    desc = "Restore the side-by-side replay layout",
    args = "",
    run = simulate.escape,
  },
}
vim.list_extend(view_commands, global_commands)

function M.available()
  return simulate.current_pane() and view_commands or global_commands
end

function M.find(name)
  for _, cmd in ipairs(M.available()) do
    if cmd.name == name then return cmd end
  end
end

return M
