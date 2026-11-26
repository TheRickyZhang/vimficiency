---@class (exact) VF.Stats.Render
---@field SPLIT_MIN_WIDTH integer
---@field build_page fun(summary: VF.Stats.Summary, query: VF.Stats.Scope, background: VF.Stats.Background, width?: integer, expanded_commands?: VF.Stats.ExpandedCommands): VF.Stats.Page
---@field chart_page fun(summary: VF.Stats.Summary, name: VF.Stats.ChartName, width: integer, height: integer, shown: VF.Stats.ChartLines, scale?: VF.Stats.ChartScale): VF.Stats.Page
---@field journal_efficiency fun(summary: VF.Stats.JournalSummary): string
---@field journal_percent fun(coverage: VF.Stats.JournalCoverage): integer
---@field run_lines fun(list: VF.Stats.RunList, offset: integer, sort?: VF.Stats.RunSort): string[]
---@field run_items fun(lines: string[], count: integer): VF.Stats.RunCell[]
---@field run_name fun(row: VF.Stats.RunRow): string
---@field run_preview fun(row: VF.Stats.RunRow, detail: VF.Session.Viewable|nil, err: string|nil, width: integer): VF.Stats.RunPreview
local M = {}

local log = require("vimficiency.stats.log")
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

---@alias VF.Stats.ChartName "sessions"|"journal"|"nav_timing"|"edit_timing"

---@class VF.Stats.ChartScale
---@field unit string
---@field bucket_size integer
---@field buckets integer
---@field tick_step integer
---@field overview? boolean

--- Which of a chart float's lines are drawn.
---@class VF.Stats.ChartLines
---@field primary boolean
---@field samples boolean

---@class VF.Stats.LinePoint
---@field value number
---@field count integer
---@field id string
---@field label string
---@field detail string
---@field drill VF.Stats.Filter

---@class VF.Stats.LinePlot
---@field buckets integer
---@field points table<integer, VF.Stats.LinePoint>
---@field history? VF.Stats.ChartScale
---@field labels? string[]
---@field primary_legend string
---@field secondary_legend string

---@alias VF.Stats.Source "sessions"|"journal"
---@alias VF.Stats.CommandSection "less"|"more"|"never"
---@alias VF.Stats.ExpandedCommands table<VF.Stats.Source, table<VF.Stats.CommandSection, boolean>>

--- Something in the stats the keys act on.
---@class VF.Stats.Item : VF.Ui.NavItem
---@field kind "drill"|"chart"|"day"|"timing_bucket"|"run_length"|"chart_unit"|"command_toggle"
---@field source? VF.Stats.Source  # whose band of the stats it is in
---@field drill? VF.Stats.Filter   # the runs <CR> lists: a row's or a day's
---@field label? string            # those runs' title
---@field detail? string           # the selected point's values
---@field order? VF.Stats.Order
---@field chart? VF.Stats.ChartName
---@field score? number            # a day's
---@field samples? integer         # the runs a day's score is over
---@field selector? { values: (string|integer)[], index: integer }
---@field command_section? VF.Stats.CommandSection

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
-- The fewest a chart is drawn with.
local CHART_ROWS = 5
local OVERVIEW_CHART_ROWS = 10
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

