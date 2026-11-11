local atom_stream = require("vimficiency.capture.atom_stream")
local session_store = require("vimficiency.session.store")

local M = {}

local LISTENER_NAME = "recall_capture"

---@param atom VF.Atom
---@return boolean
local function in_live_file_buffer(atom)
  return vim.api.nvim_buf_is_valid(atom.buf)
    and vim.bo[atom.buf].buftype == ""
    and vim.api.nvim_win_is_valid(atom.win)
end

--- Silently drops anything that is not a replayable command in a live file buffer.
---@param atom VF.Atom
---@param decision VF.Atom.Decision
local function recall_listener(atom, decision)
  if decision == "record" and in_live_file_buffer(atom) then
    session_store.ingest_recall_atom(atom)
  end
end

---@return boolean started  true if the listener was newly attached
function M.start_continuous_capture()
  local ok = atom_stream.add_listener(LISTENER_NAME, recall_listener)
  if not ok then
    vim.notify(
      "vimfy: recall capture already installed (listener name " ..
      "'recall_capture' in use). If you called setup() twice, this is " ..
      "harmless — recall is still active from the first call. Otherwise " ..
      "another plugin is colliding on the listener name.",
      vim.log.levels.WARN)
  end
  return ok
end

return M
