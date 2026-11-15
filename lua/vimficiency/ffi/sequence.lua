local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

---@param seq string
---@return VF.Sequence.Token[] tokens
---@return string|nil error
function M.tokenize_movements(seq)
  if not seq or seq == "" then return {}, nil end
  local tokens, err = core.decode(lib.vf_tokenize_movements(seq, #seq))
  return tokens or {}, err
end

---@param seq string
---@return VF.Sequence.Token[] tokens
---@return string|nil error
function M.tokenize_sequence(seq)
  if not seq or seq == "" then return {}, nil end
  local tokens, err = core.decode(lib.vf_tokenize_sequence(seq, #seq))
  return tokens or {}, err
end

return M
