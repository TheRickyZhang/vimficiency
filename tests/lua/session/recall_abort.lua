-- Recall records are per buffer, and an abort atom ends every active one in
-- its buffer while reseeding the predecessor snapshot from the post-abort state.

local session_store = require("vimficiency.session.store")
local h             = require("_helpers")

---@param buf integer
---@return VF.Atom
local function atom_in(buf)
  return { keys = "x", buf = buf, win = vim.api.nvim_get_current_win() }
end

test("abort_recall: drops the buffer's active records and leaves other buffers alone", function()
  local _, buf_a, id_a = h.seed_recall()
  local _, buf_b, id_b = h.seed_recall()
  local win = vim.api.nvim_get_current_win()

  vim.api.nvim_win_set_buf(win, buf_a)
  session_store.abort_recall(buf_a, win)

  assert_eq(session_store.summarize(id_a), nil, "record in aborted buffer must be gone")
  assert_true(session_store.summarize(id_b) ~= nil, "record in other buffer must survive")

  session_store.remove(id_b)
  pcall(vim.api.nvim_buf_delete, buf_a, { force = true })
  pcall(vim.api.nvim_buf_delete, buf_b, { force = true })
end)

test("abort_recall: the next atom starts a record from the post-abort text", function()
  local buf = h.new_buf({ "before" })
  local win = vim.api.nvim_get_current_win()

  vim.api.nvim_buf_set_lines(buf, 0, -1, false, { "after" })
  session_store.abort_recall(buf, win)
  session_store.ingest_recall_atom(atom_in(buf))

  local rec = session_store.get_active("1")
  assert_true(rec ~= nil, "abort reseeds the snapshot, so the first atom after it spawns a record")
  rec = assert(rec)
  assert_eq(rec.buf, buf)
  assert_eq(rec.start_state.lines[1], "after")

  session_store.remove(rec.id)
  pcall(vim.api.nvim_buf_delete, buf, { force = true })
end)

test("ingest_recall_atom: an atom extends only its own buffer's records", function()
  local rec_a, buf_a, id_a = h.seed_recall()
  local rec_b, buf_b, id_b = h.seed_recall()

  vim.api.nvim_win_set_buf(vim.api.nvim_get_current_win(), buf_a)
  session_store.ingest_recall_atom(atom_in(buf_a))

  assert_eq(#rec_a.atoms, 1)
  assert_eq(#rec_b.atoms, 0, "atom in buf_a must not reach buf_b's record")

  session_store.remove(id_a)
  session_store.remove(id_b)
  pcall(vim.api.nvim_buf_delete, buf_a, { force = true })
  pcall(vim.api.nvim_buf_delete, buf_b, { force = true })
end)

test("recall listener: drops an atom whose window no longer shows its buffer", function()
  local recall = require("vimficiency.capture.recall")
  local shown = h.new_buf({ "shown" })
  local other = h.new_buf({ "other" })
  local win = vim.api.nvim_get_current_win()
  vim.api.nvim_win_set_buf(win, shown)

  local before = #session_store.summarize_all()
  recall._for_test.listener({ keys = "x", buf = other, win = win }, "record")
  assert_eq(#session_store.summarize_all(), before, "a buffer/window mismatch must not record")

  pcall(vim.api.nvim_buf_delete, shown, { force = true })
  pcall(vim.api.nvim_buf_delete, other, { force = true })
end)
