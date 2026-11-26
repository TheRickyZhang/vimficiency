local history = require("vimficiency.capture.buffer_history")

---@class VF.Replay.Delta
---@field first integer
---@field old_count integer
---@field lines string[]

---@class VF.Replay.Position
---@field row integer
---@field col integer
---@field mode string

---@class VF.Replay.Step : VF.Replay.Position
---@field keys string
---@field typed? string   # Pressed keys when available and different from the resolved keys
---@field deltas VF.Replay.Delta[]

---@class VF.Replay.Recording : VF.Replay.Position
---@field steps VF.Replay.Step[]

---@class (exact) VF.Capture.Recording
---@field capture fun(active: VF.Session.Active, initial: string[], final: VF.BufferState): VF.Replay.Recording, integer, integer
---@field localize fun(recording: VF.Replay.Recording, above: integer)
---@field endpoints fun(result: VF.Session.Viewable): VF.Replay.Recording
local M = {}

function M.localize(recording, above)
  if above == 0 then return end
  recording.row = recording.row - above
  for _, step in ipairs(recording.steps) do
    step.row = step.row - above
    for i, delta in ipairs(step.deltas) do
      step.deltas[i] = { first = delta.first - above, old_count = delta.old_count, lines = delta.lines }
    end
  end
end

function M.capture(active, initial, final)
  local start = active.start_state
  local recording = { row = start.row, col = start.col, mode = start.mode, steps = {} }
  local steps = recording.steps
  local height = #initial
  local above, below = start.row, height - start.row - 1
  local function append(keys, typed, view, deltas)
    for _, delta in ipairs(deltas) do
      above = math.min(above, delta.first)
      below = math.min(below, height - delta.first - delta.old_count)
      height = height + #delta.lines - delta.old_count
    end
    above = math.min(above, view.row)
    below = math.min(below, height - view.row - 1)
    steps[#steps + 1] = { keys = keys, typed = typed ~= "" and typed ~= keys and typed or nil,
      row = view.row, col = view.col, mode = view.mode, deltas = deltas }
  end
  local seq = active.start_seq
  if seq then
    local final_seq = history.mark(active.buf)
    local keys, typed = {}, {}
    local first = 1
    for i, atom in ipairs(active.atoms) do
      keys[#keys + 1] = atom.keys
      typed[#typed + 1] = atom.typed
      local next_atom = active.atoms[i + 1]
      if not next_atom or not atom.tick or atom.tick ~= next_atom.tick then
        local mark = assert(atom.mark)
        local deltas, min_height = history.changes(active.buf, seq, mark.seq)
        min_height = math.min(min_height, height)
        append(table.concat(keys), table.concat(typed), mark.view, deltas)
        for j = first, i do
          for _, row in ipairs(active.atoms[j].visited_rows or {}) do
            above = math.min(above, row)
            below = math.min(below, math.max(0, min_height - row - 1))
          end
        end
        seq, keys, typed = mark.seq, {}, {}
        first = i + 1
      end
    end
    local last = steps[#steps] or start
    if final_seq ~= seq or last.row ~= final.row or last.col ~= final.col or last.mode ~= final.mode then
      append("", "", final, history.changes(active.buf, seq, final_seq))
    end
  else
    local keys, typed = {}, {}
    for i, atom in ipairs(active.atoms) do keys[i], typed[i] = atom.keys, atom.typed end
    append(table.concat(keys), table.concat(typed), final, { history.difference(initial, final.lines) })
  end
  return recording, above, below
end

function M.endpoints(result)
  return {
    row = result.start_row, col = result.start_col, mode = "",
    steps = { {
      keys = result.user_seq, typed = result.user_typed ~= "" and result.user_typed or nil,
      row = result.end_row, col = result.end_col, mode = "",
      deltas = { { first = 0, old_count = #result.lines, lines = result.goal_lines } },
    } },
  }
end

return M
