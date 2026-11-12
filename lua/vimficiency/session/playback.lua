local alias_mod = require("vimficiency.session.alias")
local archive = require("vimficiency.session.archive")
local disk = require("vimficiency.session.disk")
local session_store = require("vimficiency.session.store")
local simulate = require("vimficiency.simulate")

local M = {}

---@param result VF.Session.Result
---@param label string
---@param count integer|nil
local function run_compare(result, label, count)
  local optimal_results = result.optimal_results
  local user_seq = result.user_seq
  local first_optimal = optimal_results[1] and optimal_results[1].seq or ""

  local user_item = nil
  if user_seq ~= "" and user_seq ~= first_optimal then
    user_item = { seq = user_seq, cost = string.format("%.2f", result.user_cost) }
  end

  local suggestions = {}
  for _, r in ipairs(optimal_results) do
    table.insert(suggestions, { seq = r.seq, cost = string.format("%.2f", r.cost) })
  end

  if user_item == nil and #suggestions == 0 then
    vim.notify("No sequences to simulate", vim.log.levels.WARN)
    return
  end

  simulate.simulate_compare(
    result.lines,
    result.start_row,
    result.start_col,
    {
      user = user_item,
      suggestions = suggestions,
    },
    {
      label = label,
      end_row = result.end_row,
      end_col = result.end_col,
      initial_window_count = count,
    }
  )
end

--- Workspace first, disk second. A disk-only hit under a named alias is fetched
--- into the workspace so "what I just replayed is in my session" holds.
---@param alias_or_filename string
---@param count integer|nil
function M.simulate(alias_or_filename, count)
  local in_memory, miss_err = archive.resolve_alias(alias_or_filename)
  local on_disk = nil
  if alias_mod.is_valid_filename(alias_or_filename) then
    local data, err, is_missing = disk.load(alias_or_filename)
    if data then
      on_disk = data
    elseif not is_missing then
      vim.notify("simulate: " .. err, vim.log.levels.ERROR)
      return
    end
  end

  local result
  if in_memory and on_disk then
    vim.notify(
      "'" .. alias_or_filename .. "' exists in both session memory and on disk — " ..
      "replaying the in-memory copy. (The disk copy is untouched; " ..
      "`:Vimfy fetch " .. alias_or_filename .. " <other-alias>` to inspect it " ..
      "separately, or `:Vimfy store " .. alias_or_filename .. " <new-filename>` to move " ..
      "the in-memory copy aside so future sims see the disk copy.)",
      vim.log.levels.WARN)
    result = in_memory
  elseif in_memory then
    result = in_memory
  elseif on_disk then
    if alias_mod.is_named(alias_or_filename) then
      if session_store.has_active(alias_or_filename) then
        vim.notify("vimfy: replaying '" .. alias_or_filename .. "' directly from disk " ..
          "(an active session holds that alias). Use `:Vimfy fetch " .. alias_or_filename ..
          " <alias>` to keep it in the workspace.", vim.log.levels.WARN)
      else
        session_store.register_fetched_result(alias_or_filename, on_disk)
        vim.notify("vimfy: fetched [" .. alias_or_filename .. "] into session", vim.log.levels.INFO)
      end
    end
    result = on_disk
  else
    vim.notify(miss_err or ("No results for '" .. alias_or_filename .. "' in session or on disk."),
      vim.log.levels.ERROR)
    return
  end

  run_compare(result, alias_or_filename, count)
end

return M