--- `line` cut to `width` display columns, ending in `…` when cut.
---@param line string
---@param width integer
---@return string
local function fit(line, width)
  if width < 1 then return "" end
  local prefix = vim.fn.strcharpart(line, 0, width, true)
  if #prefix == #line and vim.fn.strdisplaywidth(prefix) <= width then return line end
  local low, high = 0, width - 1
  while low < high do
    local mid = math.ceil((low + high) / 2)
    if vim.fn.strdisplaywidth(vim.fn.strcharpart(prefix, 0, mid, true)) < width then
      low = mid
    else
      high = mid - 1
    end
  end
  return vim.fn.strcharpart(prefix, 0, low, true) .. "…"
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
---@param hl? string
local function drillable(out, drill, label, id, hl)
  local row = #out.lines - 1
  out.items[#out.items + 1] = {
    row = row, last_row = row, cursor = { row, out.lines[#out.lines]:find("%S") - 1 },
    id = id or label, kind = "drill", drill = drill, label = label,
  }
  mark_text(out, hl or highlights.STATS_DRILL)
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

---@param unit string
---@return VF.Stats.ChartScale
local function overview_scale(unit)
  return { unit = unit, bucket_size = 1, buckets = CHART_BUCKETS, tick_step = TIME_TICK_STEP }
end

---@param ago integer
---@param unit string
---@return string
local function time_label(ago, unit)
  if unit == "minutes" and ago % 60 == 0 then return ago / 60 .. "h" end
  if unit == "hours" and ago % 24 == 0 then return ago / 24 .. "d" end
  return ago .. unit:sub(1, 1)
end

--- The line of time labels under a chart whose buckets are `bucket_width` columns.
---@param axis_indent string
---@param bucket_width integer
---@param scale VF.Stats.ChartScale
---@return string
local function time_axis(axis_indent, bucket_width, scale)
  local axis = axis_indent
  local plot_width = bucket_width * scale.buckets
  local label_end = 0
  local tick_unit = time_label(scale.tick_step * scale.bucket_size, scale.unit):sub(-1)
  for ago = scale.buckets, 0, -scale.tick_step do
    local label = ago == 0 and "0" .. tick_unit or time_label(ago * scale.bucket_size, scale.unit)
    local tick = (scale.buckets - ago) * bucket_width
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
local function push_chart(out, days, indent, width, height, unit)
  local by_ago = {}
  for _, day in ipairs(days) do by_ago[day.days_ago] = day end
  local day_width = math.floor(width / CHART_BUCKETS)
  local gap = day_width >= 3 and 1 or 0
  local bar = day_width - gap
  local rows, score_step, tick_spacing = chart_scale(math.max(CHART_ROWS, height - 2))

  ---@param prefix string
  ---@param cell_of fun(day: VF.Stats.DayScore|nil, ago: integer): string
  ---@param fill? string   # what the gap after a day is drawn with
  local function push_row(prefix, cell_of, fill)
    local line = prefix
    for ago = CHART_BUCKETS - 1, 0, -1 do
      local cell = cell_of(by_ago[ago], ago)
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
  push_row(indent .. "  0 └", function(_, ago)
    if (ago + 1) % TIME_TICK_STEP == 0 then return "┬" .. string.rep("─", bar - 1) end
    return string.rep("─", bar)
  end, "─")
  local axis_indent = indent .. string.rep(" ", CHART_AXIS_WIDTH)
  push(out, time_axis(axis_indent, day_width, overview_scale(unit)))
end

-- A braille cell's dots, by column then row from the top.
local BRAILLE_DOTS = { { 0x01, 0x02, 0x04, 0x40 }, { 0x08, 0x10, 0x20, 0x80 } }
local POINT = "•"
-- One dot in this many along a dotted line.
local DOTTED_PERIOD = 3
-- "├ " and a count.
local RIGHT_AXIS_WIDTH = 2 + 3

---@param value number
---@return number
local function latency_step(value)
  if value == 0 then return 1 end
  local power = 10 ^ math.floor(math.log10(value))
  local ratio = value / power
  return power * (ratio <= 1 and 1 or ratio <= 2 and 2 or ratio <= 5 and 5 or 10)
end

---@param indent string
---@param width integer
---@param labels string[]
---@return string
local function size_axis(indent, width, labels)
  local parts = { indent }
  for _, label in ipairs(labels) do
    if vim.fn.strdisplaywidth(label) > width then label = label:gsub("^%d+–", "≤") end
    local left = math.max(0, math.floor((width - vim.fn.strdisplaywidth(label)) / 2))
    parts[#parts + 1] = pad(string.rep(" ", left) .. label, width)
  end
  return table.concat(parts)
end

--- Two lines over the same buckets: the values against the left axis and the
--- sample counts against the right. A line breaks where a bucket has no data.
---@param out VF.Stats.Page
---@param width integer
---@param height integer
---@param data VF.Stats.LinePlot
---@param shown VF.Stats.ChartLines
---@return VF.Stats.Item[] bucket_items
local function push_line_chart(out, width, height, data, shown)
  local history = data.history
  local most, highest = 0, 0
  for _, entry in pairs(data.points) do
    most = math.max(most, entry.count)
    highest = math.max(highest, entry.value)
  end
  local indent, label_width = " ", history and 3 or 8
  local axis_indent = indent .. string.rep(" ", label_width + 2)
  local plot_width = math.max(CHART_BUCKETS, width - #axis_indent - RIGHT_AXIS_WIDTH - 1)
  local buckets = data.buckets
  local bucket_width = math.floor(plot_width / buckets)
  local bar = bucket_width - (bucket_width >= 3 and 1 or 0)
  local cells = bucket_width * buckets
  -- Over the plot a legend; under it the baseline and the time labels.
  local rows, score_step, tick_spacing = chart_scale(math.max(CHART_ROWS, height - 3))
  local ticks = 100 / score_step
  local value_step = history and score_step or latency_step(highest / ticks)
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
    return (buckets - 1 - ago) * bucket_width * 2 + bar, math.floor(share * (rows * 4 - 1) + 0.5)
  end
  for series, share_of in ipairs({
    function(entry) return entry.value / (value_step * ticks) end,
    function(entry) return entry.count / (count_step * count_ticks) end,
  }) do
    for ago = buckets - 1, 0, -1 do
      local entry, following = data.points[ago], data.points[ago - 1]
      if entry and (series == 1 and shown.primary or series == 2 and shown.samples) then
        local x, y = point(ago, share_of(entry))
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
  -- Each line with the key that toggles it; a hidden one loses its sample and colour.
  local legend = {
    (shown.primary and "⠒⠒ " or "   ") .. data.primary_legend,
    (shown.samples and "⠂⠂ " or "   ") .. data.secondary_legend,
  }
  if shown.primary then mark(#axis_indent, legend[1], highlights.STATS_EFFICIENCY) end
  if shown.samples then mark(#axis_indent + #legend[1] + 3, legend[2], highlights.STATS_VOLUME) end
  push(out, axis_indent .. legend[1] .. "   " .. legend[2])

  local first = #out.lines
  -- Each bucket's highlighted cells, by offset: its column in every row.
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
    for ago = 0, buckets - 1 do
      local left = (buckets - 1 - ago) * bucket_width
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
    local label = tick and string.format(history and "%3d" or "%8.2g", tick * value_step)
      or string.rep(" ", label_width)
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
  push(out, plot_row(indent .. string.rep(" ", label_width - 1) .. "0 └", function(cell)
    local ago = buckets - math.floor((cell - 1) / bucket_width)
    local at = (cell - 1) % bucket_width
    local tick = history and at == 0 and ago % history.tick_step == 0
      or not history and at == math.floor(bar / 2)
    return tick and "┬" or "─"
  end) .. "┘ 0")
  push(out, history and time_axis(axis_indent, bucket_width, history) or size_axis(axis_indent, bucket_width, data.labels))

  ---@type VF.Stats.Item[]
  local items = {}
  local plot_left = vim.fn.strdisplaywidth(axis_indent)
  for index = 1, buckets do
    local ago = history and index - 1 or buckets - index
    local entry = data.points[ago]
    if entry then
      local left = (buckets - 1 - ago) * bucket_width
      items[#items + 1] = {
        row = first, last_row = baseline,
        left = plot_left + left, right = plot_left + left + bar,
        cursor = { baseline, spans[ago][rows + 1][2] },
        spans = spans[ago],
        id = entry.id, kind = history and "day" or "timing_bucket",
        drill = entry.drill, label = entry.label, detail = entry.detail,
        score = entry.value, samples = entry.count, order = not history and "slowest" or nil,
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

---@param days VF.Stats.DayScore[]
---@param scale VF.Stats.ChartScale
---@return VF.Stats.LinePlot
local function history_plot(days, scale)
  local points = {}
  for _, day in ipairs(days) do
    local label = day_label(day)
    points[day.days_ago] = {
      value = day.score, count = day.spans, drill = day.drill, id = day_id(day, scale.unit), label = label,
      detail = string.format("%s: %.1f%% · %d samples", label, day.score, day.spans),
    }
  end
  local bucket = scale.bucket_size == 1 and scale.unit:sub(1, -2) or scale.bucket_size .. " " .. scale.unit
  return {
    history = scale, buckets = scale.buckets, points = points,
    primary_legend = "efficiency % (e)", secondary_legend = "samples per " .. bucket .. " (s)",
  }
end

local function push_average(out, score, width)
  local caption = string.format("%s average | %d sample%s", format_score(score), score.spans,
    score.spans == 1 and "" or "s")
  push(out, centered(caption, width))
end

---@param out VF.Stats.Page
---@param name VF.Stats.ChartName
---@param first integer
local function chart_item(out, name, first)
  out.items[#out.items + 1] = {
    row = first, last_row = #out.lines - 1, cursor = { first, #PROSE_INDENT },
    id = "chart:" .. name, kind = "chart", chart = name,
  }
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
  push_chart(out, days, PROSE_INDENT,
    math.max(CHART_BUCKETS, (width or 0) - #PROSE_INDENT - CHART_AXIS_WIDTH - 1), OVERVIEW_CHART_ROWS + 2, unit)
  chart_item(out, name, first)
end

local function format_ratio(r)
  if r == math.huge then return "∞×" end
  local text = string.format("%.2f×", r)
  return r > 0 and text == "0.00×" and "<0.01×" or text
end

---@param n integer
---@param noun string
---@return string
local function plural(n, noun)
  return string.format("%d %s%s", n, noun, n == 1 and "" or "s")
end

---@param ms number
---@return string
local function millis(ms)
  return string.format(ms < 1 and "%.2f" or "%.1f", ms)
end

---@param bucket VF.Stats.TimingBucket
---@return string   # `1`, `2–3`, `11+`
local function bucket_range(bucket)
  local first, last = bucket.first, bucket.last
  return last == 0 and (first .. "+") or first == last and tostring(first) or first .. "–" .. last
end

---@param bins VF.Stats.TimingBucket[]
---@return VF.Stats.LinePlot
local function timing_plot(bins)
  local points, labels = {}, {}
  for i, bucket in ipairs(bins) do
    local first = bucket.first
    local range = bucket_range(bucket)
    labels[i] = range
    local t = bucket.timing
    if t.n > 0 then
      local label = range .. " positions"
      local detail = string.format("%s · %s ms p50 · n=%d", range, millis(t.p50), t.n)
      if t.n >= 100 then detail = detail .. " · " .. millis(t.p95) .. " ms p95" end
      if bucket.deadline_hit > 0 then detail = detail .. " · " .. plural(bucket.deadline_hit, "deadline hit") end
      points[#bins - i] = {
        value = t.p50, count = t.n, drill = t.drill, id = "timing:" .. first,
        label = label, detail = detail,
      }
    end
  end
  return {
    buckets = #bins, points = points, labels = labels,
    primary_legend = "median ms (t)", secondary_legend = "samples (s)",
  }
end

---@param out VF.Stats.Page
---@param name "nav_timing"|"edit_timing"
---@param title string
---@param bins VF.Stats.TimingBucket[]
---@param width? integer
local function push_timing_chart(out, name, title, bins, width)
  local count = 0
  for _, bucket in ipairs(bins) do count = count + bucket.timing.n end
  if count == 0 then return end
  local row = #out.lines
  push_prose(out, string.format("%s · %s", title, plural(count, "sample")), width)
  chart_item(out, name, row)
  mark_text(out, highlights.STATS_DRILL)
end

--- In the stats: the score, its chart and the optimizer's response time. The
--- rest is the chart's own page.
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
  local t = sessions.timing
  if t then
    local row = #out.lines
    push_prose(out, string.format("Optimizer response, last 30 days · %s ms median · %s",
      millis(t.p50), plural(t.n, "session")), width)
    drillable(out, t.drill, "Session response times", "session_timing")
    local item = out.items[#out.items]
    item.row, item.order = row, "slowest"
  else
    push_prose(out, "No response times in the last 30 days.", width)
  end
end

--- A heading, then a row per category of its runs. Only the categories list runs.
---@param out VF.Stats.Page
---@param heading string
---@param title string   # the heading's runs, in a run list's title
---@param hl string
---@param categories { runs: integer, name: string, drill: VF.Stats.Filter }[]
local function push_categories(out, heading, title, hl, categories)
  push(out, PROSE_INDENT .. heading)
  mark_text(out, hl)
  local width = 0
  for _, category in ipairs(categories) do width = math.max(width, #tostring(category.runs)) end
  for _, category in ipairs(categories) do
    push(out, string.format("%s    %" .. width .. "d %s", PROSE_INDENT, category.runs, category.name))
    drillable(out, category.drill, title .. " · " .. category.name, nil, hl)
  end
end

---@param out VF.Stats.Page
---@param section VF.Stats.SessionSummary|VF.Stats.JournalSummary
---@param noun string
local function push_unbeaten(out, section, noun)
  if section.unbeaten == 0 then return end
  push_categories(out, "Unbeaten (100% efficient): " .. plural(section.unbeaten, noun),
    "unbeaten " .. noun .. "s", highlights.STATS_UNBEATEN, section.unbeaten_tags)
end

---@param out VF.Stats.Page
---@param summary VF.Stats.Summary
local function push_sessions_details(out, summary)
  local lifetime, sessions = summary.lifetime, summary.sessions
  push(out, string.format("    %s  (mark %d · watch %d · recall %d · suggest %d) · %s",
    plural(lifetime.sessions, "session"), lifetime.mark, lifetime.watch, lifetime.recall, lifetime.suggest,
    plural(lifetime.commands, "command")))
  push_unbeaten(out, sessions, "session")
  if sessions.beats > 0 then
    push(out, string.format("    You beat the optimizer: %s", plural(sessions.beats, "session")))
    drillable(out, sessions.beats_drill, "sessions that beat the optimizer")
  end
  if sessions.no_result > 0 then
    push_categories(out, "Not scored: " .. plural(sessions.no_result, "session"), "sessions not scored",
      highlights.STATS_NOT_SCORED,
      { { runs = sessions.no_result, name = "no result", drill = sessions.no_result_drill } })
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

---@param coverage VF.Stats.JournalCoverage
---@return integer   # percent of the journal scored under the current settings
function M.journal_percent(coverage)
  return coverage.journal_bytes > 0 and math.floor(100 * coverage.scored_bytes / coverage.journal_bytes) or 100
end

--- In the stats: the chart, the run lengths and the search times, and the
--- scoring only while it needs attention. The rest is the chart's own page.
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
    push(out, centered(string.format("Scoring journal… %d%%", M.journal_percent(summary)), width))
  else
    if not config.journal.sampling.auto and M.journal_percent(summary) < 100 then
      push_prose(out, "Background scoring is off; :Vimfy stats refresh scores the rest.", width)
    end
    push_average(out, summary.chart_score, width)
  end

  if summary.score.spans > 0 then
    push_daily_chart(out, "journal", summary.days, width, unit)
  elseif #summary.unscored > 0 or not background.scoring then
    local row = #out.lines
    push(out, string.format("    No scored runs of %d commands yet.", summary.charted_size))
    if #summary.unscored > 0 then
      chart_item(out, "journal", row)
      mark_text(out, highlights.STATS_DRILL)
    end
  end
  local sizes = {}
  for _, entry in ipairs(summary.sizes) do sizes[#sizes + 1] = entry.size end
  push_options(out, "Run length", "run_length", sizes, summary.charted_size)
  local before = #out.items
  push_timing_chart(out, "nav_timing", "Movement search", summary.nav_timing, width)
  push_timing_chart(out, "edit_timing", "Composition search", summary.edit_timing, width)
  if #out.items == before then push_prose(out, "No search times for this run length.", width) end
end

--- `cells` shared out in proportion to `counts`, each getting at least one.
---@param counts integer[]
---@param cells integer
---@return integer[]
local function share_cells(counts, cells)
  local total = 0
  for _, count in ipairs(counts) do total = total + count end
  local widths, rest, used = {}, {}, 0
  for i, count in ipairs(counts) do
    local exact = count / total * cells
    widths[i] = math.max(1, math.floor(exact))
    rest[i] = exact - widths[i]
    used = used + widths[i]
  end
  -- What rounding left over goes to, or comes from, whichever is furthest off.
  while used ~= cells do
    local step = used < cells and 1 or -1
    local pick
    for i in ipairs(widths) do
      if (step > 0 or widths[i] > 1) and (not pick or step * rest[i] > step * rest[pick]) then pick = i end
    end
    if not pick then break end
    widths[pick], rest[pick], used = widths[pick] + step, rest[pick] - step, used + step
  end
  return widths
end

--- A row of cells a column apart.
---@param out VF.Stats.Page
---@param prefix string
---@param cells { text: string, hl?: string }[]
---@return { [1]: integer, [2]: integer, [3]: integer }[] spans   # each cell's
local function push_cells(out, prefix, cells)
  local row, parts, col, spans = #out.lines, { prefix }, #prefix, {}
  for i, cell in ipairs(cells) do
    if i > 1 then
      parts[#parts + 1] = " "
      col = col + 1
    end
    spans[i] = { row, col, col + #cell.text }
    if cell.hl then out.marks[#out.marks + 1] = { row = row, col = col, end_col = col + #cell.text, hl = cell.hl } end
    parts[#parts + 1] = cell.text
    col = col + #cell.text
  end
  push(out, table.concat(parts))
  return spans
end

--- The scored runs as a bar split into movement and edits, over the same runs
--- split again by query size. Every segment whose runs can be listed is an item.
---@param out VF.Stats.Page
---@param summary VF.Stats.JournalSummary
---@param width integer
local function push_kind_bars(out, summary, width)
  local kinds = {
    { id = "nav", title = "Movement only", score = summary.nav, drill = summary.nav_drill,
      buckets = summary.nav_timing, hl = highlights.STATS_MOVEMENT },
    { id = "edit", title = "With edits", score = summary.edit, drill = summary.edit_drill,
      buckets = summary.edit_timing, hl = highlights.STATS_EDIT },
  }
  -- Per kind, its size buckets with runs, then the runs that kept no size.
  local parts, counts = {}, {}
  for _, kind in ipairs(kinds) do
    kind.first = #parts + 1
    local sized = 0
    for _, bucket in ipairs(kind.buckets) do
      local score = bucket.score
      if score.spans > 0 then
        sized = sized + score.spans
        local range = bucket_range(bucket)
        local label = string.format("%s · %s positions", kind.title, range)
        parts[#parts + 1] = {
          kind = kind, text = range, glyph = "▄", id = kind.id .. ":" .. bucket.first, drill = bucket.drill,
          label = label, detail = string.format("%s: %s efficiency · %s", label, format_score(score), plural(score.spans, "run")),
        }
        counts[#counts + 1] = score.spans
      end
    end
    if kind.score.spans > sized then
      parts[#parts + 1] = { kind = kind, text = "no size", glyph = "░" }
      counts[#counts + 1] = kind.score.spans - sized
    end
    kind.last = #parts
  end
  if #parts == 0 then return end

  local size_label = PROSE_INDENT .. "Size   "
  local gutter = string.rep(" ", #size_label)
  local widths = share_cells(counts, math.max(#parts, width - 2 * #gutter - (#parts - 1)))
  local column = #gutter
  for i, part in ipairs(parts) do
    part.left, part.right = column, column + widths[i]
    column = part.right + 1
  end

  local shown, bars = {}, {}
  local movement_share = math.floor(1000 * summary.nav.spans / summary.score.spans + 0.5) / 1000
  for _, kind in ipairs(kinds) do
    if kind.last >= kind.first then
      kind.left, kind.right = parts[kind.first].left, parts[kind.last].right
      local stats = string.format("%s efficiency · %s", format_score(kind.score), plural(kind.score.spans, "run"))
      local share = kind.id == "nav" and movement_share or 1 - movement_share
      kind.detail = kind.title .. ": " .. stats
      kind.caption = string.format("%s  %s (%.3f)", kind.title, stats, share)
      shown[#shown + 1] = kind
      bars[#bars + 1] = { text = string.rep("▄", kind.right - kind.left), hl = kind.hl }
    end
  end
  local titles, title_spans = "", {}
  for i, kind in ipairs(shown) do
    local text = kind.caption
    local text_width = vim.fn.strdisplaywidth(text)
    local at = kind.left
    if i == #shown then at = math.min(at, kind.right - text_width) end
    if i > 1 then
      local next_col = vim.fn.strdisplaywidth(titles) + 2
      if next_col + text_width > width then
        push(out, titles)
        titles = ""
      else
        at = math.max(at, next_col)
      end
    end
    titles = pad(titles, math.max(0, math.min(at, width - text_width)))
    if i > 1 and title_spans[i - 1][1] == #out.lines then title_spans[i - 1][3] = #titles end
    title_spans[i] = { #out.lines, #titles, #titles + #text }
    out.marks[#out.marks + 1] = { row = #out.lines, col = #titles, end_col = #titles + #text, hl = kind.hl }
    titles = titles .. text
  end
  titles = pad(titles, math.max(vim.fn.strdisplaywidth(titles), shown[#shown].right))
  title_spans[#title_spans][3] = #titles
  push(out, titles)
  local sizes, labels = {}, {}
  for i, part in ipairs(parts) do
    sizes[i] = { text = string.rep(part.glyph, widths[i]), hl = part.kind.hl }
    local text = vim.fn.strdisplaywidth(part.text) <= widths[i] and part.text or ""
    labels[i] = { text = pad(centered(text, widths[i]), widths[i]) }
  end

  -- The cursor rests on the bar: a title can start left of its segment.
  ---@param segment table   # a kind or a part
  ---@param bar { [1]: integer, [2]: integer, [3]: integer }
  ---@param caption { [1]: integer, [2]: integer, [3]: integer }
  local function add_item(segment, bar, caption)
    out.items[#out.items + 1] = {
      row = math.min(bar[1], caption[1]), last_row = math.max(bar[1], caption[1]),
      left = segment.left, right = segment.right,
      cursor = { bar[1], bar[2] }, spans = { bar, caption },
      id = segment.id, kind = "drill", drill = segment.drill, label = segment.label or segment.title,
      detail = segment.detail,
    }
  end
  local bar_spans = push_cells(out, gutter, bars)
  for i, kind in ipairs(shown) do add_item(kind, bar_spans[i], title_spans[i]) end
  local size_spans = push_cells(out, gutter, sizes)
  local label_spans = push_cells(out, size_label, labels)
  for i, part in ipairs(parts) do
    if part.drill then add_item(part, size_spans[i], label_spans[i]) end
  end
  push(out, "")
end

---@param out VF.Stats.Page
---@param summary VF.Stats.JournalSummary
---@param width integer
local function push_journal_details(out, summary, width)
  push_kind_bars(out, summary, width)

  local unscored, total = {}, 0
  for i, entry in ipairs(summary.unscored) do
    unscored[i] = { runs = entry.runs, name = (entry.outcome:gsub("_", " ")), drill = entry.drill }
    total = total + entry.runs
  end
  if total > 0 then
    push_categories(out, "Not scored: " .. plural(total, "run"), "runs not scored",
      highlights.STATS_NOT_SCORED, unscored)
  end
  push_unbeaten(out, summary, "run")
  if summary.deadline_hit > 0 then
    push(out, string.format("    Search cut short by the deadline: %s", plural(summary.deadline_hit, "run")))
  end
end

function M.journal_efficiency(summary)
  return string.format("%s efficiency · %s of %d commands",
    format_score(summary.score), plural(summary.score.spans, "run"), summary.charted_size)
end

---@param share number
---@return string
local function format_share(share)
  local text = string.format("%.1f%%", share * 100)
  return share > 0 and text == "0.0%" and "<0.1%" or text
end

--- A command row's cells: its name, ratio and the two shares.
---@param row VF.Stats.CommandRow
---@return string[]
local function command_cells(row)
  return {
    row.text, format_ratio(row.ratio), format_share(row.user_share), format_share(row.optimizer_share),
  }
end

local COMMAND_CELL_GAP = "   "
local COMMAND_LIMIT = 5
---@type VF.Stats.Source[]
local SOURCES = { "sessions", "journal" }
local SOURCE_TITLES = { sessions = "Sessions", journal = "Journal" }

--- One list as a table: the name left-aligned and the numbers right-aligned,
--- so the `vs` line up.
---@param out VF.Stats.Page
---@param source VF.Stats.Source
---@param rows VF.Stats.CommandRow[]
---@param side string   # breadcrumb suffix
---@param section VF.Stats.CommandSection
---@param expanded boolean
---@param width? integer
local function push_command_rows(out, source, rows, side, section, expanded, width)
  if #rows == 0 then
    push(out, "    (no command is used 10 times yet)")
    return
  end
  local shown = expanded and #rows or math.min(#rows, COMMAND_LIMIT)
  local widths = { 0, 0, 0, 0 }
  for index = 1, shown do
    for i, cell in ipairs(command_cells(rows[index])) do
      widths[i] = math.max(widths[i], vim.fn.strdisplaywidth(cell))
    end
  end
  if width then
    local numeric_width = widths[2] + widths[3] + widths[4] + 2 * #COMMAND_CELL_GAP + #" vs "
    widths[1] = math.min(widths[1], math.max(1, width - #PROSE_INDENT - numeric_width))
  end
  ---@param s string
  ---@param cell_width integer
  local function right(s, cell_width)
    return string.rep(" ", cell_width - vim.fn.strdisplaywidth(s)) .. s
  end
  for index = 1, shown do
    local row = rows[index]
    local cells = command_cells(row)
    push(out, PROSE_INDENT .. pad(fit(cells[1], widths[1]), widths[1]) .. COMMAND_CELL_GAP .. right(cells[2], widths[2])
      .. COMMAND_CELL_GAP .. right(cells[3], widths[3]) .. " vs " .. right(cells[4], widths[4]))
    -- A merged row's split goes with its runs.
    local name = row.text .. " " .. side
    local label = SOURCE_TITLES[source] .. " › " .. name
    drillable(out, row.drill, row.split ~= "" and label .. " · " .. row.split or label, source .. ":" .. name)
    if index == COMMAND_LIMIT and #rows > COMMAND_LIMIT then
      push(out, expanded and "    Show fewer" or string.format("    … and %d more", #rows - COMMAND_LIMIT))
      local toggle_row = #out.lines - 1
      out.items[#out.items + 1] = {
        row = toggle_row, last_row = toggle_row, cursor = { toggle_row, 4 },
        id = "command_toggle:" .. source .. ":" .. section, kind = "command_toggle", command_section = section,
      }
      mark_text(out, highlights.STATS_DRILL)
    end
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
---@param left_width integer
---@param right_width integer
---@return VF.Stats.Page
local function beside(left, right, left_width, right_width)
  local out = new_page()
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
---@return VF.Stats.Page
local function top_page(summary)
  local out = new_page()
  push(out, "")
  if summary.hidden_runs > 0 then
    push(out, string.format("  %s from other scorings hidden (v)", plural(summary.hidden_runs, "run")))
    push(out, "")
  end
  push_options(out, "Look back", "chart_unit", { "minutes", "hours", "days", "weeks" }, summary.chart_unit)
  return out
end

---@param source VF.Stats.Source
---@param summary VF.Stats.Summary
---@param background VF.Stats.Background
---@param width? integer
---@return VF.Stats.Page
local function source_page(source, summary, background, width)
  local out = new_page()
  push_title(out, centered(SOURCE_TITLES[source], width))
  if source == "sessions" then
    push_sessions(out, summary, width)
  else
    push_journal(out, summary.journal, background, width, summary.chart_unit)
  end
  push(out, "")
  return out
end

---@param width integer
---@return VF.Stats.Page
local function rule_page(width)
  local out = new_page()
  push(out, "  " .. string.rep("─", width - 4))
  mark_text(out, highlights.STATS_RULE)
  push(out, "")
  return out
end

---@return VF.Stats.Page
local function footer_page()
  local out = new_page()
  push(out, "  h/j/k/l move · h/l change selector options · <CR> opens or expands · ? for keys")
  push(out, "")
  return out
end

---@param source VF.Stats.Source
---@param summary VF.Stats.Summary
---@param width? integer
---@param expanded table<VF.Stats.CommandSection, boolean>
---@return VF.Stats.Page
local function commands_page(source, summary, width, expanded)
  local out = new_page()
  local commands = summary[source].commands
  local counted = source == "sessions" and plural(commands.spans, "scored session")
    or string.format("%s of %d commands", plural(commands.spans, "scored run"), summary.journal.charted_size)
  push_prose(out, "Commands · " .. counted, width)
  push_title(out, "  Underused commands")
  push_command_rows(out, source, commands.less, "(you less)", "less", expanded.less == true, width)
  push(out, "")
  push_title(out, "  Overused commands")
  push_command_rows(out, source, commands.more, "(you more)", "more", expanded.more == true, width)
  push(out, "")
  if #commands.never > 0 then
    push_title(out, "  Never used by the optimizer")
    push_command_rows(out, source, commands.never, "(you only)", "never", expanded.never == true, width)
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
    drillable(out, commands.typed_drill, SOURCE_TITLES[source] .. " › extra typed text", source .. ":extra typed text")
    out.items[#out.items].row = first
  end
  push(out, "")
  return out
end

--- One chart in a `width` by `height` viewport: every bucket with data
--- an item, over what the stats leave out of its section.
---@param summary VF.Stats.Summary
---@param name VF.Stats.ChartName
---@param width integer
---@param height integer
---@param shown VF.Stats.ChartLines
---@param scale? VF.Stats.ChartScale
---@return VF.Stats.Page
function M.chart_page(summary, name, width, height, shown, scale)
  local out = new_page()
  local details = new_page()
  push(details, "")
  local plot
  if name == "journal" then
    plot = history_plot(summary.journal.days, scale or overview_scale(summary.chart_unit))
    local scored = M.journal_percent(summary.journal)
    if scored < 100 then
      push_prose(out, string.format("Scored under the current settings: %d%% of the journal", scored), width)
    end
    push_journal_details(details, summary.journal, width)
  elseif name == "sessions" then
    plot = history_plot(summary.sessions.days, scale or overview_scale(summary.chart_unit))
    push_sessions_details(details, summary)
  else
    local bins = name == "nav_timing" and summary.journal.nav_timing or summary.journal.edit_timing
    plot = timing_plot(bins)
    push(details, centered("Buffer positions in the larger unpadded region", width))
  end

  out.items = push_line_chart(out, width, math.floor(height * 0.9) - #out.lines, plot, shown)
  out = stacked(out, details)
  for _ = #out.lines + 1, height do push(out, "") end
  return out
end

--- The stats in one page: the Sessions band over the Journal's, each its
--- chart and its own Commands tables. The Commands go beside their chart when
--- `width` has the room, and under it when not or without one.
---@param summary VF.Stats.Summary
---@param query VF.Stats.Scope
---@param background VF.Stats.Background
---@param width? integer   # display columns to draw in
---@param expanded_commands? VF.Stats.ExpandedCommands
---@return VF.Stats.Page
function M.build_page(summary, query, background, width, expanded_commands)
  local expanded = expanded_commands or {}
  local left_width, right_width = width, width
  ---@type integer|nil
  local room
  if width and width >= M.SPLIT_MIN_WIDTH then
    local columns = width - COLUMN_GAP
    room = math.max(OVERVIEW_MIN_WIDTH,
      math.min(math.floor(columns * OVERVIEW_SHARE), columns - COMMANDS_MIN_WIDTH))
    left_width, right_width = room, columns - room
  end
  local recorded = { sessions = summary.lifetime.sessions > 0, journal = summary.journal.present }
  ---@type table<VF.Stats.Source, VF.Stats.Page>
  local commands = {}
  -- One width for both bands' tables, so their rows highlight alike.
  local commands_width = 0
  for _, source in ipairs(SOURCES) do
    commands[source] = recorded[source] and commands_page(source, summary, right_width, expanded[source] or {})
      or new_page()
    commands_width = math.max(commands_width, page_width(commands[source]))
  end
  ---@param left VF.Stats.Page
  ---@param right VF.Stats.Page
  ---@return VF.Stats.Page
  local function band(left, right)
    return room and beside(left, right, room, commands_width) or stacked(left, right)
  end
  local page = new_page()
  push(page, string.format(" Stats    %s · weight: %s · runs of %d",
    query.current_only and summary.scoring_label or "all scorings", query.weight, summary.journal.charted_size))
  push(page, " Log      " .. vim.fn.fnamemodify(log.path(), ":~"))
  stacked(page, band(top_page(summary), new_page()))
  for i, source in ipairs(SOURCES) do
    if i > 1 then
      page = stacked(page, rule_page(room and room + COLUMN_GAP + commands_width or width or page_width(page)))
    end
    local both = band(source_page(source, summary, background, left_width), commands[source])
    for _, item in ipairs(both.items) do item.source = source end
    page = stacked(page, both)
  end
  return stacked(page, footer_page())
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
  { title = "time ms", order = "slowest", width = 11,
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

--- What a run is filed under: a session's saved filename, which `:Vimfy view`
--- and `pin` take, or a journal run's file and where in it the run ends.
---@param row VF.Stats.RunRow
---@return string
function M.run_name(row)
  local journal = row.id.journal
  if journal then
    return string.format("%s · %d commands to atom %d", journal.file, journal.window_size, journal.last_atom)
  end
  return row.saved_filename == "" and "—" or row.saved_filename
end

--- A run's name and keys, spaced by token, then its text before and after side
--- by side with the changed lines marked and the cursor's cell shaded in each.
--- `detail` is nil for a run whose text is gone (`err` says why).
---@param row VF.Stats.RunRow
---@param detail VF.Session.Viewable|nil
---@param err string|nil
---@param width integer
---@return VF.Stats.RunPreview
function M.run_preview(row, detail, err, width)
  local lines = {}
  push_keys(lines, " id:", M.run_name(row), width)
  -- A journal run's resolved keys parse where its typed ones may not.
  push_keys(lines, " you:", sequence_display.inline(detail and detail.user_seq or row.user_keys), width)
  push_keys(lines, " found:", row.best_seq == "" and "—" or sequence_display.inline(row.best_seq), width)
  lines[#lines + 1] = ""
  local marks = {}
  if not detail then
    lines[#lines + 1] = " Text not available: " .. err
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
  local hunks = vim.text.diff(joined(detail.lines), joined(detail.goal_lines), { result_type = "indices" })
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
