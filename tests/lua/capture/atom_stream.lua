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

check("_describe_abort names the culprit and its effect", function()
  local d = atom_stream._describe_abort
  eq(d("excmd", ":m .+1<NL>", ":m .+1<NL>", ":m .+1<NL>", true, true),
    "ex command `:m .+1` changed the buffer and moved the cursor", "ex edit")
  eq(d("excmd", ":5<NL>", ":5<NL>", ":5<NL>", false, true),
    "ex command `:5` moved the cursor", "ex jump")
  eq(d("mapping", nil, "", "<Space>V", false, true),
    "Lua mapping `<Space>V` (lossy_mappings = \"warn\" or \"allow\" records its typed keys instead) moved the cursor",
    "lua cb")
  eq(d("operator", "", "", "g/", true, false),
    "mapping `g/` with no replayable key expansion changed the buffer", "lossy mapping")
  eq(d("mouse", "<LeftMouse>", "<LeftMouse>", "<LeftMouse>", false, true),
    "mouse input `<LeftMouse>` moved the cursor", "mouse")
end)

check("_lossy_passthrough records effectful Lua callbacks only when permissive", function()
  local p = atom_stream._lossy_passthrough
  eq(p(nil, "f;", false, true, true), true, "lua cb moved, permissive")
  eq(p(nil, "f;", true, false, true), true, "lua cb changed, permissive")
  eq(p(nil, "f;", false, true, false), false, "lua cb moved, strict")
  eq(p(nil, "<Space>yp", false, false, true), false, "lua cb no-op stays skip")
  eq(p(nil, "", false, true, true), false, "no lhs to record")
  eq(p("", "g/", true, false, true), false, "unexpressible keys are not a lua cb")
  eq(p("dd", "dd", true, false, true), false, "replayable keys untouched")
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

---@type { atom: VF.Atom, verdict: VF.Atom.Decision }[]
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

  check("an atom carries the event's type as its kind", function()
    for typed, kind in pairs({ dw = "operator", w = "motion", ["ifoo<Esc>"] = "insert" }) do
      reset_buf(); feed(typed)
      eq(#captured, 1, typed .. ": one atom")
      eq(captured[1].atom.kind, kind, typed .. ": kind")
    end
  end)

  check("a nested reindent with reported keys preserves the capture", function()
    local buf = reset_buf()
    vim.keymap.set("i", "<F5>", "<CR>```<Cmd>normal! ====<CR><Up><End><CR>", { buffer = buf })
    feed("o```cpp<F5><Esc>")
    truthy(table.concat(vim.api.nvim_buf_get_lines(buf, 0, -1, false), "\n"):find("```cpp", 1, true),
      "the buffer contains the fence")
    eq(captured[#captured].verdict, "record", "keep the event and the observed result")
  end)

  check("synthetic count=1 is stripped; genuine counts kept", function()
    reset_buf(); feed("ifoo<Esc>");  eq(recorded_keys(), "ifoo<Esc>", "no synthetic 1i")
    reset_buf(); feed("2ifoo<Esc>"); eq(recorded_keys(), "2ifoo<Esc>", "genuine count kept")
    reset_buf(); feed("i1<Esc>");    eq(recorded_keys(), "i1<Esc>", "content digit kept")
  end)

  local config = require("vimficiency.config")
  local lossy_mappings = config.lossy_mappings
  vim.keymap.set("o", "Q", function() vim.fn.search("gamma") end)
  vim.keymap.set("o", "Z", "<Cmd>call search('gamma')<CR>")

  check("an operator over a Lua-mapped motion is recorded as typed, flagged lossy", function()
    config.lossy_mappings = "allow"
    for _, typed in ipairs({ "dQ", "dZ", "cQxy<Esc>" }) do
      reset_buf(); feed(typed)
      eq(#captured, 1, typed .. ": one atom")
      eq(captured[1].verdict, "record", typed .. ": verdict")
      eq(captured[1].atom.keys, typed, typed .. ": keys")
      eq(captured[1].atom.lossy, true, typed .. ": lossy")
    end
    reset_buf(); feed("2dQ")
    eq(captured[1].atom.keys, "2dQ", "count re-prefixed")
  end)

  check("an operator over a Lua-mapped motion aborts under lossy_mappings = drop", function()
    config.lossy_mappings = "drop"
    reset_buf(); feed("dQ")
    eq(captured[1].verdict, "abort", "verdict")
    eq(captured[1].atom.keys, "", "no keys")
    truthy(captured[1].atom.abort_reason:find("Lua mapping `dQ`", 1, true), captured[1].atom.abort_reason)
  end)

  config.lossy_mappings = lossy_mappings
  vim.keymap.del("o", "Q")
  vim.keymap.del("o", "Z")

  ---@return string  every atom's pressed keys, in order
  local function pressed_keys()
    local parts = {}
    for _, e in ipairs(captured) do parts[#parts + 1] = e.atom.typed end
    return table.concat(parts)
  end

  check("typed is what was pressed where keys is what ran", function()
    for typed, keys in pairs({ ["3x"] = "3dl", ["d2w"] = "2dw", ["J"] = "2J", ["ifoo<Esc>"] = "ifoo<Esc>" }) do
      reset_buf(); feed(typed)
      eq(#captured, 1, typed .. ": one atom")
      eq(captured[1].atom.typed, typed, typed .. ": typed")
      eq(captured[1].atom.keys, keys, typed .. ": keys")
    end
  end)

  check("typed keeps a mapping's own keys, its count, and text as pressed", function()
    vim.keymap.set("n", "gQ", "dw")
    vim.keymap.set("i", "(", "()<Left>")
    reset_buf(); feed("2gQ")
    eq(captured[1].atom.typed, "2gQ", "mapping with a count")
    eq(captured[1].atom.keys, "2dw", "resolved")
    reset_buf(); feed("ia(b<Esc>")
    eq(captured[1].atom.typed, "ia(b<Esc>", "insert mapping not expanded")
    eq(captured[1].atom.keys, "ia()<Left>b<Esc>", "resolved")
    vim.keymap.del("n", "gQ")
    vim.keymap.del("i", "(")
  end)

  check("typed_insert is the keys pressed inside an insert session", function()
    vim.keymap.set("i", "(", "()<Left>")
    vim.keymap.set("n", "gQ", "ofoo<Esc>")
    for typed, typed_insert in pairs({
      ["dw"] = "",
      ["ifoo<Esc>"] = "foo",
      ["cwa<BS>b<Esc>"] = "a<BS>b",
      ["ia(b<Left><Esc>"] = "a(b<Left>",
      ["Rxy<Esc>"] = "xy",
      -- The mapping typed its text; the user pressed nothing in Insert mode.
      ["gQ"] = "",
    }) do
      reset_buf(); feed(typed)
      eq(#captured, 1, typed .. ": one atom")
      eq(captured[1].atom.typed, typed, typed .. ": typed")
      eq(captured[1].atom.typed_insert, typed_insert, typed .. ": typed_insert")
    end
    vim.keymap.del("i", "(")
    vim.keymap.del("n", "gQ")
  end)

  ---@return string  every atom's insert text, in order
  local function pressed_insert()
    local parts = {}
    for _, e in ipairs(captured) do parts[#parts + 1] = e.atom.typed_insert end
    return table.concat(parts)
  end

  check("typed_insert leaves out whichever press left Insert mode", function()
    vim.keymap.set("i", "jk", "<Esc>")
    vim.keymap.set("i", "<F2>", "<C-o>0")
    vim.keymap.set("i", "<F3>", function() vim.cmd("stopinsert") end)
    for typed, typed_insert in pairs({
      ["ifoojk"] = "foo",
      ["ia<F3>"] = "a",
      -- Back in Insert mode by the next press, so still a key pressed in it.
      ["ia<F2>b<Esc>"] = "a<F2>b",
      -- `<C-o>` lends one Normal command; the insert goes on after `w`.
      ["ia<C-o>wb<Esc>"] = "a<C-o>b",
      -- Delivered together: the first `<Esc>` is followed by a Normal-mode read.
      ["ifoo<Esc>ibar<Esc>"] = "foobar",
      -- `x` is read in Select mode and starts the insert. Neovim reports no atom
      -- for it, so the keys arrive with `l`.
      ["ghxy<Esc>l"] = "xy",
    }) do
      reset_buf(); feed(typed)
      eq(pressed_insert(), typed_insert, typed)
    end
    vim.keymap.del("i", "jk")
    vim.keymap.del("i", "<F2>")
    vim.keymap.del("i", "<F3>")

    -- Sent apart: an interrupt discards input still queued ahead of it.
    reset_buf(); feed("ifoo"); feed("<C-c>")
    eq(pressed_insert(), "foo", "<C-c>")

    -- No key caused this exit, so the text before it stays.
    reset_buf(); feed("ia"); vim.cmd("stopinsert"); sleep(80)
    eq(pressed_insert(), "a", "stopinsert from outside")
  end)

  -- flash.nvim's `f` and mini.ai's `i` read their target with getchar(), which
  -- `vim.on_key` never sees.
  check("typed includes keys a mapping read with getchar()", function()
    config.lossy_mappings = "allow"
    vim.keymap.set({ "n", "o", "x" }, "gF", function() vim.fn.search(vim.fn.getcharstr()) end)
    vim.keymap.set({ "o", "x" }, "gI", function() return "i" .. vim.fn.getcharstr() end, { expr = true })
    for _, typed in ipairs({ "gFg", "3gFa", "dgFg", "2dgFa", "vgFgd" }) do
      reset_buf(); feed(typed)
      eq(pressed_keys(), typed, typed)
    end
    -- Tripwire: Neovim gives on_key no `typed` for an expr mapping that calls
    -- getchar(), so its own keys are lost. Fails once Neovim reports them.
    for typed, pressed in pairs({ ygIw = "yw", vgIwd = "vwd", ["cgIwx<Esc>"] = "cwx<Esc>" }) do
      reset_buf(); feed(typed)
      eq(pressed_keys(), pressed, typed)
    end
    vim.keymap.del({ "n", "o", "x" }, "gF")
    vim.keymap.del({ "o", "x" }, "gI")
    config.lossy_mappings = lossy_mappings
  end)

  -- which-key reads the keys after a trigger with getchar() and feeds them back
  -- as typed, so Neovim reads them twice.
  check("typed counts keys a plugin read and fed back once", function()
    vim.keymap.set("n", "Z", function()
      local count = vim.v.count > 0 and tostring(vim.v.count) or ""
      vim.api.nvim_feedkeys(count .. "d" .. vim.fn.getcharstr(), "mit", false)
    end)
    for typed, keys in pairs({ Zw = "dw", ["2Zw"] = "2dw" }) do
      reset_buf(); feed(typed)
      eq(pressed_keys(), typed, typed .. ": typed")
      eq(recorded_keys(), keys, typed .. ": keys")
    end
    vim.keymap.del("n", "Z")
  end)

  check("atoms delivered together leave their pressed keys on the first", function()
    reset_buf(); feed("dwdw")
    eq(#captured, 2, "two atoms")
    eq(captured[1].atom.typed, "dwdw", "first carries the burst")
    eq(captured[2].atom.typed, "", "second carries none")
  end)

  check("recorded replay and search share visited lines and complete input bursts", function()
    local buf = reset_buf()
    local lines = {}
    for i = 1, 40 do lines[i] = "line " .. i end
    vim.api.nvim_buf_set_lines(buf, 0, -1, false, lines)
    vim.api.nvim_win_set_cursor(0, { 8, 0 })
    local session = require("vimficiency.session")
    local store = require("vimficiency.session.store")
    local compute = require("vimficiency.session.compute")
    local disk = require("vimficiency.session.disk")
    require("vimficiency.capture.recall").start_continuous_capture()
    session.start("observed")
    feed("30G")
    feed("Vd")
    feed("u")
    feed("8G")
    feed("G8Gx")
    feed("ll")
    local recall = assert(store.get_active("1"))
    eq(#recall.atoms, 2, "recall inside a burst includes both commands")
    eq(recall.start_state.col, 0, "start before the burst")
    local active = assert(store.get_active("observed"))
    local result = compute.compute_result_for_active(active, compute.capture_end_state(active))
    local recording = assert(result.recording)
    eq(#recording.steps, 6, "one frame per delivered burst")
    eq(recording.steps[2].keys, "Vd", "record visual delete directly")
    eq(recording.steps[6].keys, "ll", "keep the burst together")
    eq(recording.steps[6].col, 2, "only the observed landing")
    eq(result.lines[#result.lines], "line 40", "visit and return within an editing burst")
    eq(result.lines[recording.steps[1].row + 1], "line 30", "the search includes the excursion")
    local user = assert(require("vimficiency.simulate.pool").build({
      user = { recording = recording }, suggestions = {},
    }, result.lines).user)
    local local_row = recording.steps[1].row + 1
    eq(user.states[3].lines[local_row], "line 31", "line after the visual deletion")
    eq(user.states[4].lines[local_row], "line 30", "undo restores the deleted line")
    eq(user.states[7].lines, result.goal_lines, "recorded final slice")
    result.kind = "mark"
    disk.write_pin("observed", result)
    eq(assert(disk.load("pinned", "observed")).recording, recording, "a pin preserves the steps")
    session.close("observed")
  end)

  -- Neovim emits a nil-keys `mapping` atom for a mapped move inside a selection,
  -- then a Visual atom whose keys already hold every move.
  check("a mapping run inside Visual is recorded once", function()
    vim.keymap.set({ "n", "x" }, "j", "v:count == 0 ? 'gj' : 'j'", { expr = true })
    for _, policy in ipairs({ "allow", "drop" }) do
      config.lossy_mappings = policy
      reset_buf(); feed("Vjjy")
      eq(recorded_keys(), "Vgjgjy", policy .. ", burst: keys")
      eq(pressed_keys(), "Vjjy", policy .. ", burst: typed")

      reset_buf()
      captured = {}
      for key in ("Vjjy"):gmatch(".") do
        vim.api.nvim_input(key)
        sleep(40)
      end
      eq(recorded_keys(), "Vgjgjy", policy .. ", typed slowly: keys")
      eq(pressed_keys(), "Vjjy", policy .. ", typed slowly: typed")
      for _, e in ipairs(captured) do truthy(e.verdict ~= "abort", policy .. ": no abort") end
    end
    vim.keymap.del({ "n", "x" }, "j")
    config.lossy_mappings = lossy_mappings
  end)

  finish()
end

co = coroutine.create(run_cases)
vim.schedule(resume)
