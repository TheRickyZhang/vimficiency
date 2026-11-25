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

local ffi = require("ffi")
local atom_stream = require("vimficiency.capture.atom_stream")
local buffer_history = require("vimficiency.capture.buffer_history")
local config = require("vimficiency.config")
local ffi_core = require("vimficiency.ffi.core")
local ffi_versions = require("vimficiency.ffi.versions")
local capture_state = require("vimficiency.capture.state")
local util = require("vimficiency.util")

local JOURNAL_FORMAT = ffi_versions.versions().journal_format
local LISTENER_NAME = "journal"
local AUGROUP = "VimficiencyJournal"
local TICK_END = ffi_core.encode("vf.journal.File", { records = { { tick_end = {} } } })
ffi.cdef[[
  int fileno(void *stream);
  int flock(int fd, int operation);
]]
local LOCK_SH, LOCK_NB = 1, 4

---@class VF.Journal.Run
---@field file file*
---@field path string
---@field started_mono_ns integer
---@field since_snapshot table<integer, integer>
---@field tick integer
---@field tick_open boolean
---@field atoms_seen integer
---@field last_resync string|nil
---@field group integer|nil
---@field listening boolean

---@type VF.Journal.Run|nil
local active = nil

function M.dir()
  return vim.fn.stdpath("data") .. "/vimficiency/journal"
end

---@param run VF.Journal.Run
---@param finish_tick boolean
---@return string|nil
local function release(run, finish_tick)
  if active ~= run then return end
  active = nil

  local write_err
  if finish_tick and run.tick_open then
    local _
    _, write_err = run.file:write(TICK_END)
  end
  local _, close_err = run.file:close()
  if run.group then vim.api.nvim_del_augroup_by_id(run.group) end
  if run.listening then
    atom_stream.remove_listener(LISTENER_NAME)
    buffer_history.set_observer(nil)
  end
  return write_err or close_err
end

---@param run VF.Journal.Run
---@param bytes string
local function append(run, bytes)
  local ok, err = run.file:write(bytes)
  if not ok then
    release(run, false)
    error("vimfy journal: stopped, could not write " .. run.path .. ": " .. tostring(err), 0)
  end
end

---@param run VF.Journal.Run
local function close_tick(run)
  if not run.tick_open then return end
  run.tick_open = false
  append(run, TICK_END)
end

--- A callback scheduled now runs after every atom already queued, so the tick
--- closes exactly when the current burst has been delivered.
---@param run VF.Journal.Run
---@return integer
local function current_tick(run)
  if not run.tick_open then
    run.tick = run.tick + 1
    run.tick_open = true
    local tick = run.tick
    vim.schedule(util.entry(function()
      if active == run and run.tick == tick then close_tick(run) end
    end))
  end
  return run.tick
end

---@param run VF.Journal.Run
---@param kind "header"|"snapshot"|"delta"|"resync"|"atom"
---@param body table
local function write(run, kind, body)
  run.last_resync = nil
  append(run, ffi_core.encode("vf.journal.File", { records = { {
    ms = math.floor((vim.uv.hrtime() - run.started_mono_ns) / 1e6),
    tick = current_tick(run),
    [kind] = body,
  } } }))
end

---@param run VF.Journal.Run
---@param buf integer
---@param lines string[]
local function write_snapshot(run, buf, lines)
  run.since_snapshot[buf] = 0
  write(run, "snapshot", {
    buf = buf,
    bufname = vim.api.nvim_buf_get_name(buf),
    filetype = vim.bo[buf].filetype,
    lines = lines,
  })
end

---@param buf integer
---@param win integer
---@return table  # `vf.journal.ViewState`
local function view_state(buf, win)
  local view = capture_state.capture_view(buf, win)
  return {
    row = view.row,
    col = view.col,
    top_row = view.top_row,
    bottom_row = view.bottom_row,
    window_height = view.window_height,
    scroll_amount = view.scroll_amount,
    indent = view.indent,
  }
end

--- Record the focused window's state as one no atom accounts for.
---@param run VF.Journal.Run
local function resync(run)
  if active ~= run then return end
  local buf, win = vim.api.nvim_get_current_buf(), vim.api.nvim_get_current_win()
  -- Outside Normal mode the change belongs to an atom that has not finished.
  if vim.api.nvim_get_mode().mode ~= "n" or not atom_stream.in_live_file_buffer(buf, win) then
    return
  end

  buffer_history.mark(buf)
  if not run.since_snapshot[buf] then
    write_snapshot(run, buf, vim.api.nvim_buf_get_lines(buf, 0, -1, false))
  end

  local view = view_state(buf, win)
  local key = string.format("%d:%d:%d:%d", buf, win, view.row, view.col)
  if key == run.last_resync then return end
  write(run, "resync", { buf = buf, win = win, view = view })
  run.last_resync = key
