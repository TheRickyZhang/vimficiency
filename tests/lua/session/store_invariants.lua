-- Store-level invariants that do not belong to a single feature.

local session       = require("vimficiency.session")
local session_store = require("vimficiency.session.store")
local h             = require("_helpers")

test("get_active('Ns'): returns nil when ring is younger than N seconds", function()
  local _, buf, id = h.seed_recall()

  assert_eq(session_store.get_active("3s"), nil,
    "`3s` against a queue with a single ~0s-old record must not resolve")

  session_store.remove(id)
  pcall(vim.api.nvim_buf_delete, buf, { force = true })
end)

test("get_active('1s'): resolves when a record is older than the window", function()
  local _, buf, id = h.seed_recall({ backdate_s = 5 })

  local resolved = session_store.get_active("1s")
  assert_true(resolved ~= nil, "`1s` must resolve when a 5s-old record exists")
  resolved = assert(resolved)
  assert_eq(resolved.id, id)

  session_store.remove(id)
  pcall(vim.api.nvim_buf_delete, buf, { force = true })
end)

test("remove: no-op when the id is unknown", function()
  session_store.remove("NoName__20250101-000000-000__1__0001__b1")
end)

test("finish failure on a recall record preserves the ring slice", function()
  local _, buf_a, id = h.seed_recall({ backdate_s = 5 })

  local buf_b = h.new_buf({ "y" })

  h.silence_notify(function()
    assert_error(function() session.recall("3s") end, "buf " .. buf_b,
      "recall from another buffer must raise, naming the current buffer")
  end)

  assert_true(session_store.summarize(id) ~= nil,
    "recall record must survive a finish() failure")

  session_store.remove(id)
  pcall(vim.api.nvim_buf_delete, buf_a, { force = true })
  pcall(vim.api.nvim_buf_delete, buf_b, { force = true })
end)

test("has_room_for_named: finished records do not consume capacity slots", function()
  h.new_buf()
  for _, alias in ipairs({ "capa", "capb", "capc", "capd", "cape" }) do
    session.start(alias)
    local rec = session_store.get_active(alias)
    assert_true(rec ~= nil, "start failed for " .. alias)
    rec = assert(rec)
    session_store.convert_to_finished(rec.id, h.fake_result({ user_seq = "" }), alias)
  end
  assert_eq(session_store.has_room_for_named("capf"), true,
    "a new alias must be allowed when all existing records are finished")
end)

test("finish from another buffer raises and keeps the mark record for a re-run", function()
  local buf_a = h.new_buf()
  session.start("kfinishfail")
  assert_true(session_store.get_active("kfinishfail") ~= nil,
    "mark session should exist after start")

  local buf_b = h.new_buf({ "y" })
  h.silence_notify(function()
    assert_error(function() session.finish("kfinishfail") end, "buf " .. buf_b,
      "finish from another buffer must raise, naming the current buffer")
  end)

  assert_true(session_store.get_active("kfinishfail") ~= nil,
    "mark record must survive so the user can refocus and re-run")

  pcall(vim.api.nvim_buf_delete, buf_a, { force = true })
  pcall(vim.api.nvim_buf_delete, buf_b, { force = true })
end)
