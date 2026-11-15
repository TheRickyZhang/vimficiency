local ffi = require("ffi")
local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

local TypedChunks = ffi.typeof("VFTypedChunk[?]")
local OptimizerResults = ffi.typeof("VFOptimizerResult[?]")
local SessionResult = ffi.typeof("VFSessionResult")

---@param seq string
---@param tokenize boolean
---@param sectionize boolean
---@return string[]
function M.sequence_display_lines(seq, tokenize, sectionize)
  return core.unwrap(lib.vf_sequence_display_lines(seq, #seq, tokenize, sectionize))
end

---@param seq string
---@param tokenize boolean
---@return string
function M.sequence_display_inline(seq, tokenize)
  return core.unwrap(lib.vf_sequence_display_inline(seq, #seq, tokenize))
end

---@param text string
---@return string
function M.sequence_display_key_notation(text)
  return core.unwrap(lib.vf_sequence_display_key_notation(text, #text))
end

---@param text string
---@param key_notation boolean
---@return string
function M.sequence_display_typed_text(text, key_notation)
  return core.unwrap(lib.vf_sequence_display_typed_text(text, #text, key_notation))
end

---@param chunks { kind: string, text: string }[]
---@param tokenize boolean
---@return string
function M.sequence_display_typed_chunks(chunks, tokenize)
  local count = #chunks
  local typed = TypedChunks(count) --[[@as VF.C.TypedChunk[] ]]
  for i = 1, count do
    core.set_slice(typed[i - 1].kind, chunks[i].kind)
    core.set_slice(typed[i - 1].text, chunks[i].text)
  end
  local slice = lib.vf_sequence_display_typed_chunks(typed, count, tokenize)
  core.keep_alive(chunks, typed)
  return core.unwrap(slice)
end

---@param result VF.Session.Result
---@return string
function M.format_result_position(result)
  return core.unwrap(lib.vf_format_result_position(
    result.start_row, result.start_col, result.end_row, result.end_col))
end

---@param result VF.Session.Result
---@return string
function M.format_result_kind_suffix(result)
  local kind = result.kind or ""
  return core.unwrap(lib.vf_format_result_kind_suffix(kind, #kind))
end

---@param title string
---@param result VF.Session.Result
---@param tokenize boolean
---@param sectionize boolean
---@return string
function M.format_result_message(title, result, tokenize, sectionize)
  local lines, lines_count = core.byte_slices(result.lines)
  local optimal_count = #result.optimal_results
  local optimal_results = OptimizerResults(optimal_count) --[[@as VF.C.OptimizerResult[] ]]
  for i = 1, optimal_count do
    local rec = result.optimal_results[i]
    core.set_slice(optimal_results[i - 1].seq, rec.seq)
    optimal_results[i - 1].cost = rec.cost
  end

  local c_result = SessionResult() --[[@as VF.C.SessionResult]]
  c_result.lines = lines
  c_result.lines_count = lines_count
  c_result.start_row = result.start_row
  c_result.start_col = result.start_col
  c_result.end_row = result.end_row
  c_result.end_col = result.end_col
  core.set_slice(c_result.user_seq, result.user_seq)
  c_result.user_cost = result.user_cost
  core.set_slice(c_result.kind, result.kind or "")
  c_result.optimal_results = optimal_results
  c_result.optimal_results_count = optimal_count

  local slice = lib.vf_format_result_message(
    title, #title, c_result, tokenize, sectionize)
  core.keep_alive(result, lines, optimal_results)
  return core.unwrap(slice)
end

return M
