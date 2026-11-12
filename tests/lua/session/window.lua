-- tests/lua/session_window.lua
-- Covers capture_end_state's window-fallback (#1): when the original session
-- window is closed but the buffer is still current via a different window, we
-- should proceed instead of hard-rejecting.

local compute = require("vimficiency.session.compute")
local ffi_lib = require("vimficiency.ffi")
local util    = require("vimficiency.util")
local h       = require("_helpers")

local function fake_active(buf, win)
  return {
    id = "test-" .. tostring(vim.uv.hrtime()),
    stream_sub = nil,
    win = win,
    buf = buf,
    start_state = util.capture_state(buf, win),
    started_mono_ns = vim.uv.hrtime(),
    atoms = {},
  }
end

test("capture_end_state: window-closed fallback to current window", function()
  local buf = h.new_buf({ "hello world", "second line" })

  -- Two windows, both showing buf. We'll treat the current one as
  -- "original", then close it and land in the split.
  local win_original = vim.api.nvim_get_current_win()
  vim.cmd("vsplit")
  local win_other = vim.api.nvim_get_current_win()
  assert_true(win_original ~= win_other, "sanity: vsplit must yield a new window")

  local active = fake_active(buf, win_original)

  -- Focus the split, close the original window. We're now in win_other
  -- but still in `buf`.
  vim.api.nvim_set_current_win(win_other)
  vim.api.nvim_win_close(win_original, true)
  assert_eq(vim.api.nvim_get_current_buf(), buf,
    "sanity: still in original buffer after closing original window")
  assert_true(not vim.api.nvim_win_is_valid(win_original),
    "sanity: original window is gone")

  local end_state, err = compute.capture_end_state(active)
  assert_true(end_state ~= nil, "must capture via the surviving window, got: " .. tostring(err))

  pcall(vim.api.nvim_buf_delete, buf, { force = true })
end)

test("capture_end_state: buffer-gone still hard-rejects", function()
  local buf = h.new_buf({ "x" })
  local active = fake_active(buf, vim.api.nvim_get_current_win())

  vim.cmd("enew")
  vim.api.nvim_buf_delete(buf, { force = true })

  local end_state, err = compute.capture_end_state(active)
  assert_eq(end_state, nil, "should fail when buffer gone")
  assert_match(err, "buffer", "error must mention buffer")
end)

test("capture_end_state: different-buffer still hard-rejects", function()
  local buf_a = h.new_buf({ "a" })
  local active = fake_active(buf_a, vim.api.nvim_get_current_win())
  local buf_b = h.new_buf({ "b" })

  local end_state, err = compute.capture_end_state(active)
  assert_eq(end_state, nil, "should fail when current buf differs")
  assert_match(err, "buf " .. buf_a, "error must name captured buffer")
  assert_match(err, "buf " .. buf_b, "error must name current buffer")

  pcall(vim.api.nvim_buf_delete, buf_a, { force = true })
  pcall(vim.api.nvim_buf_delete, buf_b, { force = true })
end)

test("compute_result_for_active: includes capture key debug", function()
  local buf = h.new_buf({ "cout << i+1 << endl;" })
  local win = vim.api.nvim_get_current_win()
  vim.api.nvim_win_set_cursor(win, { 1, 8 })
  local active = fake_active(buf, win)
  -- One atom per command: CmdAtom has already resolved operator-pending, so the
  -- replayable `keys` are just concatenated (no reduction, no mode tracking).
  active.atoms = {
    { keys = "cwW2<Esc>", win = win, buf = buf },
    { keys = "dl",        win = win, buf = buf },
  }
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, { "cout << W2 << endl;" })

  local result
  h.with_patch({
    { ffi_lib, "analyze", function()
      return {}, 9.0, ""
    end },
  }, function()
    local end_state = assert(compute.capture_end_state(active))
    result = assert(compute.compute_result_for_active(active, end_state))
  end)

  assert_eq(result.user_seq, "cwW2<Esc>dl")
  assert_eq(vim.inspect(result.capture_debug.atom_keys), vim.inspect({ "cwW2<Esc>", "dl" }))

  pcall(vim.api.nvim_buf_delete, buf, { force = true })
end)
