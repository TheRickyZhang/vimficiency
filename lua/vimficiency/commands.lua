local util = require("vimficiency.util")
local subcommands = require("vimficiency.commands.defs")

local M = {}

---@param subcmd string
---@param args string[]  Whitespace-split, no empty entries
function M.dispatch_subcommand(subcmd, args)
  local cmd = subcommands[subcmd]
  if not cmd then
    error("Unknown subcommand '" .. subcmd .. "'. Run :Vimfy help for usage", 0)
  end
  if #args < (cmd.min_args or 0) then
    error("Usage: Vimfy " .. cmd.usage, 0)
  end
  cmd.fn(args)
end

-- `:Vimfy` entry point: bare `:Vimfy` lists subcommands, otherwise dispatches.
function M.handle(arg_string)
  local args = vim.split(arg_string or "", "%s+", { trimempty = true })
  local subcmd = table.remove(args, 1)

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

  M.dispatch_subcommand(subcmd, args)
end

function M.complete(arg_lead, cmd_line, cursor_pos)
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
  if not cmd or not cmd.complete then return {} end
  return matching(cmd.complete(args))
end

return M
