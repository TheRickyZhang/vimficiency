---@type table<string, VF.Subcommand>
local subcommands = require("vimficiency.subcommands")

local M = {}

---@param subcmd string
---@param args string[]  Whitespace-split, no empty entries
function M.dispatch_subcommand(subcmd, args)
  ---@type VF.Subcommand?
  local cmd = subcommands[subcmd]
  if not cmd then
    error("Unknown subcommand '" .. subcmd .. "'. Run :Vimfy help for usage", 0)
  end
  if #args < (cmd.min_args or 0) then
    error("Usage: Vimfy " .. cmd.usage, 0)
  end
  cmd.fn(args)
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
  local candidates = cmd and cmd.complete and cmd.complete[#args - 1]
  if not candidates then return {} end
  if type(candidates) == "function" then candidates = candidates() end
  return matching(candidates)
end

return M
