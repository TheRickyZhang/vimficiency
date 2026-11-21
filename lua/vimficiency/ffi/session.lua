local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

---@param key_seq VF.KeyEvent[]
---@return string
function M.build_sequence(key_seq)
  local events = {}
  for i, ev in ipairs(key_seq) do
    events[i] = { mode = ev.mode, key_typed = ev.key_typed }
  end
  return core.call(lib.vf_build_sequence, "vf.session.BuildSequenceRequest",
    { events = events }, "vf.Text").text
end

--- The keys pressed for an atom, from what Neovim read as typed, when some of
--- it was fed by a plugin rather than pressed.
---@param inputs { keys: string, fed: boolean }[]  in order, key notation
---@return string
function M.typed_keys(inputs)
  return core.call(lib.vf_typed_keys, "vf.session.TypedKeysRequest",
    { inputs = inputs }, "vf.Text").text
end

---@param start_row integer
---@param cursor_row integer
---@param last_key_time_ns integer|nil
---@param now_ns integer
---@param max_search_lines integer
---@param manual_idle_timeout_seconds integer
---@return string|nil
function M.manual_evict_reason(
  start_row,
  cursor_row,
  last_key_time_ns,
  now_ns,
  max_search_lines,
  manual_idle_timeout_seconds
)
  local reason = core.call(lib.vf_manual_evict_reason, "vf.session.ManualEvictRequest", {
    start_row = start_row,
    cursor_row = cursor_row,
    last_key_time_ns = last_key_time_ns,
    now_ns = now_ns,
    max_search_lines = max_search_lines,
    manual_idle_timeout_seconds = manual_idle_timeout_seconds,
  }, "vf.session.ManualEvict").reason
  if reason == "drift" then
    return "cursor drifted beyond MAX_SEARCH_LINES (" .. max_search_lines .. ")"
  end
  if reason == "idle" then
    return "idle for more than " .. manual_idle_timeout_seconds .. "s"
  end
  return nil
end

return M
