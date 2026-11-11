-- tests/lua/session/save_default.lua
-- Covers `archive.default_filename` — the helper behind `:Vimfy save <alias>`
-- when the user omits the filename.
--
-- Rule under test: a name or recall alias is returned verbatim (every one of
-- them already passes `is_valid_filename`). `@` resolves to the alias the
-- caller passed to `session_store.mark_finished`, stored on the record at
-- finish time.

local archive       = require("vimficiency.session.archive")
local session_store = require("vimficiency.session.store")
local h             = require("_helpers")

--- Seed the store with a named session under `alias`, then finish it with
--- `finish_alias` so `@` has something to resolve to.
local function seed_and_finish(alias, finish_alias)
  local id = "test-save-default-" .. alias
  h.seed_named(alias, id)
  session_store.mark_finished(id, h.fake_result({ user_seq = "" }), finish_alias)
  return id
end

test("default_filename: name and recall aliases are returned verbatim", function()
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
