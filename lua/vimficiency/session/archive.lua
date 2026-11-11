-- Finished results as named things: selectors (`@`, `$`, alias, recall window)
-- over the in-memory store, and saved names on disk.

local alias_mod = require("vimficiency.session.alias")
local disk = require("vimficiency.session.disk")
local result_window = require("vimficiency.session.result_window")
local session_store = require("vimficiency.session.store")

local M = {}

---@param name string
local function validate_saved_name(name)
  if not alias_mod.is_valid_saved_name(name) then
    error("Invalid saved name '" .. tostring(name) .. "'. " ..
      "Allowed: alphanumeric, '.', '_', '-'; must start with a letter, digit, or underscore.", 0)
  end
end

---@param selector string
---@return string|nil
function M.default_save_name(selector)
  local special_id, _, is_special = session_store.resolve_special(selector)
  if is_special then
    return special_id and session_store.get_alias_by_id(special_id) or nil
  end
  return selector
end

--- Message for a special selector that has nothing recorded yet.
---@param selector string
---@return string
local function special_missing_message(selector)
  if selector == "$" then
    return "No regression captured yet. vimfy records one when it next " ..
      "suggests a faster sequence."
  end
  return "No recently finished session. Run ':Vimfy finish <alias>' first."
end

---@param selector string
---@return VF.Session.Result|nil result
---@return string|nil err
local function resolve_in_memory(selector)
  local _, special_result, is_special = session_store.resolve_special(selector)
  if is_special then
    if not special_result then
      return nil, special_missing_message(selector)
    end
    return special_result, nil
  end
  local result = session_store.get_result(selector)
  if not result then
    return nil, "No finished result for '" .. selector .. "'. Is the session still active?"
  end
  return result, nil
end

--- In-memory first, then a saved name on disk.
---@param selector string
---@return VF.Session.Result|nil result
---@return string|nil err
function M.resolve_result(selector)
  local result, err = resolve_in_memory(selector)
  if result then return result, nil end

  if not alias_mod.is_special_selector(selector) and alias_mod.is_valid_saved_name(selector) then
    local data, load_err, is_missing = disk.load(selector)
    if data then return data, nil end
    if not is_missing then
      return nil, load_err or "unknown error"
    end
  end

  return nil, err
end

--- Explicit `[<name>]` wins; otherwise `default_save_name()`.
---@param verb string
---@param selector string
---@param explicit string|nil
---@return string
local function resolve_save_name(verb, selector, explicit)
  local name = explicit or M.default_save_name(selector)
  if not name then
    error(verb .. " " .. selector .. ": nothing to name automatically. " ..
      "Pass an explicit <name>, or run a session first.", 0)
  end
  validate_saved_name(name)
  return name
end

---@param verb string
---@param name string
---@param result VF.Session.Result
---@return string display_path
local function write_result(verb, name, result)
  local path, err = disk.write_with_overwrite_warn(name, result)
  if not path then
    error("vimfy " .. verb .. " failed: " .. (err or "unknown error"), 0)
  end
  return vim.fn.fnamemodify(path, ":~")
end

---@param selector string
---@param explicit_name string|nil
function M.save(selector, explicit_name)
  local name = resolve_save_name("save", selector, explicit_name)
  local result, err = resolve_in_memory(selector)
  if not result then error(err, 0) end

  local display_path = write_result("save", name, result)
  vim.notify("vimfy saved [" .. name .. "] → " .. display_path, vim.log.levels.INFO)
end

---@param selector string
---@param explicit_name string|nil
function M.store(selector, explicit_name)
  local name = resolve_save_name("store", selector, explicit_name)

  if session_store.get_active(selector) then
    local kind = alias_mod.parse(selector)
    local resolve_cmd = (kind == "recall_key" or kind == "recall_time")
      and (":Vimfy recall " .. selector)
      or (":Vimfy finish " .. selector)
    error("Session '" .. selector .. "' is still active. Finish it with '" ..
      resolve_cmd .. "' first.", 0)
  end

  local special_id, _, is_special = session_store.resolve_special(selector)
  local id = is_special and special_id or session_store.get_id(selector)
  local result = id and session_store.get_result_by_id(id)
  if not result then
    error("No finished result for '" .. selector .. "'.", 0)
  end

  local display_path = write_result("store", name, result)
  session_store.remove(id)
  vim.notify("vimfy stored [" .. selector .. "] → [" .. name .. "] at " ..
    display_path .. " (removed from session)", vim.log.levels.INFO)
end

---@param name string
---@param alias string
function M.fetch(name, alias)
  validate_saved_name(name)
  if not alias_mod.is_valid_manual(alias) then
    error("Invalid alias '" .. tostring(alias) ..
      "'. Manual aliases must be alphabetic only (e.g. `foo`).", 0)
  end

  local data, err = disk.load(name)
  if not data then
    error("vimfy fetch failed: " .. (err or "unknown error"), 0)
  end
  local id, reg_err = session_store.register_fetched_result(alias, data)
  if not id then
    error("vimfy fetch failed: " .. (reg_err or "unknown error"), 0)
  end
  vim.notify("vimfy fetched [" .. name .. "] → [" .. alias .. "]", vim.log.levels.INFO)
end

---@param name string
function M.rm(name)
  validate_saved_name(name)
  local ok, path_or_err = disk.remove(name)
  if not ok then
    error(path_or_err or "vimfy rm failed", 0)
  end
  vim.notify("vimfy removed [" .. name .. "] ← " .. path_or_err, vim.log.levels.INFO)
end

---@param old_alias string
---@param new_alias string
---@return boolean ok
---@return string|nil err
function M.rename_active(old_alias, new_alias)
  if not alias_mod.is_valid_manual(old_alias) then
    return false, "source alias must be a manual (alphabetic) alias"
  end
  return session_store.rename_manual_alias(old_alias, new_alias)
end

---@param src_alias string
---@param dst_alias string
---@return boolean ok
---@return string|nil err
function M.duplicate_active(src_alias, dst_alias)
  if not alias_mod.is_valid_manual(dst_alias) then
    return false, "target alias must be a manual (alphabetic) alias"
  end
  local result = session_store.get_result(src_alias)
  if not result then
    return false, "no finished result for '" .. tostring(src_alias) .. "'"
  end
  local _, reg_err = session_store.register_fetched_result(dst_alias, result)
  if reg_err then return false, reg_err end
  return true, nil
end

--- View a finished result full-screen. Accepts any selector
--- (`@`/`$`/alias/recall window) or a saved disk name; no arg lists saves.
---@param selector string|nil
function M.view(selector)
  if not selector or selector == "" then
    local saved = disk.list()
    if #saved == 0 then
      vim.notify("No saved results found", vim.log.levels.INFO)
    else
      vim.notify("Saved results:\n  " .. table.concat(saved, "\n  "), vim.log.levels.INFO)
    end
    return
  end

  local result, err = M.resolve_result(selector)
  if not result then
    error("vimfy view failed: " .. (err or "unknown error"), 0)
  end
  result_window.open(selector, result)
end

return M
