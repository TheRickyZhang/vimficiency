-- Shared LuaLS annotations for cross-module Vimficiency records.

--- `len` is a `size_t` cdata, so `tonumber` it.
---@class VF.C.ByteSlice
---@field data ffi.cdata*
---@field len ffi.cdata*

--- One search problem for analyze or explore-start: `vf.search.SearchRequest`
--- (src/LuaExports/proto/search.proto). Rows/cols are 0-indexed and relative to
--- the slice.
---@class VF.SearchRequest
---@field initial_lines string[]
---@field goal_lines string[]
---@field start_row integer
---@field start_col integer
---@field end_row integer
---@field end_col integer
---@field has_lines_above boolean
---@field has_lines_below boolean
---@field window_height integer
---@field scroll_amount integer
---@field user_seq string
---@field optimizer_overrides string        # `ffi/optimizer.encode_optimizer_overrides` output; "" for none

---@class VF.Optimizer.Result
---@field seq string
---@field cost number

--- Half-open [begin, end) span over a buffer, 0-indexed.
---@class VF.Diff.Span
---@field begin_row integer
---@field begin_col integer
---@field end_row integer
---@field end_col integer

--- One character-level change region: the initial-side (deleted) span and the
--- goal-side (inserted) span. Either may be empty (pure insertion/deletion).
---@class VF.Diff.Region
---@field init VF.Diff.Span
---@field goal VF.Diff.Span

---@class VF.OptimizerOverrides
---@field shared? table<string, number|integer|boolean>
---@field nav? table<string, number|integer|boolean>
---@field transform? table<string, number|integer|boolean>
---@field composition? table<string, number|integer|boolean>

---@class VF.Sequence.Token
---@field text string
---@field kind "movement"|"delete"|"change"|"visual"|"typed"|"escape"
---@field index integer?
---@field hl string?

---@class VF.Position
---@field row integer
---@field col integer

---@class VF.Range
---@field begin_pos VF.Position
---@field end_pos VF.Position

---@class VF.Explore.PhaseNavigate
---@field kind "Navigate"
---@field edit_index integer

---@class VF.Explore.PhaseTransform
---@field kind "Transform"
---@field edit_index integer

---@class VF.Explore.PhaseInsert
---@field kind "Insert"
---@field edit_index integer

---@alias VF.Explore.Phase
---| VF.Explore.PhaseNavigate
---| VF.Explore.PhaseTransform
---| VF.Explore.PhaseInsert

---@class VF.Explore.State
---@field phase VF.Explore.Phase
---@field is_completed boolean
---@field cursor VF.Position
---@field total_edits integer
---@field cost number
---@field seq string
---@field can_undo boolean
---@field can_redo boolean
---@field target_range VF.Range

---@class VF.Explore.Rejected
---@field reason string

--- Exactly one of `applied` / `rejected` is set; `outcome` names it.
---@class VF.Explore.ApplyResult
---@field outcome "applied"|"rejected"
---@field applied? table
---@field rejected? VF.Explore.Rejected

---@class VF.Explore.Recommendation
---@field text string
---@field cost_diff number
---@field distance number
---@field score number
---@field landing VF.Position
---@field literal_text string
---@field rank? integer

---@class VF.Explore.ReconfigurePlanResult
---@field reset boolean

---@class VF.Explore.HeaderRowsResult
---@field rows string[]

---@class VF.Explore.HeaderRows
---@field explored string[]
---@field optimal VF.Explore.HeaderRowsResult[]


return {}
