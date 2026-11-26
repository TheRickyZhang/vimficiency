-- Smoke tests for the `:Vimfy reload` Lua path.

local vimfy = require("vimficiency")
local h = require("_helpers")

test("reload: shows output before exit and keeps a failed build log", function()
  vimfy.setup({})
  local api = vim.api
  local edit_win = api.nvim_get_current_win()
  local jobstart = vim.fn.jobstart
  local job
  local starts = 0
  local message

  h.with_patch({
    { vim.fn, "filereadable", function() return 1 end },
    { vim.fn, "jobstart", function(_, opts)
      starts = starts + 1
      job = jobstart({
        "sh", "-c", "seq 20; echo '[1/2] Compiling'; read -r line; echo 'compiler error'; exit 3",
      }, opts)
      return job
    end },
    { vim, "notify", function(msg) message = msg end },
  }, function()
    vim.cmd("Vimfy reload")
    local win = vim.fn.win_getid(vim.fn.winnr("$"))
    local buf = api.nvim_win_get_buf(win)
    local function chrome()
      local cfg = api.nvim_win_get_config(win)
      return cfg.title[1][1], cfg.footer[1][1]
    end
    assert_eq(api.nvim_get_current_win(), win)
    assert_eq(api.nvim_win_get_config(win).relative, "editor")
    local title, footer = chrome()
    assert_eq(title, " Vimfy reload · Building… ")
    assert_eq(footer, " q hides this window · the build keeps running in the background ")
    local function log_contains(line)
      return table.concat(api.nvim_buf_get_lines(buf, 0, -1, false), "\n"):find(line, 1, true) ~= nil
    end
    assert_true(vim.wait(2000, function() return log_contains("[1/2] Compiling") end))
    assert_eq(api.nvim_win_get_cursor(win)[1], api.nvim_buf_line_count(buf))
    assert_eq(message, nil)

    api.nvim_win_hide(win)
    assert_eq(api.nvim_get_current_win(), edit_win)
    vim.cmd("Vimfy reload")
    win = vim.fn.bufwinid(buf)
    assert_true(win ~= -1)
    assert_eq(api.nvim_win_get_cursor(win)[1], api.nvim_buf_line_count(buf))
    assert_eq(starts, 1)

    api.nvim_chan_send(job, "\n")
    assert_true(vim.wait(2000, function() return message ~= nil end))
    assert_eq(message, "Vimfy build failed (exit 3).")
    title, footer = chrome()
    assert_eq(title, " Vimfy reload · Failed ")
    assert_eq(footer, " q closes this window ")
    assert_true(vim.wait(2000, function() return log_contains("compiler error") end))
    api.nvim_buf_delete(buf, { force = true })
  end)
end)

test("reload: setup is idempotent (two calls in a row don't crash)", function()
  vimfy.setup({})
  vimfy.setup({})
  assert_true(vim.fn.exists(":Vimfy") == 2, ":Vimfy still defined after second setup")
end)

test("reload: repeated setup re-arms auto-suggest from the new config", function()
  local auto_suggest = require("vimficiency.capture.auto_suggest")
  local atom_stream = require("vimficiency.capture.atom_stream")

  vimfy.setup({ auto_suggest = { idle = { ms = 3000, window = "3s" } } })
  assert_true(auto_suggest.is_enabled(), "idle trigger armed")

  vimfy.setup({ auto_suggest = { keys = { every = 50 } } })
  assert_true(atom_stream._has_listener("auto_suggest_keys"), "keys trigger armed by second setup")
  assert_true(not atom_stream._has_listener("auto_suggest_idle"), "idle trigger torn down")

  vimfy.setup({ auto_suggest = false })
  assert_true(not auto_suggest.is_enabled(), "auto_suggest = false disarms everything")
  assert_true(not atom_stream._has_listener("auto_suggest_keys"), "keys trigger torn down")
end)

test("reload: shutdown detaches atom subscribers", function()
  vimfy.setup({})
  local atom_stream = require("vimficiency.capture.atom_stream")
  assert_true(atom_stream._has_listener("recall_capture"),
    "recall_capture subscriber present after setup")
  vimfy.shutdown()
  assert_true(not atom_stream._has_listener("recall_capture"),
    "recall_capture subscriber gone after shutdown")
end)

test("reload: shutdown releases pending and ready Explore views", function()
  local explore = require("vimficiency.explore")
  local registry = require("vimficiency.explore.runtime.registry")
  local flow = require("explore._helpers")
  local result = flow.fake_result()
  h.new_buf(result.lines)
  h.silence_notify(function() explore.open("ready", result) end)
  local ready = registry.current()
  assert_true(vim.wait(5000, function() return ready.view_id ~= nil end))
  h.silence_notify(function() explore.open("pending", result) end)
  local pending = registry.current()
  local ready_group, pending_group = ready.augroup, pending.augroup
  assert_eq(pending.view_id, nil)

  vimfy.shutdown()

  assert_true(not registry.has_any())
  assert_true(pending.poll_handle.cancelled)
  assert_eq(pcall(vim.api.nvim_get_autocmds, { group = ready_group }), false)
  assert_eq(pcall(vim.api.nvim_get_autocmds, { group = pending_group }), false)
  assert_true(vim.wait(1000, function()
    return not vim.api.nvim_tabpage_is_valid(ready.scratch.tab)
      and not vim.api.nvim_tabpage_is_valid(pending.scratch.tab)
  end))
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
  local feedkeys = vim.api.nvim_feedkeys

  local stats = vimfy.reload_lua(nil)
  assert_true(stats.preserved_records >= 1,
    "reload reports >=1 preserved record; got " .. tostring(stats.preserved_records))

  local post_store = require("vimficiency.session.store")
  assert_true(pre_store ~= post_store,
    "session.store module table identity changed across reload")
  assert_eq(vim.api.nvim_feedkeys, feedkeys, "input wrappers are reused across reload")

  assert_true(post_store.get_result("@") ~= nil,
    "finished record survived reload")
end)
