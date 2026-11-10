local atoms = require("vimficiency.capture.atoms")
local session_store = require("vimficiency.session.store")

local M = {}

local SUBSCRIBER = "recall_capture"

--- Install the global recall-capture subscriber. Idempotent: a redundant
--- call (e.g. a second `setup()` invocation) returns false and emits a
--- warning so the double-setup bug surfaces instead of silently leaving
--- recall disabled.
---@return boolean installed  true if the subscriber was newly attached
function M.install()
  if not atoms.available() then
    return false
  end

  local ok = atoms.subscribe(SUBSCRIBER, function(atom, verdict)
    if verdict == "mouse" then
      session_store.mark_recall_mouse(atom.buf)
      return
    end
    if verdict ~= "record" then return end
    if not vim.api.nvim_buf_is_valid(atom.buf) then return end
    if vim.bo[atom.buf].buftype ~= "" then return end
    if not vim.api.nvim_win_is_valid(atom.win) then return end
    session_store.ingest_recall_atom(atom)
  end)

  if not ok then
    vim.notify(
      "vimfy: recall capture already installed (subscriber name " ..
      "'recall_capture' in use). If you called setup() twice, this is " ..
      "harmless — recall is still active from the first call. Otherwise " ..
      "another plugin is colliding on the subscriber name.",
      vim.log.levels.WARN)
  end
  return ok
end

return M
