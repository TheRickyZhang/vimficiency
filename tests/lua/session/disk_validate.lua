-- disk.validate must reject saved results missing cursor fields, including the
-- end_row/end_col that the display path (ffi/display.lua) asserts on.

local disk = require("vimficiency.session.disk")

local function base()
  return {
    lines = { "x" },
    start_row = 0,
    start_col = 0,
    end_row = 0,
    end_col = 0,
    optimal_results = {},
  }
end

test("disk.validate accepts a complete result", function()
  assert_eq(disk.validate(base()), nil, "complete data must validate")
end)

test("disk.validate rejects missing or non-numeric cursor fields", function()
  for _, bad in ipairs({ { "end_row", nil }, { "end_col", nil }, { "end_row", "nope" } }) do
    local data = base()
    data[bad[1]] = bad[2]
    assert_true(disk.validate(data) ~= nil, bad[1] .. " = " .. tostring(bad[2]) .. " must be rejected")
  end
end)

test("disk.validate accepts diffs/prefix/suffix when present", function()
  local data = base()
  data.prefix = ""
  data.suffix = ""
  data.diffs = {
    { init = { begin_row = 0, begin_col = 0, end_row = 0, end_col = 1 },
      goal = { begin_row = 0, begin_col = 0, end_row = 0, end_col = 2 } },
  }
  assert_eq(disk.validate(data), nil, "well-formed new fields must validate")
end)

test("disk.validate tolerates absent diffs/prefix/suffix (pre-diff saves)", function()
  local data = base()  -- none of the new fields set
  assert_eq(disk.validate(data), nil, "older saves without the new fields must still load")
end)

test("disk.validate rejects wrong-typed new fields", function()
  local data = base()
  data.prefix = 5
  assert_true(disk.validate(data) ~= nil, "non-string prefix must be rejected")

  data = base()
  data.diffs = "nope"
  assert_true(disk.validate(data) ~= nil, "non-table diffs must be rejected")
end)
