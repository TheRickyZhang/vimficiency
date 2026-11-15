local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

---@param seq string
---@param tokenize boolean
---@param sectionize boolean
---@return string[]
function M.sequence_display_lines(seq, tokenize, sectionize)
  return core.call(lib.vf_sequence_display_lines, "vf.display.SequenceRequest",
    { seq = seq, tokenize = tokenize, sectionize = sectionize }, "vf.Lines").lines
end

---@param seq string
---@param tokenize boolean
---@return string
function M.sequence_display_inline(seq, tokenize)
  return core.call(lib.vf_sequence_display_inline, "vf.display.SequenceRequest",
    { seq = seq, tokenize = tokenize }, "vf.Text").text
end

---@param text string
---@return string
function M.sequence_display_key_notation(text)
  return core.call(lib.vf_sequence_display_key_notation, "vf.Text",
    { text = text }, "vf.Text").text
end

---@param text string
---@param key_notation boolean
---@return string
function M.sequence_display_typed_text(text, key_notation)
  return core.call(lib.vf_sequence_display_typed_text, "vf.display.TypedTextRequest",
    { text = text, key_notation = key_notation }, "vf.Text").text
end

---@param chunks { kind: string, text: string }[]
---@param tokenize boolean
---@return string
function M.sequence_display_typed_chunks(chunks, tokenize)
  return core.call(lib.vf_sequence_display_typed_chunks, "vf.display.TypedChunksRequest",
    { chunks = chunks, tokenize = tokenize }, "vf.Text").text
end

---@param result VF.Session.Result
---@return string
function M.format_result_position(result)
  return core.call(lib.vf_format_result_position, "vf.display.SessionResult", {
    start_row = result.start_row,
    start_col = result.start_col,
    end_row = result.end_row,
    end_col = result.end_col,
  }, "vf.Text").text
end

---@param result VF.Session.Result
---@return string
function M.format_result_kind_suffix(result)
  return core.call(lib.vf_format_result_kind_suffix, "vf.display.SessionResult",
    { kind = result.kind }, "vf.Text").text
end

---@param title string
---@param result VF.Session.Result
---@param tokenize boolean
---@param sectionize boolean
---@return string
function M.format_result_message(title, result, tokenize, sectionize)
  local optimal_results = {}
  for i, rec in ipairs(result.optimal_results) do
    optimal_results[i] = { seq = rec.seq, cost = rec.cost }
  end
  return core.call(lib.vf_format_result_message, "vf.display.ResultMessageRequest", {
    title = title,
    result = {
      lines = result.lines,
      start_row = result.start_row,
      start_col = result.start_col,
      end_row = result.end_row,
      end_col = result.end_col,
      user_seq = result.user_seq,
      user_cost = result.user_cost,
      kind = result.kind,
      optimal_results = optimal_results,
    },
    tokenize = tokenize,
    sectionize = sectionize,
  }, "vf.Text").text
end

return M
