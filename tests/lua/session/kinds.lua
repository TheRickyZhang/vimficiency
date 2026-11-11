-- tests/lua/session_kinds.lua
-- Each creation path in the 2x2 session taxonomy must set the
-- structural (start_kind, end_kind) pair truthfully. These tests pin
-- that contract independent of the rest of session behavior.
--
--   Mark    = (manual, manual)   via session.start
--   Watch   = (manual, auto)     via session.watch
--   Recall  = (auto,   manual)   via session_store.install_recall
--   Suggest = (auto,   auto)     Recall record flipped at Suggest takeover

local config        = require("vimficiency.config")
local session       = require("vimficiency.session")
local session_store = require("vimficiency.session.store")
local h             = require("_helpers")

local WATCH_CFG = { idle = { ms = 60000 }, cooldown_ms = 0 }

local function call_new_active(start_kind, end_kind)
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
    start_kind, end_kind
  )
end

test("new_active_session: rejects invalid kinds", function()
  assert_error(
    function() call_new_active("bogus", "manual") end,
    "start_kind", "invalid start_kind must error"
  )
  assert_error(
    function() call_new_active("manual", "bogus") end,
    "end_kind", "invalid end_kind must error"
  )
end)

test("session_type_from_kinds: covers all four cells", function()
  assert_eq(session_store.session_type_from_kinds("manual", "manual"), "mark")
  assert_eq(session_store.session_type_from_kinds("manual", "auto"),   "watch")
  assert_eq(session_store.session_type_from_kinds("auto",   "manual"), "recall")
  assert_eq(session_store.session_type_from_kinds("auto",   "auto"),   "suggest")
end)

test("session_type_from_kinds: rejects invalid pairs", function()
  assert_error(function()
    ---@diagnostic disable-next-line: param-type-mismatch
    session_store.session_type_from_kinds("bogus", "manual")
  end)
  assert_error(function()
    ---@diagnostic disable-next-line: param-type-mismatch
    session_store.session_type_from_kinds("manual", "bogus")
  end)
end)

test("summarize: surfaces type + start_kind + end_kind for active sessions", function()
  h.with_buf({ "line one", "line two" }, function()
    session.start("kindssum")
    local rec = session_store.get_active("kindssum")
    rec = assert(rec)
    local summary = session_store.summarize(rec.id)
    assert_true(summary ~= nil, "summary must exist for active session")
    summary = assert(summary)
    assert_eq(summary.type,       "mark")
    assert_eq(summary.start_kind, "manual")
    assert_eq(summary.end_kind,   "manual")
    session.close("kindssum")
  end)

  h.with_buf({ "line one", "line two" }, function()
    h.with_patch({ { config, "watch", WATCH_CFG } }, function()
      session.watch("kindssumw")
      local rec = session_store.get_active("kindssumw")
      rec = assert(rec)
      local summary = session_store.summarize(rec.id)
      summary = assert(summary)
      assert_eq(summary.type,       "watch")
      assert_eq(summary.start_kind, "manual")
      assert_eq(summary.end_kind,   "auto")
      session.close("kindssumw")
    end)
  end)
end)
