local v = vim.api

local buffer_history = require("vimficiency.capture.buffer_history")
local config = require("vimficiency.config")
local disk = require("vimficiency.session.disk")
local ffi_optimizer = require("vimficiency.ffi.optimizer")
local poller = require("vimficiency.async.poller")
local ffi_session = require("vimficiency.ffi.session")
local capture_state = require("vimficiency.capture.state")
local recording = require("vimficiency.capture.recording")

---@class (exact) VF.SessionCompute
---@field capture_end_state               fun(active: VF.Session.Active): VF.BufferState
---@field compute_result_for_active       fun(active: VF.Session.Active, end_state: VF.BufferState): VF.Session.Result
---@field compute_result_for_active_async fun(active: VF.Session.Active, end_state: VF.BufferState, deadline_ms: integer, on_done: fun(result: VF.Session.Result|nil)): VF.Async.PollHandle
---@field manual_should_evict             fun(session: VF.Session.Active, cursor_row: integer, now_ns: integer): string|nil
local M = {}

-- Diagnostic probe for a slow optimizer run. On any analyze over the threshold,
-- dump the exact slice for offline replay and surface the timing. analyze_ms is
-- a main-thread stall on the sync (explicit-finish) path, but worker wall-time
-- on the async (auto_suggest) path. Tune/disable via vim.g.vimficiency_slow_fire_ms.
local SLOW_FIRE_DEFAULT_MS = 500

local function slow_fire_threshold_ms()
  return tonumber(vim.g.vimficiency_slow_fire_ms) or SLOW_FIRE_DEFAULT_MS
end

---@param active VF.Session.Active
---@param result VF.Session.Result
local function log_slow_fire(active, result)
  pcall(function()
    local dir = vim.fn.stdpath("data") .. "/vimficiency/slow"
    vim.fn.mkdir(dir, "p")
    local path = string.format("%s/%d-%s.json", dir, os.time(), tostring(active.id))
    vim.fn.writefile({ vim.json.encode(result) }, path)
    vim.schedule(function()
      vim.notify(string.format(
        "vimfy: optimizer ran %.0fms on a %d-line slice — repro: %s",
        result.analyze_ms or 0, #result.lines,
        vim.fn.fnamemodify(path, ":~")), vim.log.levels.WARN)
    end)
  end)
end

---@param buf integer
---@return string
local function buf_display_name(buf)
  if not v.nvim_buf_is_valid(buf) then
    return "<invalid buffer " .. tostring(buf) .. ">"
  end
  local name = v.nvim_buf_get_name(buf)
  if name == "" then name = "[No Name]" end
  local buftype = vim.bo[buf].buftype
  local suffix = ""
  if buftype ~= "" then
    suffix = " [" .. buftype .. "]"
  end
  return string.format("'%s' (buf %d)%s", name, buf, suffix)
end

-- Raises when the captured buffer is not the current one.
---@param active VF.Session.Active
---@return VF.BufferState end_state
function M.capture_end_state(active)
  local curr_buf = v.nvim_get_current_buf()
  if curr_buf ~= active.buf then
    error(string.format(
      "session captured in %s, but you're now in %s. " ..
      "For recall, this usually means one of the recent keystrokes fired " ..
      "while a different buffer was focused (scratch/result window from a " ..
      "prior :Vimfy command, help buffer, popup, or a <C-^>/<C-w>w switch). " ..
      "Focus the captured buffer and re-run, or use a smaller window.",
      buf_display_name(active.buf),
      buf_display_name(curr_buf)), 0)
  end

  local win = active.win
  -- Use current if can't find
  if not v.nvim_win_is_valid(win) or v.nvim_win_get_buf(win) ~= active.buf then
    win = v.nvim_get_current_win()
  end

  local end_state = capture_state.capture_state(active.buf, win)
  capture_state.check_state_inconsistencies(active.start_state, end_state)
  return end_state
end

-- Marshal both snapshots and the keystroke stream into the bundle consumed by
-- both the sync and async analyze paths. C++ cuts them to the search region.
---@param active VF.Session.Active
---@param end_state VF.BufferState
---@return table input
local function build_analyze_input(active, end_state)
  local start_state = active.start_state
  local initial_lines = active.kind == "recall"
    and buffer_history.lines_at(active.buf, active.start_seq)
    or (start_state --[[@as VF.BufferState]]).lines

  -- Snapshot: a recall record keeps accumulating atoms while the async worker
  -- runs, and `commands` must describe the atoms user_seq was built from.
  -- CmdAtom already resolves mappings and operator-pending duplication, so the
  -- replayable sequence is just each atom's keys concatenated — no reduction.
  local commands = {}
  local raw_parts, typed_parts, typed_insert_parts = {}, {}, {}
  for i, atom in ipairs(active.atoms) do
    commands[i] = { keys = atom.keys, kind = atom.kind }
    raw_parts[i] = atom.keys
    typed_parts[i] = atom.typed
    typed_insert_parts[i] = atom.typed_insert
  end

  local observed, above, below = recording.capture(active, initial_lines, end_state)
  local request = ffi_optimizer.build_search_request({
    initial_lines = initial_lines,
    untouched_above = above,
    untouched_below = below,
    goal_lines = end_state.lines,
    start_row = start_state.row,
    start_col = start_state.col,
    end_row = end_state.row,
    end_col = end_state.col,
    window_height = start_state.window_height,
    scroll_amount = start_state.scroll_amount,
    indent = start_state.indent,
    user_seq = table.concat(raw_parts),
    user_typed = table.concat(typed_parts),
  }, config.SLICE_PADDING, config.MAX_SEARCH_LINES)
  recording.localize(observed, start_state.row - request.start_row)
  request.optimizer_overrides =
    ffi_optimizer.encode_optimizer_overrides({ shared = config.optimizer })

  return {
    active = active,
    recording = observed,
    request = request,
    results_calculated = config.RESULTS_CALCULATED,
    commands = #commands > 0 and commands or disk.empty_array(),
    user_typed_insert = table.concat(typed_insert_parts),
  }
