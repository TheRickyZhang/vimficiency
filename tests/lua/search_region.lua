local ffi_optimizer = require("vimficiency.ffi.optimizer")

---@param overrides table
---@return VF.Optimizer.SessionSpan
local function span(overrides)
  return vim.tbl_extend("force", {
    initial_lines = { "a" },
    goal_lines = { "a" },
    start_row = 0,
    start_col = 0,
    end_row = 0,
    end_col = 0,
    window_height = 40,
    scroll_amount = 20,
    user_seq = "",
  }, overrides)
end

local NINE = { "a", "b", "c", "d", "e", "f", "g", "h", "i" }

test("build_search_request: cuts to the cursor span when buffers are equal", function()
  local request = ffi_optimizer.build_search_request(span({
    initial_lines = NINE,
    goal_lines = NINE,
    start_row = 5,
    start_col = 1,
    end_row = 7,
  }), 2, 500)

  assert_eq(request.initial_lines, { "d", "e", "f", "g", "h", "i" })
  assert_eq(request.goal_lines, { "d", "e", "f", "g", "h", "i" })
  assert_eq(request.start_row, 2)
  assert_eq(request.start_col, 1)
  assert_eq(request.end_row, 4)
  assert_eq(request.has_lines_above, true)
  assert_eq(request.has_lines_below, false)
  assert_eq(request.window_height, 40)
  assert_eq(request.scroll_amount, 20)
end)

test("build_search_request: expands to cover changed lines outside the cursor span", function()
  local goal = vim.list_extend({}, NINE)
  goal[2] = "changed"
  local request = ffi_optimizer.build_search_request(span({
    initial_lines = NINE,
    goal_lines = goal,
    start_row = 6,
    end_row = 7,
  }), 0, 500)

  assert_eq(request.initial_lines, { "b", "c", "d", "e", "f", "g", "h" })
  assert_eq(request.goal_lines[1], "changed")
  assert_eq(request.start_row, 5)
  assert_eq(request.has_lines_above, true)
  assert_eq(request.has_lines_below, true)
end)

test("build_search_request: keeps rows only the longer buffer has", function()
  local request = ffi_optimizer.build_search_request(span({
    goal_lines = { "a", "" },
  }), 0, 500)

  assert_eq(request.initial_lines, { "a" })
  assert_eq(request.goal_lines, { "a", "" })
  assert_eq(request.has_lines_above, false)
  assert_eq(request.has_lines_below, false)
end)

test("an excursion expands the submitted slice and counts toward the size limit", function()
  local input = span({
    initial_lines = NINE, goal_lines = NINE, start_row = 4, end_row = 4,
    untouched_above = 1, untouched_below = 2,
  })
  local request = ffi_optimizer.build_search_request(input, 1, 7)
  assert_eq(request.initial_lines, { "a", "b", "c", "d", "e", "f", "g", "h" })
  assert_eq(request.goal_lines, request.initial_lines)
  assert_error(function() ffi_optimizer.build_search_request(input, 1, 6) end, "MAX_SEARCH_LINES")
end)

test("build_search_request: bounds the search by screen-line motions as line motions", function()
  local request = ffi_optimizer.build_search_request(span({
    initial_lines = NINE,
    goal_lines = NINE,
    end_row = 1,
    user_seq = "gjgkgj",
    user_typed = "gjgkgj",
  }), 0, 500)

  assert_eq(request.user_seq, "jkj")
  assert_eq(request.user_typed, "gjgkgj", "pressed keys are costed as pressed")
end)

test("build_search_request: refuses a region taller than max_search_lines", function()
  local ok, err = pcall(ffi_optimizer.build_search_request, span({
    initial_lines = NINE,
    goal_lines = NINE,
    end_row = 8,
  }), 0, 4)

  assert_eq(ok, false)
  assert_eq(err, "search range exceeds MAX_SEARCH_LINES")
end)
