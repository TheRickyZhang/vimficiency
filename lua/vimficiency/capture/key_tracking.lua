-- Pure key-sequence helpers for interactive Explore's frontier replay.
--
-- Capture (manual/recall/triggers) moved to the `CmdAtom` event; see
-- `capture/atom_stream.lua` and `dev/lua/atom-stream.md`. Explore still runs its own
-- `vim.on_key` namespace (`explore/key_capture.lua`) and accumulates raw
-- `VF.KeyEvent`s on the view, then reduces them with the helpers below.
local M = {}
local ffi_session = require("vimficiency.ffi.session")

---@class VF.KeyEvent
---@field t integer              # hrtime timestamp
---@field win integer            # window id (useful for multi-session matching)
---@field buf integer            # buffer id
---@field mode string            # vim mode at time of keypress
---@field key_sent_raw string    # raw key sent (for debugging)
---@field key_sent string        # keytrans'd key sent
---@field key_typed_raw string   # raw typed key (before mappings)
---@field key_typed string       # keytrans'd typed key

--- Build a key sequence string from key events, with deduplication.
---@param key_seq VF.KeyEvent[]
---@return string
function M.build_sequence(key_seq)
	return ffi_session.build_sequence(key_seq)
end

--- Remove the trailing events whose `key_typed_raw` bytes match `typed_raw`.
--- Explore calls this to drop the LHS bytes of a resolved multi-key mapping
--- that its on_key listener already recorded.
---@param key_seq VF.KeyEvent[]
---@param typed_raw string
---@return integer popped
function M.strip_matching_tail(key_seq, typed_raw)
	if #typed_raw == 0 or #key_seq == 0 then return 0 end
	local acc = ""
	local i = #key_seq
	while i > 0 and #acc < #typed_raw do
		local ev = key_seq[i]
		acc = (ev.key_typed_raw or "") .. acc
		i = i - 1
	end
	if acc ~= typed_raw then return 0 end
	local popped = #key_seq - i
	for j = #key_seq, i + 1, -1 do
		key_seq[j] = nil
	end
	return popped
end

return M
