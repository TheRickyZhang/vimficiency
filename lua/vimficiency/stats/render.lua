---@class (exact) VF.Stats.Render
---@field build_pages fun(summary: VF.Stats.Summary, background: VF.Stats.Background, weight: VF.Stats.Weight): VF.Stats.Page[]
---@field run_lines fun(list: VF.Stats.RunList, offset: integer, sort?: VF.Stats.RunSort): string[]
---@field run_column_at fun(virtcol: integer): VF.Stats.Order|nil
---@field run_preview fun(row: VF.Stats.RunRow, detail: VF.Stats.RunDetail|nil, err: string|nil, width: integer): VF.Stats.RunPreview
local M = {}

local config = require("vimficiency.config")
local highlights = require("vimficiency.ui.highlights")
local log = require("vimficiency.stats.log")
local sequence_display = require("vimficiency.ui.sequence_display")

--- The column a run list is sorted by; without one it lists the worst first.
---@class VF.Stats.RunSort
---@field order VF.Stats.Order
---@field ascending boolean

---@class VF.Stats.RunPreview
---@field lines string[]
---@field marks { row: integer, col: integer, end_col: integer, hl: string }[]   # 0-indexed row, byte columns

--- The background scoring, as the Journal section reports it.
---@class VF.Stats.Background
---@field scoring boolean   # a run is in flight
---@field err? string       # why the last background run failed

--- Something in the stats that narrows to its runs.
---@class VF.Stats.DrillTarget
---@field row integer                         # 0-indexed line
---@field cols? { [1]: integer, [2]: integer } # a chart day's byte columns, [begin, end); nil: the whole line
---@field drill VF.Stats.Filter
---@field label string                        # its breadcrumb

---@class VF.Stats.Page
---@field lines string[]
---@field title_rows integer[]   # 0-indexed
---@field targets VF.Stats.DrillTarget[]

local SPARK_LEVELS = { "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█" }

-- 4 rows × 8 SPARK_LEVELS = 32 levels across 0..100.
local CHART_DAYS = 30
local CHART_ROWS = 4
local CHART_Y_LABELS = { "100", " 75", " 50", " 25" }
-- "    " + "100" + " ┤": the section indent, then the axis tick.
local CHART_PREFIX_WIDTH = 4 + 3 + 2
local SECONDS_PER_DAY = 86400

---@return VF.Stats.Page
local function new_page()
  return { lines = {}, title_rows = {}, targets = {} }
end