end

---@param input table
---@param analyze VF.Optimizer.AnalyzeResult
---@param analyze_ms number
---@return VF.Session.Result
local function build_result(input, analyze, analyze_ms)
  local active = input.active
  local results = analyze.results

  local dbg = analyze.dbg
  if dbg and dbg ~= "" then
    pcall(function()
      local debug_dir = vim.fn.stdpath("data") .. "/vimficiency/debug"
      vim.fn.mkdir(debug_dir, "p")
      local debug_path = debug_dir .. "/" .. active.id .. ".txt"
      vim.fn.writefile(vim.split(dbg, "\n"), debug_path)
    end)
  end

  local optimal_results = {}
  for i = 1, math.min(#results, config.RESULTS_SAVED) do
    table.insert(optimal_results, results[i])
  end
  if #optimal_results == 0 then
    optimal_results = disk.empty_array()
  end

  -- The planner's own regions — the partition the search ran against — for
  -- column highlighting in views.
  local diffs = analyze.diffs
  if #diffs == 0 then
    diffs = disk.empty_array()
  end

  local request = input.request
  local result = {
    recording = input.recording,
    query = request.query,
    lines = request.initial_lines,
    goal_lines = request.goal_lines,
    start_row = request.start_row,
    start_col = request.start_col,
    end_row = request.end_row,
    end_col = request.end_col,
    has_lines_above = request.has_lines_above,
    has_lines_below = request.has_lines_below,
    -- Reserved boundary context. At the whole-buffer level the slice IS the
    -- edit region (C++ `sliceBoundary`), so horizontal prefix/suffix are always
    -- empty; nonempty values only arise per planned edit inside
    -- CompositionOptimizer. Views render them faded when present.
    prefix = "",
    suffix = "",
    diffs = diffs,
    window_height = request.window_height,
    scroll_amount = request.scroll_amount,
    indent = request.indent,
    filetype = active.start_state.filetype,
    user_seq = request.user_seq,
    user_typed = request.user_typed,
    user_typed_insert = input.user_typed_insert,
    optimizer_overrides = request.optimizer_overrides,
    results_calculated = input.results_calculated,
    user_cost = analyze.user_cost,
    optimal_results = optimal_results,
    unbeaten = analyze.unbeaten,
    commands = input.commands,
    key_count = #input.commands,
    finished_wall_epoch = os.time(),
    analyze_ms = analyze_ms,
  }
  if analyze_ms >= slow_fire_threshold_ms() then
    log_slow_fire(active, result)
  end
  return result
end

---@param active VF.Session.Active
---@param end_state VF.BufferState
---@return VF.Session.Result result
function M.compute_result_for_active(active, end_state)
  local input = build_analyze_input(active, end_state)

  -- Timestamp and call analyze()
  local analyze_t0 = vim.uv.hrtime()
  local results, user_cost, dbg, diffs, unbeaten =
    ffi_optimizer.analyze(input.request, input.results_calculated)
  local analyze_ms = (vim.uv.hrtime() - analyze_t0) / 1e6

  -- Save to corresponding file name, preserve cost, results, and debug info
  return build_result(input,
    { results = results, user_cost = user_cost, dbg = dbg, diffs = diffs, unbeaten = unbeaten },
    analyze_ms)
end

-- Async variant for auto_suggest: the analyze search runs on a C++ worker thread
-- so the main thread never blocks on it. `on_done(result)` is invoked on the
-- main thread once the worker finishes; `result` is nil when the worker failed,
-- and that failure raises from the poll tick. Slicing/start failures raise
-- synchronously instead.
---@param active VF.Session.Active
---@param end_state VF.BufferState
---@param deadline_ms integer
---@param on_done fun(result: VF.Session.Result|nil)
---@return VF.Async.PollHandle
function M.compute_result_for_active_async(active, end_state, deadline_ms, on_done)
  local input = build_analyze_input(active, end_state)

  local analyze_t0 = vim.uv.hrtime()
  local job_id = ffi_optimizer.analyze_start_async(
    input.request, input.results_calculated, deadline_ms)

  return poller.start(
    function() return ffi_optimizer.analyze_poll(job_id) end,
    function() ffi_optimizer.analyze_cancel(job_id) end,
    function(payload)
      local analyze_ms = (vim.uv.hrtime() - analyze_t0) / 1e6
      on_done(build_result(input, payload, analyze_ms))
    end,
    function() on_done(nil) end)
end

---@param session VF.Session.Active
---@param cursor_row integer
---@param now_ns integer
---@return string|nil
function M.manual_should_evict(session, cursor_row, now_ns)
  return ffi_session.manual_evict_reason(
    session.start_state.row,
    cursor_row,
    session.last_atom_mono_ns,
    now_ns,
    config.MAX_SEARCH_LINES,
    config.MANUAL_IDLE_TIMEOUT_SECONDS
  )
end

return M
