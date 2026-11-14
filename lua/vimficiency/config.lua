-- Central config surface. Add or change accepted top-level config keys here.

local validate = require("vimficiency.config.validate")

--- Marks a key that C++ owns; see the bottom of `fields` and `ffi_config.apply_globals`.
local CPP = {}

local fields = {
  -- Optimizer/search knobs
  RESULTS_CALCULATED = 20,
  RESULTS_SAVED = 5,
  SLICE_PADDING = 5,
  MAX_SEARCH_LINES = 500,

  -- User overrides for shared base optimizer parameters. Empty by
  -- default — C++ owns the canonical defaults (`OptimizerParamFields.h`).
  -- Anything the user sets here flows to every canonical optimizer call
  -- (marks/watches/recall/auto/explore-defaults) as `shared:` overrides.
  -- Explore-session sidecar overrides (set live via the explore panel)
  -- shadow these per-session.
  --
  -- Recognized keys: maxResults, maxNodesPopped, exploreFactor,
  -- effortWeight, distanceWeight, minPrefixCount, maxPrefixCount. See
  -- `OptimizerParamFields.h` for semantics and defaults.
  optimizer = {},

  -- Recall ring / session safety
  KEY_SESSION_CAPACITY = 200,
  MAX_RETENTION_SECONDS = 120,
  MANUAL_IDLE_TIMEOUT_SECONDS = 300,
  -- A Lua-callback mapping that moves the cursor or changes text is lossy: Neovim
  -- cannot report replayable keys for it. "drop" ends the session; "warn" and
  -- "allow" record the typed keys (`lhs`) instead, "warn" with a once-per-session
  -- notice. Replay is faithful only when the callback did what those keys do in
  -- plain Vim.
  lossy_mappings = "warn",

  -- Session features
  auto_suggest = {
    cooldown_ms = 5000,
    -- Minimum gap between *shown* suggestion toasts. A suggestion suppressed
    -- by this guard is still finished + stored (reachable via `@`), just not
    -- toasted. Distinct from cooldown_ms, which throttles how often the
    -- optimizer runs.
    notif_cooldown_ms = 2000,
    -- Wall-clock budget for the off-thread suggest analyze. The worker returns
    -- best-so-far past this; 0 = run to the natural caps. Responsiveness comes
    -- from the search being off-thread, not this bound — it just caps how long
    -- a single suggestion takes to surface (and the worker's CPU per fire).
    compute_deadline_ms = 2000,
    -- Fallback cost gate armed by `:Vimfy suggest on` when no idle/keys/cost
    -- trigger is configured. Tuning it does not auto-enable at startup; only
    -- an explicit trigger does.
    default_cost = { m = 1.5, b = 2.0, ms = 1000, window = "3s" },
  },
  watch = false,

  sequence_display = {
    tokenize = true,   -- true: add spaces between parsed tokens (`ciw foo <Esc>`); false: keep raw adjacency (`ciwfoo<Esc>`)
    sectionize = true, -- true: split movement/edit phases onto separate display lines; false: keep one flat display line
  },

  -- Explore-view UI preferences. Schema + built-in defaults live in
  -- `lua/vimficiency/explore/settings.lua` as `VIEW_SETTINGS` (the runtime
  -- source of truth). This table is just the user-override bag: values placed
  -- here via `setup({ explore = { ... } })` shadow the built-in defaults.
  -- Runtime toggles via the `gs` modal also persist here in-memory and
  -- reset on restart — declare overrides in `setup()` to make them
  -- permanent.
  explore = {},

  play = {
    include_user_sequence = true,                  -- show the user's typed sequence as the leftmost pane
    window_count = 2,                              -- total panes shown side-by-side; 1..4 (user pane consumes one slot if included)
  },

  -- C++ process globals (`VFConfig` in src/LuaExports/Api.def): the keyboard cost
  -- model and Vim options, pushed once by `ffi_config.apply_globals`. Everything
  -- above is Lua-owned and read from this table at call time.
  default_keyboard = CPP,
  weights = CPP,
  keys = CPP,
  slice_buffer_amount = CPP,
  shiftwidth = CPP,
  use_count_penalty_overrides = CPP,
  count_penalty_overrides = CPP,
}

function fields.is_lua_key(name)
  return fields._defaults[name] ~= nil
end

--- Snapshot the Lua-owned defaults: plain data fields, excluding `CPP` markers.
local function capture_defaults()
  local out = {}
  for name, value in pairs(fields) do
    if type(value) ~= "function" and name:sub(1, 1) ~= "_" and value ~= CPP then
      out[name] = type(value) == "table" and vim.deepcopy(value) or value
    end
  end
  return out
end

function fields.reset()
  for name, value in pairs(fields._defaults) do
    fields[name] = vim.deepcopy(value)
  end
end

--- Install the Lua-owned settings: defaults deep-merged with `user_config`, then
--- assigned onto this module so readers see live values. Returns the keys taken.
---@param user_config table
---@return table<string, true> consumed
function fields.apply(user_config)
  fields.reset()

  local merged = vim.tbl_deep_extend("force", vim.deepcopy(fields._defaults), user_config)
  merged.watch = validate.normalize_watch(user_config.watch)
  merged.auto_suggest = validate.normalize_auto_suggest(user_config.auto_suggest, fields._defaults.auto_suggest)
  merged.lossy_mappings = validate.normalize_lossy_mappings(merged.lossy_mappings)

  for name, value in pairs(merged) do
    if fields._defaults[name] ~= nil then
      fields[name] = value
    end
  end

  local consumed = {}
  for name in pairs(user_config) do
    if fields.is_lua_key(name) then
      consumed[name] = true
    end
  end
  return consumed
end

fields._defaults = capture_defaults()
fields._for_test = fields._for_test or {}
fields._for_test.fields = fields

fields.reset()

return fields
