-- Single source of truth for the alias grammar. Every user-typed handle is one
-- of two things: an alias, which the store resolves to a session id, or a
-- filename, which names a saved result on disk (`is_valid_filename`).
--
-- Alias kinds: name (`^%a+$`), recall_key (`^%d+$`), recall_time (`^%d+s$`),
-- last_finished (`@`), last_suggest (`$`).

local M = {}

---@alias VF.Session.AliasKind "name"|"recall_key"|"recall_time"|"last_finished"|"last_suggest"

--- Common `Ns` windows surfaced by tab completion.
M.TIME_HINTS = { "3s", "5s", "10s", "30s" }
M.SPECIAL = { "@", "$" }

--- Kind sets commands accept (see `expect`).
M.RECALL_KINDS = { "recall_key", "recall_time" }
M.ALL_KINDS = { "name", "recall_key", "recall_time", "last_finished", "last_suggest" }

--- Parse an alias. Returns `(kind, value)` for valid aliases, else `(nil, nil)`;
--- `value` is the numeric window for recall kinds.
---@param s any
---@return VF.Session.AliasKind|nil kind
---@return integer|nil value
function M.parse(s)
  if type(s) ~= "string" or s == "" then return nil, nil end

  if s == "@" then return "last_finished", nil end
  if s == "$" then return "last_suggest", nil end

  local secs = s:match("^(%d+)s$")
  if secs then
    local n = tonumber(secs)
    if n and n > 0 then return "recall_time", n end
    return nil, nil
  end

  local keys = s:match("^(%d+)$")
  if keys then
    local n = tonumber(keys)
    if n and n > 0 then return "recall_key", n end
    return nil, nil
  end

  if s:match("^%a+$") then
    return "name", nil
  end

  return nil, nil
end

local KIND_DESC = {
  name          = "a session name (letters only, e.g. 'a')",
  recall_key    = "a recall window (N commands ago, e.g. '5')",
  recall_time   = "a recall window (Ns seconds ago, e.g. '3s')",
  last_finished = "'@' (the last finished session)",
  last_suggest  = "'$' (the last suggest regression)",
}

--- Parse `s`, requiring one of `kinds`. Raises a user-facing error otherwise:
--- the accepted forms for malformed input, or which kind `s` actually is when
--- it is well-formed but not accepted.
---@param s string
---@param kinds VF.Session.AliasKind[]
---@return VF.Session.AliasKind kind
---@return integer|nil value
function M.expect(s, kinds)
  local kind, value = M.parse(s)
  local accepted = {}
  for _, k in ipairs(kinds) do accepted[#accepted + 1] = KIND_DESC[k] end
  local expected = "Expected " .. table.concat(accepted, " or ") .. "."
  if not kind then
    error("Invalid alias '" .. tostring(s) .. "'. " .. expected, 0)
  end
  if not vim.tbl_contains(kinds, kind) then
    error("'" .. s .. "' is " .. KIND_DESC[kind] .. ". " .. expected, 0)
  end
  return kind, value
end

--- True iff `s` is a session-name alias.
---@param s any
---@return boolean
function M.is_name(s)
  return M.parse(s) == "name"
end

--- True iff `s` is a recall-window alias (key or time form).
---@param s any
---@return boolean
function M.is_recall(s)
  local kind = M.parse(s)
  return kind == "recall_key" or kind == "recall_time"
end

--- True iff `s` is a safe saved-result filename (without the `.json` suffix).
--- Grammar: `^[%w_][%w._-]*$`. Every name and recall alias is also a valid
--- filename, which is what lets `save <alias>` default the filename to the alias.
---@param s any
---@return boolean
function M.is_valid_filename(s)
  return type(s) == "string" and s:match("^[%w_][%w._-]*$") ~= nil
end

return M
