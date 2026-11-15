-- Shared LuaLS annotations for cross-module Vimficiency records.

--- Reads yield cdata (`len` is a `size_t`, so `tonumber` it); writes take a Lua
--- string and integer.
---@class VF.C.ByteSlice
---@field data ffi.cdata*|string
---@field len ffi.cdata*|integer

--- cdata view of `VFSearchRequest` (Api.def); pointer fields borrow Lua memory.
---@class VF.C.SearchRequest
---@field initial_lines ffi.cdata*         # `VFByteSlice[]` from `ffi/core.byte_slices`
---@field initial_lines_count integer
---@field goal_lines ffi.cdata*
---@field goal_lines_count integer
---@field start_row integer
---@field start_col integer
---@field end_row integer
---@field end_col integer
---@field has_lines_above boolean
---@field has_lines_below boolean
---@field window_height integer
---@field scroll_amount integer
---@field user_seq string
---@field user_seq_len integer
---@field optimizer_overrides string
---@field optimizer_overrides_len integer

--- cdata views of the other `Api.def` input structs. Arrays are 0-indexed.

---@class VF.C.KeyEvent
---@field mode VF.C.ByteSlice
---@field key_typed VF.C.ByteSlice

---@class VF.C.TypedChunk
---@field kind VF.C.ByteSlice
---@field text VF.C.ByteSlice

---@class VF.C.OptimizerResult
---@field seq VF.C.ByteSlice
---@field cost number

---@class VF.C.SessionResult
---@field lines ffi.cdata*                 # `VFByteSlice[]`
---@field lines_count integer
---@field start_row integer
---@field start_col integer
---@field end_row integer
---@field end_col integer
---@field user_seq VF.C.ByteSlice
---@field user_cost number
---@field kind VF.C.ByteSlice
---@field optimal_results ffi.cdata*       # `VFOptimizerResult[]`
---@field optimal_results_count integer

--- One search problem for analyze or explore-start; marshalled into a
--- `VF.C.SearchRequest` by `ffi/core.with_search_request`. Rows/cols are
--- 0-indexed and relative to the slice.
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
