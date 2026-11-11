-- Finished results by handle. An alias (any kind: named, recall window, `@`,
-- `$`) resolves through the in-memory store; a filename names a saved result
-- on disk. `play`/`view`/`explore` take either and try the store first.

local alias_mod = require("vimficiency.session.alias")
local disk = require("vimficiency.session.disk")
local result_window = require("vimficiency.session.result_window")
local session_store = require("vimficiency.session.store")

---@class (exact) VF.SessionArchive
--- Resolution
---@field resolve_alias    fun(alias: string): VF.Session.Result|nil, string|nil
---@field resolve_result   fun(alias_or_filename: string): VF.Session.Result|nil, string|nil
---@field default_filename fun(alias: string): string|nil
--- Workspace <-> disk
---@field save             fun(alias: string, explicit_filename: string|nil)
---@field store            fun(alias: string, explicit_filename: string|nil)
---@field fetch            fun(filename: string, alias: string)
---@field rm               fun(filename: string)
--- Workspace only
---@field rename_active    fun(old_alias: string, new_alias: string): boolean, string|nil
---@field duplicate_active fun(src_alias: string, dst_alias: string): boolean, string|nil
--- Display
---@field view             fun(alias_or_filename: string|nil)
local M = {}

---@param filename string
local function validate_filename(filename)
  if not alias_mod.is_valid_filename(filename) then
    error("Invalid filename '" .. tostring(filename) .. "'. " ..
      "Allowed: alphanumeric, '.', '_', '-'; must start with a letter, digit, or underscore.", 0)
  end
end

--- Default filename for `save`/`store` when none is given: the alias itself.
--- `@`/`$` resolve to the alias their record was finished under, except a
--- suggest result, whose recall alias (`3s`) would collide on every fire; it
--- gets a timestamped name instead.
---@param alias string
---@return string|nil
function M.default_filename(alias)
  local kind = alias_mod.parse(alias)
  if kind ~= "last_finished" and kind ~= "last_suggest" then return alias end
  local result = session_store.get_result(alias)
  if not result then return nil end
  if session_store.session_type(result.kind) == "suggest" then
    return "suggest-" .. os.date("%Y%m%d-%H%M%S", result.finished_wall_epoch)
  end
  return session_store.get_finish_alias(alias)
end

---@param alias string
---@return string
local function missing_message(alias)
  local kind = alias_mod.parse(alias)
  if kind == "last_suggest" then
    return "No regression captured yet. vimfy records one when it next " ..
      "suggests a faster sequence."
  end
  if kind == "last_finished" then
    return "No recently finished session. Run ':Vimfy finish <alias>' first."
  end
  return "No finished result for '" .. alias .. "'. Is the session still active?"
end

---@param alias string
---@return VF.Session.Result|nil result
---@return string|nil err
function M.resolve_alias(alias)
  local result = session_store.get_result(alias)
  if not result then return nil, missing_message(alias) end
  return result, nil
end

--- Workspace first, then disk.
---@param alias_or_filename string
---@return VF.Session.Result|nil result
---@return string|nil err
function M.resolve_result(alias_or_filename)
  local result, err = M.resolve_alias(alias_or_filename)
  if result then return result, nil end

  if alias_mod.is_valid_filename(alias_or_filename) then
    local data, load_err, is_missing = disk.load(alias_or_filename)
    if data then return data, nil end
    if not is_missing then
      return nil, load_err or "unknown error"
    end
  end

  return nil, err
end

--- Explicit `[<filename>]` wins; otherwise `default_filename()`.
---@param verb string
---@param alias string
---@param explicit string|nil
---@return string
local function resolve_filename(verb, alias, explicit)
  local filename = explicit or M.default_filename(alias)
  if not filename then
    error(verb .. " " .. alias .. ": nothing to name automatically. " ..
      "Pass an explicit <filename>, or run a session first.", 0)
  end
  validate_filename(filename)
  return filename
end

