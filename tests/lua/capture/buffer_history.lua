-- `buffer_history` rebuilds an earlier buffer text from the current one plus the
-- lines replaced between marks. The oracle is a full snapshot taken at every mark.

local buffer_history = require("vimficiency.capture.buffer_history")
local session_store  = require("vimficiency.session.store")
local h              = require("_helpers")

local ESC, CTRL_R = "\27", "\18"

---@param buf integer
---@return string[]
local function snapshot(buf)
  return vim.api.nvim_buf_get_lines(buf, 0, -1, false)
end

---@type (fun(buf: integer))[]
local EDITS = {
  function() vim.cmd("silent! normal! dd") end,
  function() vim.cmd("silent! normal! x") end,
  function() vim.cmd("silent! normal! onew line" .. ESC) end,
  function() vim.cmd("silent! normal! Oabove" .. ESC) end,
  function() vim.cmd("silent! normal! J") end,
  function() vim.cmd("silent! normal! 3J") end,
  function() vim.cmd("silent! normal! yyp") end,
  function() vim.cmd("silent! normal! yjP") end,
  function() vim.cmd("silent! normal! >>") end,
  function() vim.cmd("silent! normal! 2dd") end,
  function() vim.cmd("silent! normal! gqq") end,
  function() vim.cmd("silent! normal! cwsplit" .. "\r" .. "here" .. ESC) end,
  function() vim.cmd("silent! normal! u") end,
  function() vim.cmd("silent! normal! " .. CTRL_R) end,
  function() vim.cmd("silent! move 0") end,
  function() vim.cmd("silent! move $") end,
  function() vim.cmd("silent! s/./X/e") end,
  function() vim.cmd("silent! %s/e/EE/ge") end,
  function() vim.cmd("silent! normal! ggdG") end,
  function(buf)
    local n = vim.api.nvim_buf_line_count(buf)
    local first = math.random(0, n)
    local last = math.random(first, n)
    local repl = {}
    for i = 1, math.random(0, 3) do repl[i] = "set " .. i end
    vim.api.nvim_buf_set_lines(buf, first, last, false, repl)
  end,
  function(buf)
    local row = math.random(0, vim.api.nvim_buf_line_count(buf) - 1)
    vim.api.nvim_buf_set_text(buf, row, 0, row, 0, { "head", "tail " })
  end,
}

test("lines_at: every earlier seq rebuilds to the snapshot taken there", function()
  math.randomseed(20261114)
  local buf = h.new_buf({ "alpha one", "beta two", "gamma three", "delta four", "epsilon five" })
  vim.bo[buf].textwidth = 12

  local marks = { { seq = buffer_history.mark(buf), lines = snapshot(buf) } }
  for _ = 1, 1000 do
    for _ = 1, math.random(1, 4) do
      local n = vim.api.nvim_buf_line_count(buf)
      vim.api.nvim_win_set_cursor(0, { math.random(1, n), 0 })
      local before = snapshot(buf)
      local op = math.random(1, #EDITS)
      EDITS[op](buf)
      if not pcall(buffer_history.lines_at, buf, 0) then
        error(("edit %d reset the history: %s -> %s"):format(op, vim.inspect(before), vim.inspect(snapshot(buf))), 0)
      end
    end
    marks[#marks + 1] = { seq = buffer_history.mark(buf), lines = snapshot(buf) }
  end

  for _, mark in ipairs(marks) do
    assert_eq(buffer_history.lines_at(buf, mark.seq), mark.lines, "seq " .. mark.seq)
  end

  local mid = marks[math.floor(#marks / 2)].seq
  buffer_history.trim(buf, mid)
  for _, mark in ipairs(marks) do
    if mark.seq >= mid then
      assert_eq(buffer_history.lines_at(buf, mark.seq), mark.lines, "after trim, seq " .. mark.seq)
    end
  end

  vim.api.nvim_buf_delete(buf, { force = true })
end)

test("mark: every change since the last mark folds into one seq step", function()
  local lines = {}
  for i = 1, 200 do lines[i] = (i % 2 == 0 and "x " or "keep ") .. i end
  local buf = h.new_buf(lines)

  local start = buffer_history.mark(buf)
  vim.cmd("silent g/^x/d")
  assert_eq(buffer_history.mark(buf), start + 1)
  assert_eq(buffer_history.mark(buf), start + 1, "a mark with nothing pending adds no step")
  assert_eq(buffer_history.lines_at(buf, start), lines)

  vim.api.nvim_buf_delete(buf, { force = true })
end)

test("recall records rebuild their own start text", function()
  local buf = h.new_buf({ "a" })
  local win = vim.api.nvim_get_current_win()
  local atom = { keys = "x", typed = "x", buf = buf, win = win, lossy = false }

  session_store.ingest_recall_atom(atom)
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, { "b", "b2" })
  session_store.ingest_recall_atom(atom)
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, { "c" })
  session_store.ingest_recall_atom(atom)

  local older = assert(session_store.get_active("2"))
  local newer = assert(session_store.get_active("1"))
  assert_eq(buffer_history.lines_at(buf, older.start_seq), { "a" })
  assert_eq(buffer_history.lines_at(buf, newer.start_seq), { "b", "b2" })

  vim.api.nvim_buf_delete(buf, { force = true })
end)

test("wiping a buffer drops its recall records", function()
  local _, buf, id = h.seed_recall()
  vim.api.nvim_buf_delete(buf, { force = true })
  assert_eq(session_store.summarize(id), nil)
end)

test("mark refuses an unloaded buffer, and records it once loaded", function()
  local path = vim.fn.tempname()
  vim.fn.writefile({ "on disk" }, path)
  local buf = vim.fn.bufadd(path)
  assert_eq(pcall(buffer_history.mark, buf), false)

  vim.fn.bufload(buf)
  local seq = buffer_history.mark(buf)
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, { "edited" })
  buffer_history.mark(buf)
  assert_eq(buffer_history.lines_at(buf, seq), { "on disk" })

  vim.api.nvim_buf_delete(buf, { force = true })
  vim.fn.delete(path)
end)

test("reload drops the buffer's recall records and reseeds on the next atom", function()
  local path = vim.fn.tempname()
  vim.fn.writefile({ "on disk" }, path)
  vim.cmd("edit " .. vim.fn.fnameescape(path))
  local buf = vim.api.nvim_get_current_buf()
  local win = vim.api.nvim_get_current_win()
  local _, _, id = h.seed_recall({ buf = buf })

  vim.api.nvim_buf_set_lines(buf, 0, -1, false, { "unsaved" })
  vim.cmd("edit!")
  assert_eq(session_store.summarize(id), nil)

  local atom = { keys = "x", typed = "x", buf = buf, win = win, lossy = false }
  session_store.ingest_recall_atom(atom)
  session_store.ingest_recall_atom(atom)
  local rec = assert(session_store.get_active("1"))
  assert_eq(buffer_history.lines_at(buf, rec.start_seq), { "on disk" })

  vim.api.nvim_buf_delete(buf, { force = true })
  vim.fn.delete(path)
end)
