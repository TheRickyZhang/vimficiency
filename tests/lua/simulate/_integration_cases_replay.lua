return function(ctx)
  local sim = ctx.sim
  local with_replay = ctx.with_replay
  local get_window = ctx.get_window
  local get_snapshot = ctx.get_snapshot
  local header_lines = ctx.header_lines
  local load_vimficiency_file = ctx.load_vimficiency_file
  local assert_eq = ctx.assert_eq
  local assert_true = ctx.assert_true

  return {
  {
    name = "View shows observed plugin edits and groups commands in the same delivery",
    run = function(next)
      with_replay({}, { "abc" }, function()
        local user = assert(sim._debug_get_pool().user)
        assert_eq(#user.tokens, 1, "one observed boundary")
        assert_eq(user.tokens[1].text, "Vdww", "all the delivered keys")
        sim._debug_seek_to(1)
        assert_eq(get_snapshot(1, 1), { lines = { "aXbc" }, cursor = { 1, 1 }, mode = "n" }, "recorded state")
        sim._debug_seek_to(0)
        assert_eq(get_snapshot(1, 0).lines, { "abc" }, "original text")
      end, next, { recording = { row = 0, col = 0, mode = "n", steps = { {
        keys = "Vdww", row = 0, col = 1, mode = "n", deltas = { { first = 0, old_count = 1, lines = { "aXbc" } } },
      } } } })
    end,
  },
  {
    name = "simulate replays insert and visual sequence with virtual header",
    run = function(next)
      with_replay({ "jf;i<BS>3<Esc><Space>ve", "j" }, {
        "one two",
        "abc;def ghi",
        "last line",
      }, function()
        local seq1 = get_window(1)
        assert_eq(get_snapshot(1, 3).cursor, { 2, 3 }, "snapshot cursor after entering insert")
        assert_eq(get_snapshot(1, 3).mode, "i", "snapshot mode after entering insert")

        sim._debug_seek_to(3)
        assert_true(header_lines(seq1.buf)[3]:find("Mode INSERT") ~= nil,
          "insert header on mode row")

        assert_eq(get_snapshot(1, 4).lines,
          { "one two", "ab;def ghi", "last line" }, "snapshot lines after <BS>")
        sim._debug_seek_to(4)
        assert_eq(vim.api.nvim_buf_get_lines(seq1.buf, 0, -1, true),
          { "one two", "ab;def ghi", "last line" }, "rendered lines after <BS>")

        assert_eq(get_snapshot(1, 5).lines,
          { "one two", "ab3;def ghi", "last line" }, "snapshot lines after typed 3")
        sim._debug_seek_to(5)
        assert_eq(vim.api.nvim_buf_get_lines(seq1.buf, 0, -1, true),
          { "one two", "ab3;def ghi", "last line" }, "rendered lines after typed 3")

        assert_eq(get_snapshot(1, 8).mode, "v", "snapshot mode after visual enter")
        sim._debug_seek_to(8)
        assert_true(header_lines(seq1.buf)[3]:find("Mode VISUAL") ~= nil,
          "visual header on mode row")
        assert_eq(get_snapshot(1, 9).cursor, { 2, 6 }, "snapshot cursor after visual e")
      end, next)
    end,
  },
  {
    name = "View uses the saved buffer's tab stop for the probe and pane",
    run = function(next)
      local seq = "oX<Esc>"
      with_replay({ seq }, { "    source" }, function()
        local final = get_snapshot(1, #sim._debug_tokenize_for_animation(seq))
        assert_eq(final.lines, { "    source", "\tX" }, "new indent bytes")
        assert_eq(final.cursor, { 2, 1 }, "byte column of X")
        assert_eq(vim.bo[get_window(1).buf].tabstop, 4, "displayed tab width")
      end, next, { indent = { tabstop = 4, shiftwidth = 0, autoindent = true, expandtab = false, copyindent = false } })
    end,
  },
  {
    name = "simulate handles leading space and append-at-eol",
    run = function(next)
      with_replay({ "3wfa;ww", "<Space>ww", "Axyz<Esc>" }, {
        "alpha beta; gamma",
        "delta epsilon",
        "zeta",
      }, function()
        local seq3 = get_window(3)
        assert_eq(get_snapshot(2, 3).cursor, { 1, 10 }, "snapshot cursor after leading-space replay")
        assert_eq(get_snapshot(3, 3).cursor, { 1, 19 }, "snapshot append-at-eol cursor")
        sim._debug_seek_to(3)
        assert_eq(vim.api.nvim_buf_get_lines(seq3.buf, 0, -1, true),
          { "alpha beta; gammaxyz", "delta epsilon", "zeta" }, "rendered append-at-eol lines")
      end, next)
    end,
  },
  {
    name = "simulate replays insert-mode key notation as typed characters",
    run = function(next)
      local sequences = {
        "A2<Space>*<Space>i<Esc>",
        "Afoo<CR>bar<Esc>",
        "feciw2<Space>*<Space>i<Esc>",
      }
      with_replay(sequences, { "cout << endl;" }, function()
        local steps1 = #sim._debug_tokenize_for_animation(sequences[1])
        local steps2 = #sim._debug_tokenize_for_animation(sequences[2])
        local steps3 = #sim._debug_tokenize_for_animation(sequences[3])
        assert_eq(get_snapshot(1, steps1).lines,
          { "cout << endl;2 * i" }, "insert-mode <Space> should replay as spaces")
        assert_eq(get_snapshot(2, steps2).lines,
          { "cout << endl;foo", "bar" }, "insert-mode <CR> should replay as newline")
        assert_eq(get_snapshot(3, steps3).lines,
          { "cout << 2 * i;" }, "insert-mode key notation after ciw should replay fully")
      end, next)
    end,
  },
  {
    name = "simulate replays key notation after dot-repeat and ciw",
    run = function(next)
      local key_notation = "x.ciw2<Space>*<Space>i<Esc>"
      local legacy_raw = "x.ciw2 * i<Esc>"
      with_replay({ key_notation, legacy_raw }, { "cout << i+1 << endl;" }, function()
        local notation_steps = #sim._debug_tokenize_for_animation(key_notation)
        local legacy_steps = #sim._debug_tokenize_for_animation(legacy_raw)
        assert_eq(get_snapshot(1, notation_steps).lines,
          { "cout << 2 * i << endl;" },
          "key-notation spaces after x.ciw should replay fully")
        assert_eq(get_snapshot(2, legacy_steps).lines,
          { "cout << 2 * i << endl;" },
          "legacy raw spaces after x.ciw should replay fully")
      end, next, { start_col = 8 })
    end,
  },
  {
    name = "simulate replays saved-session dot-repeat ciw sequence",
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
      local seq = "Wjwrm$i0<Esc>jfnrmjFix.ciw2<Space>*<Space>i<Esc>"
      with_replay({ seq }, lines, function()
        local steps = #sim._debug_tokenize_for_animation(seq)
        local snap = get_snapshot(1, steps)
        assert_eq(snap.lines[7],
          "    cout << 2 * i << endl;",
          "saved-session opt2 snapshot should contain full typed tail")
        local win = get_window(1)
        sim._debug_seek_to(steps)
        assert_eq(vim.api.nvim_buf_get_lines(win.buf, 6, 7, true)[1],
          "    cout << 2 * i << endl;",
          "saved-session opt2 rendered buffer should contain full typed tail")
      end, next, { start_row = 3, start_col = 0 })
    end,
  },
  {
    name = "simulate replays saved a.json fixture",
    run = function(next)
      local result = load_vimficiency_file("a")
      local user_steps = #sim._debug_tokenize_for_animation(result.user_seq)
      local opt_seq = result.optimal_results[1].seq
      local opt_steps = #sim._debug_tokenize_for_animation(opt_seq)

      with_replay({ result.user_seq, opt_seq }, result.lines, function()
        local final_user = get_snapshot(1, user_steps)
        assert_eq(final_user.lines, result.goal_lines,
          "saved a.json user sequence should reach goal lines")
        assert_eq(final_user.cursor, { result.end_row + 1, result.end_col },
          "saved a.json user sequence should reach goal cursor")

        local final_opt = get_snapshot(2, opt_steps)
        assert_eq(final_opt.lines, result.goal_lines,
          "saved a.json optimal sequence should reach goal lines")
        assert_eq(final_opt.cursor, { result.end_row + 1, result.end_col },
          "saved a.json optimal sequence should reach goal cursor")
      end, next, { start_row = result.start_row, start_col = result.start_col })
    end,
  },
  {
    name = "simulate precompute drains first normal-mode token before snapshot",
    run = function(next)
      local repeats = 3
      local lines = {
        "int main() {",
        "  int m;",
        "  return 0;",
        "}",
      }
      local start_row, start_col = 1, 2  -- 0-indexed: row 2, col 2

      local iter = 1
      local function run_one()
        if iter > repeats then
          next(true)
          return
        end

        with_replay({
          "jf;i<BS>2<Esc><Space>ve",
          "$Ef3r2",
          "$Ef3s<Esc>",
        }, lines, function()

          local states = sim._debug_get_states()
          local function dump_trace(seq_idx)
            local s = states[seq_idx][2]
            local parts = { string.format(
              "      seq%d token=%q final=(%d,%d) mode=%s",
              seq_idx, s.token or "?", s.cursor[1], s.cursor[2], s.mode) }
            for _, p in ipairs(s.trace or {}) do
              parts[#parts + 1] = string.format(
                "      %-18s cursor=(%d,%d) mode=%s",
                p.label, p.cursor[1], p.cursor[2], p.mode)
            end
            return table.concat(parts, "\n")
          end
          assert_true(states[1][2].cursor[1] == 3,
            "iter " .. iter .. ": seq1 first token `j` should advance to row 3\n" ..
            dump_trace(1))
          assert_eq(states[2][2].cursor, { 2, 7 },
            "iter " .. iter .. ": seq2 first token `$` should land at EOL\n" ..
            dump_trace(2))
          assert_eq(states[3][2].cursor, { 2, 7 },
            "iter " .. iter .. ": seq3 first token `$` should land at EOL\n" ..
            dump_trace(3))
        end, function(ok, err)
          if not ok then
            next(false, err)
            return
          end
          iter = iter + 1
          vim.schedule(run_one)
        end, { start_row = start_row, start_col = start_col })
      end

      run_one()
    end,
  },
  {
    name = "simulate highlights only the visible panes, with treesitter when the filetype has a parser",
    run = function(next)
      local ts_start = vim.treesitter.start
      local started = {}
      ---@diagnostic disable-next-line: duplicate-set-field
      vim.treesitter.start = function(buf, lang)
        started[#started + 1] = buf
        ts_start(buf, lang)
      end
      with_replay({ "w", "e" }, { "local x = 1" }, function()
        local panes = { get_window(1).buf, get_window(2).buf }
        assert_eq(started, panes, "treesitter started on the panes, never the probe")
        for _, buf in ipairs(panes) do
          assert_true(vim.treesitter.highlighter.active[buf] ~= nil, "treesitter highlights pane " .. buf)
          assert_eq(vim.bo[buf].filetype, "", "no FileType, so no ftplugin or LSP")
        end
      end, function(ok, err)
        vim.treesitter.start = ts_start
        next(ok, err)
      end, { filetype = "lua" })
    end,
  },
  {
    name = "simulate falls back to 'syntax' for a filetype without a parser",
    run = function(next)
      with_replay({ "w" }, { "all: build" }, function()
        local buf = get_window(1).buf
        assert_eq(vim.bo[buf].syntax, "make", "syntax set")
        assert_eq(vim.bo[buf].filetype, "", "filetype left unset")
        assert_true(vim.treesitter.highlighter.active[buf] == nil, "no treesitter")
      end, next, { filetype = "make" })
    end,
  },
  {
    name = "simulate without a filetype leaves the panes unhighlighted",
    run = function(next)
      with_replay({ "w" }, { "all: build" }, function()
        local buf = get_window(1).buf
        assert_eq(vim.bo[buf].syntax, "", "no syntax")
        assert_true(vim.treesitter.highlighter.active[buf] == nil, "no treesitter")
      end, next)
    end,
  },
  }
end
