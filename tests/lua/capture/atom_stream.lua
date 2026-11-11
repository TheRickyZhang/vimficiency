-- capture/atom_stream.lua: the admission policy (pure) plus two input-driven
-- smoke cases for the autocmd wiring and the synthetic count-1 strip.
--
-- CmdAtom (Neovim 0.13-dev+) is a *deferred* event: it only fires once control
-- returns to the main loop, and `nvim_input` typeahead is only consumed by a
-- running loop. Neither happens under `nvim -l` (the main `run.sh` batch), so
-- this file is a self-contained async script run in its own process via
-- `-c luafile` under a nightly `$VF_NVIM` — the same pattern as
-- `simulate/integration.lua`. See dev/lua/atom-stream.md.
--
-- Driven by a coroutine: each case feeds real input, yields to a timer so the
-- loop drains the atom queue, then asserts. Under a non-nightly Neovim the
-- script reports a clean skip instead of failing.

local script = debug.getinfo(1, "S").source:sub(2)
local this_dir = script:match("(.*/)")
local plugin_root = this_dir .. "../../.."
vim.opt.rtp:prepend(plugin_root)

local atom_stream = require("vimficiency.capture.atom_stream")

--------------------------------------------------------------------------------
-- Minimal harness (standalone; the shared runner globals aren't present here).
--------------------------------------------------------------------------------

local passed, failed = 0, 0

local function record_pass() passed = passed + 1 end
local function record_fail(name, err)
  failed = failed + 1
  io.stderr:write(string.format("[  FAILED  ] atoms :: %s\n    %s\n",
    name, tostring(err):gsub("\n", "\n    ")))
end

---@param actual any
---@param expected any
---@param msg string
local function eq(actual, expected, msg)
  if not vim.deep_equal(actual, expected) then
    error(string.format("%s: expected %s, got %s",
      msg, vim.inspect(expected), vim.inspect(actual)), 2)
  end
end

---@param cond any
---@param msg string
local function truthy(cond, msg)
  if not cond then error(msg, 2) end
end

local function check(name, fn)
  local ok, err = pcall(fn)
  if ok then record_pass() else record_fail(name, err) end
end

local function write_summary(p, f)
  if vim.env.VF_TEST_SUMMARY_FILE and vim.env.VF_TEST_SUMMARY_FILE ~= "" then
    local fh = io.open(vim.env.VF_TEST_SUMMARY_FILE, "a")
    if fh then
      fh:write(string.format("passed=%d failed=%d tests=%d\n", p, f, p + f))
      fh:close()
    end
  end
end

local function finish()
  io.stdout:write(string.format("[==========] atoms: %d/%d passed\n", passed, passed + failed))
  if failed > 0 then
    io.stderr:write(string.format("%d FAILED (atoms)\n", failed))
  end
  write_summary(passed, failed)
  vim.cmd((failed == 0) and "cquit 0" or "cquit 1")
end

if not atom_stream.supported() then
  io.stdout:write("[==========] atoms: skipped — this Neovim has no ##CmdAtom\n")
  write_summary(0, 0)
  vim.cmd("cquit 0")
  return
end

--------------------------------------------------------------------------------
-- Pure cases.
--------------------------------------------------------------------------------

check("_classify verdict table", function()
  local c = atom_stream._classify
  eq(c("operator", "dd", "dd", true, false), "record", "operator")
  eq(c("motion", "j", "j", false, true), "record", "motion")
  eq(c("normal", "<Esc>", "<Esc>", false, false), "record", "normal no-op")
  eq(c("mapping", nil, "", false, false), "skip", "lua cb no-op")
  eq(c("mapping", nil, "", true, false), "abort", "lua cb changed")
  eq(c("mapping", nil, "", false, true), "abort", "lua cb moved")
  eq(c("operator", "", "", true, false), "abort", "unreplayable")
  eq(c("excmd", ":5<NL>", ":5<NL>", false, true), "abort", "ex jump")
  eq(c("excmd", ":w<NL>", ":w<NL>", false, false), "skip", "ex no-op")
  eq(c("excmd", ":s/e/E/<NL>", ":s/e/E/<NL>", true, true), "abort", "ex edit")
  eq(c("mouse", "<LeftMouse>", "<LeftMouse>", false, true), "abort", "mouse moved")
  eq(c("mouse", "<ScrollWheelDown>", "<ScrollWheelDown>", false, false), "skip", "mouse no-op")
end)

check("add_listener refuses duplicate name; remove_listener tears down", function()
  truthy(atom_stream.add_listener("lifecycle", function() end), "first listener")
  eq(atom_stream.add_listener("lifecycle", function() end), false, "duplicate refused")
  truthy(atom_stream._has_listener("lifecycle"), "attached")
  atom_stream.remove_listener("lifecycle")
  truthy(not atom_stream._has_listener("lifecycle"), "detached")
end)

--------------------------------------------------------------------------------
-- Input-driven cases (coroutine-sequenced).
--------------------------------------------------------------------------------

---@type { atom: VF.Atom, verdict: VF.Atom.Verdict }[]
local captured = {}
atom_stream.add_listener("recorder", function(atom, verdict)
  captured[#captured + 1] = { atom = atom, verdict = verdict }
end)

local co
local function resume() if coroutine.status(co) ~= "dead" then coroutine.resume(co) end end
local function sleep(ms) vim.defer_fn(resume, ms) coroutine.yield() end

local function reset_buf()
  local buf = vim.api.nvim_create_buf(true, false)
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, { "alpha beta gamma", "delta eps", "zeta" })
  vim.api.nvim_set_current_buf(buf)
  vim.api.nvim_win_set_cursor(0, { 1, 0 })
  return buf
end

--- Feed input, then yield long enough for the loop to drain the atom queue.
---@param keys string
local function feed(keys)
  captured = {}
  vim.api.nvim_input(keys)
  sleep(80)
end

---@return string  concat of every "record"-verdict atom's keys
local function recorded_keys()
  local parts = {}
  for _, e in ipairs(captured) do
    if e.verdict == "record" then parts[#parts + 1] = e.atom.keys end
  end
  return table.concat(parts)
end

local function run_cases()
  check("one command yields one record atom scoped to buf/win", function()
    local buf = reset_buf()
    local win = vim.api.nvim_get_current_win()
    feed("dw")
    eq(#captured, 1, "one atom")
    eq(captured[1].verdict, "record", "verdict")
    eq(captured[1].atom.keys, "dw", "keys")
    eq(captured[1].atom.buf, buf, "atom.buf")
    eq(captured[1].atom.win, win, "atom.win")
  end)

  check("synthetic count=1 is stripped; genuine counts kept", function()
    reset_buf(); feed("ifoo<Esc>");  eq(recorded_keys(), "ifoo<Esc>", "no synthetic 1i")
    reset_buf(); feed("2ifoo<Esc>"); eq(recorded_keys(), "2ifoo<Esc>", "genuine count kept")
    reset_buf(); feed("i1<Esc>");    eq(recorded_keys(), "i1<Esc>", "content digit kept")
  end)

  finish()
end

co = coroutine.create(run_cases)
vim.schedule(resume)
