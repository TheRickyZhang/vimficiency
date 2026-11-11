-- Smoke tests for the `:Vimfy reload` Lua path.

local vimfy = require("vimficiency")
local h = require("_helpers")

test("reload: shutdown detaches atom subscribers", function()
  vimfy.setup({})
  local atom_stream = require("vimficiency.capture.atom_stream")
  assert_true(atom_stream._has_listener("recall_capture"),
    "recall_capture subscriber present after setup")
  vimfy.shutdown()
  assert_true(not atom_stream._has_listener("recall_capture"),
    "recall_capture subscriber gone after shutdown")
end)

test("reload: reload_lua swaps module identities and preserves finished records", function()
  vimfy.setup({})

  local store = require("vimficiency.session.store")
  local id = "reload-test-id"
  local stash = store.dump_for_reload()
  stash.finished[id] = {
    id = id,
    kind = "mark",
    started_mono_ns = vim.uv.hrtime(),
    finished_mono_ns = vim.uv.hrtime(),
    result = h.fake_result({ lines = { "hello" }, user_seq = "" }),
  }
  stash.last_finished = id
  store.restore_from_dump(stash)
  assert_true(store.get_result("@") ~= nil,
    "seeded finished record is visible before reload")

  local pre_store = store

  local stats = vimfy.reload_lua(nil)
  assert_true(stats.preserved_records >= 1,
    "reload reports >=1 preserved record; got " .. tostring(stats.preserved_records))

  local post_store = require("vimficiency.session.store")
  assert_true(pre_store ~= post_store,
    "session.store module table identity changed across reload")

  assert_true(post_store.get_result("@") ~= nil,
    "finished record survived reload")
end)
