-- `:Vimfy stats`: draws the summary src/Stats/ reduces from the session log
-- (stats/log.lua) and the journal samples (stats/sampling.lua). Scores come
-- from one stamp's sessions at a time, the current one first, since scores made
-- under different stamps do not compare; <Tab> cycles through the others.
--
-- The journal is scored in the background. The float shows what is scored so
-- far and redraws when a run in flight finishes.

local v = vim.api
local config = require("vimficiency.config")
local ffi_stats = require("vimficiency.ffi.stats")
local journal = require("vimficiency.capture.journal")
local log = require("vimficiency.stats.log")
local sampling = require("vimficiency.stats.sampling")
local util = require("vimficiency.util")

--- The background scoring, as the Journal section reports it.
---@class VF.Stats.Background
---@field scoring boolean   # a run is in flight
---@field err? string       # why the last background run failed

local M = {}

local SPARK_LEVELS = { "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█" }

-- 4 rows × 8 SPARK_LEVELS = 32 levels across 0..100.
local CHART_DAYS = 30
local CHART_ROWS = 4
local CHART_Y_LABELS = { "100", " 75", " 50", " 25" }
-- "    " + "100" + " ┤": the section indent, then the axis tick.
local CHART_PREFIX_WIDTH = 4 + 3 + 2

---@param score VF.Stats.Score
---@return string
local function format_score(score)
  if score.spans == 0 then return "—" end
  return string.format("%.1f", score.score)
end

---Cell for a day's `score` in a chart row; `row_from_bottom` is 0 at the
---baseline. The day's height in eighths fills rows from the bottom up.
local function chart_cell(score, row_from_bottom)
  if not score then return " " end
  local total_eighths = math.floor((score * CHART_ROWS * 8 / 100) + 0.5)
  local row_eighths = total_eighths - row_from_bottom * 8
  if row_eighths <= 0 then return " " end
  if row_eighths >= 8 then return "█" end
  return SPARK_LEVELS[row_eighths]
end

