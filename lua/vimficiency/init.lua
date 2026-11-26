local config = require("vimficiency.config")
local atom_stream = require("vimficiency.capture.atom_stream")
local auto_suggest = require("vimficiency.capture.auto_suggest")
local subcommands = require("vimficiency.subcommands")
local recall_capture = require("vimficiency.capture.recall")
local journal = require("vimficiency.capture.journal")
local sampling = require("vimficiency.stats.sampling")
local ffi_config = require("vimficiency.ffi.config")
local ui_windows = require("vimficiency.ui.windows")

local M = {}

M.config = config

--- Named augroup for every autocmd the plugin installs.
--- Clearing on create keeps `setup()` idempotent.
M.AUGROUP = "Vimficiency"

-- Invocation roots: every `:Vimfy` and keymap invocation enters through one of these.

local terms = {
  { "<session>", "The start and end states captured for the optimizer. Vimfy looks up a finished session by alias first, then by filename on disk (pins before saved sessions)." },
  { "<alias>", "A name you give a session in this Neovim, like a or work. @ is the last finished session and $ is the latest suggestion. 5 and 3s look back five command atoms or three seconds." },
  { "<filename>", "The name of a saved or pinned result on disk. Omit .json." },
  { "command atom", "One complete user action, defined by Neovim. w, dw, and itext<Esc> each count as one. Press Ctrl-] on |CmdAtom| for Neovim help." },
}

