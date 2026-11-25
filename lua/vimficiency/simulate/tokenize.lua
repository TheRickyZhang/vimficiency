local ffi_sequence = require("vimficiency.ffi.sequence")

local M = {}

local CHUNK_SIZE = 4

---@param text string
---@return string[]
local function typed_chunks(text)
  local chunks = {}
  local chunk = ""
  local chunk_units = 0

  local function flush_chunk()
    if chunk == "" then return end
    chunks[#chunks + 1] = chunk
    chunk = ""
    chunk_units = 0
  end

  local i = 1
  while i <= #text do
    if text:sub(i, i) == "<" then
      local close = text:find(">", i, true)
      if close then
        flush_chunk()
        chunks[#chunks + 1] = text:sub(i, close)
        i = close + 1
      else
        chunk = chunk .. text:sub(i, i)
        chunk_units = chunk_units + 1
        i = i + 1
      end
    else
      chunk = chunk .. text:sub(i, i)
      chunk_units = chunk_units + 1
      i = i + 1
    end

    if chunk_units >= CHUNK_SIZE then
      flush_chunk()
    end
  end

  flush_chunk()
  return chunks
end

--- Tokenize a sequence for animation. Returns kinded tokens; consumers use
--- `.text` for feeding nvim and `.kind` for modal behavior.
---@param seq string
---@return VF.Sequence.Token[] tokens
function M.for_animation(seq)
  local tokens, err = ffi_sequence.tokenize_sequence(seq)
  if err then
    local movements, movement_error = ffi_sequence.tokenize_movements(seq)
    if movement_error then return { { text = seq, kind = "unavailable", error = err } } end
    tokens = movements
  end
  local expanded = {}
  for _, tok in ipairs(tokens) do
    if tok.kind == "typed" then
      for _, chunk in ipairs(typed_chunks(tok.text)) do
        expanded[#expanded + 1] = { text = chunk, kind = "typed" }
      end
    else
      expanded[#expanded + 1] = tok
    end
  end
  return expanded
end

return M
