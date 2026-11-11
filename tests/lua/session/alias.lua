-- tests/lua/session/alias.lua
-- Grammar tests for lua/vimficiency/session/alias.lua. Pure-function tests:
-- no buffers, no vim.uv, no state.

local alias = require("vimficiency.session.alias")

test("parse: name aliases are alphabetic", function()
  for _, input in ipairs({ "a", "refactor", "REFACTOR", "FooBar" }) do
    local t, v = alias.parse(input)
    assert_eq(t, "name", input)
    assert_eq(v, nil, input)
  end
end)

test("parse: recall aliases carry numeric values", function()
  for _, case in ipairs({
    { input = "5", kind = "recall_key", value = 5 },
    { input = "123", kind = "recall_key", value = 123 },
    { input = "3s", kind = "recall_time", value = 3 },
    { input = "90s", kind = "recall_time", value = 90 },
  }) do
    local t, v = alias.parse(case.input)
    assert_eq(t, case.kind, case.input)
    assert_eq(v, case.value, case.input)
  end
end)

test("parse: @ and $ are aliases too", function()
  assert_eq(alias.parse("@"), "last_finished")
  assert_eq(alias.parse("$"), "last_suggest")
  for _, s in ipairs(alias.SPECIAL) do
    assert_true(alias.parse(s) ~= nil, s)
  end
end)

test("parse: rejects invalid alias shapes", function()
  for _, input in ipairs({
    "0", "0s", "my-refactor", "_tmp",
    "tmp1", "v2", "1a", "3m", "3ms", "2h",
    "", " ", "a b", " a", "@@", "$1",
  }) do
    assert_eq(alias.parse(input), nil, input)
  end
  assert_eq(alias.parse(nil), nil)
  assert_eq(alias.parse(42), nil)
  assert_eq(alias.parse({}), nil)
  assert_eq(alias.parse(true), nil)
end)

test("expect: accepts listed kinds and names the actual kind otherwise", function()
  assert_eq(alias.expect("a", { "name" }), "name")
  local kind, value = alias.expect("3s", alias.RECALL_KINDS)
  assert_eq(kind, "recall_time")
  assert_eq(value, 3)
  for _, s in ipairs({ "a", "5", "3s", "@", "$" }) do
    assert_true(alias.expect(s, alias.ALL_KINDS) ~= nil, s)
  end

  assert_error(function() alias.expect("3s", { "name" }) end, "recall window")
  assert_error(function() alias.expect("a", alias.RECALL_KINDS) end, "session name")
  assert_error(function() alias.expect("my-ref", { "name" }) end, "Invalid alias")
end)

test("classification helpers distinguish name and recall aliases", function()
  for _, input in ipairs({ "a", "refactor", "FOO" }) do
    assert_eq(alias.is_name(input), true, input)
  end
  for _, input in ipairs({ "my-ref", "3", "3s", "@", "", "tmp1" }) do
    assert_eq(alias.is_name(input), false, input)
  end
  assert_eq(alias.is_name(nil), false)

  for _, input in ipairs({ "3", "3s", "120" }) do
    assert_eq(alias.is_recall(input), true, input)
  end
  for _, input in ipairs({ "a", "3m", "@", "" }) do
    assert_eq(alias.is_recall(input), false, input)
  end
  assert_eq(alias.is_recall(nil), false)
end)

test("TIME_HINTS all parse as recall_time", function()
  assert_true(#alias.TIME_HINTS > 0, "expected some hints")
  for _, h in ipairs(alias.TIME_HINTS) do
    local t = alias.parse(h)
    assert_eq(t, "recall_time", "hint " .. tostring(h) .. " should be recall_time")
  end
end)

test("is_valid_filename: accepts safe filenames", function()
  for _, input in ipairs({
    "refactor", "bugfix_v2", "refactor-2026-04-13", "my.edit.v2",
    "a", "_internal", "session42", "42session", "5", "3s",
  }) do
    assert_eq(alias.is_valid_filename(input), true, input)
  end
end)

test("is_valid_filename: rejects unsafe filenames and the special aliases", function()
  for _, input in ipairs({
    "..", ".", "../foo", "../../etc/passwd", "foo/bar", "foo\\bar",
    "/etc/passwd", "/absolute", ".hidden", ".bashrc", "-rf", "-name",
    "name with space", "tab\there", "new\nline", "null\0byte",
    " leading", "trailing ", "", "name;rm -rf", "name$VAR",
    "name|pipe", "name*glob", "name:colon", "@", "$",
  }) do
    assert_eq(alias.is_valid_filename(input), false, input)
  end
  assert_eq(alias.is_valid_filename(nil), false)
  assert_eq(alias.is_valid_filename(42), false)
  assert_eq(alias.is_valid_filename({}), false)
  assert_eq(alias.is_valid_filename(true), false)
end)
