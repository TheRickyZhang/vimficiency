-- Finished results by handle. An alias (any kind: named, recall window, `@`,
-- `$`) resolves through the in-memory store; a filename names a result on
-- disk, a pin before a saved session. `view`/`explore`/`pin` take either and
-- try the store first.

local alias_mod = require("vimficiency.session.alias")
local disk = require("vimficiency.session.disk")
local session_store = require("vimficiency.session.store")

---@class (exact) VF.SessionArchive
--- Resolution
---@field resolve_result   fun(alias_or_filename: string): VF.Session.Result
--- Disk
---@field pin              fun(alias_or_filename: string, filename: string|nil)
---@field unpin            fun(filename: string)
---@field rm               fun(filename: string)
--- Workspace only
---@field rename_active    fun(old_alias: string, new_alias: string)
---@field duplicate_active fun(src_alias: string, dst_alias: string)
local M = {}

---@param filename string
local function validate_filename(filename)
  if not alias_mod.is_valid_filename(filename) then
    error("Invalid filename '" .. tostring(filename) .. "'. " ..
      "Allowed: alphanumeric, '.', '_', '-'; must start with a letter, digit, or underscore.", 0)
  end
end

---@param alias string
---@return string
local function still_active_message(alias)
  return "Session '" .. alias .. "' is still active. Finish it first."
end

---@param alias_or_filename string
---@return string
local function missing_message(alias_or_filename)
  local kind = alias_mod.parse(alias_or_filename)
  if kind == "last_suggest" then
    return "No regression captured yet. vimfy records one when it next " ..
      "suggests a faster sequence."
  end
  if kind == "last_finished" then
    return "No recently finished session. Run ':Vimfy finish <alias>' first."
  end
  if session_store.get_active(alias_or_filename) then
    return still_active_message(alias_or_filename)
  end
  return "No finished session, pin or saved session is named '" .. alias_or_filename .. "'."
end

--- The store first, then the pins, then the saved sessions.
---@param alias_or_filename string
---@return VF.Session.Result
function M.resolve_result(alias_or_filename)
  local result = session_store.get_result(alias_or_filename)
  if result then return result end

  if alias_mod.is_valid_filename(alias_or_filename) then
    for _, shelf in ipairs({ "pinned", "sessions" }) do
      local data, load_err, is_missing = disk.load(shelf, alias_or_filename)
      if data then return data end
      if not is_missing then error(load_err, 0) end
    end
  end

  error(missing_message(alias_or_filename), 0)
end

--- Copies a finished result to the pins. Without `filename` the pin takes the
--- filename its session was saved under.
---@param alias_or_filename string
---@param filename string|nil
function M.pin(alias_or_filename, filename)
  -- Else an active alias would fall through to a pin of the same name.
  if session_store.get_active(alias_or_filename) then
    error(still_active_message(alias_or_filename), 0)
  end
  local result = M.resolve_result(alias_or_filename)
  if not filename then
    if session_store.has_result(alias_or_filename) then
      filename = session_store.get_saved_filename(alias_or_filename)
      if not filename then
        error("'" .. alias_or_filename .. "' was not saved, so it has no filename of its own. " ..
          "Give one: `:Vimfy pin " .. alias_or_filename .. " <filename>`", 0)
      end
    else
      filename = alias_or_filename
    end
  end
  validate_filename(filename)
  local path = disk.write_pin(filename, result)
  vim.notify("vimfy pinned [" .. filename .. "] → " .. vim.fn.fnamemodify(path, ":~"), vim.log.levels.INFO)
end

---@param filename string
function M.unpin(filename)
  validate_filename(filename)
  local path = disk.remove("pinned", filename)
  vim.notify("vimfy unpinned [" .. filename .. "] ← " .. path, vim.log.levels.INFO)
end

--- Deletes a saved session's result. Its stats row stays.
---@param filename string
function M.rm(filename)
  validate_filename(filename)
  local path = disk.remove("sessions", filename)
  vim.notify("vimfy removed [" .. filename .. "] ← " .. path, vim.log.levels.INFO)
end

---@param old_alias string
---@param new_alias string
function M.rename_active(old_alias, new_alias)
  if not alias_mod.is_named(old_alias) then
    error("source alias must be a named alias (letters only)", 0)
  end
  session_store.rename_alias(old_alias, new_alias)
end

---@param src_alias string
---@param dst_alias string
function M.duplicate_active(src_alias, dst_alias)
  if not alias_mod.is_named(dst_alias) then
    error("target alias must be a named alias (letters only)", 0)
  end
  local result = session_store.get_result(src_alias)
  if not result then error(missing_message(src_alias), 0) end
  session_store.add_finished(dst_alias, result, session_store.get_saved_filename(src_alias))
end

return M
