-- Append-only JSONL record of the atom stream, with enough buffer text to rebuild
-- any atom boundary: a snapshot, then every line change after it. Record shapes
-- are in `dev/lua/atom-stream.md`.

---@class (exact) VF.Journal
---@field start     fun()
---@field stop      fun()
---@field path      fun(): string|nil
---@field _for_test { listener: fun(atom: VF.Atom, decision: VF.Atom.Decision) }
local M = {}

local atom_stream = require("vimficiency.capture.atom_stream")
local buffer_history = require("vimficiency.capture.buffer_history")
local config = require("vimficiency.config")
local util = require("vimficiency.util")

local FORMAT_VERSION = 1
local LISTENER_NAME = "journal"

---@type file*|nil
local fh = nil
---@type string|nil
local file_path = nil
local started_mono_ns = 0

--- Atoms journaled per buffer since its last snapshot; nil until the first one.
---@type table<integer, integer>
local since_snapshot = {}

---@param record table
local function write(record)
  record.ms = math.floor((vim.uv.hrtime() - started_mono_ns) / 1e6)
  local ok, err = (fh --[[@as file*]]):write(vim.json.encode(record), "\n")
  if not ok then
    local path = file_path
    M.stop()
    error("vimfy journal: stopped, could not write " .. tostring(path) .. ": " .. tostring(err), 0)
  end
end

---@param buf integer
---@param lines string[]
local function write_snapshot(buf, lines)
  since_snapshot[buf] = 0
  write({
    rec = "snapshot",
    buf = buf,
    bufname = vim.api.nvim_buf_get_name(buf),
    filetype = vim.bo[buf].filetype,
    lines = lines,
  })
end

---@type VF.BufferHistory.Observer
local observer = {
  on_seed = write_snapshot,
  on_delta = function(buf, first, old_count, new_lines)
    -- Before the first snapshot there is no base for a delta to apply to.
    if not since_snapshot[buf] then return end
    write({ rec = "delta", buf = buf, first = first, old_count = old_count, lines = new_lines })
  end,
}

---@param atom VF.Atom
---@param decision VF.Atom.Decision
local function listener(atom, decision)
  if not atom_stream.in_live_file_buffer(atom) then return end
  local buf = atom.buf

  buffer_history.mark(buf)
  if (since_snapshot[buf] or math.huge) >= config.journal.snapshot_every then
    write_snapshot(buf, vim.api.nvim_buf_get_lines(buf, 0, -1, false))
  end
  since_snapshot[buf] = since_snapshot[buf] + 1

  local view = util.capture_view(buf, atom.win)
  write({
    rec = "atom",
    buf = buf,
    win = atom.win,
    keys = atom.keys,
    decision = decision,
    lossy = atom.lossy,
    abort_reason = atom.abort_reason,
    row = view.row,
    col = view.col,
    top_row = view.top_row,
    bottom_row = view.bottom_row,
    window_height = view.window_height,
    scroll_amount = view.scroll_amount,
  })
end

function M.start()
  if fh then return end
  local dir = vim.fn.stdpath("data") .. "/vimficiency/journal"
  vim.fn.mkdir(dir, "p")
  local path = string.format("%s/%s-%d.jsonl", dir, os.date("%Y%m%d-%H%M%S"), vim.uv.os_getpid())
  local handle, err = io.open(path, "a")
  if not handle then
    error("vimfy journal: could not open " .. path .. ": " .. tostring(err), 0)
  end
  handle:setvbuf("line")

  fh, file_path, started_mono_ns = handle, path, vim.uv.hrtime()
  since_snapshot = {}
  write({ rec = "header", version = FORMAT_VERSION, started_wall_epoch = os.time() })

  buffer_history.set_observer(observer)
  atom_stream.add_listener(LISTENER_NAME, listener)
end

function M.stop()
  if not fh then return end
  atom_stream.remove_listener(LISTENER_NAME)
  buffer_history.set_observer(nil)
  fh:close()
  fh, file_path = nil, nil
  since_snapshot = {}
end

--- The file being written, or nil when the journal is off.
---@return string|nil
function M.path()
  return file_path
end

M._for_test = { listener = listener }

return M
