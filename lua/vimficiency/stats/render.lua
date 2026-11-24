---@class (exact) VF.Stats.Render
---@field SPLIT_MIN_WIDTH integer
---@field build_page fun(summary: VF.Stats.Summary, background: VF.Stats.Background, width?: integer): VF.Stats.Page
---@field chart_page fun(summary: VF.Stats.Summary, name: VF.Stats.ChartName, width: integer, height: integer): VF.Stats.Page
---@field run_lines fun(list: VF.Stats.RunList, offset: integer, sort?: VF.Stats.RunSort): string[]
---@field run_items fun(lines: string[], count: integer): VF.Stats.RunCell[]
---@field run_preview fun(row: VF.Stats.RunRow, detail: VF.Stats.RunDetail|nil, err: string|nil, width: integer): VF.Stats.RunPreview
local M = {}

local config = require("vimficiency.config")
local highlights = require("vimficiency.ui.highlights")
local sequence_display = require("vimficiency.ui.sequence_display")

--- The column a run list is sorted by; without one it lists the worst first.
---@class VF.Stats.RunSort
---@field order VF.Stats.Order
---@field ascending boolean

---@class VF.Stats.RunPreview
---@field lines string[]
---@field marks { row: integer, col: integer, end_col: integer, hl: string, priority?: integer }[]   # 0-indexed row, byte columns

--- The background scoring, as the Journal section reports it.
---@class VF.Stats.Background
---@field scoring boolean   # a run is in flight
---@field err? string       # why the last background run failed

---@alias VF.Stats.ChartName "sessions"|"journal"

--- Something in the stats the keys act on: a row that lists its runs, a daily
--- chart, a bucket of a chart opened on its own, or a selector row.
---@class VF.Stats.Item : VF.Ui.NavItem
---@field kind "drill"|"chart"|"day"|"run_length"|"chart_unit"
---@field drill? VF.Stats.Filter   # the runs <CR> lists: a row's or a day's
---@field label? string            # those runs' title
---@field chart? VF.Stats.ChartName
---@field score? number            # a day's
---@field samples? integer         # the runs a day's score is over
---@field selector? { values: (string|integer)[], index: integer }

---@class VF.Stats.Mark
---@field row integer       # 0-indexed
---@field col integer       # byte columns [col, end_col)
---@field end_col integer
---@field hl string
---@field priority? integer

---@class VF.Stats.Page
---@field lines string[]
---@field marks VF.Stats.Mark[]   # section titles and the rows that list runs
---@field items VF.Stats.Item[]

local SPARK_LEVELS = { "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█" }

local CHART_BUCKETS = 30
local CHART_ROWS = 5
local TIME_TICK_STEP = 5
-- "100" + " ┤": the axis tick.
local CHART_AXIS_WIDTH = 3 + 2
local SECONDS_PER_DAY = 86400

---@return VF.Stats.Page
local function new_page()
  return { lines = {}, marks = {}, items = {} }
end

--- `s` left-aligned in `width` display columns.
---@param s string
---@param width integer
---@return string
local function pad(s, width)
  return s .. string.rep(" ", width - vim.fn.strdisplaywidth(s))
end

