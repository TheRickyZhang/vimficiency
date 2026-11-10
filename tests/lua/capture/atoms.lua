-- Characterization of capture/atoms.lua against the adversarial mapping set.
--
-- CmdAtom (Neovim 0.13-dev+) is a *deferred* event: it only fires once control
-- returns to the main loop, and `nvim_input` typeahead is only consumed by a
-- running loop. Neither happens under `nvim -l` (the main `run.sh` batch), so
-- this file is a self-contained async script run in its own process via
-- `-c luafile` under a nightly `$VF_NVIM` — the same pattern as
-- `simulate/integration.lua`. See dev/lua/atom-capture.md.
--
-- Driven by a coroutine: each case feeds real input, yields to a timer so the
-- loop drains the atom queue, then asserts. The `-l` runner skips this path
-- (see tests/lua/runner.lua `isolated`), and under a non-nightly Neovim the
-- script reports a clean skip instead of failing.

local script = debug.getinfo(1, "S").source:sub(2)
local this_dir = script:match("(.*/)")
local plugin_root = this_dir .. "../../.."
vim.opt.rtp:prepend(plugin_root)

local atoms = require("vimficiency.capture.atoms")

--------------------------------------------------------------------------------
-- Minimal harness (standalone; the shared runner globals aren't present here).
--------------------------------------------------------------------------------

local passed, failed = 0, 0
---@type { name: string, err: string }[]
local failed_list = {}

local function record_pass() passed = passed + 1 end
local function record_fail(name, err)
  failed = failed + 1
  failed_list[#failed_list + 1] = { name = name, err = tostring(err) }
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

local function finish()
  local total = passed + failed
  io.stdout:write(string.format("[==========] atoms: %d/%d passed\n", passed, total))
  if failed > 0 then
    io.stderr:write(string.format("%d FAILED (atoms)\n", failed))
  end
  if vim.env.VF_TEST_SUMMARY_FILE and vim.env.VF_TEST_SUMMARY_FILE ~= "" then
    local fh = io.open(vim.env.VF_TEST_SUMMARY_FILE, "a")
    if fh then
      fh:write(string.format("passed=%d failed=%d tests=%d\n", passed, failed, total))
      fh:close()
    end
  end
  vim.cmd((failed == 0) and "cquit 0" or "cquit 1")
end

if not atoms.available() then
  io.stdout:write("[==========] atoms: skipped — this Neovim has no ##CmdAtom\n")
  if vim.env.VF_TEST_SUMMARY_FILE and vim.env.VF_TEST_SUMMARY_FILE ~= "" then
    local fh = io.open(vim.env.VF_TEST_SUMMARY_FILE, "a")
    if fh then fh:write("passed=0 failed=0 tests=0\n"); fh:close() end
  end
  vim.cmd("cquit 0")
  return
end

--------------------------------------------------------------------------------
-- Classification table (pure; runs before any input).
--------------------------------------------------------------------------------

check("_classify verdict table", function()
  local c = atoms._classify
  eq(c("operator", "dd", "dd", true, false), "record", "operator")
  eq(c("motion", "j", "j", false, true), "record", "motion")
  eq(c("normal", "<Esc>", "<Esc>", false, false), "record", "normal no-op")
  eq(c("mapping", nil, "", false, false), "skip", "lua cb no-op")
  eq(c("mapping", nil, "", true, false), "abort", "lua cb changed")
  eq(c("mapping", nil, "", false, true), "abort", "lua cb moved")
  eq(c("operator", "", "", true, false), "abort", "unreplayable")
  eq(c("excmd", ":5<NL>", ":5<NL>", false, true), "skip", "ex jump")
  eq(c("excmd", ":s/e/E/<NL>", ":s/e/E/<NL>", true, true), "abort", "ex edit")
  eq(c("mouse", "<LeftMouse>", "<LeftMouse>", false, true), "mouse", "mouse token")
end)

check("subscribe refuses duplicate name; unsubscribe tears down", function()
  truthy(atoms.subscribe("lifecycle", function() end), "first subscribe")
  eq(atoms.subscribe("lifecycle", function() end), false, "duplicate refused")
  truthy(atoms.is_subscribed("lifecycle"), "attached")
  atoms.unsubscribe("lifecycle")
  truthy(not atoms.is_subscribed("lifecycle"), "detached")
end)

--------------------------------------------------------------------------------
-- Input-driven cases (coroutine-sequenced).
--------------------------------------------------------------------------------

---@type { atom: VF.Atom, verdict: VF.Atom.Verdict }[]
local captured = {}
atoms.subscribe("recorder", function(atom, verdict)
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

---@return string[]  "<verdict>:<keys>" per captured atom, in order
local function verdicts()
  local out = {}
  for _, e in ipairs(captured) do out[#out + 1] = e.verdict .. ":" .. e.atom.keys end
  return out
end

local function with_map(mode, lhs, rhs, opts, fn)
  pcall(vim.keymap.del, mode, lhs)
  vim.keymap.set(mode, lhs, rhs, opts)
  local ok, err = pcall(fn)
  pcall(vim.keymap.del, mode, lhs)
  if not ok then error(err, 0) end
end

local function run_cases()
  check("plain operator/motion/insert resolve to one clean unit", function()
    reset_buf(); feed("dw");          eq(recorded_keys(), "dw", "dw")
    reset_buf(); feed("3j");          eq(recorded_keys(), "3j", "counted motion")
    reset_buf(); feed("ciwfoo<Esc>"); eq(recorded_keys(), "ciwfoo<Esc>", "change + text")
  end)

  check("synthetic count=1 is stripped; genuine counts kept", function()
    reset_buf(); feed("ifoo<Esc>")
    eq(recorded_keys(), "ifoo<Esc>", "no synthetic 1i")
    eq(captured[1].atom.lhs, "ifoo<Esc>", "lhs stripped too")
    reset_buf(); feed("rX"); eq(recorded_keys(), "rX", "no synthetic 1r")
    reset_buf(); feed("2ifoo<Esc>"); eq(recorded_keys(), "2ifoo<Esc>", "genuine count kept")
    reset_buf(); feed("i1<Esc>"); eq(recorded_keys(), "i1<Esc>", "content digit kept")
  end)

  check("single-key mapping Z->dd records dd, keeps Z as lhs", function()
    with_map("n", "Z", "dd", nil, function()
      reset_buf(); feed("Z")
      eq(recorded_keys(), "dd", "mapped RHS recorded")
      eq(captured[1].atom.lhs, "Z", "lhs preserved for display")
    end)
  end)

  check("own-LHS remap d->dd resolves to dd", function()
    with_map("n", "d", "dd", nil, function()
      reset_buf(); feed("d"); eq(recorded_keys(), "dd", "own-LHS")
    end)
  end)

  check("counted mapping 3Z->dd folds count into keys", function()
    with_map("n", "Z", "dd", nil, function()
      reset_buf(); feed("3Z")
      eq(recorded_keys(), "3dd", "count folded into keys")
      eq(captured[1].atom.count, 3, "count exposed")
    end)
  end)

  check("lua-callback mapping that edits aborts", function()
    with_map("n", "Q", function() vim.cmd("normal! dd") end, nil, function()
      reset_buf(); feed("Q"); eq(verdicts(), { "abort:" }, "keys=nil + changed")
    end)
  end)

  check("lua-callback no-op mapping skips", function()
    with_map("n", "Q", function() end, nil, function()
      reset_buf(); feed("Q"); eq(verdicts(), { "skip:" }, "keys=nil + inert")
    end)
  end)

  check("unchanged <Cmd> mapping skips", function()
    vim.api.nvim_create_user_command("VfAtomsProbe", function() end, {})
    with_map("n", "Z", "<Cmd>VfAtomsProbe<CR>", nil, function()
      reset_buf(); feed("Z"); eq(verdicts(), { "skip:" }, "inert <Cmd>")
    end)
    pcall(vim.api.nvim_del_user_command, "VfAtomsProbe")
  end)

  check(":substitute aborts, bare :line jump skips", function()
    -- An aborting ex command still carries its keys; the verdict is what matters.
    reset_buf(); feed(":s/e/E/<CR>")
    eq(#captured, 1, "one ex-edit atom")
    eq(captured[1].verdict, "abort", "buffer-changing ex command → abort")
    reset_buf(); feed(":2<CR>")
    eq(#captured, 1, "one ex-jump atom")
    eq(captured[1].verdict, "skip", "cmdline jump → skip")
  end)

  check("operator-pending remap: onoremap w iw, dw -> diw", function()
    with_map("o", "w", "iw", nil, function()
      reset_buf(); feed("dw"); eq(recorded_keys(), "diw", "o-pending remap")
    end)
  end)

  check("visual remap: xnoremap Z d, vZ -> vd", function()
    with_map("x", "Z", "d", nil, function()
      reset_buf(); feed("vZ"); eq(recorded_keys(), "vd", "visual remap")
    end)
  end)

  check("atom carries originating buffer and focused window", function()
    local buf = reset_buf()
    local win = vim.api.nvim_get_current_win()
    feed("x")
    eq(captured[1].atom.buf, buf, "atom.buf")
    eq(captured[1].atom.win, win, "atom.win")
  end)

  finish()
end

co = coroutine.create(run_cases)
vim.schedule(resume)
