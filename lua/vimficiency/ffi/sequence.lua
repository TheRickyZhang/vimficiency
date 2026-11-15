local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

---@return VF.Sequence.Token[] tokens
---@return string|nil error
local function tokenize(fn, seq)
  if not seq or seq == "" then return {}, nil end
  local bytes = core.encode("vf.session.SequenceRequest", { seq = seq })
  local result, err = core.decode(fn(bytes, #bytes), "vf.session.Tokens")
  return result and result.tokens or {}, err
end

---@param seq string
---@return VF.Sequence.Token[] tokens
---@return string|nil error
function M.tokenize_movements(seq)
  return tokenize(lib.vf_tokenize_movements, seq)
end

---@param seq string
---@return VF.Sequence.Token[] tokens
---@return string|nil error
function M.tokenize_sequence(seq)
  return tokenize(lib.vf_tokenize_sequence, seq)
end

return M
