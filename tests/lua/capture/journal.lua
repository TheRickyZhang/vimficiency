-- The journal must let a reader rebuild the buffer text at every atom from
-- snapshots and deltas alone. The oracle is a full copy taken at each atom.

local journal        = require("vimficiency.capture.journal")
local session_store  = require("vimficiency.session.store")
local config         = require("vimficiency.config")
local h              = require("_helpers")

---@param path string
---@return table[]
local function read_records(path)
  local records = {}
  for _, line in ipairs(vim.fn.readfile(path)) do
    records[#records + 1] = vim.json.decode(line)
  end
  return records
end

--- Text of `buf` at each of its atom records, rebuilt from the journal.
---@param records table[]
---@param buf integer
---@return string[][]
local function replay(records, buf)
  local text, at_atoms = nil, {}
  for _, r in ipairs(records) do
    if r.buf == buf then
      if r.rec == "snapshot" then
        text = r.lines
      elseif r.rec == "delta" then
        local tail = vim.list_slice(text, r.first + r.old_count + 1)
        text = vim.list_slice(text, 1, r.first)
        vim.list_extend(text, r.lines)
        vim.list_extend(text, tail)
      elseif r.rec == "atom" then
        at_atoms[#at_atoms + 1] = vim.deepcopy(text)
      end
    end
  end
  return at_atoms
end

---@param fn fun(buf: integer, atom: VF.Atom)
---@return table[] records
local function with_journal(fn)
  h.use_temp_data_home()
  journal.start()
  local path = assert(journal.path())
  local buf = h.new_buf({ "one", "two", "three" })
  local ok, err = pcall(fn, buf,
    { keys = "x", buf = buf, win = vim.api.nvim_get_current_win(), lossy = false })
  journal.stop()
  vim.api.nvim_buf_delete(buf, { force = true })
  if not ok then error(err, 0) end
  return read_records(path)
end

test("journal: snapshot plus deltas rebuild the text at every atom", function()
  local expected = {}
  local buf_id
  local records = with_journal(function(buf, atom)
    buf_id = buf
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

  assert_eq(records[1].rec, "header")
  assert_eq(replay(records, buf_id), expected)

  local decisions = {}
  for _, r in ipairs(records) do
    if r.rec == "atom" then decisions[#decisions + 1] = r.decision end
  end
  assert_eq(decisions, { "record", "record", "skip", "abort", "record" })
end)

test("journal: recall marking first still yields every delta once", function()
  local expected = {}
  local buf_id
  local records = with_journal(function(buf, atom)
    buf_id = buf
    for i = 1, 4 do
      vim.api.nvim_buf_set_lines(buf, 0, 1, false, { "edit " .. i })
      session_store.ingest_recall_atom(atom)
      journal._for_test.listener(atom, "record")
      expected[#expected + 1] = vim.api.nvim_buf_get_lines(buf, 0, -1, false)
    end
  end)
  assert_eq(replay(records, buf_id), expected)
end)

test("journal: a full snapshot recurs every snapshot_every atoms", function()
  h.with_patch({ { config.journal, "snapshot_every", 2 } }, function()
    local records = with_journal(function(_, atom)
      for _ = 1, 5 do journal._for_test.listener(atom, "record") end
    end)
    local kinds = {}
    for _, r in ipairs(records) do kinds[#kinds + 1] = r.rec end
    assert_eq(kinds, { "header", "snapshot", "atom", "atom", "snapshot", "atom", "atom", "snapshot", "atom" })
  end)
end)

test("journal: a reload resnapshots instead of emitting a delta", function()
  local path = vim.fn.tempname()
  vim.fn.writefile({ "on disk" }, path)
  h.use_temp_data_home()
  journal.start()
  local journal_path = assert(journal.path())
  vim.cmd("edit " .. vim.fn.fnameescape(path))
  local buf = vim.api.nvim_get_current_buf()
  local atom = { keys = "x", buf = buf, win = vim.api.nvim_get_current_win(), lossy = false }

  vim.api.nvim_buf_set_lines(buf, 0, -1, false, { "unsaved" })
  journal._for_test.listener(atom, "record")
  vim.cmd("edit!")
  journal._for_test.listener(atom, "record")
  journal.stop()

  assert_eq(replay(read_records(journal_path), buf), { { "unsaved" }, { "on disk" } })
  vim.api.nvim_buf_delete(buf, { force = true })
  vim.fn.delete(path)
end)