local function render_terms(buf, win, ns, first_row)
  local win_width = vim.api.nvim_win_get_width(win)
  local rows = {}
  for _, term in ipairs(terms) do
    local prefix = ("%-12s - "):format(term[1])
    local line = ""
    for word in term[2]:gmatch("%S+") do
      if line ~= "" and #prefix + #line + 1 + #word > win_width then
        rows[#rows + 1] = prefix .. line
        prefix = string.rep(" ", #prefix)
        line = ""
      end
      line = line == "" and word or line .. " " .. word
    end
    rows[#rows + 1] = prefix .. line
  end
  local block_width = 0
  for _, row in ipairs(rows) do block_width = math.max(block_width, #row) end
  local indent = string.rep(" ", math.max(0, math.floor((win_width - block_width) / 2)))
  for i, row in ipairs(rows) do rows[i] = indent .. row end
  local title_col = math.max(0, math.floor((win_width - #"Terms") / 2))
  table.insert(rows, 1, string.rep(" ", title_col) .. "Terms")

  vim.api.nvim_buf_clear_namespace(buf, ns, first_row, -1)
  vim.bo[buf].modifiable = true
  vim.api.nvim_buf_set_lines(buf, first_row, -1, false, rows)
  vim.bo[buf].modifiable = false
  vim.api.nvim_buf_set_extmark(buf, ns, first_row, title_col, { end_col = title_col + #"Terms", hl_group = "Title" })
  for i, row in ipairs(rows) do
    local first, last = row:find("|CmdAtom|", 1, true)
    if first then
      vim.api.nvim_buf_set_extmark(buf, ns, first_row + i - 1, first - 1, { end_col = last, hl_group = "Underlined" })
    end
  end
end

local function show_commands()
  local commands = subcommands.available()
  local widths = {}
  for _, cmd in ipairs(commands) do
    local width = widths[cmd.group] or { name = 0, args = 0 }
    width.name = math.max(width.name, #cmd.name)
    width.args = math.max(width.args, #cmd.args)
    widths[cmd.group] = width
  end

  local lines = {}
  local marks = {}
  local group
  for _, cmd in ipairs(commands) do
    if cmd.group ~= group then
      if group then lines[#lines + 1] = "" end
      group = cmd.group
      marks[#marks + 1] = { #lines, 0, #group, "Title" }
      lines[#lines + 1] = group
    end
    local width = widths[cmd.group]
    local usage = cmd.name .. string.rep(" ", width.name - #cmd.name)
    if width.args > 0 then
      usage = usage .. " " .. cmd.args .. string.rep(" ", width.args - #cmd.args)
    end
    marks[#marks + 1] = { #lines, 2, 2 + #cmd.name, "Identifier" }
    lines[#lines + 1] = "  " .. usage .. "  " .. cmd.desc
  end
  lines[#lines + 1] = ""
  local terms_row = #lines
  local buf, win = ui_windows.show_output("Vimfy Commands", table.concat(lines, "\n"), {
    margin = 2,
    ui_keys = { title = "Vimfy Scratch Output Keys", docs = true },
    window = { wrap = false },
  })
  local ns = vim.api.nvim_create_namespace("vimfy_commands")
  vim.keymap.set("n", "<C-]>", function()
    return vim.fn.expand("<cword>") == "CmdAtom" and "<Cmd>help CmdAtom<CR>" or "<C-]>"
  end, { buffer = buf, expr = true })
  for _, mark in ipairs(marks) do
    vim.api.nvim_buf_set_extmark(buf, ns, mark[1], mark[2], { end_col = mark[3], hl_group = mark[4] })
  end
  local function center_terms()
    render_terms(buf, win, ns, terms_row)
  end
  center_terms()
  vim.api.nvim_create_autocmd("VimResized", { buffer = buf, callback = center_terms })
end

--- The only error boundary for `:Vimfy`. Everything below raises.
local function cmdline_entry(opts)
  local args = vim.split(opts.args, "%s+", { trimempty = true })
  local subcmd = table.remove(args, 1)

  --- No subcommand called
  if not subcmd then
    show_commands()
    return
  end

  -- Otherwise, dispatch to subcommand handlers (cmd.run)
  local cmd = subcommands.find(subcmd)
  if not cmd then
    vim.notify("Unknown subcommand '" .. subcmd .. "'. Run :Vimfy help for usage", vim.log.levels.ERROR)
    return
  end
  if #args < (cmd.min_args or 0) then
    vim.notify("Usage: Vimfy " .. cmd.name .. " " .. cmd.args, vim.log.levels.ERROR)
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
    return matching(vim.tbl_map(function(cmd) return cmd.name end, subcommands.available()))
  end

  local cmd = subcommands.find(args[1])
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
---@param user_config? table  Keys from `config/init.lua` and `ffi/config.lua`; unknown keys warn
function M.setup(user_config)
  -- Require CmdAtom for our capture semantics to work
  if not atom_stream.supported() then
    error("vimfy: this Neovim lacks the CmdAtom event; session capture needs 0.13-dev / nightly", 0)
  end
  user_config = user_config or {}
  -- A repeated setup() must not keep triggers armed by the previous config.
  auto_suggest.disable()
  journal.stop()
  sampling.stop_background()
  config.reset()

  -- Stash config so reload can re-run `setup()` with the same values.
  _G.__vimficiency_last_user_config = user_config

  vim.api.nvim_create_augroup(M.AUGROUP, { clear = true })

  local source = debug.getinfo(1, "S").source
  local caller = debug.getinfo(2, "S")
  if caller and caller.source ~= source and caller.source:sub(1, 1) == "@" then
    local path = vim.fn.fnamemodify(caller.source:sub(2), ":p")
    if vim.fn.filereadable(path) == 1 then config.setup_path = path end
  end
  local plugin_dir = source:sub(2):match("(.*)/lua/")
  vim.cmd.helptags(plugin_dir .. "/doc")

  -- Lua is for runtime concerns
  local lua_consumed = config.apply(user_config)
  require("vimficiency.ui.highlights").refresh_selection()
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
  if config.journal.enabled then journal.start() end
  -- Journals from when it was on still get scored.
  sampling.start_background()

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
  require("vimficiency.explore.runtime.lifecycle").shutdown()

  auto_suggest.disable()

  -- Drop active captures; finished records stay.
  local dropped_active = 0
  local ok_drop, n = pcall(session_store.teardown_active)
  if ok_drop then dropped_active = n or 0 end

  journal.stop()
  sampling.stop_background()

  -- Tear down the CmdAtom capture and every subscriber last.
  atom_stream.shutdown()
  require("vimficiency.capture.buffer_history").shutdown()

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
  fresh.config.setup_path = config.setup_path
  fresh.setup(_G.__vimficiency_last_user_config)

  require("vimficiency.session.store").restore_from_dump(dump)

  return {
    dropped_active    = status.dropped_active,
    preserved_records = preserved_records,
  }
end

return M
