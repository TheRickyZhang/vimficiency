-- disk.validate is the schema for the disk ingress: every field VF.Session.Result
-- declares non-nil is required here, so nothing downstream re-checks shape.

local disk = require("vimficiency.session.disk")
local h = require("_helpers")

local REQUIRED = {
  "lines", "goal_lines", "start_row", "start_col", "end_row", "end_col",
  "has_lines_above", "has_lines_below", "prefix", "suffix", "diffs",
  "window_height", "scroll_amount", "user_seq", "user_cost", "optimal_results",
  "key_count", "finished_wall_epoch", "kind",
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

test("disk.decode accepts the current recording and the previous endpoint-only format", function()
  local result = h.fake_result()
  local function encode(version)
    return vim.json.encode(vim.tbl_extend("force", result, { version = version }))
  end

  assert_true(disk.decode(encode(1)) ~= nil)
  local _, recording_err = disk.decode(encode(disk.FORMAT_VERSION))
  assert_match(recording_err or "", "recording")
  result.recording = { row = result.start_row, col = result.start_col, mode = "n", steps = disk.empty_array() }
  local data, err = disk.decode(encode(disk.FORMAT_VERSION))
  assert_eq(err, nil)
  data = assert(data)
  assert_eq(data.user_seq, result.user_seq)
  assert_eq(rawget(data, "version"), nil, "version is stripped on load")

  local _, newer_err = disk.decode(encode(disk.FORMAT_VERSION + 1))
  assert_match(newer_err or "", "format version", "a different version must be rejected")
  local _, missing_err = disk.decode(vim.json.encode(result))
  assert_match(missing_err or "", "format version <none>", "an unversioned file must be rejected")
end)
