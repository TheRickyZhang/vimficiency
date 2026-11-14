return {
  "TheRickyZhang/vimficiency",
  version = "*",
  build = "./scripts/install.sh",
  lazy = false,
  config = function()

    require("vimficiency").setup({
      -- Values shown are the defaults. Field reference: doc-src/09-configuration.md.

      ----------------------------------------------------------------
      -- Keyboard: the effort model every suggestion is ranked by
      ----------------------------------------------------------------
      -- 0 = NONE, 1 = UNIFORM, 2 = QWERTY, 3 = COLEMAK_DH
      default_keyboard = 1,
      -- setup() auto-fills from vim.o.shiftwidth if you omit it.
      -- shiftwidth = 2,

      ----------------------------------------------------------------
      -- Capture
      ----------------------------------------------------------------
      -- A key mapped to a Lua function (flash.nvim's f, a vim.keymap.set
      -- callback) that moves the cursor or edits text has no replayable keys.
      -- "drop": a session spanning it ends (the warning names the mapping).
      -- "warn" / "allow": the keys you typed are recorded instead, with or
      -- without a once-per-session notice; replay may diverge.
      lossy_mappings = "warn",

      ----------------------------------------------------------------
      -- Features
      ----------------------------------------------------------------
      -- Watch (`:Vimfy watch <alias>` ends on idle): omit entirely to disable.
      watch = {
        idle = { ms = 3000 },
        cooldown_ms = 5000,
      },

      -- Auto-suggest (`:Vimfy suggest on`): any idle/keys/cost trigger arms it at
      -- startup; `default_cost` only tunes the on-demand gate. `auto_suggest =
      -- false` disables it entirely. Field reference: doc-src/06-suggest.md.
      -- auto_suggest = {
      --   cooldown_ms = 5000,        -- min gap between optimizer runs
      --   notif_cooldown_ms = 2000,  -- min gap between shown toasts
      --   idle = { ms = 3000, window = "3s" },
      --   -- keys = { every = 50 },
      --   -- cost = { m = 1.5, b = 2.0, ms = 300, window = "100" },
      -- },

      ----------------------------------------------------------------
      -- Search / performance
      ----------------------------------------------------------------
      -- Bound how much work each optimizer run does. A* overrides such as
      -- maxNodesPopped go in `optimizer = { ... }`.
      RESULTS_CALCULATED = 20,
      RESULTS_SAVED = 5,
      SLICE_PADDING = 5,
      MAX_SEARCH_LINES = 500,

      ----------------------------------------------------------------
      -- Recall / session safety
      ----------------------------------------------------------------
      KEY_SESSION_CAPACITY = 200,
      MAX_RETENTION_SECONDS = 120,
      MANUAL_IDLE_TIMEOUT_SECONDS = 300,

      ----------------------------------------------------------------
      -- Advanced effort-model knobs (C++)
      ----------------------------------------------------------------
      -- slice_buffer_amount = 5,

      -- weights = {
      --   key_weight         = 1.0,
      --   same_finger_weight = 0.0,
      --   same_key_weight    = 0.0,
      --   alt_hand_weight    = 0.0,
      --   good_roll_weight   = 0.0,
      --   bad_roll_weight    = 0.0,
      -- },

      -- Override specific keys by ffi.Key index; hand/finger values come from
      -- ffi.Hand / ffi.Finger.
      -- local ffi = require("vimficiency.ffi")
      -- keys = {
      --   [ffi.Key.Space] = { hand = ffi.Hand.Left, finger = ffi.Finger.Lt, base_cost = 0.2 },
      -- },

      -- If count_penalty_overrides is present and use_count_penalty_overrides
      -- is omitted, Lua enables it for you.
      -- use_count_penalty_overrides = true,
      -- count_penalty_overrides = {
      --   MotionWord = { base = 2.0, count_slope = 0.8 },
      --   EditLine   = { base = 1.5, span_slope = 0.2 },
      --   Join       = { base = 3.0 },
      -- },

      ----------------------------------------------------------------
      -- View preferences
      ----------------------------------------------------------------
      -- Both views also toggle these live with `gs`; toggles persist to sidecar
      -- JSON under stdpath("data")/vimficiency and win over the values here,
      -- which set the cold-start baseline.

      -- explore = {
      --   display_mode = "above",        -- "off" | "highlight" | "inplace" | "above" | "below"
      --   recommendation_sort = "effort",-- "effort" | "distance" | "score"
      --   recommendation_count = 5,      -- suggestions surfaced; 1..10
      --   show_user_typed = true,        -- include your captured typed sequence
      --   show_result_count = 1,         -- optimal_results shown alongside Explored
      --   compute_deadline_ms = 2000,    -- off-thread search budget at open; 0 = natural caps
      -- },

      -- play = {
      --   include_user_sequence = true,  -- your typed sequence as the leftmost pane
      --   window_count = 2,              -- panes side-by-side; 1..4 (user pane takes one slot)
      -- },
    })

    local function cmd_map(lhs, subcmd, desc)
      vim.keymap.set("n", lhs, "<Cmd>Vimfy " .. subcmd .. "<CR>", { silent = true, desc = desc })
    end

    local function prompt_map(lhs, subcmd, desc)
      vim.keymap.set("n", lhs, function()
        vim.ui.input({ prompt = subcmd .. " alias: " }, function(name)
          if name and name ~= "" then
            vim.cmd("Vimfy " .. subcmd .. " " .. name)
          end
        end)
      end, { desc = desc })
    end

    cmd_map("<leader>vl", "list", "Vimfy list")
    cmd_map("<leader>vz", "suggest toggle", "Toggle auto-suggest")
    cmd_map("<leader>vu", "play @", "Play last finished session")

    prompt_map("<leader>vb", "start", "Start mark")
    prompt_map("<leader>vf", "finish", "Finish mark")
    prompt_map("<leader>vw", "watch", "Start watch")
    prompt_map("<leader>vr", "recall", "Finish recall (N or Ns)")

    prompt_map("<leader>vs", "save", "Vimfy save")
    prompt_map("<leader>vp", "play", "Vimfy play")
    prompt_map("<leader>ve", "explore", "Explore mark")

    cmd_map("<leader>vS", "save $", "Save recent suggestion")
    cmd_map("<leader>vP", "play $", "Play recent suggestion")
    cmd_map("<leader>vE", "explore $", "Explore recent suggestion")
    cmd_map("<leader>vV", "view $", "View recent suggestion")

    cmd_map("<leader>vc", "reload", "Vimfy reload")

  end,
}