end

--- Resync unless an atom is delivered first. An atom is queued when its command
--- ends, ahead of anything scheduled from the events that command fired.
---@param run VF.Journal.Run
local function resync_unless_atom(run)
  local seen = run.atoms_seen
  vim.schedule(util.entry(function()
    if run.atoms_seen == seen then resync(run) end
  end))
end

---@param run VF.Journal.Run
---@param atom VF.Atom
---@param decision VF.Atom.Decision
local function listener(run, atom, decision)
  run.atoms_seen = run.atoms_seen + 1
  if not atom_stream.in_live_file_buffer(atom.buf, atom.win) then return end
  local buf = atom.buf

  buffer_history.mark(buf)
  if (run.since_snapshot[buf] or math.huge) >= config.journal.snapshot_every then
    write_snapshot(run, buf, vim.api.nvim_buf_get_lines(buf, 0, -1, false))
  end
  run.since_snapshot[buf] = run.since_snapshot[buf] + 1

  write(run, "atom", {
    buf = buf,
    win = atom.win,
    keys = atom.keys,
    kind = atom.kind,
    visited_rows = atom.visited_rows,
    typed = atom.typed,
    typed_insert = atom.typed_insert,
    decision = decision,
    lossy = atom.lossy,
    abort_reason = atom.abort_reason,
    mode = vim.api.nvim_get_mode().mode,
    view = view_state(buf, atom.win),
  })
end

---@param run VF.Journal.Run
local function install(run)
  if ffi.C.flock(ffi.C.fileno(run.file), LOCK_SH + LOCK_NB) ~= 0 then
    error(("vimfy journal: could not lock %s (errno %d)"):format(run.path, ffi.errno()), 0)
  end
  local ok, err = run.file:setvbuf("no")
  if not ok then error("vimfy journal: could not configure " .. run.path .. ": " .. tostring(err), 0) end
  write(run, "header", { version = JOURNAL_FORMAT, started_wall_epoch = os.time() })

  run.listening = atom_stream.add_listener(LISTENER_NAME, function(atom, decision)
    listener(run, atom, decision)
  end)
  assert(run.listening, "vimfy journal: listener already installed")
  buffer_history.set_observer({
    on_seed = function(buf, lines) write_snapshot(run, buf, lines) end,
    on_delta = function(buf, first, old_count, new_lines, min_height)
      -- Before the first snapshot there is no base for a delta to apply to.
      if run.since_snapshot[buf] then
        write(run, "delta", { buf = buf, first = first, old_count = old_count, lines = new_lines, min_height = min_height })
      end
    end,
  })

  local group = vim.api.nvim_create_augroup(AUGROUP, { clear = true })
  run.group = group
  local resync_run = util.entry(function() resync(run) end)
  vim.api.nvim_create_autocmd({ "BufEnter", "WinEnter" }, {
    group = group,
    callback = function() vim.schedule(resync_run) end,
  })
  vim.api.nvim_create_autocmd({ "CursorMoved", "TextChanged" }, {
    group = group,
    callback = function() resync_unless_atom(run) end,
  })
  vim.schedule(resync_run)
end

function M.start()
  if active then return end
  local dir = M.dir()
  vim.fn.mkdir(dir, "p")
  local mono_ns = vim.uv.hrtime()
  local path = string.format("%s/%s-%d-%d.vfj", dir, os.date("%Y%m%d-%H%M%S"), vim.uv.os_getpid(), mono_ns)
  local handle, err = io.open(path, "ab")
  if not handle then
    error("vimfy journal: could not open " .. path .. ": " .. tostring(err), 0)
  end

  local run = {
    file = handle,
    path = path,
    started_mono_ns = mono_ns,
    since_snapshot = {},
    tick = 0,
    tick_open = false,
    atoms_seen = 0,
    listening = false,
  }
  active = run
  local ok, install_err = pcall(install, run)
  if not ok then
    release(run, false)
    error(install_err, 0)
  end
end

function M.stop()
  local run = active
  if not run then return end
  local err = release(run, true)
  if err then
    vim.notify("vimfy journal: could not finish " .. run.path .. ": " .. err, vim.log.levels.ERROR)
  end
end

--- The file being written, or nil when the journal is off.
---@return string|nil
function M.path()
  return active and active.path
end

M._for_test = {
  listener = function(atom, decision) listener(assert(active), atom, decision) end,
  resync = function() resync(assert(active)) end,
  close_tick = function() close_tick(assert(active)) end,
}

return M
