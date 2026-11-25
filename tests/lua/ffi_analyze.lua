local ffi_lib = require("vimficiency.ffi")

local function find_result(results, seq)
  for _, result in ipairs(results) do
    if result.seq == seq then
      return result
    end
  end
  return nil
end

local function find_result_ending_with(results, suffix)
  for _, result in ipairs(results) do
    if result.seq:sub(-#suffix) == suffix then
      return result
    end
  end
  return nil
end

-- Cursor at (0,0) in a 40-line window unless `overrides` says otherwise. The
-- keys were pressed as they ran unless `user_typed` is given.
---@param initial_lines string[]
---@param goal_lines string[]
---@param overrides table
---@return VF.SearchRequest
local function request(initial_lines, goal_lines, overrides)
  ---@type VF.SearchRequest
  local req = {
    initial_lines = initial_lines,
    goal_lines = goal_lines,
    start_row = 0, start_col = 0,
    end_row = 0, end_col = 0,
    has_lines_above = false, has_lines_below = false,
    window_height = 40, scroll_amount = 20,
    user_seq = "",
    optimizer_overrides = "",
  }
  for k, val in pairs(overrides) do req[k] = val end
  req.user_typed = overrides.user_typed or req.user_seq
  return req
end

local function with_reset_globals(fn)
  ffi_lib.reset_globals()
  local ok, err = pcall(fn)
  ffi_lib.reset_globals()
  if not ok then error(err) end
end

test("ffi.analyze: typed text with whitespace survives the boundary", function()
  -- Every way to turn an empty line into this goal has to type both words, so
  -- a sequence cut short at the space would fail on every result.
  local results, user_cost, dbg, diffs = ffi_lib.analyze(request({ "" }, { "hello world" }, {
    end_col = 10,
    user_seq = "ihello world<Esc>",
  }), 5)

  assert_true(#results > 0, "an insertion must produce at least one result")
  for _, result in ipairs(results) do
    assert_true(result.seq:find("hello.+world") ~= nil,
      "typed text must cross intact; got " .. vim.inspect(result.seq))
    assert_true(type(result.cost) == "number" and result.cost > 0,
      "cost must cross as a number; got " .. vim.inspect(result.cost))
  end
  assert_true(user_cost > 0, "user cost must cross as a positive number")
  assert_true(type(dbg) == "string", "debug text must always be present")
  assert_eq(#diffs, 1, "one planned region; got " .. vim.inspect(diffs))
end)

test("ffi.analyze: the user is costed by the keys pressed, not the keys that ran", function()
  local line = "one two three four"
  local function user_cost(user_typed)
    local _, cost = ffi_lib.analyze(request({ line }, { line }, {
      end_col = 14,
      user_seq = "www",
      user_typed = user_typed,
    }), 5)
    return cost
  end
  assert_true(user_cost("t") < user_cost("www"), "one pressed key costs less than the three that ran")
end)

test("ffi.analyze: captured buffer indentation survives slicing and determines open-line entries", function()
  local capture = require("vimficiency.capture.state")
  local buf = vim.api.nvim_create_buf(false, true)
  local win = vim.api.nvim_get_current_win()
  local previous = vim.api.nvim_win_get_buf(win)
  vim.api.nvim_win_set_buf(win, buf)
  local ok, err = pcall(function()
    local bo = vim.bo[buf]
    bo.autoindent, bo.tabstop, bo.shiftwidth = true, 4, 0
    for _, setting in ipairs({ { true, false }, { false, false }, { false, true } }) do
      bo.expandtab, bo.copyindent = setting[1], setting[2]
      local captured = capture.capture_view(buf, win).indent
      local search = ffi_lib.build_search_request({
        initial_lines = { "        source" }, goal_lines = { "        source", "        X" },
        start_row = 0, start_col = 8, end_row = 1, end_col = 8,
        window_height = 40, scroll_amount = 20, user_seq = "", user_typed = "", indent = captured,
      }, 5, 500)
      assert_eq(search.indent, captured)
      local results = ffi_lib.analyze(search, 20)
      assert_true(#results > 0)
      assert_eq(find_result(results, "oX<Esc>") ~= nil, bo.expandtab or bo.copyindent)
    end
  end)
  vim.api.nvim_win_set_buf(win, previous)
  vim.api.nvim_buf_delete(buf, { force = true })
  if not ok then error(err) end
end)

test("ffi.analyze: identical buffers plan no diff regions", function()
  local line = "one two three"
  local _, _, _, diffs = ffi_lib.analyze(request({ line }, { line }, {
    end_col = 4,
    user_seq = "w",
  }), 5)
  assert_eq(#diffs, 0)
end)

test("ffi.configure: count penalty overrides affect analyze costs", function()
  with_reset_globals(function()
    -- After reset_globals every key costs 1.0 and only the count penalty is
    -- added on top, so `4w`/`4W` are the only two-key paths to col 19 and
    -- become the strict minimum once their penalty is zeroed. Asserting on
    -- that extreme (and its opposite) avoids pinning a ranked menu.
    local line = "one two three four five six"
    local function analyze()
      return ffi_lib.analyze(request({ line }, { line }, {
        end_col = 19,
      }), 20)
    end

    local NO_PENALTY = { base = 0.0, count_slope = 0.0, span_slope = 0.0 }
    ffi_lib.apply_globals({
      count_penalty_overrides = { MovementWord = NO_PENALTY, MovementBigWord = NO_PENALTY },
    })
    local free = analyze()
    local free_4w = find_result(free, "4w")
    local free_4W = find_result(free, "4W")
    assert_true(free_4w ~= nil and free_4W ~= nil,
      "zero count penalty must surface 4w and 4W; got " .. vim.inspect(free))
    ---@cast free_4w -nil
    ---@cast free_4W -nil
    assert_eq(free_4w.cost, 2.0, "4w with no count penalty costs its two keys")
    assert_eq(free_4W.cost, 2.0, "4W with no count penalty costs its two keys")
    assert_eq(free[1].cost, 2.0, "two-key counted motions must be the cheapest result")

    ffi_lib.apply_globals({
      count_penalty_overrides = {
        MovementWord = { base = 60.0 },
        MovementBigWord = { base = 60.0 },
      },
    })
    local expensive = analyze()
    for _, seq in ipairs({ "4w", "4W" }) do
      local candidate = find_result(expensive, seq)
      assert_true(candidate == nil or candidate.cost > 60.0,
        seq .. " must be priced out or carry the 60.0 base; got " .. vim.inspect(candidate))
    end
  end)
end)

test("ffi.analyze: preserves substitute payload and cost for exact 2->3 scenario", function()
  local initial_lines = {
    "int main() {",
    "  int m;",
    "  for(int i = 2; i < n; i++) {",
    "",
    "  }",
    "}",
  }
  local goal_lines = {
    "int main() {",
    "  int m;",
    "  for(int i = 3; i < n; i++) {",
    "",
    "  }",
    "}",
  }

  local results, user_cost, _, diffs = ffi_lib.analyze(request(initial_lines, goal_lines, {
    start_row = 1, start_col = 2,
    end_row = 2, end_col = 14,
    user_seq = "jf;i<BS>3<Esc>",
  }), 20)

  assert_true(user_cost > 0, "user cost should parse as a positive number")

  -- The planner's region for a one-character replacement is forced: `2` at
  -- (2,14) becomes `3`, both spans [14, 15) on row 2.
  assert_eq(#diffs, 1, "one planned region; got " .. vim.inspect(diffs))
  assert_eq(diffs[1].init.begin_row, 2); assert_eq(diffs[1].init.begin_col, 14)
  assert_eq(diffs[1].init.end_row, 2);   assert_eq(diffs[1].init.end_col, 15)
  assert_eq(diffs[1].goal.begin_row, 2); assert_eq(diffs[1].goal.begin_col, 14)
  assert_eq(diffs[1].goal.end_row, 2);   assert_eq(diffs[1].goal.end_col, 15)

  -- The nav prefix is whatever the search currently prefers; only the edit
  -- tail and its payload are the FFI contract under test.
  local replace = find_result_ending_with(results, "r3")
  assert_true(replace ~= nil,
    "expected a replace candidate ending in r3; got " .. vim.inspect(results))
  replace = assert(replace)
  assert_true(replace.cost > 0, replace.seq .. " should keep a positive cost")

  local substitute = find_result_ending_with(results, "s3<Esc>")
  assert_true(substitute ~= nil,
    "expected a substitute candidate ending in s3<Esc>; got " .. vim.inspect(results))
  substitute = assert(substitute)
  assert_true(substitute.cost > 0,
    substitute.seq .. " should keep a positive cost, got " .. tostring(substitute.cost))

  for _, result in ipairs(results) do
    assert_true(result.seq:sub(-1) ~= "s",
      "substitute payload must survive ffi parsing; got truncated " .. result.seq)
  end
end)

test("ffi.analyze_start_async: worker result matches sync analyze", function()
  local initial_lines = { "for(int i = 2; i < n; i++) {" }
  local goal_lines    = { "for(int i = 3; i < n; i++) {" }
  local req = request(initial_lines, goal_lines, {
    start_col = 11,
    end_col = 11,
    user_seq = "f2r3",
  })

  local sync_results, sync_user_cost, _, sync_diffs = ffi_lib.analyze(req, 20)

  -- 0 = no deadline, so the worker runs to the same natural caps as sync.
  local job_id = ffi_lib.analyze_start_async(req, 20, 0)

  local payload
  vim.wait(5000, function()
    payload = ffi_lib.analyze_poll(job_id)
    return payload ~= nil
  end, 10)
  assert_true(payload ~= nil, "async analyze must finish within the timeout")

  assert_eq(payload.user_cost, sync_user_cost, "user_cost must match the sync path")
  assert_eq(#payload.results, #sync_results, "result count must match the sync path")
  assert_true(#sync_diffs > 0, "a text change must plan at least one region")
  assert_true(vim.deep_equal(payload.diffs, sync_diffs),
    "worker diffs must match the sync path; got " .. vim.inspect(payload.diffs))

  local function to_map(results)
    local m = {}
    for _, r in ipairs(results) do m[r.seq] = r.cost end
    return m
  end
  local sync_map = to_map(sync_results)
  for _, r in ipairs(payload.results) do
    assert_true(sync_map[r.seq] ~= nil, "async produced seq absent from sync: " .. r.seq)
    assert_eq(r.cost, sync_map[r.seq], "cost mismatch for seq " .. r.seq)
  end
end)

test("ffi.analyze: accepts one empty physical line", function()
  local results, user_cost = ffi_lib.analyze(request({ "" }, { "" }, {}), 1)

  assert_true(type(results) == "table", "results should parse")
  assert_true(user_cost >= 0, "user cost should parse")
end)

test("ffi.analyze: preserves trailing empty physical lines", function()
  local results, user_cost = ffi_lib.analyze(request({ "a", "" }, { "a", "" }, {
    end_row = 1,
    user_seq = "j",
  }), 1)

  assert_true(type(results) == "table", "results should parse")
  assert_true(user_cost > 0, "user cost should include j")
end)

test("ffi.analyze: typed non-ASCII text is analyzed, not rejected", function()
  -- `é` is two bytes with no physical key; both must come back in the sequence.
  local results = ffi_lib.analyze(request({ "" }, { "é" }, {
    end_col = 1,
    user_seq = "ié<Esc>",
  }), 5)

  assert_true(#results > 0, "inserting non-ASCII text must produce a result")
  for _, result in ipairs(results) do
    assert_true(result.seq:find("é", 1, true) ~= nil,
      "typed bytes must cross intact; got " .. vim.inspect(result.seq))
  end
end)

test("ffi.analyze: rejects empty line arrays", function()
  assert_error(function()
    ffi_lib.analyze(request({}, { "" }, {}), 1)
  end, "at least one line")
end)

test("ffi.analyze: a misspelled request field raises instead of defaulting", function()
  assert_error(function()
    ffi_lib.analyze(request({ "a" }, { "a" }, { window_hieght = 40 }), 1)
  end, "unknown field 'search.window_hieght' in vf.search.AnalyzeRequest")
end)

test("ffi.analyze: a request field of the wrong type raises", function()
  assert_error(function()
    ffi_lib.analyze(request({ "a" }, { "a" }, { start_row = "first" }), 1)
  end, "start_row")
end)

test("ffi.analyze: rejects NUL bytes inside lines", function()
  assert_error(function()
    ffi_lib.analyze(request({ "a" .. string.char(0) .. "b" }, { "a" }, {}), 1)
  end, "NUL byte")
end)

test("ffi.analyze: polling a finished job again raises", function()
  local job_id = ffi_lib.analyze_start_async(request({ "a b" }, { "a b" }, { end_col = 2 }), 1, 0)
  assert_true(
    vim.wait(5000, function() return ffi_lib.analyze_poll(job_id) ~= nil end, 5),
    "analyze job did not finish")

  assert_error(function() ffi_lib.analyze_poll(job_id) end, "job_id")
end)