---@param out VF.Stats.Page
---@param s string
local function push(out, s)
  out.lines[#out.lines + 1] = s
end

---@param out VF.Stats.Page
---@param s string
local function push_title(out, s)
  push(out, s)
  out.title_rows[#out.title_rows + 1] = #out.lines - 1
end

--- Makes the last pushed line a drill target.
---@param out VF.Stats.Page
---@param drill VF.Stats.Filter
---@param label string
local function drillable(out, drill, label)
  out.targets[#out.targets + 1] = { row = #out.lines - 1, drill = drill, label = label }
end

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

--- `Nov 20`, from the local day the drill selects.
---@param day VF.Stats.DayScore
---@return string
local function day_label(day)
  -- Local day numbers count from 1970-01-01, so the UTC calendar names them.
  local t = day.drill.days.first * SECONDS_PER_DAY
  return os.date("!%b ", t) --[[@as string]] .. tonumber(os.date("!%d", t))
end

--- The title, CHART_ROWS rows, the baseline and the x-axis labels; every day
--- with data is a target in each row and the baseline.
---@param out VF.Stats.Page
---@param days VF.Stats.DayScore[]
local function push_daily_chart(out, days)
  push(out, "    Daily score")
  local by_ago = {}
  for _, day in ipairs(days) do by_ago[day.days_ago] = day end
  ---@param prefix string
  ---@param cell_of fun(day: VF.Stats.DayScore|nil): string
  local function push_row(prefix, cell_of)
    local cells, col = {}, #prefix
    for ago = CHART_DAYS - 1, 0, -1 do
      local day = by_ago[ago]
      local cell = cell_of(day)
      if day then
        out.targets[#out.targets + 1] = {
          row = #out.lines, cols = { col, col + #cell }, drill = day.drill, label = day_label(day),
        }
      end
      cells[#cells + 1] = cell
      col = col + #cell
    end
    push(out, prefix .. table.concat(cells, ""))
  end
  for i = 1, CHART_ROWS do
    push_row("    " .. CHART_Y_LABELS[i] .. " ┤", function(day)
      return chart_cell(day and day.score, CHART_ROWS - i)
    end)
  end
  push_row("      0 └", function() return "─" end)
  local left, right = CHART_DAYS .. "d", "today"
  push(out, string.rep(" ", CHART_PREFIX_WIDTH) .. left ..
    string.rep(" ", CHART_DAYS - #left - #right) .. right)
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

---@param out VF.Stats.Page
---@param summary VF.Stats.Summary
---@param weight VF.Stats.Weight
local function push_sessions(out, summary, weight)
  local lifetime, sessions = summary.lifetime, summary.sessions
  if lifetime.sessions == 0 then
    push(out, "    No sessions yet: :Vimfy start, watch or recall to add some.")
    return
  end
  push(out, string.format("    %s  (mark %d · watch %d · recall %d · suggest %d) · %s",
    plural(lifetime.sessions, "session"), lifetime.mark, lifetime.watch, lifetime.recall, lifetime.suggest,
    plural(lifetime.commands, "command")))
  push(out, string.format("    Score:    %s / 100  (%s, weighted by %s)", format_score(sessions.score),
    plural(sessions.score.spans, "scored session"), weight))
  if sessions.unbeaten > 0 then
    push(out, string.format("    Unbeaten (scored 100): %s", plural(sessions.unbeaten, "session")))
    drillable(out, sessions.unbeaten_drill, "unbeaten sessions")
  end
  if sessions.no_result > 0 then
    push(out, string.format("    Not scored: %s with no solution found", plural(sessions.no_result, "session")))
    drillable(out, sessions.no_result_drill, "no-result sessions")
  end
  push_daily_chart(out, sessions.days)
end

--- `Run length: 4 (90.0) · [8] (75.0) · 16 (—)`, the charted size bracketed.
---@param out VF.Stats.Page
---@param summary VF.Stats.JournalSummary
local function push_run_lengths(out, summary)
  local sizes = {}
  for _, size in ipairs(summary.sizes) do
    local name = size.size == summary.charted_size and "[" .. size.size .. "]" or tostring(size.size)
    sizes[#sizes + 1] = string.format("%s (%s)", name, format_score(size.score))
  end
  push(out, "    Run length: " .. table.concat(sizes, " · ") .. (#sizes > 1 and "   l to switch" or ""))
end

---@param out VF.Stats.Page
---@param summary VF.Stats.JournalSummary
---@param background VF.Stats.Background
local function push_journal(out, summary, background)
  if not summary.present then
    push(out, "    No journal recorded. Set journal = { enabled = true } to start one.")
    return
  end
  local scored = summary.journal_bytes > 0 and 100 * summary.scored_bytes / summary.journal_bytes or 100
  push(out, string.format("    Scored under the current settings: %d%% of the journal%s",
    math.floor(scored), background.scoring and " · scoring in the background…" or ""))
  if background.err then
    push(out, "    Last background scoring failed: " .. background.err)
  end
  if not background.scoring and scored < 100 and not config.journal.sampling.auto then
    push(out, "    Background scoring is off; :Vimfy stats refresh scores the rest.")
  end

  local charted = summary.charted_size
  if summary.score.spans == 0 then
    push(out, string.format("    No scored runs of %d commands yet.", charted))
    push_run_lengths(out, summary)
    return
  end
  push(out, string.format("    Score:    %s / 100  (%s of %d commands)",
    format_score(summary.score), plural(summary.score.spans, "run"), charted))
  push_run_lengths(out, summary)
  push_daily_chart(out, summary.days)

  push(out, string.format("    Movement only:  %s (%s)", format_score(summary.nav), plural(summary.nav.spans, "run")))
  drillable(out, summary.nav_drill, "nav")
  push(out, string.format("    With edits:     %s (%s)", format_score(summary.edit), plural(summary.edit.spans, "run")))
  drillable(out, summary.edit_drill, "edit")

  for i, entry in ipairs(summary.unscored) do
    local outcome = entry.outcome:gsub("_", " ")
    push(out, string.format("    %-16s%d %s", i == 1 and "Not scored:" or "", entry.runs, outcome))
    drillable(out, entry.drill, outcome)
  end
  if summary.unbeaten > 0 then
    push(out, string.format("    Unbeaten (scored 100): %s", plural(summary.unbeaten, "run")))
    drillable(out, summary.unbeaten_drill, "unbeaten runs")
  end
  if summary.deadline_hit > 0 then
    push(out, string.format("    Search cut short by the deadline: %s", plural(summary.deadline_hit, "run")))
  end
end

---@param out VF.Stats.Page
---@param rows VF.Stats.CommandRow[]
---@param side string   # breadcrumb suffix
local function push_command_rows(out, rows, side)
  if #rows == 0 then
    push(out, "    (no command is used 10 times yet)")
    return
  end
  for _, row in ipairs(rows) do
    local line = string.format("    %-10s %s   (%d vs %d)", row.text, format_ratio(row.ratio), row.user, row.optimizer)
    if row.split ~= "" then line = line .. "   " .. row.split end
    push(out, line)
    drillable(out, row.drill, row.text .. " " .. side)
  end
end

--- The stats as two pages: Sessions and Journal, then Commands and Dev.
---@param summary VF.Stats.Summary
---@param background VF.Stats.Background
---@param weight VF.Stats.Weight
---@return VF.Stats.Page[]
function M.build_pages(summary, background, weight)
  local out = new_page()
  push(out, "")
  if summary.hidden_runs > 0 then
    push(out, string.format("  %s from other scorings hidden (v)", plural(summary.hidden_runs, "run")))
    push(out, "")
  end
  push_title(out, "  Sessions")
  push_sessions(out, summary, weight)
  push(out, "")

  push_title(out, "  Journal")
  push_journal(out, summary.journal, background)
  push(out, "")

  local pages = { out }
  out = new_page()
  pages[2] = out
  push(out, "")
  local commands = summary.commands
  push_title(out, "  Commands — you use MORE than the optimizer")
  push_command_rows(out, commands.more, "(you more)")
  push(out, "")
  if #commands.never > 0 then
    push_title(out, "  Commands — you use, the optimizer NEVER does")
    push_command_rows(out, commands.never, "(you only)")
    if commands.never_cut > 0 then
      push(out, string.format("    … and %d more", commands.never_cut))
    end
    push(out, "")
  end
  push_title(out, "  Commands — you use LESS than the optimizer")
  push_command_rows(out, commands.less, "(you less)")
  push(out, string.format("    From %s and %s of %d commands.", plural(commands.sessions, "scored session"),
    plural(commands.runs, "journal run"), summary.journal.charted_size))
  push(out, string.format("    Characters typed in insert mode: you %d · optimizer %d",
    commands.user_typed_chars, commands.optimizer_typed_chars))
  push(out, "")

  push_title(out, "  Dev")
  push(out, string.format("    Optimizer beats queued: %d", summary.sessions.beats))
  local t = summary.sessions.timing
  if t then
    push(out, string.format(
      "    Optimizer time, last 30 days: p50 %.1f · p95 %.1f · p99 %.1f · max %.1f ms  (n=%d)",
      t.p50, t.p95, t.p99, t.max, t.n))
  end
  push(out, string.format("    Log: %s", vim.fn.fnamemodify(log.path(), ":~")))
  push(out, "")
  push(out, "  <CR> on a highlighted row or a chart day lists its runs · ? for keys")
  push(out, "")
  return pages
end

--- `s` left-aligned in `width` display columns.
---@param s string
---@param width integer
---@return string
local function pad(s, width)
  return s .. string.rep(" ", width - vim.fn.strdisplaywidth(s))
end

--- `line` cut to `width` display columns, ending in `…` when cut.
---@param line string
---@param width integer
---@return string
local function fit(line, width)
  if vim.fn.strdisplaywidth(line) <= width then return line end
  local chars = vim.fn.strcharlen(line)
  while chars > 0 and vim.fn.strdisplaywidth(vim.fn.strcharpart(line, 0, chars)) > width - 1 do
    chars = chars - 1
  end
  return vim.fn.strcharpart(line, 0, chars) .. "…"
end

--- How much less effort the best sequence takes, or why there is none.
---@param row VF.Stats.RunRow
---@return string
local function saved(row)
  if row.outcome == "unbeaten" then return "unbeaten" end
  if row.outcome ~= "scored" then return (row.outcome:gsub("_", " ")) end
  if row.user_cost == 0 then return "—" end
  local percent = math.floor((row.best_cost - row.user_cost) / row.user_cost * 100 + 0.5)
  if percent < 0 then return "−" .. -percent .. "%" end
  return (percent > 0 and "+" or "") .. percent .. "%"
end

---@class VF.Stats.RunColumn
---@field title string
---@field order VF.Stats.Order   # what sorting by it asks for
---@field width integer          # display columns
---@field cell fun(row: VF.Stats.RunRow): string

---@type VF.Stats.RunColumn[]
local RUN_COLUMNS = {
  { title = "when", order = "newest", width = 12,
    cell = function(row) return os.date("%b %d %H:%M", row.wall_epoch) --[[@as string]] end },
  { title = "you", order = "user_cost", width = 8,
    cell = function(row) return string.format("%.1f", row.user_cost) end },
  { title = "found", order = "best_cost", width = 8,
    cell = function(row)
      local has_best = row.outcome == "scored" or row.outcome == "unbeaten"
      return has_best and string.format("%.1f", row.best_cost) or "—"
    end },
  { title = "saved", order = "saved", width = 10, cell = saved },
  { title = "search ms", order = "slowest", width = 11,
    cell = function(row) return string.format("%.1f", row.analyze_ms) end },
}
local RUN_COLUMN_GAP = 2

---@param cells string[]   # one per RUN_COLUMNS
---@return string
local function run_line(cells)
  local out = {}
  for i, column in ipairs(RUN_COLUMNS) do
    local cell = cells[i]
    -- The first column reads as a label; the rest are figures.
    local space = string.rep(" ", column.width - vim.fn.strdisplaywidth(cell))
    out[i] = i == 1 and cell .. space or space .. cell
  end
  return " " .. table.concat(out, string.rep(" ", RUN_COLUMN_GAP))
end

--- The order the run-list column at 1-indexed display column `virtcol` sorts
--- by. A column owns the gap to its right.
---@param virtcol integer
---@return VF.Stats.Order|nil
function M.run_column_at(virtcol)
  local first = 2
  for _, column in ipairs(RUN_COLUMNS) do
    local following = first + column.width + RUN_COLUMN_GAP
    if virtcol >= first and virtcol < following then return column.order end
    first = following
  end
end

--- The run list: a header, a line per run, then a footer.
---@param list VF.Stats.RunList
---@param offset integer
---@param sort? VF.Stats.RunSort   # marked in the header
---@return string[]
function M.run_lines(list, offset, sort)
  local titles = {}
  for i, column in ipairs(RUN_COLUMNS) do
    local mark = sort and sort.order == column.order and (sort.ascending and " ▲" or " ▼") or ""
    titles[i] = column.title .. mark
  end
  local lines = { run_line(titles) }
  for _, row in ipairs(list.runs) do
    local cells = {}
    for i, column in ipairs(RUN_COLUMNS) do cells[i] = column.cell(row) end
    lines[#lines + 1] = run_line(cells)
  end
  lines[#lines + 1] = ""
  local footer = {
    #list.runs > 0 and string.format("runs %d–%d of %d", offset + 1, offset + #list.runs, list.total)
      or string.format("no runs to list of %d", list.total),
  }
  if list.rolled_up > 0 then
    footer[#footer + 1] = string.format("%d rolled up (not listable)", list.rolled_up)
  end
  if list.rollup_runs_without_commands > 0 then
    footer[#footer + 1] = string.format("%d rolled up without commands, left out", list.rollup_runs_without_commands)
  end
  lines[#lines + 1] = table.concat(footer, " · ")
  return lines
end

---@param lines string[]
---@return string
local function joined(lines)
  return table.concat(lines, "\n") .. "\n"
end

--- A run's keys in full, then its text before and after side by side with the
--- changed lines marked. `detail` is nil for a session, which keeps no text,
--- and for a run whose journal is gone (`err` says why).
---@param row VF.Stats.RunRow
---@param detail VF.Stats.RunDetail|nil
---@param err string|nil
---@param width integer
---@return VF.Stats.RunPreview
function M.run_preview(row, detail, err, width)
  local lines = {
    " you:   " .. sequence_display.inline(row.user_keys),
    " found: " .. (row.best_seq == "" and "—" or sequence_display.inline(row.best_seq)),
    "",
  }
  local marks = {}
  if not detail then
    lines[#lines + 1] = " " .. (err and "Text not available: " .. err or "A session keeps no text.")
    return { lines = lines, marks = marks }
  end

  local half = math.floor((width - 4) / 2)
  local tab = string.rep(" ", vim.o.tabstop)
  ---@param left string
  ---@param right string
  ---@return integer row      # 0-indexed
  ---@return integer divider  # byte column the left cell ends at
  local function push_pair(left, right)
    local cell = pad(fit((left:gsub("\t", tab)), half), half)
    lines[#lines + 1] = " " .. cell .. " │ " .. fit((right:gsub("\t", tab)), half)
    return #lines - 1, 1 + #cell
  end
  push_pair(string.format("before · cursor %d:%d", detail.start_row + 1, detail.start_col + 1),
    string.format("after · cursor %d:%d", detail.end_row + 1, detail.end_col + 1))

  local changed_before, changed_after = {}, {}
  local hunks = vim.diff(joined(detail.lines), joined(detail.goal_lines), { result_type = "indices" })
  for _, hunk in ipairs(hunks --[[@as integer[][] ]]) do
    for i = hunk[1], hunk[1] + hunk[2] - 1 do changed_before[i] = true end
    for i = hunk[3], hunk[3] + hunk[4] - 1 do changed_after[i] = true end
  end
  for i = 1, math.max(#detail.lines, #detail.goal_lines) do
    local at, divider = push_pair(detail.lines[i] or "", detail.goal_lines[i] or "")
    if changed_before[i] then
      marks[#marks + 1] = { row = at, col = 1, end_col = divider, hl = highlights.VIEW_DIFF_DELETE }
    end
    if changed_after[i] then
      marks[#marks + 1] = { row = at, col = divider + #" │ ", end_col = #lines[at + 1], hl = highlights.VIEW_DIFF_ADD }
    end
  end
  return { lines = lines, marks = marks }
end

return M
