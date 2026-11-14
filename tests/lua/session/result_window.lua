-- Full-screen result viewer: layout, content, and diff/cursor extmarks.
-- Opens a real tab + panes, asserts structure, then tears the tab down so the
-- shared test Neovim returns to baseline.

local v = vim.api
local h = require("_helpers")
local result_window = require("vimficiency.session.result_window")


local function open_fixture(overrides)
  local result = h.fake_result(vim.tbl_extend("force", {
    lines = { "aaa bbb", "ccc" },
    goal_lines = { "aaa zzz", "ccc" },
    start_row = 0, start_col = 0,
    end_row = 0, end_col = 6,
    has_lines_above = true,
    has_lines_below = true,
    prefix = "",
    suffix = "",
    user_seq = "x",
    user_cost = 12,
    optimal_results = { { seq = "y", cost = 4 } },
    diffs = {
      { init = { begin_row = 0, begin_col = 4, end_row = 0, end_col = 7 },
        goal = { begin_row = 0, begin_col = 4, end_row = 0, end_col = 7 } },
    },
  }, overrides or {}))

  local before = #v.nvim_list_tabpages()
  result_window.open("test", result)
  return before
end

--- Close the current (view) tab, restoring the prior one.
local function close_view_tab()
  if #v.nvim_list_tabpages() > 1 then
    vim.cmd("tabclose")
  end
end

test("result_window: recomputes diffs when the result lacks them", function()
  -- nil diffs (e.g. a pre-diff saved file) must not error; the view recomputes.
  open_fixture({ diffs = nil })
  assert_true(#v.nvim_tabpage_list_wins(0) == 3, "view still opens with recomputed diffs")
  close_view_tab()
end)
