-- Tests for the explore settings side panel wrapper. Shared settings
-- widget behavior lives in tests/lua/settings_window.lua; this file
-- checks that the explore wrapper opens, refreshes, reuses, and closes
-- its side-pane state.
local helpers = require("_helpers")
local panel = require("vimficiency.explore.render.panel")

local function make_view()
  -- Anchor window for the panel's split. Use a regular scratch buffer
  -- so the vsplit lands in a predictable layout.
  local buf = helpers.new_buf({ "scratch" })
  return {
    scratch = { win = vim.api.nvim_get_current_win(), buf = buf },
    panel = nil,
  }
end

local function teardown(view)
  pcall(panel.close, view)
  vim.cmd("silent! tabonly")
  vim.cmd("silent! %bwipeout!")
end

local function open_panel(view, schema)
  panel.open(view, schema, function() end)
  assert_true(view.panel ~= nil, "panel should be open")
  return view.panel
end

local function make_schema(state)
  -- Minimal schema covering the three value kinds + an action row. The
  -- `state` table is the test's view of what the user sees / changes.
  state = state or {}
  state.flag = state.flag == nil and false or state.flag
  state.count = state.count or 1
  state.mode = state.mode or "off"
  state.reset_called = state.reset_called or 0
  return {
    { kind = "setting",
      label = "Flag",
      value_kind = "bool",
      get = function() return state.flag end,
      set = function(v) state.flag = v end },
    { kind = "setting",
      label = "Count",
      value_kind = "int", min = 0, max = 5,
      get = function() return state.count end,
      set = function(v) state.count = v end },
    { kind = "setting",
      label = "Mode",
      value_kind = "enum", values = { "off", "highlight", "above" },
      get = function() return state.mode end,
      set = function(v) state.mode = v end },
    { kind = "separator" },
    { kind = "action",
      label = "Reset",
      run = function() state.reset_called = state.reset_called + 1 end },
  }, state
end

test("explore panel: re-opening on already-open view re-renders in place", function()
  local view = make_view()
  local schema_a = make_schema({ flag = false })
  local panel_view = open_panel(view, schema_a)
  local first_buf = panel_view.buf

  -- Second open with a different schema. The panel should reuse the
  -- existing window (not stack two splits) and update its rows.
  local schema_b, state_b = make_schema({ flag = true })
  panel_view = open_panel(view, schema_b)

  assert_eq(panel_view.buf, first_buf)
  local lines = vim.api.nvim_buf_get_lines(panel_view.buf, 0, -1, false)
  assert_match(lines[1], "[✓]")  -- new schema's flag = true
  _ = state_b

  teardown(view)
end)

test("explore panel: refresh on closed panel is a no-op", function()
  local view = make_view()
  -- panel.refresh must not error when the panel was never opened or
  -- has been closed — callers (e.g. settings reset) call it
  -- unconditionally.
  panel.refresh(view)
  assert_true(view.panel == nil)

  teardown(view)
end)
