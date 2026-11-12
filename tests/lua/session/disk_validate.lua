-- disk.validate is the schema for the disk ingress: every field VF.Session.Result
-- declares non-nil is required here, so nothing downstream re-checks shape.

local disk = require("vimficiency.session.disk")
local h = require("_helpers")

local REQUIRED = {
  "lines", "goal_lines", "start_row", "start_col", "end_row", "end_col",
  "has_lines_above", "has_lines_below", "window_height", "scroll_amount",
  "user_seq", "user_cost", "optimal_results", "key_count", "finished_wall_epoch",
}

test("disk.validate accepts a complete result", function()
  assert_eq(disk.validate(h.fake_result()), nil)
end)

test("disk.validate rejects each missing required field", function()
  for _, key in ipairs(REQUIRED) do
    local data = h.fake_result()
    data[key] = nil
    assert_match(disk.validate(data) or "", key, key .. " must be required")
  end
end)

test("disk.validate rejects wrong-typed fields", function()
  for _, bad in ipairs({ { "end_row", "nope" }, { "prefix", 5 }, { "diffs", "nope" }, { "user_seq", 1 } }) do
    local data = h.fake_result()
    data[bad[1]] = bad[2]
    assert_match(disk.validate(data) or "", bad[1], bad[1] .. " = " .. tostring(bad[2]) .. " must be rejected")
  end
end)

test("disk.validate tolerates absent prefix/suffix/diffs (pre-diff saves)", function()
  local data = h.fake_result()
  data.prefix, data.suffix, data.diffs = nil, nil, nil
  assert_eq(disk.validate(data), nil)
end)

test("disk.validate rejects empty lines and malformed optimal_results", function()
  for _, bad in ipairs({
    { "lines", {} },
    { "optimal_results", { { seq = 1, cost = 1 } } },
    { "optimal_results", { { seq = "x" } } },
  }) do
    local data = h.fake_result()
    data[bad[1]] = bad[2]
    assert_true(disk.validate(data) ~= nil, bad[1] .. " must be rejected")
  end
end)