---@param verb string
---@param filename string
---@param result VF.Session.Result
---@return string display_path
local function write_result(verb, filename, result)
  local path, err = disk.write_with_overwrite_warn(filename, result)
  if not path then
    error("vimfy " .. verb .. " failed: " .. (err or "unknown error"), 0)
  end
  return vim.fn.fnamemodify(path, ":~")
end

---@param alias string
---@param explicit_filename string|nil
function M.save(alias, explicit_filename)
  local filename = resolve_filename("save", alias, explicit_filename)
  local result, err = M.resolve_alias(alias)
  if not result then error(err, 0) end

  local display_path = write_result("save", filename, result)
  vim.notify("vimfy saved [" .. filename .. "] → " .. display_path, vim.log.levels.INFO)
end

---@param alias string
---@param explicit_filename string|nil
function M.store(alias, explicit_filename)
  local filename = resolve_filename("store", alias, explicit_filename)

  if session_store.get_active(alias) then
    error("Session '" .. alias .. "' is still active. Finish it first.", 0)
  end

  -- Pin the id before writing: recall aliases drift.
  local id = session_store.get_id(alias)
  if not id then error("No finished result for '" .. alias .. "'.", 0) end
  local result = session_store.get_result_by_id(id)
  if not result then error("No finished result for '" .. alias .. "'.", 0) end

  local display_path = write_result("store", filename, result)
  session_store.remove(id)
  vim.notify("vimfy stored [" .. alias .. "] → [" .. filename .. "] at " ..
    display_path .. " (removed from session)", vim.log.levels.INFO)
end

---@param filename string
---@param alias string
function M.fetch(filename, alias)
  validate_filename(filename)
  alias_mod.expect(alias, { "named" })

  local data, err = disk.load(filename)
  if not data then
    error("vimfy fetch failed: " .. (err or "unknown error"), 0)
  end
  local id, reg_err = session_store.register_fetched_result(alias, data)
  if not id then
    error("vimfy fetch failed: " .. (reg_err or "unknown error"), 0)
  end
  vim.notify("vimfy fetched [" .. filename .. "] → [" .. alias .. "]", vim.log.levels.INFO)
end

---@param filename string
function M.rm(filename)
  validate_filename(filename)
  local ok, path_or_err = disk.remove(filename)
  if not ok then
    error(path_or_err or "vimfy rm failed", 0)
  end
  vim.notify("vimfy removed [" .. filename .. "] ← " .. path_or_err, vim.log.levels.INFO)
end

---@param old_alias string
---@param new_alias string
---@return boolean ok
---@return string|nil err
function M.rename_active(old_alias, new_alias)
  if not alias_mod.is_named(old_alias) then
    return false, "source alias must be a named alias (letters only)"
  end
  return session_store.rename_alias(old_alias, new_alias)
end

---@param src_alias string
---@param dst_alias string
---@return boolean ok
---@return string|nil err
function M.duplicate_active(src_alias, dst_alias)
  if not alias_mod.is_named(dst_alias) then
    return false, "target alias must be a named alias (letters only)"
  end
  local result = session_store.get_result(src_alias)
  if not result then
    return false, "no finished result for '" .. tostring(src_alias) .. "'"
  end
  local _, reg_err = session_store.register_fetched_result(dst_alias, result)
  if reg_err then return false, reg_err end
  return true, nil
end

--- View a finished result full-screen; no arg lists saved files.
---@param alias_or_filename string|nil
function M.view(alias_or_filename)
  if not alias_or_filename or alias_or_filename == "" then
    local saved = disk.list()
    if #saved == 0 then
      vim.notify("No saved results found", vim.log.levels.INFO)
    else
      vim.notify("Saved results:\n  " .. table.concat(saved, "\n  "), vim.log.levels.INFO)
    end
    return
  end

  local result, err = M.resolve_result(alias_or_filename)
  if not result then
    error("vimfy view failed: " .. (err or "unknown error"), 0)
  end
  result_window.open(alias_or_filename, result)
end

return M
