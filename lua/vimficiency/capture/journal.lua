-- Append-only record of the atom stream, with enough buffer text to rebuild any
-- atom boundary: a snapshot, then every line change after it. The records are
-- `vf.journal.Record` (src/LuaExports/proto/journal.proto); how they are read
-- is in `dev/lua/atom-stream.md`.

---@class (exact) VF.Journal
---@field start     fun()
---@field stop      fun()
---@field path      fun(): string|nil
---@field dir       fun(): string
---@field _for_test { listener: fun(atom: VF.Atom, decision: VF.Atom.Decision), resync: fun(), close_tick: fun() }
local M = {}

local atom_stream = require("vimficiency.capture.atom_stream")
local buffer_history = require("vimficiency.capture.buffer_history")
local config = require("vimficiency.config")
local ffi_core = require("vimficiency.ffi.core")
local util = require("vimficiency.util")

local FORMAT_VERSION = 3
local LISTENER_NAME = "journal"
local AUGROUP = "VimficiencyJournal"

---@type file*|nil
local fh = nil
---@type string|nil
local file_path = nil
local started_mono_ns = 0

--- Atoms journaled per buffer since its last snapshot; nil until the first one.
---@type table<integer, integer>
local since_snapshot = {}

local tick = 0
local tick_open = false

--- Every atom delivered, in scope or not. A cursor or text event that no atom
--- follows was not caused by a command.
local atoms_seen = 0

--- `buf:win:row:col` of a resync that is still the last record written.
---@type string|nil
local last_resync = nil

function M.dir()
  return vim.fn.stdpath("data") .. "/vimficiency/journal"
end

--- A callback scheduled now runs after every atom already queued, so the tick
--- closes exactly when the current burst has been delivered.
---@return integer
local function current_tick()
  if not tick_open then
    tick = tick + 1
    tick_open = true
    vim.schedule(function() tick_open = false end)
  end
  return tick
end

---@param kind "header"|"snapshot"|"delta"|"resync"|"atom"
---@param body table
local function write(kind, body)
  last_resync = nil
  local bytes = ffi_core.encode("vf.journal.File", { records = { {
    ms = math.floor((vim.uv.hrtime() - started_mono_ns) / 1e6),
    tick = current_tick(),
    [kind] = body,
  } } })
  local ok, err = (fh --[[@as file*]]):write(bytes)
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
  write("snapshot", {
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
    write("delta", { buf = buf, first = first, old_count = old_count, lines = new_lines })
  end,
}

---@param buf integer
---@param win integer
---@return table  # `vf.journal.ViewState`
local function view_state(buf, win)
  local view = util.capture_view(buf, win)
  return {
    row = view.row,
    col = view.col,
    top_row = view.top_row,
    bottom_row = view.bottom_row,
    window_height = view.window_height,
    scroll_amount = view.scroll_amount,
  }
end

--- Record the focused window's state as one no atom accounts for.
local function resync()
  if not fh then return end
  local buf, win = vim.api.nvim_get_current_buf(), vim.api.nvim_get_current_win()
  -- Outside Normal mode the change belongs to an atom that has not finished.
  if vim.api.nvim_get_mode().mode ~= "n" or not atom_stream.in_live_file_buffer(buf, win) then
    return
  end

  buffer_history.mark(buf)
  if not since_snapshot[buf] then
    write_snapshot(buf, vim.api.nvim_buf_get_lines(buf, 0, -1, false))
  end

  local view = view_state(buf, win)
  local key = string.format("%d:%d:%d:%d", buf, win, view.row, view.col)
  if key == last_resync then return end
  write("resync", { buf = buf, win = win, view = view })
  last_resync = key
end

--- Resync unless an atom is delivered first. An atom is queued when its command
--- ends, ahead of anything scheduled from the events that command fired.
local function resync_unless_atom()
  local seen = atoms_seen
  vim.schedule(util.entry(function()
    if atoms_seen == seen then resync() end
  end))
end

---@param atom VF.Atom
---@param decision VF.Atom.Decision
local function listener(atom, decision)
  atoms_seen = atoms_seen + 1
  if not atom_stream.in_live_file_buffer(atom.buf, atom.win) then return end
  local buf = atom.buf

  buffer_history.mark(buf)
  if (since_snapshot[buf] or math.huge) >= config.journal.snapshot_every then
    write_snapshot(buf, vim.api.nvim_buf_get_lines(buf, 0, -1, false))
  end
  since_snapshot[buf] = since_snapshot[buf] + 1

  write("atom", {
    buf = buf,
    win = atom.win,
    keys = atom.keys,
    kind = atom.kind,
    typed = atom.typed,
    decision = decision,
    lossy = atom.lossy,
    abort_reason = atom.abort_reason,
    mode = vim.api.nvim_get_mode().mode,
    view = view_state(buf, atom.win),
  })
end

function M.start()
  if fh then return end
  local dir = M.dir()
  vim.fn.mkdir(dir, "p")
  local path = string.format("%s/%s-%d.vfj", dir, os.date("%Y%m%d-%H%M%S"), vim.uv.os_getpid())
  local handle, err = io.open(path, "ab")
  if not handle then
    error("vimfy journal: could not open " .. path .. ": " .. tostring(err), 0)
  end
  handle:setvbuf("no")

  fh, file_path, started_mono_ns = handle, path, vim.uv.hrtime()
  since_snapshot = {}
  write("header", { version = FORMAT_VERSION, started_wall_epoch = os.time() })

  buffer_history.set_observer(observer)
  atom_stream.add_listener(LISTENER_NAME, listener)

  local group = vim.api.nvim_create_augroup(AUGROUP, { clear = true })
  vim.api.nvim_create_autocmd({ "BufEnter", "WinEnter" }, {
    group = group,
    callback = function() vim.schedule(util.entry(resync)) end,
  })
  vim.api.nvim_create_autocmd({ "CursorMoved", "TextChanged" }, {
    group = group,
    callback = resync_unless_atom,
  })
  vim.schedule(util.entry(resync))
end

function M.stop()
  if not fh then return end
  vim.api.nvim_del_augroup_by_name(AUGROUP)
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

M._for_test = {
  listener = listener,
  resync = resync,
  close_tick = function() tick_open = false end,
}

return M
