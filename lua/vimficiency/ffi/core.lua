local ffi = require("ffi")

local M = {}

local function find_plugin_root()
  local source = debug.getinfo(1, "S").source
  if source:sub(1, 1) == "@" then
    source = source:sub(2)
  end
  return vim.fn.fnamemodify(source, ":h:h:h:h")
end

local function read_ffi_api_def()
  local path = find_plugin_root() .. "/src/LuaExports/Api.def"
  local f, err = io.open(path, "rb")
  if not f then
    error("vimfy: failed to read FFI API declarations at " .. path .. ": " .. tostring(err), 0)
  end
  local text = f:read("*all")
  f:close()
  return text
end

local api_def_text = read_ffi_api_def()
ffi.cdef(api_def_text)

---@return string filename
---@return string system_name
local function lib_filenames()
  local os_name = jit and jit.os or ""
  if os_name == "OSX" then
    return "libvimficiency.dylib", "vimficiency"
  elseif os_name == "Windows" then
    return "vimficiency.dll", "vimficiency"
  else
    return "libvimficiency.so", "vimficiency"
  end
end

local function load_lib()
  local root = find_plugin_root()
  local lib_filename, system_name = lib_filenames()
  local paths = {}
  if type(vim.env.VIMFICIENCY_LIB_PATH) == "string"
      and vim.env.VIMFICIENCY_LIB_PATH ~= "" then
    table.insert(paths, vim.env.VIMFICIENCY_LIB_PATH)
  end
  if type(_G.__vimficiency_reload_lib_path) == "string" then
    table.insert(paths, _G.__vimficiency_reload_lib_path)
  end
  table.insert(paths, root .. "/build/" .. lib_filename)
  table.insert(paths, system_name)

  local expected_hash = vim.fn.sha256(api_def_text)

  for _, path in ipairs(paths) do
    local ok, lib = pcall(ffi.load, path)
    if ok then
      local got_hash = ffi.string(lib.vf_abi_hash())
      if got_hash ~= expected_hash then
        error(string.format(
          "vimficiency: ABI mismatch — loaded %s reports Api.def hash %s, " ..
          "but the Lua bindings were authored against %s. The shared library " ..
          "is stale. Rebuild with `cmake --build %s/build` (or `:Lazy build " ..
          "vimficiency` / your plugin manager's equivalent).",
          path, got_hash, expected_hash, root), 0)
      end
      M.lib_path = path
      return lib
    end
  end

  error(string.format(
    "vimficiency: could not load %s (tried: %s). Build it with " ..
    "`./scripts/install.sh` in the plugin directory, or `:Lazy build " ..
    "vimficiency` / your plugin manager's equivalent.",
    lib_filename, table.concat(paths, ", ")), 0)
end

local function build_enum(count, name_fn)
  local t = {}
  for i = 0, count - 1 do
    local name = ffi.string(name_fn(i))
    t[name] = i
  end
  return t
end

M.lib = load_lib()
M.lib_filename = lib_filenames()

M.Key = build_enum(M.lib.VF_KEY_COUNT, M.lib.vf_key_name)
M.Finger = build_enum(M.lib.VF_FINGER_COUNT, M.lib.vf_finger_name)
M.Hand = build_enum(M.lib.VF_HAND_COUNT, M.lib.vf_hand_name)
M.CountClass = build_enum(M.lib.VF_COUNT_CLASS_COUNT, M.lib.vf_count_class_name)

-- Every `VFByteSlice` return is a msgpack map holding exactly one of `value`,
-- `error`, or `pending` (C++ `storeBytes` / `storePending`).
---@param slice VF.C.ByteSlice
---@return { value: any, error: string?, pending: boolean? }
local function envelope(slice)
  return vim.mpack.decode(ffi.string(slice.data, tonumber(slice.len)))
end

--- Returns the C++ error instead of raising it.
---@param slice VF.C.ByteSlice
---@return any value
---@return string|nil err
function M.decode(slice)
  local e = envelope(slice)
  return e.value, e.error
end

--- Raises on a C++ error; returns nil while an async job is still pending.
---@param slice VF.C.ByteSlice
---@return any
function M.unwrap(slice)
  local e = envelope(slice)
  if e.error then error(e.error, 0) end
  return e.value
end

-- The cdata built by `set_slice` and `byte_slices` holds raw pointers and
-- anchors neither the Lua strings it points into nor any cdata array stored in
-- a struct field. Pass both to `keep_alive` after the FFI call: a local that is
-- dead after its last use (or a frame dropped by a tail call) is collectable
-- while C++ still reads it.

function M.keep_alive(...) end

---@param slice VF.C.ByteSlice
---@param text string
function M.set_slice(slice, text)
  slice.data = text
  slice.len = #text
end

local ByteSlices = ffi.typeof("VFByteSlice[?]")

---@param strings string[]
---@return ffi.cdata* slices
---@return integer count
function M.byte_slices(strings)
  local count = #strings
  local slices = ByteSlices(count)
  for i = 1, count do
    M.set_slice(slices[i - 1], strings[i])
  end
  return slices, count
end

local VFSearchRequest = ffi.typeof("VFSearchRequest")

--- Fills a `VFSearchRequest` from `req` and passes it to `call`.
---@generic T
---@param req VF.SearchRequest
---@param call fun(request: VF.C.SearchRequest): T
---@return T
function M.with_search_request(req, call)
  local initial_lines, initial_lines_count = M.byte_slices(req.initial_lines)
  local goal_lines, goal_lines_count = M.byte_slices(req.goal_lines)
  local request = VFSearchRequest() --[[@as VF.C.SearchRequest]]
  request.initial_lines = initial_lines
  request.initial_lines_count = initial_lines_count
  request.goal_lines = goal_lines
  request.goal_lines_count = goal_lines_count
  request.start_row = req.start_row
  request.start_col = req.start_col
  request.end_row = req.end_row
  request.end_col = req.end_col
  request.has_lines_above = req.has_lines_above
  request.has_lines_below = req.has_lines_below
  request.window_height = req.window_height
  request.scroll_amount = req.scroll_amount
  request.user_seq = req.user_seq
  request.user_seq_len = #req.user_seq
  request.optimizer_overrides = req.optimizer_overrides
  request.optimizer_overrides_len = #req.optimizer_overrides
  local result = call(request)
  M.keep_alive(req, initial_lines, goal_lines)
  return result
end

function M.version_string()
  return ffi.string(M.lib.vf_version_string())
end

function M.debug_config()
  return M.unwrap(M.lib.vf_debug_config())
end

return M
