-- tests/lua/session/special_selectors.lua
-- `@` and `$` are aliases like any other: the store resolves them to an id.
-- `$` is the most-recent suggest regression, pinned separately from `@`
-- (most-recent finished, any kind).

local archive       = require("vimficiency.session.archive")
local session_store = require("vimficiency.session.store")
local h             = require("_helpers")

--- Seed a finished record under `alias`, finishing with `finish_alias`.
local function seed_finished(alias, finish_alias, result)
  local id = "test-special-" .. alias
  h.seed_named(alias, id)
  session_store.convert_to_finished(id, result or h.fake_result({ user_seq = "" }), finish_alias)
  return id
end

test("$ resolves to the pinned suggest regression, independent of @", function()
  local suggest_id = seed_finished("regr", "regr", h.fake_result({ user_seq = "ciwx" }))
  session_store.set_last_suggest_id(suggest_id)

  -- A later, unrelated finish moves @ but must not touch $.
  seed_finished("other", "other")

  assert_eq(session_store.get_id("@"), "test-special-other")
  assert_eq(archive.default_filename("@"), "other")

  local dollar, err = archive.resolve_result("$")
  dollar = assert(dollar, "$ resolves to the pinned regression: " .. tostring(err))
  assert_eq(dollar.user_seq, "ciwx")
  assert_eq(archive.default_filename("$"), "regr")
end)

test("$ unset yields a regression-specific error", function()
  -- Simulate an evicted/never-set pin: the id has no backing record, so
  -- get_last_suggest_id reports nothing.
  session_store.set_last_suggest_id("test-special-evicted")

  local res, err = archive.resolve_result("$")
  assert_eq(res, nil)
  assert_true(err and err:find("regression") ~= nil,
    "error should mention regression: " .. tostring(err))
  assert_eq(archive.default_filename("$"), nil)
end)
