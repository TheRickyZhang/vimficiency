return function(ctx)
  local helpers = ctx.helpers
  local sim = ctx.sim
  local playback = require("vimficiency.session.playback")
  local with_replay = ctx.with_replay
  local assert_eq = ctx.assert_eq
  local assert_true = ctx.assert_true

  return {
  {
    name = "simulate keeps finished suggestion rendered while user pane continues",
    run = function(next)
      local lines = {
        "#include <bits/stdc++.h>",
        "using namespace std;",
        "",
        "int main() {",
        "  int n = 1;",
        "  for(int i = 0; i < n; i++) {",
        "    cout << i+1 << endl;",
        "  }",
        "}",
      }
      local steps = {}
      for i = 1, 40 do steps[i] = { keys = "l", row = 3, col = i % 5, mode = "n", deltas = {} } end
      local opt1 = "Wjwrm$i0<Esc>jfnrmjF+Xce2<Space>*<Space>i<Esc>"
      local opt2 = "Wjwrm$i0<Esc>jfnrmjFix.ciw2<Space>*<Space>i<Esc>"
      with_replay({ opt1, opt2 }, lines, function()
        sim._debug_seek_to(#steps)
        local windows = sim._debug_get_windows()
        assert_eq(vim.api.nvim_buf_get_lines(windows[3].buf, 6, 7, true)[1],
          "    cout << 2 * i << endl;",
          "finished opt2 pane should keep full final buffer while user pane continues")
      end, next, {
        start_row = 3,
        start_col = 0,
        recording = { row = 3, col = 0, mode = "n", steps = steps },
      })
    end,
  },
  {
    name = "session simulate replays saved-session dot-repeat ciw sequence",
    run = function(next)
      local lines = {
        "#include <bits/stdc++.h>",
        "using namespace std;",
        "",
        "int main() {",
        "  int n = 1;",
        "  for(int i = 0; i < n; i++) {",
        "    cout << i+1 << endl;",
        "  }",
        "}",
      }
      local user_seq = "jwwrmf;i0<Esc>jfnrmjBBBcWi<BS>2<Space>*<Space>i<Esc>"
      local opt1 = "Wjwrm$i0<Esc>jfnrmjF+Xce2<Space>*<Space>i<Esc>"
      local opt2 = "Wjwrm$i0<Esc>jfnrmjFix.ciw2<Space>*<Space>i<Esc>"
      local result = helpers.fake_result({
        lines = lines,
        goal_lines = lines,
        start_row = 3,
        start_col = 0,
        end_row = 6,
        end_col = 16,
        user_seq = user_seq,
        user_cost = 29,
        optimal_results = {
          { seq = opt1, cost = 26 },
          { seq = opt2, cost = 28 },
        },
        finished_wall_epoch = 2,
      })

      helpers.new_buf({ "session simulate source" })
      local view = require("vimficiency.simulate.settings")
      view.set_setting("include_user_sequence", true)
      view.set_setting("window_count", 3)
      local warned = false
      helpers.with_patch({ { vim, "notify", function(message, level)
        if level == vim.log.levels.WARN and message:find("older save format (v1)", 1, true) then warned = true end
      end } }, function() playback.view(result, "simreplay", 3) end)

      local tries = 0
      local function finish(ok, err)
        sim.cleanup_compare()
        next(ok, err)
      end

      local function wait_ready()
        tries = tries + 1
        local windows = sim._debug_get_windows()
        local states = sim._debug_get_states()
        if #windows == 3 and #states == 3 then
          local ok, err = pcall(function()
            local user = assert(sim._debug_get_pool().user, "missing user replay pool entry")
            assert_true(warned, "View warns that this is a v1 save")
            assert_eq(#user.tokens, 1, "older sessions show their recorded endpoints")
            assert_eq(user.states[2].lines, result.goal_lines, "v1 finish text")
            assert_eq(user.states[2].cursor, { result.end_row + 1, result.end_col }, "v1 finish cursor")
            sim._debug_seek_to(1000)
            assert_eq(vim.api.nvim_buf_get_lines(windows[2].buf, 6, 7, true)[1],
              "    cout << 2 * i << endl;",
              "session.simulate opt1 pane should render the full typed tail")
            assert_eq(vim.api.nvim_buf_get_lines(windows[3].buf, 6, 7, true)[1],
              "    cout << 2 * i << endl;",
              "session.simulate opt2 pane should render the full typed tail")
          end)
          finish(ok, err)
        elseif tries > 300 then
          finish(false, "session.simulate did not finish precompute")
        else
          vim.defer_fn(wait_ready, 10)
        end
      end

      wait_ready()
    end,
  },
  {
    name = "session simulate reports capped panes and tags the user's top result",
    run = function(next)
      local lines = { "alpha beta gamma" }
      local result = helpers.fake_result({
        lines = lines,
        goal_lines = lines,
        start_row = 0,
        start_col = 0,
        end_row = 0,
        end_col = 6,
        user_seq = "w",
        user_cost = 1,
        optimal_results = {
          { seq = "w", cost = 1 },
          { seq = "fb", cost = 2 },
        },
        finished_wall_epoch = 2,
      })

      helpers.new_buf({ "session simulate source" })
      require("vimficiency.simulate.settings").set_setting("include_user_sequence", true)
      playback.view(result, "simcap", 4)

      local tries = 0
      local function finish(ok, err)
        sim.cleanup_compare()
        next(ok, err)
      end

      local function wait_ready()
        tries = tries + 1
        local windows = sim._debug_get_windows()
        if #windows == 3 and #sim._debug_get_states() == 3 then
          local ok, err = pcall(function()
            assert_true(sim._debug_statusline():find("panes 3/4 · only 2 results", 1, true) ~= nil,
              "status line should report the capped pane count: " .. sim._debug_statusline())
            assert_true(ctx.header_lines(windows[2].buf)[2]:find("[1 · yours] Simulated", 1, true) ~= nil,
              "top pane should be tagged as the user's sequence")
            assert_true(ctx.header_lines(windows[3].buf)[2]:find("[2] ", 1, true) ~= nil,
              "second pane keeps its plain rank label")
          end)
          finish(ok, err)
        elseif tries > 300 then
          finish(false, "session.simulate did not finish precompute")
        else
          vim.defer_fn(wait_ready, 10)
        end
      end

      wait_ready()
    end,
  },
  {
    name = "simulate <CR> focuses the window under cursor and toggles back",
    run = function(next)
      with_replay({ "j", "w", "b" }, {
        "alpha beta gamma",
      }, function()
        -- Pre-condition: three side-by-side windows, same tab.
        local before = sim._debug_get_windows()
        assert_eq(#before, 3, "three replay windows before focus")

        -- Move cursor into window #2, then invoke the toggle handler.
        vim.api.nvim_set_current_win(before[2].win)
        sim._debug_toggle_focus()

        -- Post-condition: a single window remains in the tab; the focused
        -- index is 2 per focus_state.
        local focused = sim._debug_get_windows()
        assert_eq(#focused, 1, "one window after focus")
        assert_eq(focused[1].seq_idx, 2, "focused entry carries seq_idx = 2")
        local state = sim._debug_get_focus_state()
        assert_true(state ~= nil, "focus_state set after focus")
        assert_eq(state.focused_idx, 2, "focused_idx matches the window we were on")

        -- Second `<CR>` from within focus escapes back to the split.
        sim._debug_toggle_focus()
        assert_eq(#sim._debug_get_windows(), 3, "split restored after escape")
        assert_true(sim._debug_get_focus_state() == nil, "focus_state cleared after escape")

        -- seq_idx is populated on every entry post-restore.
        for i, entry in ipairs(sim._debug_get_windows()) do
          assert_eq(entry.seq_idx, i, "restored windows carry seq_idx = " .. i)
        end
      end, next)
    end,
  },
  {
    name = "simulate <Tab> cycles sim windows (split) and swaps buffer (focus)",
    run = function(next)
      with_replay({ "j", "w", "b" }, {
        "alpha beta gamma",
      }, function()
        local wins = sim._debug_get_windows()
        assert_eq(#wins, 3, "three replay windows before cycling")

        -- Split mode: <Tab> from window 1 → window 2, <Tab> → window 3,
        -- <Tab> wraps back to window 1.
        vim.api.nvim_set_current_win(wins[1].win)
        sim._debug_cycle_next()
        assert_eq(vim.api.nvim_get_current_win(), wins[2].win, "cycle from 1 → 2")
        sim._debug_cycle_next()
        assert_eq(vim.api.nvim_get_current_win(), wins[3].win, "cycle from 2 → 3")
        sim._debug_cycle_next()
        assert_eq(vim.api.nvim_get_current_win(), wins[1].win, "cycle from 3 → 1 (wrap)")

        -- <S-Tab> wraps the other direction.
        sim._debug_cycle_prev()
        assert_eq(vim.api.nvim_get_current_win(), wins[3].win, "cycle prev 1 → 3 (wrap)")

        -- Enter focus on window 3, then <Tab> swaps to sequence 1 in place.
        vim.api.nvim_set_current_win(wins[3].win)
        sim._debug_toggle_focus()
        assert_eq(sim._debug_get_focus_state().focused_idx, 3, "focused on 3")
        sim._debug_cycle_next()
        assert_eq(sim._debug_get_focus_state().focused_idx, 1, "focus wraps 3 → 1")
        local focused = sim._debug_get_windows()
        assert_eq(#focused, 1, "still one window after cycle-in-focus")
        assert_eq(focused[1].seq_idx, 1, "entry's seq_idx follows focused_idx")
      end, next)
    end,
  },
  }
end
