local archive = require("vimficiency.session.archive")
local simulate = require("vimficiency.simulate")
local recording = require("vimficiency.capture.recording")

---@class (exact) VF.SessionPlayback
---@field play     fun(run: VF.Session.Playable, label: string, count: integer|nil)
---@field simulate fun(alias_or_filename: string, count: integer|nil)
local M = {}

--- Replays what the user did beside each found sequence.
---@param run VF.Session.Playable
---@param label string
---@param count integer|nil
function M.play(run, label, count)
  if not run.recording then
    vim.notify("Vimfy: older save format (v1). Play shows the recorded start and finish in one step.",
      vim.log.levels.WARN)
  end
  local user_item = { recording = run.recording or recording.endpoints(run), cost = string.format("%.2f", run.user_cost) }

  local suggestions = {}
  for _, r in ipairs(run.optimal_results) do
    table.insert(suggestions, {
      seq = r.seq, cost = string.format("%.2f", r.cost),
    })
  end

  simulate.simulate_compare(
    run.lines,
    run.start_row,
    run.start_col,
    {
      user = user_item,
      suggestions = suggestions,
    },
    {
      label = label,
      indent = run.indent,
      filetype = run.filetype,
      end_row = run.end_row,
      end_col = run.end_col,
      initial_window_count = count,
    }
  )
end

---@param alias_or_filename string
---@param count integer|nil
function M.simulate(alias_or_filename, count)
  M.play(archive.resolve_result(alias_or_filename), alias_or_filename, count)
end

return M
