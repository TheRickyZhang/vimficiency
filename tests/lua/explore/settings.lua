-- Tests for explore's settings layer: default-shape construction,
-- resolve_param's per-scope -> shared-base -> nil fallback, and sidecar
-- envelope edge cases.
local explore = require("vimficiency.explore")
local settings = require("vimficiency.explore.settings")
local settings_profile = require("vimficiency.settings_profile")

local helpers = explore._for_test

test("explore settings: default_settings seeds view from VIEW_SETTINGS spec", function()
  local defaults = helpers.default_settings()

  -- View defaults come from VIEW_SETTINGS in explore/settings.lua (the source of
  -- truth). Cold start with empty config.explore should land on the
  -- built-in defaults.
  assert_eq(defaults.view.display_mode, "above")
  assert_eq(defaults.view.recommendation_sort, "effort")
  assert_eq(defaults.view.recommendation_count, 5)
  assert_eq(defaults.view.show_user_typed, true)
  assert_eq(defaults.view.show_result_count, 1)

  -- Optimizer scopes start empty — C++ owns those defaults.
  assert_eq(defaults.shared, {})
  assert_eq(defaults.nav, {})
  assert_eq(defaults.transform, {})
  assert_eq(defaults.composition, {})
end)

test("explore settings: user config.explore overrides VIEW_SETTINGS defaults", function()
  local config = require("vimficiency.config")
  require("_helpers").with_patch({ { config.explore, "display_mode", "below" } }, function()
    local defaults = helpers.default_settings()
    assert_eq(defaults.view.display_mode, "below")
    -- Untouched keys still come from the spec.
    assert_eq(defaults.view.recommendation_sort, "effort")
    assert_eq(defaults.view.recommendation_count, 5)
  end)
end)

test("explore settings: a false override is not collapsed to the spec default", function()
  local config = require("vimficiency.config")
  require("_helpers").with_patch({ { config.explore, "show_user_typed", false } }, function()
    assert_eq(helpers.default_settings().view.show_user_typed, false)
  end)
end)

test("explore settings: settings_profile.load handles missing sidecar", function()
  -- The XDG_DATA_HOME the runner sets up may have a sidecar from a
  -- prior test; remove it so this exercises the cold path.
  local path = vim.fn.stdpath("data") .. "/vimficiency/explore_settings.json"
  os.remove(path)

  local envelope = settings_profile.load("explore")
  assert_eq(envelope.version, nil)
  assert_eq(envelope.data, {})
end)

test("explore settings: settings_profile.load handles malformed JSON", function()
  local path = vim.fn.stdpath("data") .. "/vimficiency/explore_settings.json"
  vim.fn.mkdir(vim.fn.fnamemodify(path, ":h"), "p")
  local fh = assert(io.open(path, "w"))
  fh:write("{not json")
  fh:close()

  local envelope = settings_profile.load("explore")
  assert_eq(envelope.version, nil)
  assert_eq(envelope.data, {})

  os.remove(path)
end)

test("explore settings: shared fallback is base-only", function()
  settings_profile.save("explore", settings.EXPLORE_SCHEMA_VERSION, {
    shared = {
      minPrefixCount = 6,
      fMotionThreshold = 99,
    },
  })
  settings.invalidate_cache()

  assert_eq(helpers.resolve_param("minPrefixCount", "nav"), 6)
  assert_eq(helpers.resolve_param("fMotionThreshold", "nav"), nil)

  settings_profile.clear("explore")
  settings.invalidate_cache()
end)