--- Marks the last pushed line's text.
---@param out VF.Stats.Page
---@param hl string
local function mark_text(out, hl)
  local line = out.lines[#out.lines]
  out.marks[#out.marks + 1] = { row = #out.lines - 1, col = line:find("%S") - 1, end_col = #line, hl = hl }
end

---@param out VF.Stats.Page
---@param s string
local function push(out, s)
  out.lines[#out.lines + 1] = s
end

local PROSE_INDENT = "    "

--- `text` word-wrapped into `width` display columns; unwrapped without one.
---@param out VF.Stats.Page
---@param text string
---@param width? integer
local function push_prose(out, text, width)
  local line = PROSE_INDENT
  for word in text:gmatch("%S+") do
    local longer = line == PROSE_INDENT and line .. word or line .. " " .. word
    if width and line ~= PROSE_INDENT and vim.fn.strdisplaywidth(longer) > width then
      push(out, line)
      longer = PROSE_INDENT .. word
    end
    line = longer
  end
  push(out, line)
end

---@param out VF.Stats.Page
---@param s string
local function push_title(out, s)
  push(out, s)
  mark_text(out, highlights.STATS_SECTION_TITLE)
end

--- Makes the last pushed line list its runs.
---@param out VF.Stats.Page
---@param drill VF.Stats.Filter
---@param label string
---@param id? string   # when the label holds counts, which change
local function drillable(out, drill, label, id)
  local row = #out.lines - 1
  out.items[#out.items + 1] = {
    row = row, last_row = row, cursor = { row, out.lines[#out.lines]:find("%S") - 1 },
    id = id or label, kind = "drill", drill = drill, label = label,
  }
  mark_text(out, highlights.STATS_DRILL)
end

---@param score VF.Stats.Score
---@return string
local function format_score(score)
  if score.spans == 0 then return "—" end
  return string.format("%.1f%%", score.score)
end

---Cell for a day's `score` in a chart of `rows` rows; `row_from_bottom` is 0
---at the baseline. The day's height in eighths fills rows from the bottom up.
local function chart_cell(score, row_from_bottom, rows)
  if not score then return " " end
  local total_eighths = math.floor((score * rows * 8 / 100) + 0.5)
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
  if day.drill.epoch_seconds then
    return os.date("%b %d %H:%M", day.drill.epoch_seconds.first) --[[@as string]]
  end
  local t = day.drill.days.first * SECONDS_PER_DAY
  return os.date("!%b ", t) --[[@as string]] .. tonumber(os.date("!%d", t))
end

--- `n` in at most three columns: `940`, `12k`.
---@param n integer
---@return string
local function compact_count(n)
  return n < 1000 and tostring(n) or math.floor(n / 1000 + 0.5) .. "k"
end

--- Score ticks sit a whole number of rows apart: the step that fills the most
--- of `room` rows, and of those the finest.
---@param room integer
---@return integer rows
---@return integer score_step
---@return integer tick_spacing   # rows
local function chart_scale(room)
  local rows, score_step, tick_spacing = 0, 0, 0
  for _, step in ipairs({ 5, 10, 20 }) do
    local spacing = math.floor(room * step / 100)
    if spacing * (100 / step) > rows then
      rows, score_step, tick_spacing = spacing * (100 / step), step, spacing
    end
  end
  return rows, score_step, tick_spacing
end

--- The line of time labels under a chart whose buckets are `day_width` columns.
---@param axis_indent string
---@param day_width integer
---@param unit string
---@return string
local function time_axis(axis_indent, day_width, unit)
  local axis = axis_indent
  local plot_width = day_width * CHART_BUCKETS
  local label_end = 0
  for ago = CHART_BUCKETS, 0, -TIME_TICK_STEP do
    local label = ago .. unit:sub(1, 1)
    local tick = (CHART_BUCKETS - ago) * day_width
    local col = math.max(0, math.min(tick - math.floor(#label / 2), plot_width - #label))
    axis = axis .. string.rep(" ", col - label_end) .. label
    label_end = col + #label
  end
  return axis
end

---@param day VF.Stats.DayScore
---@param unit string
---@return string
local function day_id(day, unit)
  return (unit == "days" and "day" or unit) .. ":" .. (day.drill.epoch_seconds or day.drill.days).first
end

--- Fit the bars, score ticks, and time labels into the available columns and rows.
---@param out VF.Stats.Page
---@param days VF.Stats.DayScore[]
---@param indent string
---@param width integer
---@param height integer
---@return VF.Stats.Item[] day_items   # each day with data as its column, most recent first
local function push_chart(out, days, indent, width, height, unit)
  local by_ago = {}
  for _, day in ipairs(days) do by_ago[day.days_ago] = day end
  local first = #out.lines
  local day_width = math.floor(width / CHART_BUCKETS)
  local gap = day_width >= 3 and 1 or 0
  local bar = day_width - gap
  local rows, score_step, tick_spacing = chart_scale(math.max(CHART_ROWS, height - 2))

  -- Each day's highlighted cells, by days_ago: its bar in every row.
  ---@type table<integer, { [1]: integer, [2]: integer, [3]: integer }[]>
  local spans = {}
  ---@param prefix string
  ---@param cell_of fun(day: VF.Stats.DayScore|nil, ago: integer): string
  ---@param fill? string   # what the gap after a day is drawn with
  local function push_row(prefix, cell_of, fill)
    local line = prefix
    for ago = CHART_BUCKETS - 1, 0, -1 do
      local cell = cell_of(by_ago[ago], ago)
      spans[ago] = spans[ago] or {}
      table.insert(spans[ago], { #out.lines, #line, #line + #cell })
      line = line .. cell .. string.rep(fill or " ", gap)
    end
    push(out, line)
  end

  for row = rows, 1, -1 do
    local prefix = row % tick_spacing == 0
      and string.format("%s%3d ┤", indent, row / tick_spacing * score_step)
      or indent .. "    │"
    push_row(prefix, function(day)
      return string.rep(chart_cell(day and day.score, row - 1, rows), bar)
    end)
  end
  local baseline = #out.lines
  push_row(indent .. "  0 └", function(_, ago)
    if (ago + 1) % TIME_TICK_STEP == 0 then return "┬" .. string.rep("─", bar - 1) end
    return string.rep("─", bar)
  end, "─")
  local axis_indent = indent .. string.rep(" ", CHART_AXIS_WIDTH)
  push(out, time_axis(axis_indent, day_width, unit))

  ---@type VF.Stats.Item[]
  local items = {}
  local plot = vim.fn.strdisplaywidth(axis_indent)
  for ago = 0, CHART_BUCKETS - 1 do
    local day = by_ago[ago]
    if day then
      local left = (CHART_BUCKETS - 1 - ago) * day_width
      items[#items + 1] = {
        row = first, last_row = spans[ago][#spans[ago]][1],
        left = plot + left, right = plot + left + bar,
        cursor = { baseline, spans[ago][rows + 1][2] },
        spans = spans[ago],
        id = day_id(day, unit), kind = "day",
        drill = day.drill, label = day_label(day), score = day.score, samples = day.spans,
      }
    end
  end
  return items
end

-- A braille cell's dots, by column then row from the top.
local BRAILLE_DOTS = { { 0x01, 0x02, 0x04, 0x40 }, { 0x08, 0x10, 0x20, 0x80 } }
local POINT = "•"
-- One dot in this many along a dotted line.
local DOTTED_PERIOD = 3
-- "├ " and a count.
local RIGHT_AXIS_WIDTH = 2 + 3

--- Two lines over the same buckets: the scores against the left axis and the
--- sample counts against the right. A line breaks where a bucket has no data.
---@param out VF.Stats.Page
---@param days VF.Stats.DayScore[]
---@param indent string
---@param width integer    # of the plot
---@param height integer
---@param unit string
---@return VF.Stats.Item[] day_items   # each day with data as its column, most recent first
local function push_line_chart(out, days, indent, width, height, unit)
  local by_ago = {}
  local most = 0
  for _, day in ipairs(days) do
    by_ago[day.days_ago] = day
    most = math.max(most, day.spans)
  end
  local day_width = math.floor(width / CHART_BUCKETS)
  local bar = day_width - (day_width >= 3 and 1 or 0)
  local cells = day_width * CHART_BUCKETS
  -- Over the plot a legend; under it the baseline and the time labels.
  local rows, score_step, tick_spacing = chart_scale(math.max(CHART_ROWS, height - 3))
  local ticks = 100 / score_step
  -- The counts' labels are whole: they take the most of the ticks, evenly
  -- spaced, that the busiest bucket has samples for.
  local count_ticks = 1
  for divisor = 2, ticks do
    if ticks % divisor == 0 and divisor <= most then count_ticks = divisor end
  end
  local count_step = math.max(1, math.ceil(most / count_ticks))
  local ticks_per_count = ticks / count_ticks

  -- Per series, row from the top and cell: the cell's dots.
  ---@type table<integer, table<integer, integer>>[]
  local dots = { {}, {} }
  -- Per series, row and cell: a bucket's own value is there, drawn as one
  -- larger dot; what joins two buckets is not data.
  ---@type table<integer, table<integer, boolean>>[]
  local points = { {}, {} }
  ---@param series integer
  ---@param x integer   # dot column
  ---@param y integer   # dot row, up from the baseline
  local function plot(series, x, y)
    local row, cell = rows - math.floor(y / 4), math.floor(x / 2) + 1
    dots[series][row] = dots[series][row] or {}
    dots[series][row][cell] = bit.bor(dots[series][row][cell] or 0, BRAILLE_DOTS[x % 2 + 1][4 - y % 4])
  end
  ---@param ago integer
  ---@param share number   # of the axis
  ---@return integer x
  ---@return integer y
  local function point(ago, share)
    return (CHART_BUCKETS - 1 - ago) * day_width * 2 + bar, math.floor(share * (rows * 4 - 1) + 0.5)
  end
  for series, share_of in ipairs({
    function(day) return day.score / 100 end,
    function(day) return day.spans / (count_step * count_ticks) end,
  }) do
    for ago = CHART_BUCKETS - 1, 0, -1 do
      local day, following = by_ago[ago], by_ago[ago - 1]
      if day then
        local x, y = point(ago, share_of(day))
        local row = rows - math.floor(y / 4)
        points[series][row] = points[series][row] or {}
        points[series][row][math.floor(x / 2) + 1] = true
        if following then
          local x1, y1 = point(ago - 1, share_of(following))
          -- A point is drawn in the middle of its cell, so the line runs
          -- between the cells' middle dots: out of the right column and
          -- into the left, on the rows that face each other.
          local base, base1 = y - y % 4, y1 - y1 % 4
          y = base + (base1 > base and 2 or 1)
          y1 = base1 + (base1 < base and 2 or 1)
          x, x1 = x - x % 2 + 1, x1 - x1 % 2
          local dx, dy, sy = x1 - x, math.abs(y1 - y), y1 > y and 1 or -1
          local err = dx - dy
          local step = 0
          while x ~= x1 or y ~= y1 do
            local doubled = 2 * err
            if doubled > -dy then err, x = err - dy, x + 1 end
            if doubled < dx then err, y = err + dx, y + sy end
            step = step + 1
            -- The counts' line is dotted, to tell it from the scores'.
            if series == 1 or step % DOTTED_PERIOD == 0 then plot(series, x, y) end
          end
        end
      end
    end
  end

  ---@param col integer
  ---@param text string
  ---@param hl string
  local function mark(col, text, hl)
    out.marks[#out.marks + 1] = { row = #out.lines, col = col, end_col = col + #text, hl = hl }
  end
  local axis_indent = indent .. string.rep(" ", CHART_AXIS_WIDTH)
  local legend = { "⠒⠒ efficiency %", "⠂⠂ samples per " .. unit:sub(1, -2) }
  mark(#axis_indent, legend[1], highlights.STATS_EFFICIENCY)
  mark(#axis_indent + #legend[1] + 3, legend[2], highlights.STATS_VOLUME)
  push(out, axis_indent .. legend[1] .. "   " .. legend[2])

  local first = #out.lines
  -- Each day's highlighted cells, by days_ago: its column in every row.
  ---@type table<integer, { [1]: integer, [2]: integer, [3]: integer }[]>
  local spans = {}
  ---@param prefix string
  ---@param cell_of fun(cell: integer): string, string|nil   # its text and highlight
  ---@return string line
  local function plot_row(prefix, cell_of)
    local parts, starts, col = { prefix }, {}, #prefix
    for cell = 1, cells do
      local text, hl = cell_of(cell)
      starts[cell] = col
      if hl then mark(col, text, hl) end
      parts[#parts + 1] = text
      col = col + #text
    end
    starts[cells + 1] = col
    for ago = 0, CHART_BUCKETS - 1 do
      local left = (CHART_BUCKETS - 1 - ago) * day_width
      spans[ago] = spans[ago] or {}
      table.insert(spans[ago], { #out.lines, starts[left + 1], starts[left + bar + 1] })
    end
    return table.concat(parts)
  end

  for row = 1, rows do
    local from_bottom = rows - row + 1
    local tick = from_bottom % tick_spacing == 0 and from_bottom / tick_spacing
    local scores, counts = dots[1][row] or {}, dots[2][row] or {}
    local score_points, count_points = points[1][row] or {}, points[2][row] or {}
    local label = tick and string.format("%3d", tick * score_step) or "   "
    local line = plot_row(indent .. label .. (tick and " ┤" or " │"),
      function(cell)
        if score_points[cell] then return POINT, highlights.STATS_EFFICIENCY end
        if count_points[cell] then return POINT, highlights.STATS_VOLUME end
        if not scores[cell] and not counts[cell] then return " " end
        -- Where the lines meet, the cell takes the scores' colour.
        return vim.fn.nr2char(0x2800 + bit.bor(scores[cell] or 0, counts[cell] or 0), true),
          scores[cell] and highlights.STATS_EFFICIENCY or highlights.STATS_VOLUME
      end)
    if tick then mark(#indent, label, highlights.STATS_EFFICIENCY) end
    if tick and tick % ticks_per_count == 0 then
      local count = compact_count(tick / ticks_per_count * count_step)
      mark(#line + #"├ ", count, highlights.STATS_VOLUME)
      line = line .. "├ " .. count
    else
      line = line .. "│"
    end
    push(out, line)
  end
  local baseline = #out.lines
  push(out, plot_row(indent .. "  0 └", function(cell)
    local ago = CHART_BUCKETS - 1 - math.floor((cell - 1) / day_width)
    return (cell - 1) % day_width == 0 and (ago + 1) % TIME_TICK_STEP == 0 and "┬" or "─"
  end) .. "┘ 0")
  push(out, time_axis(axis_indent, day_width, unit))

  ---@type VF.Stats.Item[]
  local items = {}
  local plot_left = vim.fn.strdisplaywidth(axis_indent)
  for ago = 0, CHART_BUCKETS - 1 do
    local day = by_ago[ago]
    if day then
      local left = (CHART_BUCKETS - 1 - ago) * day_width
      items[#items + 1] = {
        row = first, last_row = baseline,
        left = plot_left + left, right = plot_left + left + bar,
        cursor = { baseline, spans[ago][rows + 1][2] },
        spans = spans[ago],
        id = day_id(day, unit), kind = "day",
        drill = day.drill, label = day_label(day), score = day.score, samples = day.spans,
      }
    end
  end
  return items
end

local function centered(text, width)
  local columns = width or CHART_BUCKETS + CHART_AXIS_WIDTH + 4
  local padding = math.max(0, math.floor((columns - vim.fn.strdisplaywidth(text)) / 2))
  return string.rep(" ", padding) .. text
end

local function push_average(out, score, width)
  local caption = string.format("%s average | %d sample%s", format_score(score), score.spans,
    score.spans == 1 and "" or "s")
  push(out, centered(caption, width))
end

--- The chart under its title, as one item, with as many whole columns per day as fit.
---@param out VF.Stats.Page
---@param name VF.Stats.ChartName
---@param days VF.Stats.DayScore[]
---@param width? integer
local function push_daily_chart(out, name, days, width, unit)
  local first = #out.lines
  local titles = { days = "Daily", weeks = "Weekly", hours = "Hourly", minutes = "Minute" }
  push(out, "    " .. titles[unit] .. " percent efficiency")
  local indent = "    "
  push_chart(out, days, indent,
    math.max(CHART_BUCKETS, (width or 0) - #indent - CHART_AXIS_WIDTH - 1), CHART_ROWS + 2, unit)
  out.items[#out.items + 1] = {
    row = first, last_row = #out.lines - 1, cursor = { first, #indent },
    id = "chart:" .. name, kind = "chart", chart = name,
  }
end

local function format_ratio(r)
  if r == math.huge then return "∞×" end
  return string.format("%.2f×", r)
end

---@param n integer
---@param noun string
---@return string
local function plural(n, noun)
  return string.format("%d %s%s", n, noun, n == 1 and "" or "s")
end

--- In the stats: the score and its chart. The rest is the chart's own page.
---@param out VF.Stats.Page
---@param summary VF.Stats.Summary
---@param width? integer
local function push_sessions(out, summary, width)
  local sessions = summary.sessions
  if summary.lifetime.sessions == 0 then
    push_prose(out, "No sessions yet: :Vimfy start, watch or recall to add some.", width)
    return
  end
  push_average(out, sessions.chart_score, width)
  push_daily_chart(out, "sessions", sessions.days, width, summary.chart_unit)
end

---@param out VF.Stats.Page
---@param summary VF.Stats.Summary
local function push_sessions_details(out, summary)
  local lifetime, sessions = summary.lifetime, summary.sessions
  push(out, string.format("    %s  (mark %d · watch %d · recall %d · suggest %d) · %s",
    plural(lifetime.sessions, "session"), lifetime.mark, lifetime.watch, lifetime.recall, lifetime.suggest,
    plural(lifetime.commands, "command")))
  if sessions.unbeaten > 0 then
    push(out, string.format("    Unbeaten (100%% efficient): %s", plural(sessions.unbeaten, "session")))
    drillable(out, sessions.unbeaten_drill, "unbeaten sessions")
  end
  if sessions.beats > 0 then
    push(out, string.format("    You beat the optimizer: %s", plural(sessions.beats, "session")))
    drillable(out, sessions.beats_drill, "sessions that beat the optimizer")
  end
  if sessions.no_result > 0 then
    push(out, string.format("    Not scored: %s with no solution found", plural(sessions.no_result, "session")))
    drillable(out, sessions.no_result_drill, "no-result sessions")
  end
end

local function push_options(out, title, kind, values, selected)
  local line = "    " .. title .. ":"
  local row = #out.lines
  local index = 1
  for i, value in ipairs(values) do
    line = line .. " "
    local col = #line
    line = line .. "[" .. value .. "]"
    if value == selected then
      index = i
      out.marks[#out.marks + 1] = {
        row = row, col = col, end_col = #line, hl = highlights.SETTINGS_ACTION_ACTIVE, priority = 210,
      }
    end
  end
  push(out, line)
  out.items[#out.items + 1] = {
    row = row, last_row = row, cursor = { row, 4 }, id = kind, kind = kind,
    selector = { values = values, index = index },
  }
end

---@param summary VF.Stats.JournalSummary
---@return number   # percent of the journal scored under the current settings
local function journal_scored(summary)
  return summary.journal_bytes > 0 and 100 * summary.scored_bytes / summary.journal_bytes or 100
end

--- In the stats: the run lengths and the chart, and the scoring only while
--- it needs attention. The rest is the chart's own page.
---@param out VF.Stats.Page
---@param summary VF.Stats.JournalSummary
---@param background VF.Stats.Background
---@param width? integer
local function push_journal(out, summary, background, width, unit)
  if not summary.present then
    push_prose(out, "No journal recorded. Set journal = { enabled = true } to start one.", width)
    return
  end
  if background.err then
    push_prose(out, "Last background scoring failed: " .. background.err, width)
  end
  if background.scoring then
    push(out, centered("Scoring journal…", width))
  else
    if not config.journal.sampling.auto and journal_scored(summary) < 100 then
      push_prose(out, "Background scoring is off; :Vimfy stats refresh scores the rest.", width)
    end
    push_average(out, summary.chart_score, width)
  end

  if summary.score.spans > 0 then
    push_daily_chart(out, "journal", summary.days, width, unit)
  elseif not background.scoring then
    push(out, string.format("    No scored runs of %d commands yet.", summary.charted_size))
  end
  local sizes = {}
  for _, entry in ipairs(summary.sizes) do sizes[#sizes + 1] = entry.size end
  push_options(out, "Run length", "run_length", sizes, summary.charted_size)
end

---@param out VF.Stats.Page
---@param summary VF.Stats.JournalSummary
local function push_journal_details(out, summary)
  push(out, string.format("    Efficiency:     %s  (%s of %d commands)",
    format_score(summary.score), plural(summary.score.spans, "run"), summary.charted_size))
  push(out, string.format("    Scored under the current settings: %d%% of the journal",
    math.floor(journal_scored(summary))))
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
    push(out, string.format("    Unbeaten (100%% efficient): %s", plural(summary.unbeaten, "run")))
    drillable(out, summary.unbeaten_drill, "unbeaten runs")
  end
  if summary.deadline_hit > 0 then
    push(out, string.format("    Search cut short by the deadline: %s", plural(summary.deadline_hit, "run")))
  end
end

--- A command row's cells: its name, ratio and the two shares.
---@param row VF.Stats.CommandRow
---@return string[]
local function command_cells(row)
  return {
    row.text, format_ratio(row.ratio),
    string.format("%.1f%%", row.user_share * 100), string.format("%.1f%%", row.optimizer_share * 100),
  }
end

local COMMAND_CELL_GAP = "   "

--- One list as a table: the name left-aligned and the numbers right-aligned,
--- so the `vs` line up.
---@param out VF.Stats.Page
---@param rows VF.Stats.CommandRow[]
---@param side string   # breadcrumb suffix
local function push_command_rows(out, rows, side)
  if #rows == 0 then
    push(out, "    (no command is used 10 times yet)")
    return
  end
  local widths = { 0, 0, 0, 0 }
  for _, row in ipairs(rows) do
    for i, cell in ipairs(command_cells(row)) do
      widths[i] = math.max(widths[i], vim.fn.strdisplaywidth(cell))
    end
  end
  ---@param s string
  ---@param width integer
  local function right(s, width)
    return string.rep(" ", width - vim.fn.strdisplaywidth(s)) .. s
  end
  for _, row in ipairs(rows) do
    local cells = command_cells(row)
    push(out, "    " .. pad(cells[1], widths[1]) .. COMMAND_CELL_GAP .. right(cells[2], widths[2])
      .. COMMAND_CELL_GAP .. right(cells[3], widths[3]) .. " vs " .. right(cells[4], widths[4]))
    -- A merged row's split goes with its runs.
    local id = row.text .. " " .. side
    drillable(out, row.drill, row.split ~= "" and id .. " · " .. row.split or id, id)
  end
end

local COLUMN_GAP = 4
-- The widest unwrappable line of each column: the Look back selector, and a
-- command row. Prose wraps to its column.
local OVERVIEW_MIN_WIDTH = 60
local COMMANDS_MIN_WIDTH = 48
local OVERVIEW_SHARE = 0.58
-- From here up the Commands sit beside the charts; below, under them.
M.SPLIT_MIN_WIDTH = OVERVIEW_MIN_WIDTH + COLUMN_GAP + COMMANDS_MIN_WIDTH

---@param page VF.Stats.Page
---@return integer
local function page_width(page)
  local width = 0
  for _, line in ipairs(page.lines) do width = math.max(width, vim.fn.strdisplaywidth(line)) end
  return width
end

--- `right` beside `left` in one page. Each side is a group of items; an item
--- that spanned its page's width spans its column's.
---@param left VF.Stats.Page
---@param right VF.Stats.Page
---@return VF.Stats.Page
local function beside(left, right, left_width)
  local out = new_page()
  local right_width = page_width(right)
  local gap = string.rep(" ", COLUMN_GAP)
  -- Per line, the bytes each side's cell starts and ends at.
  local zero, left_ends, right_starts, right_ends = {}, {}, {}, {}
  for i = 1, math.max(#left.lines, #right.lines) do
    local cell = pad(left.lines[i] or "", left_width)
    out.lines[i] = cell .. gap .. pad(right.lines[i] or "", right_width)
    zero[i], left_ends[i], right_starts[i], right_ends[i] = 0, #cell, #cell + #gap, #out.lines[i]
  end
  ---@param page VF.Stats.Page
  ---@param group string
  ---@param column integer    # display column its cell starts at
  ---@param width integer
  ---@param starts integer[]
  ---@param ends integer[]
  local function place(page, group, column, width, starts, ends)
    for _, item in ipairs(page.items) do
      item.group = group
      if item.spans then
        item.left, item.right = item.left + column, item.right + column
        for _, span in ipairs(item.spans) do
          span[2], span[3] = span[2] + starts[span[1] + 1], span[3] + starts[span[1] + 1]
        end
      else
        item.left, item.right, item.spans = column, column + width, {}
        for row = item.row, item.last_row do
          item.spans[#item.spans + 1] = { row, starts[row + 1], ends[row + 1] }
        end
      end
      item.cursor[2] = item.cursor[2] + starts[item.cursor[1] + 1]
      out.items[#out.items + 1] = item
    end
    for _, mark in ipairs(page.marks) do
      mark.col, mark.end_col = mark.col + starts[mark.row + 1], mark.end_col + starts[mark.row + 1]
      out.marks[#out.marks + 1] = mark
    end
  end
  place(left, "left", 0, left_width, zero, left_ends)
  place(right, "right", left_width + COLUMN_GAP, right_width, right_starts, right_ends)
  return out
end

--- `bottom` under `top` in one page.
---@param top VF.Stats.Page
---@param bottom VF.Stats.Page
---@return VF.Stats.Page
local function stacked(top, bottom)
  local offset = #top.lines
  vim.list_extend(top.lines, bottom.lines)
  for _, item in ipairs(bottom.items) do
    item.row, item.last_row, item.cursor[1] = item.row + offset, item.last_row + offset, item.cursor[1] + offset
    for _, span in ipairs(item.spans or {}) do span[1] = span[1] + offset end
    top.items[#top.items + 1] = item
  end
  for _, mark in ipairs(bottom.marks) do
    mark.row = mark.row + offset
    top.marks[#top.marks + 1] = mark
  end
  return top
end

---@param summary VF.Stats.Summary
---@param background VF.Stats.Background
---@param width? integer
---@return VF.Stats.Page
local function overview_page(summary, background, width)
  local out = new_page()
  push(out, "")
  if summary.hidden_runs > 0 then
    push(out, string.format("  %s from other scorings hidden (v)", plural(summary.hidden_runs, "run")))
    push(out, "")
  end
  push_options(out, "Look back", "chart_unit", { "minutes", "hours", "days", "weeks" }, summary.chart_unit)
  push_title(out, centered("Sessions", width))
  push_sessions(out, summary, width)
  push(out, "")

  push_title(out, centered("Journal", width))
  push_journal(out, summary.journal, background, width, summary.chart_unit)
  push(out, "")
  return out
end

---@param summary VF.Stats.Summary
---@return VF.Stats.Page
local function footer_page(summary)
  local out = new_page()
  local t = summary.sessions.timing
  if t then
    push_title(out, "  Dev")
    push(out, string.format(
      "    Optimizer time, last 30 days: p50 %.1f · p95 %.1f · p99 %.1f · max %.1f ms  (n=%d)",
      t.p50, t.p95, t.p99, t.max, t.n))
    push(out, "")
  end
  push(out, "  h/j/k/l move · h/l change options on selector rows · <CR> opens · ? for keys")
  push(out, "")
  return out
end

---@param summary VF.Stats.Summary
---@param width? integer
---@return VF.Stats.Page
local function commands_page(summary, width)
  local out = new_page()
  local commands = summary.commands
  push_title(out, "  Underused commands")
  push_command_rows(out, commands.less, "(you less)")
  push(out, "")
  push_title(out, "  Overused commands")
  push_command_rows(out, commands.more, "(you more)")
  push(out, "")
  if #commands.never > 0 then
    push_title(out, "  Never used by the optimizer")
    push_command_rows(out, commands.never, "(you only)")
    if commands.never_cut > 0 then
      push(out, string.format("    … and %d more", commands.never_cut))
    end
    push(out, "")
  end
  local user, optimizer = commands.user_typed_chars, commands.optimizer_typed_chars
  local typed = string.format("Typed text: %d vs %d chars", user, optimizer)
  if optimizer > 0 and user > optimizer then
    typed = string.format("Extra typed text: %d%% (%d vs %d chars)",
      math.floor(100 * (user - optimizer) / optimizer + 0.5), user, optimizer)
  end
  local first = #out.lines
  push_prose(out, typed, width)
  if user > optimizer then
    drillable(out, commands.typed_drill, "extra typed text")
    out.items[#out.items].row = first
  end
  push(out, "")
  return out
end

--- One daily chart on its own page, `width` by `height`: every day with data
--- an item, over what the stats leave out of its section.
---@param summary VF.Stats.Summary
---@param name VF.Stats.ChartName
---@param width integer
---@param height integer
---@return VF.Stats.Page
function M.chart_page(summary, name, width, height)
  local details = new_page()
  push(details, "")
  local days = name == "journal" and summary.journal.days or summary.sessions.days
  if name == "journal" then
    push_journal_details(details, summary.journal)
  else
    push_sessions_details(details, summary)
  end

  local out = new_page()
  local indent = " "
  out.items = push_line_chart(out, days, indent,
    math.max(CHART_BUCKETS, width - #indent - CHART_AXIS_WIDTH - RIGHT_AXIS_WIDTH - 1),
    height - #details.lines, summary.chart_unit)
  out = stacked(out, details)
  for _ = #out.lines + 1, height do push(out, "") end
  return out
end

--- The stats in one page: Sessions and Journal, the Commands tables, then Dev.
--- The Commands go beside Sessions and Journal when `width` has the room, and
--- under them when not or without one.
---@param summary VF.Stats.Summary
---@param background VF.Stats.Background
---@param width? integer   # display columns to draw in
---@return VF.Stats.Page
function M.build_page(summary, background, width)
  local footer = footer_page(summary)
  if width and width >= M.SPLIT_MIN_WIDTH then
    local columns = width - COLUMN_GAP
    local room = math.max(OVERVIEW_MIN_WIDTH,
      math.min(math.floor(columns * OVERVIEW_SHARE), columns - COMMANDS_MIN_WIDTH))
    -- Level with the first section's title.
    local right = new_page()
    push(right, "")
    local commands = stacked(right, commands_page(summary, columns - room))
    return stacked(beside(overview_page(summary, background, room), commands, room), footer)
  end
  return stacked(stacked(overview_page(summary, background, width), commands_page(summary, width)), footer)
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
  { title = "extra typed", order = "typed", width = 13,
    cell = function(row)
      local has_best = row.outcome == "scored" or row.outcome == "unbeaten"
      if not has_best then return "—" end
      local extra = row.user_typed_chars - row.best_typed_chars
      if extra < 0 then return "−" .. -extra end
      return (extra > 0 and "+" or "") .. extra
    end },
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

--- A run's cell in one column of the run list.
---@class VF.Stats.RunCell : VF.Ui.NavItem
---@field index integer          # into the listed runs
---@field order VF.Stats.Order   # what sorting by its column asks for

--- An item per cell of the `count` runs in `lines` (M.run_lines).
---@param lines string[]
---@param count integer
---@return VF.Stats.RunCell[]
function M.run_items(lines, count)
  local items = {}
  for index = 1, count do
    local line, left = lines[index + 1], 1
    for _, column in ipairs(RUN_COLUMNS) do
      local right = left + column.width
      -- Every character of a run's line is one column wide.
      local col, end_col = vim.fn.byteidx(line, left), vim.fn.byteidx(line, right)
      items[#items + 1] = {
        id = index .. ":" .. column.order, row = index, last_row = index, left = left, right = right,
        cursor = { index, col }, spans = { { index, col, end_col } }, index = index, order = column.order,
      }
      left = right + RUN_COLUMN_GAP
    end
  end
  return items
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

local KEY_LABEL_WIDTH = #" found: "
local KEY_LINES = 2

--- `label`, then `text` wrapped at its token breaks into at most KEY_LINES
--- lines of `width` columns, the later ones indented under the first.
---@param lines string[]
---@param label string
---@param text string
---@param width integer
local function push_keys(lines, label, text, width)
  local room = math.max(1, width - KEY_LABEL_WIDTH - 1)
  local wrapped, line = {}, ""
  local words = vim.split(text, " ", { plain = true })
  local i, word = 1, words[1]
  while word do
    local joined_line = line == "" and word or line .. " " .. word
    if vim.fn.strdisplaywidth(joined_line) <= room then
      line = joined_line
      i = i + 1
      word = words[i]
    elseif line == "" then
      -- A token longer than a line breaks where the line ends.
      wrapped[#wrapped + 1] = vim.fn.strcharpart(word, 0, room)
      word = vim.fn.strcharpart(word, room)
    else
      wrapped[#wrapped + 1] = line
      line = ""
    end
  end
  if line ~= "" then wrapped[#wrapped + 1] = line end
  if #wrapped > KEY_LINES then wrapped[KEY_LINES] = fit(wrapped[KEY_LINES] .. " …", room) end
  for n = 1, math.min(#wrapped, KEY_LINES) do
    lines[#lines + 1] = pad(n == 1 and label or "", KEY_LABEL_WIDTH) .. wrapped[n]
  end
end

--- Where the cursor at byte `col` of `text` is once its tabs are `tab`.
---@param text string
---@param col integer   # 0-indexed
---@param tab string
---@return integer col       # display column
---@return integer bytes     # before it
---@return integer length    # bytes it covers; one for a tab or the end of the line
local function cursor_cell(text, col, tab)
  local before = text:sub(1, col):gsub("\t", tab)
  local char = vim.fn.strcharpart(text:sub(col + 1), 0, 1)
  return vim.fn.strdisplaywidth(before), #before, (char == "" or char == "\t") and 1 or #char
end

--- A run's keys, spaced by token, then its text before and after side by side
--- with the changed lines marked and the cursor's cell shaded in each.
--- `detail` is nil for a session, which keeps no text, and for a run whose
--- journal is gone (`err` says why).
---@param row VF.Stats.RunRow
---@param detail VF.Stats.RunDetail|nil
---@param err string|nil
---@param width integer
---@return VF.Stats.RunPreview
function M.run_preview(row, detail, err, width)
  local lines = {}
  -- A journal run's resolved keys parse where its typed ones may not.
  push_keys(lines, " you:", sequence_display.inline(detail and detail.keys or row.user_keys), width)
  push_keys(lines, " found:", row.best_seq == "" and "—" or sequence_display.inline(row.best_seq), width)
  lines[#lines + 1] = ""
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
    lines[#lines + 1] = " " .. cell .. " │ " .. pad(fit((right:gsub("\t", tab)), half), half)
    return #lines - 1, 1 + #cell
  end
  push_pair("before", "after")

  local changed_before, changed_after = {}, {}
  local hunks = vim.diff(joined(detail.lines), joined(detail.goal_lines), { result_type = "indices" })
  for _, hunk in ipairs(hunks --[[@as integer[][] ]]) do
    for i = hunk[1], hunk[1] + hunk[2] - 1 do changed_before[i] = true end
    for i = hunk[3], hunk[3] + hunk[4] - 1 do changed_after[i] = true end
  end
  --- Shades the cursor's cell, unless the line was cut before it.
  ---@param at integer
  ---@param base integer   # byte column its side's cell starts at
  ---@param text string
  ---@param col integer
  ---@param hl string
  local function mark_cursor(at, base, text, col, hl)
    local display_col, bytes, length = cursor_cell(text, col, tab)
    if display_col >= half - 1 then return end
    marks[#marks + 1] = { row = at, col = base + bytes, end_col = base + bytes + length, hl = hl, priority = 200 }
  end
  for i = 1, math.max(#detail.lines, #detail.goal_lines) do
    local before, after = detail.lines[i] or "", detail.goal_lines[i] or ""
    local at, divider = push_pair(before, after)
    local right = divider + #" │ "
    if changed_before[i] then
      marks[#marks + 1] = { row = at, col = 1, end_col = divider, hl = highlights.VIEW_DIFF_DELETE }
    end
    if changed_after[i] then
      marks[#marks + 1] = { row = at, col = right, end_col = #lines[at + 1], hl = highlights.VIEW_DIFF_ADD }
    end
    if i == detail.start_row + 1 then
      mark_cursor(at, 1, before, detail.start_col, highlights.VIEW_CURSOR_START)
    end
    if i == detail.end_row + 1 then
      mark_cursor(at, right, after, detail.end_col, highlights.VIEW_CURSOR_END)
    end
  end
  return { lines = lines, marks = marks }
end

return M
