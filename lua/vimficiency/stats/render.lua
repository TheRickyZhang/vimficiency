---@class (exact) VF.Stats.Render
---@field build_pages fun(summary: VF.Stats.Summary, background: VF.Stats.Background, weight: VF.Stats.Weight, split: boolean): VF.Stats.Page[]
---@field run_lines fun(list: VF.Stats.RunList, offset: integer, width: integer): string[]
local M = {}

local config = require("vimficiency.config")
local log = require("vimficiency.stats.log")

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

--- The stats as one page, or with `split` as two: Sessions and Journal, then
--- Commands and Dev.
---@param summary VF.Stats.Summary
---@param background VF.Stats.Background
---@param weight VF.Stats.Weight
---@param split boolean
---@return VF.Stats.Page[]
function M.build_pages(summary, background, weight, split)
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
  if split then
    out = new_page()
    pages[2] = out
    push(out, "")
  end
  local commands = summary.commands
  push_title(out, "  Commands — you use MORE than the optimizer")
  push_command_rows(out, commands.more, "(you more)")
  push(out, "")
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

local WHEN_WIDTH = 12
local EFFORT_WIDTH = 13
local SAVED_WIDTH = 11

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

---@param row VF.Stats.RunRow
---@return string
local function run_line(row)
  local has_best = row.outcome == "scored" or row.outcome == "unbeaten"
  local effort = string.format("%5.1f → %s", row.user_cost, has_best and string.format("%.1f", row.best_cost) or "—")
  return string.format("%s   %s  %s  you: %s   best: %s", os.date("%b %d %H:%M", row.wall_epoch),
    pad(effort, EFFORT_WIDTH), pad(saved(row), SAVED_WIDTH), row.user_display,
    row.best_display == "" and "—" or row.best_display)
end

---@param list VF.Stats.RunList
---@param offset integer
---@param width integer
---@return string[]
function M.run_lines(list, offset, width)
  local lines = {
    fit(string.format("%s   %s  %s  keys", pad("when", WHEN_WIDTH), pad("effort", EFFORT_WIDTH),
      pad("saved", SAVED_WIDTH)), width),
  }
  for _, row in ipairs(list.runs) do lines[#lines + 1] = fit(run_line(row), width) end
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

return M