---@param days VF.Stats.DayScore[]
---@return string[]  # CHART_ROWS rows, the baseline and the x-axis label
local function render_30day_chart(days)
  local by_ago = {}
  for _, day in ipairs(days) do by_ago[day.days_ago] = day.score end
  local out = {}
  for i = 1, CHART_ROWS do
    local cells = {}
    for ago = CHART_DAYS - 1, 0, -1 do
      cells[#cells + 1] = chart_cell(by_ago[ago], CHART_ROWS - i)
    end
    out[#out + 1] = "    " .. CHART_Y_LABELS[i] .. " ┤" .. table.concat(cells, "")
  end
  out[#out + 1] = "      0 └" .. string.rep("─", CHART_DAYS)
  local left, right = "30 days ago", "today"
  out[#out + 1] = string.rep(" ", CHART_PREFIX_WIDTH) .. left ..
    string.rep(" ", CHART_DAYS - #left - #right) .. right
  return out
end

local function format_ratio(r)
  if r == math.huge then return "  ∞×" end
  return string.format("%5.2f×", r)
end

---@param n integer
---@param noun string
---@return string
local function plural(n, noun)
  return string.format("%d %s%s", n, noun, n == 1 and "" or "s")
end

---@param sessions VF.Stats.SessionSummary
---@param push fun(line: string)
local function push_sessions(sessions, push)
  push(string.format("    Scoring:  %s%s", sessions.label, sessions.current and " (current)" or " (older)"))
  if sessions.groups > 1 then
    push(string.format("    %s under %s · <Tab> to switch",
      plural(sessions.other_sessions, "session"), plural(sessions.groups - 1, "other scoring")))
  end
  push(string.format("    Score:    %s / 100  (%s, weighted by %s)", format_score(sessions.score),
    plural(sessions.score.spans, "scored session"), config.stats.weight))
  if sessions.unbeaten > 0 then
    push(string.format("    Unbeaten (scored 100): %s", plural(sessions.unbeaten, "session")))
  end
  if sessions.no_result > 0 then
    push(string.format("    Not scored: %s with no solution found", plural(sessions.no_result, "session")))
  end
  push("    30-day score:")
  for _, line in ipairs(render_30day_chart(sessions.days)) do push(line) end
end

---@param summary VF.Stats.JournalSummary
---@param background VF.Stats.Background
---@param push fun(line: string)
local function push_journal(summary, background, push)
  if not summary.present then
    push("    No journal recorded. Set journal = { enabled = true } to start one.")
    return
  end
  local scored = summary.journal_bytes > 0 and 100 * summary.scored_bytes / summary.journal_bytes or 100
  push(string.format("    Scored under the current settings: %d%% of the journal%s",
    math.floor(scored), background.scoring and " · scoring in the background…" or ""))
  if background.err then
    push("    Last background scoring failed: " .. background.err)
  end
  if not background.scoring and scored < 100 and not config.journal.sampling.auto then
    push("    Background scoring is off; :Vimfy stats refresh scores the rest.")
  end

  local charted = summary.charted_size
  if summary.score.spans == 0 then
    push(string.format("    No scored runs of %d commands yet.", charted))
    return
  end
  push(string.format("    Score:    %s / 100  (%s of %d commands)",
    format_score(summary.score), plural(summary.score.spans, "run"), charted))
  push(string.format("    30-day score, runs of %d commands:", charted))
  for _, line in ipairs(render_30day_chart(summary.days)) do push(line) end

  local per_size = {}
  for _, size in ipairs(summary.sizes) do
    per_size[#per_size + 1] = string.format("%d → %s", size.size, format_score(size.score))
  end
  push("    By run length:  " .. table.concat(per_size, "   "))
  push(string.format("    Movement only:  %s (%s)   With edits: %s (%s)",
    format_score(summary.nav), plural(summary.nav.spans, "run"),
    format_score(summary.edit), plural(summary.edit.spans, "run")))

  local unscored = {}
  for _, entry in ipairs(summary.unscored) do
    unscored[#unscored + 1] = string.format("%d %s", entry.runs, (entry.outcome:gsub("_", " ")))
  end
  if #unscored > 0 then
    push("    Not scored:     " .. table.concat(unscored, " · "))
  end
  if summary.unbeaten > 0 then
    push(string.format("    Unbeaten (scored 100): %s", plural(summary.unbeaten, "run")))
  end
  if summary.deadline_hit > 0 then
    push(string.format("    Search cut short by the deadline: %s", plural(summary.deadline_hit, "run")))
  end
end

---@param rows VF.Stats.CommandRow[]
---@param push fun(line: string)
local function push_command_rows(rows, push)
  if #rows == 0 then
    push("    (no command is used 10 times yet)")
    return
  end
  for _, row in ipairs(rows) do
    local line = string.format("    %-10s %s   (%d vs %d)", row.text, format_ratio(row.ratio), row.user, row.optimizer)
    if row.split ~= "" then line = line .. "   " .. row.split end
    push(line)
  end
end

---@param summary VF.Stats.Summary
---@param background VF.Stats.Background
---@return string[] lines
---@return integer[] title_rows  0-indexed rows that should render as section titles
function M.build_lines(summary, background)
  local L = {}
  local title_rows = {}
  local function push(s) L[#L + 1] = s end
  local function push_title(s)
    push(s)
    title_rows[#title_rows + 1] = #L - 1
  end

  push("")
  push_title("  Lifetime")
  local lifetime = summary.lifetime
  push(string.format("    Sessions: %d  (mark %d · watch %d · recall %d · suggest %d)",
    lifetime.sessions, lifetime.mark, lifetime.watch, lifetime.recall, lifetime.suggest))
  push(string.format("    Commands: %d recorded in sessions", lifetime.commands))
  push("")

  push_title("  Efficiency")
  push_sessions(summary.sessions, push)
  push("")

  push_title("  Journal")
  push_journal(summary.journal, background, push)
  push("")

  local commands = summary.commands
  push_title("  Commands — you use MORE than the optimizer")
  push_command_rows(commands.more, push)
  push("")
  push_title("  Commands — you use LESS than the optimizer")
  push_command_rows(commands.less, push)
  push(string.format("    From %s and %s of %d commands.", plural(commands.sessions, "scored session"),
    plural(commands.runs, "journal run"), summary.journal.charted_size))
  push(string.format("    Characters typed in insert mode: you %d · optimizer %d",
    commands.user_typed_chars, commands.optimizer_typed_chars))
  push("")

  push_title("  Dev")
  push(string.format("    Optimizer beats queued: %d", summary.sessions.beats))
  local t = summary.sessions.timing
  if t then
    push(string.format(
      "    Optimizer time, last 30 days: p50 %.1f · p95 %.1f · p99 %.1f · max %.1f ms  (n=%d)",
      t.p50, t.p95, t.p99, t.max, t.n))
  end
  push(string.format("    Log: %s", vim.fn.fnamemodify(log.path(), ":~")))
  push("")
  push("  Press q / <Esc> to close.")
  push("")
  return L, title_rows
end

--- The summary with stamp group `shown` (1-based) as the Efficiency section.
---@param shown integer
---@return VF.Stats.Summary
function M.summary(shown)
  return ffi_stats.summary({
    session_log = log.path(),
    stamp = log.stamp(),
    shown_group = shown - 1,
    journal_dir = journal.dir(),
    sample_params = sampling.params(),
    weight = config.stats.weight,
  })
end

-- Links to Title so section titles follow the colorscheme's headings.
v.nvim_set_hl(0, "VimficiencyStatsSectionTitle", { link = "Title", default = true })
local stats_ns = v.nvim_create_namespace("vimfy_stats")

local WIDTH = 72

---@param line_count integer
---@return table  # float geometry that centers `line_count` lines
local function geometry(line_count)
  local height = math.min(line_count, vim.o.lines - 4)
  return {
    relative = "editor",
    row = math.floor((vim.o.lines - height) / 2),
    col = math.floor((vim.o.columns - WIDTH) / 2),
    width = WIDTH,
    height = height,
  }
end

---@return VF.Stats.Background
local function background()
  return { scoring = sampling.running(), err = sampling.last_error() }
end

function M.open()
  local shown = 1
  local summary = M.summary(shown)

  local buf = v.nvim_create_buf(false, true)
  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  vim.bo[buf].swapfile = false
  vim.bo[buf].filetype = "vimficiency_stats"

  ---@return integer line_count
  local function render()
    local lines, title_rows = M.build_lines(summary, background())
    vim.bo[buf].modifiable = true
    v.nvim_buf_set_lines(buf, 0, -1, false, lines)
    v.nvim_buf_clear_namespace(buf, stats_ns, 0, -1)
    for _, row in ipairs(title_rows) do
      v.nvim_buf_set_extmark(buf, stats_ns, row, 0, {
        end_row = row + 1, end_col = 0,
        hl_group = "VimficiencyStatsSectionTitle",
        hl_eol = true,
      })
    end
    vim.bo[buf].modifiable = false
    return #lines
  end

  local win = v.nvim_open_win(buf, true, vim.tbl_extend("error", geometry(render()), {
    border = "rounded", style = "minimal",
    title = " Vimfy Stats ", title_pos = "center",
  }))
  util.configure_scratch_window(win)

  local function close()
    pcall(v.nvim_win_close, win, true)
  end
  local function next_scoring()
    shown = shown % summary.sessions.groups + 1
    summary = M.summary(shown)
    v.nvim_win_set_config(win, geometry(render()))
  end
  vim.keymap.set("n", "q",     close, { buffer = buf, nowait = true, silent = true })
  vim.keymap.set("n", "<Esc>", close, { buffer = buf, nowait = true, silent = true })
  vim.keymap.set("n", "<Tab>", next_scoring, { buffer = buf, nowait = true, silent = true })

  sampling.when_done(function()
    if not v.nvim_win_is_valid(win) then return end
    summary = M.summary(shown)
    v.nvim_win_set_config(win, geometry(render()))
  end)
end

--- Scores the journal without opening the float.
function M.refresh()
  sampling.refresh(function(err)
    if err then
      vim.notify("vimfy: could not score the journal: " .. err, vim.log.levels.ERROR)
    else
      vim.notify("vimfy: journal scored")
    end
  end)
end

return M
