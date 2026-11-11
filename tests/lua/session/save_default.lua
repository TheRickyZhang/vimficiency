-- tests/lua/session/save_default.lua
-- Covers `archive.default_filename` — the helper behind `:Vimfy save <alias>`
-- when the user omits the filename.
--
-- Rule under test: a named or recall alias is returned verbatim (every one of
-- them already passes `is_valid_filename`). `@` resolves to the alias the
-- caller passed to `session_store.convert_to_finished`, stored on the record at
-- finish time, except suggest results, which get `suggest-<timestamp>`.

local archive       = require("vimficiency.session.archive")
local session_store = require("vimficiency.session.store")
local h             = require("_helpers")

--- Seed the store with a named session under `alias`, then finish it with
--- `finish_alias` so `@` has something to resolve to.
local function seed_and_finish(alias, finish_alias)
  local id = "test-save-default-" .. alias
  h.seed_named(alias, id)
  session_store.convert_to_finished(id, h.fake_result({ user_seq = "" }), finish_alias)
  return id
end

test("default_filename: named and recall aliases are returned verbatim", function()
  for _, alias in ipairs({ "a", "refactor", "WIP", "5", "123", "3s", "30s" }) do
    assert_eq(archive.default_filename(alias), alias, alias)
  end
end)

test("default_filename: @ resolves to the last finish alias", function()
  seed_and_finish("savedefault", "savedefault")
  assert_eq(archive.default_filename("@"), "savedefault")
  -- Also verifies the kind stamping: the header formatter depends on this field.
  local last_result = session_store.get_result("@")
  assert_eq(last_result and last_result.kind, "mark")

  -- Named session, but finished under a recall-form alias — exercises the
  -- "caller's literal alias is what we store" path. `@` resolution is governed
  -- entirely by whatever the finish caller passed.
  seed_and_finish("anothername", "3s")
  assert_eq(archive.default_filename("@"), "3s")

  seed_and_finish("firstalias", "firstalias")
  seed_and_finish("secondalias", "secondalias")
  assert_eq(archive.default_filename("@"), "secondalias")
end)

test("default_filename: a suggest result gets a timestamped name, not its recall alias", function()
  local _, _, id = h.seed_recall({ backdate_s = 10 })
  local epoch = os.time({ year = 2026, month = 11, day = 11, hour = 14, min = 5, sec = 9 })
  local result = h.fake_result({ user_seq = "", finished_wall_epoch = epoch })
  assert_true(session_store.convert_to_finished(id, result, "3s", "suggest_idle"))
  session_store.set_last_suggest_id(id)

  assert_eq(archive.default_filename("$"), "suggest-20261111-140509")
  assert_eq(archive.default_filename("@"), "suggest-20261111-140509")
  assert_true(require("vimficiency.session.alias").is_valid_filename(archive.default_filename("$")))
end)
