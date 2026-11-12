local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

local function checked_string(slice)
  local text = core.slice_to_string(slice)
  if text:sub(1, 6) == "ERROR:" then
    error(text, 2)
  end
  return text
end

---@param result VF.Session.Result
local function encode_result(result)
  local recs = {}
  for _, rec in ipairs(result.optimal_results) do
    recs[#recs + 1] = rec.seq
    recs[#recs + 1] = tostring(rec.cost)
  end

  return core.encode_string_list({
    core.encode_string_list(result.lines),
    tostring(result.start_row),
    tostring(result.start_col),
    tostring(result.end_row),
    tostring(result.end_col),
    result.user_seq,
    "1",
    tostring(result.user_cost),
    result.kind or "",
    core.encode_string_list(recs),
  })
end

---@param seq string
---@param tokenize boolean
---@param sectionize boolean
---@return string[]
function M.sequence_display_lines(seq, tokenize, sectionize)
  local payload = checked_string(lib.vf_sequence_display_lines(seq, #seq, tokenize, sectionize))
  return core.decode_string_list(payload)
end

---@param seq string
---@param tokenize boolean
---@return string
function M.sequence_display_inline(seq, tokenize)
  return checked_string(lib.vf_sequence_display_inline(seq, #seq, tokenize))
end

---@param text string
---@return string
function M.sequence_display_key_notation(text)
  return checked_string(lib.vf_sequence_display_key_notation(text, #text))
end

---@param text string
---@param key_notation boolean
---@return string
function M.sequence_display_typed_text(text, key_notation)
  return checked_string(lib.vf_sequence_display_typed_text(text, #text, key_notation))
end

---@param chunks { kind: string, text: string }[]
---@param tokenize boolean
---@return string
function M.sequence_display_typed_chunks(chunks, tokenize)
  local fields = {}
  for _, chunk in ipairs(chunks) do
    fields[#fields + 1] = chunk.kind
    fields[#fields + 1] = chunk.text
  end
  local payload = core.encode_string_list(fields)
  return checked_string(lib.vf_sequence_display_typed_chunks(payload, #payload, tokenize))
end

---@param result VF.Session.Result
---@return string
function M.format_result_position(result)
  return checked_string(lib.vf_format_result_position(
    result.start_row, result.start_col, result.end_row, result.end_col))
end

---@param result VF.Session.Result
---@return string
function M.format_result_kind_suffix(result)
  local kind = result.kind or ""
  return checked_string(lib.vf_format_result_kind_suffix(kind, #kind))
end

---@param title string
---@param result VF.Session.Result
---@param tokenize boolean
---@param sectionize boolean
---@return string
function M.format_result_message(title, result, tokenize, sectionize)
  local payload = encode_result(result)
  return checked_string(lib.vf_format_result_message(
    title, #title, payload, #payload, tokenize, sectionize))
end

return M
