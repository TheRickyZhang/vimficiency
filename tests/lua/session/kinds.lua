-- tests/lua/session/kinds.lua
-- One enum, `VF.Session.Kind`, says how a session came to be and how it ends.
-- Each creation path must stamp it truthfully, and only auto_suggest may change
-- it, by promoting a recall record to the suggest_* that fired.
--
--   mark      via session.start
--   watch     via session.watch
--   recall    via session_store.ingest_recall_atom
--   suggest_* a recall record promoted at convert_to_finished

local config        = require("vimficiency.config")
local session       = require("vimficiency.session")
local session_store = require("vimficiency.session.store")
local h             = require("_helpers")

local WATCH_CFG = { idle = { ms = 60000 }, cooldown_ms = 0 }

local function call_new_active(kind)
  local win = vim.api.nvim_get_current_win()
  local buf = vim.api.nvim_get_current_buf()
  return session_store.new_active_session(
    "id1", nil,
    win,
    buf,
    {
      row = 0,
      col = 0,
      lines = {},
      bufname = vim.api.nvim_buf_get_name(buf),
      filetype = vim.bo[buf].filetype,
      top_row = 0,
      bottom_row = 0,
      window_height = vim.api.nvim_win_get_height(win),
      scroll_amount = vim.api.nvim_get_option_value("scroll", { win = win }),
    },
    kind
  )
end

test("new_active_session: accepts only creatable kinds", function()
  for _, kind in ipairs({ "mark", "watch", "recall" }) do
    assert_eq(call_new_active(kind).kind, kind)
  end
  assert_error(function() call_new_active("bogus") end, "kind")
  assert_error(function() call_new_active("suggest_idle") end, "kind",
    "suggest_* is reached only by promotion")
end)

test("session_type: folds the suggest triggers", function()
  assert_eq(session_store.session_type("mark"),         "mark")
  assert_eq(session_store.session_type("watch"),        "watch")
  assert_eq(session_store.session_type("recall"),       "recall")
  assert_eq(session_store.session_type("suggest_idle"), "suggest")
  assert_eq(session_store.session_type("suggest_keys"), "suggest")
  assert_eq(session_store.session_type("suggest_cost"), "suggest")
  assert_error(function()
    ---@diagnostic disable-next-line: param-type-mismatch
    session_store.session_type("bogus")
  end)
end)

test("convert_to_finished: promotes only recall -> suggest_*", function()
  h.new_buf()
  h.seed_named("kindsmark", "test-kinds-mark")
  assert_error(function()
    session_store.convert_to_finished(
      "test-kinds-mark", h.fake_result({ user_seq = "" }), "kindsmark", "suggest_idle")
  end, "promotes")

  h.seed_named("kindskeep", "test-kinds-keep")
  local result = h.fake_result({ user_seq = "" })
  assert_eq(session_store.convert_to_finished("test-kinds-keep", result, "kindskeep"), true)
  assert_eq(result.kind, "mark", "convert_to_finished stamps the kind onto the result")
end)

test("summarize: surfaces kind + type for active sessions", function()
  h.with_buf({ "line one", "line two" }, function()
    session.start("kindssum")
    local rec = session_store.get_active("kindssum")
    rec = assert(rec)
    local summary = session_store.summarize(rec.id)
    assert_true(summary ~= nil, "summary must exist for active session")
    summary = assert(summary)
    assert_eq(summary.kind, "mark")
    assert_eq(summary.type, "mark")
    session.close("kindssum")
  end)

  h.with_buf({ "line one", "line two" }, function()
    h.with_patch({ { config, "watch", WATCH_CFG } }, function()
      session.watch("kindssumw")
      local rec = session_store.get_active("kindssumw")
      rec = assert(rec)
      local summary = session_store.summarize(rec.id)
      summary = assert(summary)
      assert_eq(summary.kind, "watch")
      assert_eq(summary.type, "watch")
      session.close("kindssumw")
    end)
  end)
end)
