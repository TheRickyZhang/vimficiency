-- The journal must let a reader rebuild the buffer text at every atom from
-- snapshots and deltas alone. The oracle is a full copy taken at each atom.

local journal        = require("vimficiency.capture.journal")
local session_store  = require("vimficiency.session.store")
local config         = require("vimficiency.config")
local ffi_core       = require("vimficiency.ffi.core")
local ffi_versions   = require("vimficiency.ffi.versions")
local h              = require("_helpers")

---@param path string
---@return table[]  # `vf.journal.Record`
local function read_records(path)
  local fh = assert(io.open(path, "rb"))
  local bytes = fh:read("*a")
  fh:close()
  return ffi_core.decode_message("vf.journal.File", bytes).records
end

---@param records table[]
---@return string[]
local function kinds(records)
  local out = {}
  for _, r in ipairs(records) do out[#out + 1] = r.body end
  return out
end

--- Text of `buf` at each of its `kind` records, rebuilt from the journal.
---@param records table[]
---@param buf integer
---@param kind "atom"|"resync"
---@return string[][]
local function replay(records, buf, kind)
  local text, at = nil, {}
  for _, r in ipairs(records) do
    local body = r[r.body]
    if body.buf == buf then
      if r.body == "snapshot" then
        text = body.lines
      elseif r.body == "delta" then
        local before = assert(text, "journal delta precedes its buffer snapshot")
        local tail = vim.list_slice(before, body.first + body.old_count + 1)
        text = vim.list_slice(before, 1, body.first)
        vim.list_extend(text, body.lines)
        vim.list_extend(text, tail)
      elseif r.body == kind then
        at[#at + 1] = vim.deepcopy(assert(text, "journal " .. kind .. " precedes its buffer snapshot"))
      end
    end
  end
  return at
end

--- Runs `fn` against an open journal on a fresh buffer. Callbacks an earlier
--- test scheduled are drained first so none lands in this journal.
---@param fn fun(buf: integer, atom: VF.Atom)
---@return table[] records, integer buf
local function with_journal(fn)
  h.use_temp_data_home()
  local buf = h.new_buf({ "one", "two", "three" })
  vim.wait(5, function() return false end)
  journal.start()
  local path = assert(journal.path())
  local ok, err = pcall(fn, buf,
    { keys = "x", kind = "operator", typed = "x", typed_insert = "", buf = buf, win = vim.api.nvim_get_current_win(), lossy = false })
  journal.stop()
  vim.api.nvim_buf_delete(buf, { force = true })
  if not ok then error(err, 0) end
  return read_records(path), buf
end

test("journal: snapshot plus deltas rebuild the text at every atom", function()
  local expected = {}
  local records, buf_id = with_journal(function(buf, atom)
    local function step(decision)
      journal._for_test.listener(atom, decision)
      expected[#expected + 1] = vim.api.nvim_buf_get_lines(buf, 0, -1, false)
    end
    step("record")
    vim.api.nvim_buf_set_lines(buf, 1, 2, false, { "TWO", "two and a half" })
    step("record")
    vim.api.nvim_buf_set_lines(buf, 0, 1, false, {})
    vim.api.nvim_buf_set_lines(buf, -1, -1, false, { "four" })
    step("skip")
    vim.api.nvim_buf_set_lines(buf, 0, -1, false, {})
    step("abort")
    step("record")
  end)

  assert_eq(records[1].body, "header")
  assert_eq(records[1].header.version, ffi_versions.versions().journal_format)
  assert_eq(replay(records, buf_id, "atom"), expected)

  local decisions = {}
  for _, r in ipairs(records) do
    if r.body == "atom" then decisions[#decisions + 1] = r.atom.decision end
  end
  assert_eq(decisions, { "record", "record", "skip", "abort", "record" })
end)

test("journal: an atom carries its keys, kind, mode and the cursor after it", function()
  local records = with_journal(function(_, atom)
    vim.api.nvim_win_set_cursor(0, { 2, 1 })
    atom.keys = "jl"
    atom.kind = "motion"
    atom.typed = "<Space>m"
    atom.typed_insert = "x<BS>"
    journal._for_test.listener(atom, "record")
  end)
  local atom = records[#records].atom
  assert_eq(atom.keys, "jl")
  assert_eq(atom.kind, "motion")
  assert_eq(atom.typed, "<Space>m")
  assert_eq(atom.typed_insert, "x<BS>")
  assert_eq(atom.mode, "n")
  assert_eq({ atom.view.row, atom.view.col }, { 1, 1 })
  assert_eq(atom.lossy, false)
end)

test("journal: recall marking first still yields every delta once", function()
  local expected = {}
  local records, buf_id = with_journal(function(buf, atom)
    for i = 1, 4 do
      vim.api.nvim_buf_set_lines(buf, 0, 1, false, { "edit " .. i })
      session_store.ingest_recall_atom(atom)
      journal._for_test.listener(atom, "record")
      expected[#expected + 1] = vim.api.nvim_buf_get_lines(buf, 0, -1, false)
    end
  end)
  assert_eq(replay(records, buf_id, "atom"), expected)
end)

test("journal: a full snapshot recurs every snapshot_every atoms", function()
  h.with_patch({ { config.journal, "snapshot_every", 2 } }, function()
    local records = with_journal(function(_, atom)
      for _ = 1, 5 do journal._for_test.listener(atom, "record") end
    end)
    assert_eq(kinds(records),
      { "header", "snapshot", "atom", "atom", "snapshot", "atom", "atom", "snapshot", "atom" })
  end)
end)

test("journal: a reload resnapshots instead of emitting a delta", function()
  local path = vim.fn.tempname()
  vim.fn.writefile({ "on disk" }, path)
  h.use_temp_data_home()
  vim.cmd("edit " .. vim.fn.fnameescape(path))
  local buf = vim.api.nvim_get_current_buf()
  vim.wait(5, function() return false end)
  journal.start()
  local journal_path = assert(journal.path())
  local atom = { keys = "x", typed = "x", buf = buf, win = vim.api.nvim_get_current_win(), lossy = false }

  vim.api.nvim_buf_set_lines(buf, 0, -1, false, { "unsaved" })
  journal._for_test.listener(atom, "record")
  vim.cmd("edit!")
  journal._for_test.listener(atom, "record")
  journal.stop()

  assert_eq(replay(read_records(journal_path), buf, "atom"), { { "unsaved" }, { "on disk" } })
  vim.api.nvim_buf_delete(buf, { force = true })
  vim.fn.delete(path)
end)

test("journal: a resync records the state before the first atom", function()
  local records, buf_id = with_journal(function(buf, atom)
    vim.api.nvim_win_set_cursor(0, { 3, 2 })
    journal._for_test.resync()
    vim.api.nvim_buf_set_lines(buf, 0, 1, false, { "ONE" })
    journal._for_test.listener(atom, "record")
  end)
  assert_eq(kinds(records), { "header", "snapshot", "resync", "delta", "atom" })
  assert_eq(replay(records, buf_id, "resync"), { { "one", "two", "three" } })
  assert_eq({ records[3].resync.view.row, records[3].resync.view.col }, { 2, 2 })
end)

test("journal: a resync repeats only once the state has moved on", function()
  local records, buf_id = with_journal(function(buf)
    journal._for_test.resync()
    journal._for_test.resync()
    vim.api.nvim_win_set_cursor(0, { 2, 0 })
    journal._for_test.resync()
    vim.api.nvim_buf_set_lines(buf, 0, 1, false, { "changed with no atom" })
    journal._for_test.resync()
  end)
  assert_eq(kinds(records), { "header", "snapshot", "resync", "resync", "delta", "resync" })
  assert_eq(replay(records, buf_id, "resync")[3], { "changed with no atom", "two", "three" })
end)

test("journal: no resync outside a file buffer", function()
  local records = with_journal(function()
    local scratch = vim.api.nvim_create_buf(false, true)
    vim.api.nvim_set_current_buf(scratch)
    journal._for_test.resync()
    vim.api.nvim_buf_delete(scratch, { force = true })
  end)
  assert_eq(kinds(records), { "header" })
end)

test("journal: an atom delivered through the autocmd window is left out", function()
  local records = with_journal(function()
    local hidden = vim.api.nvim_create_buf(true, false)
    vim.api.nvim_buf_set_lines(hidden, 0, -1, false, { "not in any window" })
    vim.api.nvim_buf_call(hidden, function()
      local win = vim.api.nvim_get_current_win()
      assert_eq(vim.fn.win_gettype(win), "autocmd")
      journal._for_test.listener(
        { keys = "x", kind = "operator", typed = "x", typed_insert = "", buf = hidden, win = win, lossy = false }, "record")
    end)
    vim.api.nvim_buf_delete(hidden, { force = true })
  end)
  assert_eq(kinds(records), { "header" })
end)

test("journal: entering a buffer resyncs once the event queue drains", function()
  local entered
  local records = with_journal(function()
    entered = h.new_buf({ "second buffer" })
    vim.wait(20, function() return false end)
    vim.api.nvim_buf_delete(entered, { force = true })
  end)
  local resyncs = {}
  for _, r in ipairs(records) do
    if r.body == "resync" then resyncs[#resyncs + 1] = r.resync.buf end
  end
  assert_true(h.list_contains(resyncs, entered), "the entered buffer has a resync")
end)

test("journal: records share a tick until the event queue drains", function()
  local records = with_journal(function(_, atom)
    journal._for_test.listener(atom, "record")
    journal._for_test.listener(atom, "record")
    journal._for_test.close_tick()
    journal._for_test.listener(atom, "record")
  end)
  local ticks = {}
  for _, r in ipairs(records) do
    if r.body == "atom" then ticks[#ticks + 1] = r.tick end
  end
  assert_eq(ticks[1], ticks[2])
  assert_true(ticks[3] > ticks[2], "a closed tick is not reused")
  assert_eq(kinds(records), { "header", "snapshot", "atom", "atom", "tick_end", "atom" })
end)

test("journal: a drained event queue ends the tick on record", function()
  local records = with_journal(function(_, atom)
    journal._for_test.listener(atom, "record")
    vim.wait(5, function() return false end)
  end)
  assert_eq(vim.list_slice(kinds(records), 1, 4), { "header", "snapshot", "atom", "tick_end" })
end)
