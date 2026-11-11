-- Single source of truth for Vimfy alias grammar.
-- Valid forms: manual (`^%a+$`), recall_key (`^%d+$`), recall_time (`^%d+s$`).

local M = {}

---@alias VF.Session.AliasType "manual" | "recall_key" | "recall_time"

--- Common `Ns` windows surfaced by tab completion.
M.TIME_HINTS = { "3s", "5s", "10s", "30s" }

--- Parse an alias.
--- Returns `(type, value)` for valid aliases, else `(nil, nil)`.
---@param s any
---@return VF.Session.AliasType|nil type
---@return integer|nil value
function M.parse(s)
  if type(s) ~= "string" or s == "" then return nil, nil end

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
    return "manual", nil
  end

  return nil, nil
end

--- The two shapes an alias can take: a user-chosen `name` or a recall `window`.
--- Distinct from `VF.Session.AliasType` (which splits windows by unit) and from
--- the "manual" of StartKind/FinishReason (which means user-initiated).
---@alias VF.Session.AliasForm "name"|"window"

---@type table<VF.Session.AliasType, VF.Session.AliasForm>
local ALIAS_FORM_OF = { manual = "name", recall_key = "window", recall_time = "window" }

local FORM_NAME = { name = "a manual alias", window = "a recall window" }
local FORM_HINT = {
  name = "letters only, e.g. 'a'",
  window = "N commands ago or Ns seconds ago",
}

--- Parse `s`, requiring it to have the expected form. Raises a user-facing error
--- otherwise: a format hint for malformed input, or which form `s` actually has
--- when it is well-formed but the wrong one.
---@param s string
---@param expected_alias_form VF.Session.AliasForm
---@return VF.Session.AliasType kind
---@return integer|nil value
function M.expect(s, expected_alias_form)
  local kind, value = M.parse(s)
  if not kind then
    error("Invalid alias '" .. tostring(s) .. "'. Expected " .. FORM_NAME[expected_alias_form] ..
      " (" .. FORM_HINT[expected_alias_form] .. ").", 0)
  end
  local form = ALIAS_FORM_OF[kind]
  if form ~= expected_alias_form then
    error("'" .. s .. "' is " .. FORM_NAME[form] .. ", not " .. FORM_NAME[expected_alias_form] .. ".", 0)
  end
  return kind, value
end

--- True iff `s` is a valid manual alias.
---@param s any
---@return boolean
function M.is_valid_manual(s)
  return M.parse(s) == "manual"
end

--- True iff `s` is any valid recall alias (key or time form).
---@param s any
---@return boolean
function M.is_recall(s)
  return ALIAS_FORM_OF[M.parse(s)] == "window"
end

--- True iff `s` is a special workspace selector. `@` = most-recent finished
--- session (any kind); `$` = most-recent suggest regression. These are
--- resolved by the store (`resolve_special`), not by `M.parse`, whose
--- grammar only covers manual/recall aliases.
---@param s any
---@return boolean
function M.is_special_selector(s)
  return s == "@" or s == "$"
end

--- True iff `s` is a safe saved-result name.
--- Grammar: `^[%w_][%w._-]*$`.
---@param s any
---@return boolean
function M.is_valid_saved_name(s)
  return type(s) == "string" and s:match("^[%w_][%w._-]*$") ~= nil
end

return M
