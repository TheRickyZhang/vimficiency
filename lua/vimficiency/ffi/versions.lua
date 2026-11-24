---@class (exact) VF.FfiVersions
---@field versions fun(): VF.Versions
local M = {}

local core = require("vimficiency.ffi.core")

--- `vf.Versions` (src/LuaExports/proto/common.proto): the library's release and
--- the format and scoring versions it reads and writes (src/Versions.cppm).
---@class VF.Versions
---@field release string
---@field journal_format integer
---@field samples_format integer
---@field rollup_format integer
---@field session_log_format integer
---@field scoring_version integer
---@field journal_schema string
---@field samples_schema string
---@field rollup_schema string
---@field session_log_schema string
---@field scoring_signature string

---@return VF.Versions
function M.versions()
  return core.unwrap(core.lib.vf_versions(), "vf.Versions")
end

return M
